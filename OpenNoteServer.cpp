#include <cerrno>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cctype>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <system_error>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

#include "BoardStructs.h"
#include "BoardDatabase.h"
#include "ServerBoardCommandProcessor.h"
#include "ServerLogging.h"

using namespace std;
using json = nlohmann::json;

constexpr int DEFAULT_SERVER_PORT = 5432; // default port when no port is provided
#define MAX_LINE 256

fd_set master; // master file descriptor list
int listener = -1;  // listening socket descriptor
int fdmax;
volatile sig_atomic_t serverStopping = 0;
mutex masterMutex; // guards master/fdmax across threads

unordered_map<uint32_t, BoardState> activeBoards; // Maps boardID to boardState in memory
unordered_map<int, uint32_t> clientBoardMap; // Maps socket identifier to a active boardID
BoardDatabase boardDatabase;
mutex stateMutex;
mutex packetWriteMutex;
mutex clientSocketsMutex;
unordered_set<int> activeClientSockets;
bool serverShuttingDown = false;
mutex clientThreadsMutex;
condition_variable clientThreadsFinished;
size_t activeClientThreads = 0;

bool handleSyncBoard(int clientSocket, const std::string& payloadStr);
void handleBoardCommand(int clientSocket, OpCode opcode, const std::string& payloadStr);

void broadcastToClients(const std::vector<int>& clients, int senderSocket, uint8_t msgType, const std::string& payloadStr, bool includeSender);
void cleanupClientSession(int clientSocket, const string& disconnectReason);
void stopClientConnections();
void waitForClientThreads();
void finishClientThread();
bool readExact(int sock, char* buffer, size_t bytesToRead);
bool sendPacket(int sock, uint8_t msgType, const std::string& payloadStr);
void sendError(int sock, int errCode, const std::string& errorMsg, const string& requestType);
BoardState* getBoardForSocketLocked(int clientSocket);

// Returns the name of a packet opcode for server log messages
string opcodeName(uint8_t msgType) {
    switch (static_cast<OpCode>(msgType)) {
        case OpCode::HEARTBEAT: return "HEARTBEAT";
        case OpCode::AUTH_REQ: return "AUTH_REQ";
        case OpCode::MOVE_CARD: return "MOVE_CARD";
        case OpCode::EDIT_CARD_CONTENT: return "EDIT_CARD_CONTENT";
        case OpCode::CREATE_CARD: return "CREATE_CARD";
        case OpCode::DELETE_CARD: return "DELETE_CARD";
        case OpCode::CREATE_CONNECTOR: return "CREATE_CONNECTOR";
        case OpCode::EDIT_CONNECTOR: return "EDIT_CONNECTOR";
        case OpCode::DELETE_CONNECTOR: return "DELETE_CONNECTOR";
        case OpCode::SYNC_BOARD: return "SYNC_BOARD";
        case OpCode::DISCONNECT: return "DISCONNECT";
        case OpCode::CLIENT_CONNECTED: return "CLIENT_CONNECTED";
        case OpCode::CLIENT_DISCONNECTED: return "CLIENT_DISCONNECTED";
        case OpCode::EDIT_BOARD_THEME: return "EDIT_BOARD_THEME";
        case OpCode::DELETE_OBJECTS: return "DELETE_OBJECTS";
        case OpCode::BRING_CARD_TO_FRONT: return "BRING_CARD_TO_FRONT";
    }
    return "UNKNOWN";
}

// Stops the server listener when the process receives a shutdown signal
void stopServer(int signalNumber) {
    (void)signalNumber;
    serverStopping = 1;
    if (listener >= 0) {
        close(listener);
    }
}

// Reads packets from one client and dispatches each request
void ChildThread(long childSocket)
{
    char headerBuf[5];

    while (1)
    {
        // handle data from a client
        if (!readExact(childSocket, headerBuf, 5)) {
            logServerMessage("INFO", "clientSocket=" + to_string(childSocket) + " event=connection_ended reason=header_read_failed");
            break;
        }
        else
        {
            uint8_t msgType = static_cast<uint8_t>(headerBuf[0]);

            uint32_t networkLen;
            memcpy(&networkLen, &headerBuf[1], 4);
            // network to host
            uint32_t payloadLen = ntohl(networkLen);

            if(payloadLen > 10 * 1024 * 1024) // if > 10 MB
            {
                sendError(childSocket, 413, "Packet payload exceeds 10 MB.", opcodeName(msgType));
                break;
            }
            else
            {
                vector<char> payloadBuf(payloadLen);
                if(payloadLen > 0 && !readExact(childSocket, payloadBuf.data(), payloadLen)) {
                    logServerMessage("WARN", "clientSocket=" + to_string(childSocket) + " request=" + opcodeName(msgType) + " outcome=rejected reason=payload_read_failed");
                    break;
                }
                else
                {
                    string payloadStr(payloadBuf.begin(), payloadBuf.end());

                    switch (msgType) {
                        case static_cast<uint8_t>(OpCode::HEARTBEAT): // HEARTBEAT (ping/pong)
                        {
                            logServerMessage("INFO", "clientSocket=" + to_string(childSocket) + " request=HEARTBEAT outcome=received");
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::AUTH_REQ): // AUTH_REQ
                        {
                            logServerMessage("INFO", "clientSocket=" + to_string(childSocket) + " request=AUTH_REQ outcome=received");
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::MOVE_CARD): // MOVE_CARD
                        case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT): // EDIT_CARD_CONTENT
                        case static_cast<uint8_t>(OpCode::CREATE_CARD): // CREATE_CARD
                        case static_cast<uint8_t>(OpCode::DELETE_CARD): // DELETE_CARD
                        case static_cast<uint8_t>(OpCode::CREATE_CONNECTOR): // CREATE_CONNECTOR
                        case static_cast<uint8_t>(OpCode::EDIT_CONNECTOR): // EDIT_CONNECTOR
                        case static_cast<uint8_t>(OpCode::DELETE_CONNECTOR): // DELETE_CONNECTOR
                        case static_cast<uint8_t>(OpCode::DELETE_OBJECTS):
                        case static_cast<uint8_t>(OpCode::EDIT_BOARD_THEME):
                        case static_cast<uint8_t>(OpCode::BRING_CARD_TO_FRONT):
                        {
                            handleBoardCommand(childSocket, static_cast<OpCode>(msgType), payloadStr);
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::SYNC_BOARD): // SYNC_BOARD
                        {
                            handleSyncBoard(childSocket, payloadStr);
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DISCONNECT): // DISCONNECT
                        {
                            logServerMessage("INFO", "clientSocket=" + to_string(childSocket) + " request=DISCONNECT outcome=received");
                            cleanupClientSession(childSocket, "client_request");
                            finishClientThread();
                            return;
                        }
                        default:
                        {
                            logServerMessage("WARN", "clientSocket=" + to_string(childSocket) + " request=UNKNOWN opcode=" + to_string(msgType) + " outcome=rejected");
                            break;
                        }
                    }
                }
            }
        }
    }
    cleanupClientSession(childSocket, "connection_closed");
    finishClientThread();
}


// Reads the requested number of bytes, handling partial TCP reads
bool readExact(int sock, char* buffer, size_t bytesToRead) {
    size_t bytesRead = 0;
    while (bytesRead < bytesToRead) {
        ssize_t result = recv(sock, buffer + bytesRead, bytesToRead - bytesRead, 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if(result <= 0) {
            // socket connection closed or erroring
            return false;
        }
        bytesRead += result;
    }
    return true;
}

// Sends every byte in a buffer before returning
bool sendAll(int sock, const char* buffer, size_t bytesToSend) {
    size_t bytesSent = 0;
    while (bytesSent < bytesToSend) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t result = send(sock, buffer + bytesSent, bytesToSend - bytesSent, flags);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        bytesSent += static_cast<size_t>(result);
    }
    return true;
}

// Sends one framed packet with its opcode and payload
bool sendPacket(int sock, uint8_t msgType, const string& payloadStr) {
    if (payloadStr.size() > 10 * 1024 * 1024) {
        return false;
    }
    lock_guard<mutex> lock(packetWriteMutex);
    char header[5];
    header[0] = static_cast<char>(msgType);
    const uint32_t networkLen = htonl(static_cast<uint32_t>(payloadStr.size()));
    memcpy(&header[1], &networkLen, sizeof(networkLen));
    return sendAll(sock, header, sizeof(header)) &&
           (payloadStr.empty() || sendAll(sock, payloadStr.data(), payloadStr.size()));
}

// Sends a JSON error response to one client
void sendError(int sock, int errCode, const string& errorMsg, const string& requestType) {
    const string level = errCode >= 500 ? "ERROR" : "WARN";
    logServerMessage(level, "clientSocket=" + to_string(sock) + " request=" + requestType +
                     " outcome=rejected errorCode=" + to_string(errCode) + " message=" + errorMsg);
    const string jsonPayload = json{{"errCode", errCode}, {"errorMsg", errorMsg}}.dump();
    if (!sendPacket(sock, 0xFF, jsonPayload)) {
        logServerMessage("ERROR", "clientSocket=" + to_string(sock) + " request=" + requestType + " response=send_failed");
    }
}

// Starts the server listener and accepts client connections
int main(int argc, char* argv[])
{
    struct sockaddr_in myaddr;     // server address
    struct sockaddr_in remoteaddr; // client address
    int newfd;                     // newly accept()ed socket descriptor
    int yes = 1;                   // for setsockopt() SO_REUSEADDR, below
    socklen_t addrlen;

    if (!openServerLog()) {
        logServerMessage("WARN", "Could not open opennote.log; writing server messages to the console only");
    }

    int serverPort = DEFAULT_SERVER_PORT;
    if (argc > 2) {
        logServerMessage("ERROR", "Usage: OpenNoteServer [port]");
        return 1;
    }
    if (argc == 2) {
        char* portEnd = nullptr;
        errno = 0;
        const long parsedPort = strtol(argv[1], &portEnd, 10);
        if (errno != 0 || portEnd == argv[1] || *portEnd != '\0' || parsedPort < 1 || parsedPort > 65535) {
            logServerMessage("ERROR", "Port must be a number between 1 and 65535.");
            return 1;
        }
        serverPort = static_cast<int>(parsedPort);
    }
    logServerMessage("INFO", "Server starting port=" + to_string(serverPort));

    if (!boardDatabase.open("opennote.db") || !boardDatabase.initialize()) {
        logServerMessage("ERROR", "Could not initialize the board database");
        return 1;
    }
    logServerMessage("INFO", "Board database ready");

    FD_ZERO(&master); // clear the master and temp sets

    // get the listener
    if ((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        logServerMessage("ERROR", string("socket failed: ") + strerror(errno));
        return 1;
    }

    // lose the pesky "address already in use" error message
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        logServerMessage("ERROR", string("setsockopt failed: ") + strerror(errno));
        close(listener);
        listener = -1;
        return 1;
    }

    // bind
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = INADDR_ANY;
    myaddr.sin_port = htons(serverPort);
    memset(&(myaddr.sin_zero), '\0', 8);
    if (::bind(listener, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1)
    {
        logServerMessage("ERROR", string("bind failed: ") + strerror(errno));
        close(listener);
        listener = -1;
        return 1;
    }

    // listen
    if (listen(listener, 10) == -1)
    {
        logServerMessage("ERROR", string("listen failed: ") + strerror(errno));
        close(listener);
        listener = -1;
        return 1;
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    signal(SIGINT, stopServer);
    signal(SIGTERM, stopServer);
    logServerMessage("INFO", "Server listening address=0.0.0.0 port=" + to_string(serverPort));

    addrlen = sizeof(remoteaddr);

    // main loop
    for (;;)
    {
        // handle new connections
        if ((newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen)) == -1)
        {
            if (serverStopping) {
                break;
            }
            logServerMessage("ERROR", string("accept failed: ") + strerror(errno));
            break;
        }
        else
        {
            {
                lock_guard<mutex> lock(masterMutex);
                FD_SET(newfd, &master); // add to master set
                if (newfd > fdmax)
                { // keep track of the maximum
                    fdmax = newfd;
                }
            }
            {
                lock_guard<mutex> lock(clientSocketsMutex);
                activeClientSockets.insert(newfd);
            }
            logServerMessage("INFO", "clientSocket=" + to_string(newfd) + " event=connected address=" + inet_ntoa(remoteaddr.sin_addr));

            {
                lock_guard<mutex> lock(clientThreadsMutex);
                activeClientThreads++;
            }
            try
            {
                thread cThread(ChildThread, (long)newfd);
                cThread.detach();
            }
            catch(const system_error &e)
            {
                logServerMessage("ERROR", string("Client thread creation failed: ") + e.what());
                cleanupClientSession(newfd, "thread_creation_failed");
                finishClientThread();
            }
        }
    }
    if (listener >= 0 && !serverStopping) {
        close(listener);
    }
    listener = -1;
    stopClientConnections();
    waitForClientThreads();
    logServerMessage("INFO", "Server shutting down");
    return serverStopping ? 0 : 1;
}

// Returns the board assigned to a client while the state lock is held
BoardState* getBoardForSocketLocked(int clientSocket) {
    auto clientIt = clientBoardMap.find(clientSocket);
    if (clientIt == clientBoardMap.end()) {
        return nullptr;
    }
    
    auto boardIt = activeBoards.find(clientIt->second);
    if (boardIt == activeBoards.end()) {
        return nullptr;
    }
    
    return &(boardIt->second);
}

// Sends a packet to selected clients, optionally including the sender
void broadcastToClients(const vector<int>& clients, int senderSocket, uint8_t msgType, const string& payloadStr, bool includeSender) {
    for (int clientSocket : clients) {
        if (!includeSender && clientSocket == senderSocket) {
            continue;
        }
        if (!sendPacket(clientSocket, msgType, payloadStr)) {
            logServerMessage("ERROR", "clientSocket=" + to_string(clientSocket) + " event=broadcast_failed");
        }
    }

}

// Applies one board command, returns errors to its sender, and broadcasts successful updates
void handleBoardCommand(int clientSocket, OpCode opcode, const string& payloadStr) {
    ServerBoardCommandResult result{false, 403, "Client is not synced to an active board.", string{}};
    vector<int> clients;
    uint32_t boardID = 0;

    {
        lock_guard<mutex> lock(stateMutex);
        BoardState* board = getBoardForSocketLocked(clientSocket);
        if (board) {
            boardID = board->boardID;
            BoardState previousBoard = *board;
            result = ServerBoardCommandProcessor::process(*board, opcode, payloadStr, clientSocket);
            if (result.success) {
                if (boardDatabase.saveBoard(*board)) {
                    clients = board->connectedClients;
                } else {
                    *board = previousBoard;
                    result = {false, 500, "Could not save board changes.", string{}};
                }
            }
        }
    }

    if (!result.success) {
        sendError(clientSocket, result.errorCode, result.errorMessage, opcodeName(static_cast<uint8_t>(opcode)));
        return;
    }

    logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " boardID=" + to_string(boardID) +
                     " request=" + opcodeName(static_cast<uint8_t>(opcode)) + " outcome=success");
    const bool includeSender = opcode == OpCode::CREATE_CARD || opcode == OpCode::CREATE_CONNECTOR;
    broadcastToClients(clients, clientSocket, static_cast<uint8_t>(opcode), result.updatePayload, includeSender);
}

// Removes a client from its board, notifies the other clients, and closes its socket
void cleanupClientSession(int clientSocket, const string& disconnectReason) {
    vector<int> clients;
    uint32_t boardID = 0;
    bool notifyOtherClients = true;

    {
        lock_guard<mutex> lock(stateMutex);

        auto boardIt = clientBoardMap.find(clientSocket);
        if (boardIt != clientBoardMap.end()) {
            boardID = boardIt->second;

            auto activeBoardIt = activeBoards.find(boardID);
            if (activeBoardIt != activeBoards.end()) {
                BoardState& board = activeBoardIt->second;

                auto& connectedClients = board.connectedClients;
                connectedClients.erase(remove(connectedClients.begin(), connectedClients.end(), clientSocket), connectedClients.end());
                clients = connectedClients;

            }

            clientBoardMap.erase(boardIt);
        }
    }

    {
        lock_guard<mutex> lock(clientSocketsMutex);
        activeClientSockets.erase(clientSocket);
        notifyOtherClients = !serverShuttingDown;
        close(clientSocket);
    }

    logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " event=disconnected reason=" +
                     disconnectReason + " boardID=" + to_string(boardID));

    if (boardID != 0 && notifyOtherClients) {
        nlohmann::json leavePayload = {
            {"clientSocket", clientSocket},
            {"boardID", boardID}
        };
        broadcastToClients(clients, -1, static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED), leavePayload.dump(), false);
    }
}

// Shuts down active client sockets so their threads can finish
void stopClientConnections() {
    lock_guard<mutex> lock(clientSocketsMutex);
    serverShuttingDown = true;
    for (int clientSocket : activeClientSockets) {
        if (shutdown(clientSocket, SHUT_RDWR) == -1 && errno != ENOTCONN) {
            logServerMessage("WARN", "clientSocket=" + to_string(clientSocket) +
                             " shutdown failed: " + strerror(errno));
        }
    }
}

// Waits until all client threads have cleaned up their sessions
void waitForClientThreads() {
    unique_lock<mutex> lock(clientThreadsMutex);
    clientThreadsFinished.wait(lock, [] { return activeClientThreads == 0; });
}

// Marks one client thread as finished and wakes the server when none remain
void finishClientThread() {
    lock_guard<mutex> lock(clientThreadsMutex);
    if (activeClientThreads > 0) {
        activeClientThreads--;
    }
    if (activeClientThreads == 0) {
        clientThreadsFinished.notify_all();
    }
}

// Joins a client to a board and sends the current board snapshot
bool handleSyncBoard(int clientSocket, const string& payloadStr) {
    try {
        auto jsonParsed = json::parse(payloadStr);
        if (!jsonParsed.is_object()) {
            sendError(clientSocket, 400, "SYNC_BOARD payload must be a JSON object.", "SYNC_BOARD");
            return false;
        }
        uint32_t boardID = jsonParsed.value("boardID", 0);

        if (boardID == 0) {
            sendError(clientSocket, 400, "Invalid board ID.", "SYNC_BOARD");
            return false;
        }

        vector<int> previousClients;
        vector<int> currentClients;
        uint32_t previousBoardID = 0;
        bool switchedBoards = false;

        {
            lock_guard<mutex> lock(stateMutex);

            auto activeBoard = activeBoards.find(boardID);
            if (activeBoard == activeBoards.end()) {
                BoardState loadedBoard;
                bool boardFound = false;
                if (!boardDatabase.loadBoard(boardID, loadedBoard, boardFound)) {
                    sendError(clientSocket, 500, "Could not load board from the database.", "SYNC_BOARD");
                    return false;
                }
                if (!boardFound && !boardDatabase.saveBoard(loadedBoard)) {
                    sendError(clientSocket, 500, "Could not create board in the database.", "SYNC_BOARD");
                    return false;
                }
                activeBoard = activeBoards.emplace(boardID, loadedBoard).first;
            }

            const string snapshot = ServerBoardCommandProcessor::boardSnapshotToJSON(activeBoard->second).dump();
            if (!sendPacket(clientSocket, static_cast<uint8_t>(OpCode::SYNC_BOARD), snapshot)) {
                sendError(clientSocket, 500, "Failed to send board snapshot.", "SYNC_BOARD");
                return false;
            }

            const auto previousBoard = clientBoardMap.find(clientSocket);
            if (previousBoard != clientBoardMap.end() && previousBoard->second != boardID) {
                previousBoardID = previousBoard->second;
                auto previous = activeBoards.find(previousBoardID);
                if (previous != activeBoards.end()) {
                    auto& connectedClients = previous->second.connectedClients;
                    connectedClients.erase(remove(connectedClients.begin(), connectedClients.end(), clientSocket), connectedClients.end());
                    previousClients = connectedClients;
                    switchedBoards = true;
                }
            }

            clientBoardMap[clientSocket] = boardID;

            auto& connectedClients = activeBoard->second.connectedClients;
            if (std::find(connectedClients.begin(), connectedClients.end(), clientSocket) == connectedClients.end()) {
                connectedClients.push_back(clientSocket);
            }
            currentClients = connectedClients;
        }

        if (switchedBoards) {
            nlohmann::json leavePayload = {
                {"clientSocket", clientSocket},
                {"boardID", previousBoardID}
            };
            broadcastToClients(previousClients, -1, static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED), leavePayload.dump(), false);
        }

        nlohmann::json joinPayload = {{"childSocket", clientSocket}, {"boardID", boardID}};
        broadcastToClients(currentClients, clientSocket, static_cast<uint8_t>(OpCode::CLIENT_CONNECTED), joinPayload.dump(), false);

        logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " boardID=" + to_string(boardID) +
                         " request=SYNC_BOARD outcome=success");

    } catch (const exception&) {
        sendError(clientSocket, 400, "Malformed JSON in SYNC_BOARD.", "SYNC_BOARD");
        return false;
    }
    return true;
}

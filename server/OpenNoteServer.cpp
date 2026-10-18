#include <cerrno>
#include <cmath>
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
#include <chrono>
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
constexpr int AUTO_SAVE_INTERVAL_SECONDS = 30;
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
mutex autoSaveMutex;
condition_variable autoSaveCondition;
bool autoSaveStopping = false;

bool handleSyncBoard(int clientSocket, const std::string& payloadStr);
bool handleSaveBoard(int clientSocket, const std::string& payloadStr);
bool handleListBoards(int clientSocket, const std::string& payloadStr);
bool handleCreateBoard(int clientSocket, const std::string& payloadStr);
bool handleDeleteBoard(int clientSocket, const std::string& payloadStr);
void handleBoardCommand(int clientSocket, OpCode opcode, const std::string& payloadStr);

void broadcastToClients(const std::vector<int>& clients, int senderSocket, uint8_t msgType, const std::string& payloadStr, bool includeSender);
void cleanupClientSession(int clientSocket, const string& disconnectReason);
void stopClientConnections();
void waitForClientThreads();
void finishClientThread();
void saveActiveBoards(const string& saveReason);
void autoSaveBoards();
void stopAutoSave();
bool readExact(int sock, char* buffer, size_t bytesToRead);
bool sendPacket(int sock, uint8_t msgType, const std::string& payloadStr);
void sendError(int sock, int errCode, const std::string& errorMsg, const string& requestType);
BoardState* getBoardForSocketLocked(int clientSocket);

// Returns the name of a packet opcode for server log messages
string opcodeName(uint8_t msgType) {
    switch (static_cast<OpCode>(msgType)) {
        case OpCode::HEARTBEAT: return "HEARTBEAT";
        case OpCode::ERROR: return "ERROR";
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
        case OpCode::SAVE_BOARD: return "SAVE_BOARD";
        case OpCode::BOARD_SAVED: return "BOARD_SAVED";
        case OpCode::LIST_BOARDS: return "LIST_BOARDS";
        case OpCode::BOARD_LIST: return "BOARD_LIST";
        case OpCode::CREATE_BOARD: return "CREATE_BOARD";
        case OpCode::DELETE_BOARD: return "DELETE_BOARD";
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
                        case static_cast<uint8_t>(OpCode::ERROR): // ERROR
                        {
                            const json errorReport = json::parse(payloadStr, nullptr, false);
                            string unknownOpcode = "unknown";
                            if (errorReport.is_object() && errorReport.contains("unknownOpcode") &&
                                (errorReport["unknownOpcode"].is_number_integer() ||
                                 errorReport["unknownOpcode"].is_number_unsigned())) {
                                unknownOpcode = errorReport["unknownOpcode"].dump();
                            }
                            logServerMessage("WARN", "clientSocket=" + to_string(childSocket) +
                                             " request=ERROR outcome=client_reported_unknown_opcode opcode=" + unknownOpcode);
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
                        case static_cast<uint8_t>(OpCode::SAVE_BOARD): // SAVE_BOARD
                        {
                            handleSaveBoard(childSocket, payloadStr);
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::LIST_BOARDS): // LIST_BOARDS
                        {
                            handleListBoards(childSocket, payloadStr);
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_BOARD): // CREATE_BOARD
                        {
                            handleCreateBoard(childSocket, payloadStr);
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_BOARD): // DELETE_BOARD
                        {
                            handleDeleteBoard(childSocket, payloadStr);
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
                            sendError(childSocket, 400, "Unknown opcode " + to_string(msgType) + ".", "UNKNOWN");
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
    if (!sendPacket(sock, static_cast<uint8_t>(OpCode::ERROR), jsonPayload)) {
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

    thread autoSaveThread;
    try {
        autoSaveThread = thread(autoSaveBoards);
    } catch (const system_error& error) {
        logServerMessage("ERROR", string("Could not start the board auto-save thread: ") + error.what());
        close(listener);
        listener = -1;
        return 1;
    }

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
    stopAutoSave();
    if (autoSaveThread.joinable()) {
        autoSaveThread.join();
    }
    saveActiveBoards("server_shutdown");
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
    broadcastToClients(clients, clientSocket, static_cast<uint8_t>(opcode), result.updatePayload, true);
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
                if (connectedClients.empty()) {
                    if (boardDatabase.saveBoard(board, false)) {
                        logServerMessage("INFO", "boardID=" + to_string(boardID) + " event=board_saved reason=last_client_disconnected");
                    } else {
                        logServerMessage("ERROR", "boardID=" + to_string(boardID) + " event=last_client_save_failed");
                    }
                }

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

// Saves every board currently loaded on the server without changing its edit date
void saveActiveBoards(const string& saveReason) {
    lock_guard<mutex> lock(stateMutex);
    for (auto& [boardID, board] : activeBoards) {
        if (boardDatabase.saveBoard(board, false)) {
            logServerMessage("INFO", "boardID=" + to_string(boardID) + " event=board_saved reason=" + saveReason);
        } else {
            logServerMessage("ERROR", "boardID=" + to_string(boardID) + " event=board_save_failed reason=" + saveReason);
        }
    }
}

// Saves loaded boards every 30 seconds until the server is shutting down
void autoSaveBoards() {
    unique_lock<mutex> lock(autoSaveMutex);
    while (!autoSaveCondition.wait_for(lock, chrono::seconds(AUTO_SAVE_INTERVAL_SECONDS), [] {
        return autoSaveStopping;
    })) {
        lock.unlock();
        saveActiveBoards("periodic_autosave");
        lock.lock();
    }
}

// Wakes the auto-save thread so it can stop before the server exits
void stopAutoSave() {
    {
        lock_guard<mutex> lock(autoSaveMutex);
        autoSaveStopping = true;
    }
    autoSaveCondition.notify_all();
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
                if (!boardFound) {
                    sendError(clientSocket, 404, "Board does not exist.", "SYNC_BOARD");
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
                    if (connectedClients.empty()) {
                        if (boardDatabase.saveBoard(previous->second, false)) {
                            logServerMessage("INFO", "boardID=" + to_string(previousBoardID) +
                                             " event=board_saved reason=last_client_left_board");
                        } else {
                            logServerMessage("ERROR", "boardID=" + to_string(previousBoardID) +
                                             " event=last_client_save_failed");
                        }
                    }
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

// Saves the board assigned to the requesting client and confirms the saved name
bool handleSaveBoard(int clientSocket, const string& payloadStr) {
    try {
        const json request = json::parse(payloadStr);
        if (!request.is_object()) {
            sendError(clientSocket, 400, "SAVE_BOARD payload must be a JSON object.", "SAVE_BOARD");
            return false;
        }

        const bool isSaveAs = request.contains("boardName");
        string requestedBoardName;
        if (isSaveAs) {
            if (!request["boardName"].is_string()) {
                sendError(clientSocket, 400, "Board name must be text.", "SAVE_BOARD");
                return false;
            }
            requestedBoardName = request["boardName"].get<string>();
            if (requestedBoardName.size() > MAX_BOARD_NAME_LENGTH ||
                requestedBoardName.find_first_not_of(" \t\r\n") == string::npos) {
                sendError(clientSocket, 400, "Board name must contain between 1 and 64 characters.", "SAVE_BOARD");
                return false;
            }
        }

        json response;
        bool clientHasBoard = false;
        bool boardSaved = false;
        bool boardNameAlreadyExists = false;
        bool boardNameLookupFailed = false;
        {
            lock_guard<mutex> lock(stateMutex);
            BoardState* board = getBoardForSocketLocked(clientSocket);
            if (board != nullptr) {
                clientHasBoard = true;
                if (isSaveAs) {
                    BoardList boardList;
                    if (boardDatabase.listBoards(boardList)) {
                        auto existingBoard = boardList.find(requestedBoardName);
                        boardNameAlreadyExists = existingBoard != boardList.end() &&
                                                 existingBoard->second.boardID != board->boardID;
                    } else {
                        boardNameLookupFailed = true;
                    }
                }

                if (!boardNameAlreadyExists && !boardNameLookupFailed) {
                    const string previousBoardName = board->boardName;
                    if (isSaveAs) {
                        board->boardName = requestedBoardName;
                    }
                    boardSaved = boardDatabase.saveBoard(*board, false);
                    if (!boardSaved) {
                        board->boardName = previousBoardName;
                    }
                }
                if (boardSaved) {
                    response = {{"boardID", board->boardID}, {"boardName", board->boardName}};
                }
            }
        }

        if (!clientHasBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.", "SAVE_BOARD");
            return false;
        }
        if (boardNameAlreadyExists) {
            sendError(clientSocket, 409, "A board with this name already exists.", "SAVE_BOARD");
            return false;
        }
        if (boardNameLookupFailed) {
            sendError(clientSocket, 500, "Could not check the board name.", "SAVE_BOARD");
            return false;
        }
        if (!boardSaved) {
            sendError(clientSocket, 500, "Could not save board.", "SAVE_BOARD");
            return false;
        }
        if (!sendPacket(clientSocket, static_cast<uint8_t>(OpCode::BOARD_SAVED), response.dump())) {
            logServerMessage("ERROR", "clientSocket=" + to_string(clientSocket) + " request=SAVE_BOARD outcome=confirmation_failed");
            return false;
        }
        logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " boardID=" +
                         to_string(response.at("boardID").get<uint32_t>()) + " request=SAVE_BOARD outcome=success");
        return true;
    } catch (const exception&) {
        sendError(clientSocket, 400, "Malformed JSON in SAVE_BOARD.", "SAVE_BOARD");
        return false;
    }
}

// Sends the saved and active board names, IDs, and dates to one client
bool handleListBoards(int clientSocket, const string& payloadStr) {
    try {
        const json request = json::parse(payloadStr);
        if (!request.is_object()) {
            sendError(clientSocket, 400, "LIST_BOARDS payload must be a JSON object.", "LIST_BOARDS");
            return false;
        }

        BoardList boardList;
        bool boardListLoaded = false;
        {
            lock_guard<mutex> lock(stateMutex);
            boardListLoaded = boardDatabase.listBoards(boardList);
            if (boardListLoaded) {
                for (const auto& entry : activeBoards) {
                    const BoardState& board = entry.second;
                    if (board.boardName.empty()) continue;

                    BoardInfo boardInfo;
                    auto savedBoard = boardList.find(board.boardName);
                    if (savedBoard != boardList.end()) {
                        boardInfo = savedBoard->second;
                    }
                    boardInfo.boardID = board.boardID;
                    if (!board.createdAt.empty()) {
                        boardInfo.createdAt = board.createdAt;
                    }
                    if (!board.lastEditedAt.empty()) {
                        boardInfo.lastEditedAt = board.lastEditedAt;
                    }
                    boardInfo.theme = board.theme;
                    boardList[board.boardName] = boardInfo;
                }
            }
        }

        if (!boardListLoaded) {
            sendError(clientSocket, 500, "Could not list boards.", "LIST_BOARDS");
            return false;
        }

        json boardItems = json::array();
        for (const auto& entry : boardList) {
            boardItems.push_back({
                {"boardName", entry.first},
                {"boardID", entry.second.boardID},
                {"createdAt", entry.second.createdAt},
                {"lastEditedAt", entry.second.lastEditedAt},
                {"theme", entry.second.theme}
            });
        }
        const string response = json{{"boards", boardItems}}.dump();
        if (!sendPacket(clientSocket, static_cast<uint8_t>(OpCode::BOARD_LIST), response)) {
            logServerMessage("ERROR", "clientSocket=" + to_string(clientSocket) + " request=LIST_BOARDS response=send_failed");
            return false;
        }
        logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " request=LIST_BOARDS outcome=success");
        return true;
    } catch (const exception&) {
        sendError(clientSocket, 400, "Malformed JSON in LIST_BOARDS.", "LIST_BOARDS");
        return false;
    }
}

// Creates a new board with a server-assigned ID and joins the requesting client
bool handleCreateBoard(int clientSocket, const string& payloadStr) {
    try {
        const json request = json::parse(payloadStr);
        if (!request.is_object()) {
            sendError(clientSocket, 400, "CREATE_BOARD payload must be a JSON object.", "CREATE_BOARD");
            return false;
        }

        BoardState newBoard;
        bool boardCreated = false;
        {
            lock_guard<mutex> lock(stateMutex);
            boardCreated = boardDatabase.getNextBoardID(newBoard.boardID);
            if (boardCreated) {
                boardCreated = boardDatabase.saveBoard(newBoard, false);
            }
        }

        if (!boardCreated) {
            sendError(clientSocket, 500, "Could not create board.", "CREATE_BOARD");
            return false;
        }

        logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " boardID=" +
                         to_string(newBoard.boardID) + " request=CREATE_BOARD outcome=success");
        return handleSyncBoard(clientSocket, json{{"boardID", newBoard.boardID}}.dump());
    } catch (const exception&) {
        sendError(clientSocket, 400, "Malformed JSON in CREATE_BOARD.", "CREATE_BOARD");
        return false;
    }
}

// Deletes a saved board when no clients are connected to it
bool handleDeleteBoard(int clientSocket, const string& payloadStr) {
    try {
        const json request = json::parse(payloadStr);
        if (!request.is_object()) {
            sendError(clientSocket, 400, "DELETE_BOARD payload must be a JSON object.", "DELETE_BOARD");
            return false;
        }

        const uint32_t boardID = request.value("boardID", 0u);
        if (boardID == 0) {
            sendError(clientSocket, 400, "Invalid board ID.", "DELETE_BOARD");
            return false;
        }

        bool boardExists = false;
        bool boardHasClients = false;
        bool boardListLoaded = false;
        bool boardDeleted = false;
        {
            lock_guard<mutex> lock(stateMutex);
            auto activeBoard = activeBoards.find(boardID);
            boardHasClients = activeBoard != activeBoards.end() && !activeBoard->second.connectedClients.empty();

            if (!boardHasClients) {
                BoardList boardList;
                boardListLoaded = boardDatabase.listBoards(boardList);
                if (boardListLoaded) {
                    for (const auto& entry : boardList) {
                        if (entry.second.boardID == boardID) {
                            boardExists = true;
                            break;
                        }
                    }
                    if (boardExists) {
                        boardDeleted = boardDatabase.deleteBoard(boardID);
                        if (boardDeleted) {
                            activeBoards.erase(boardID);
                        }
                    }
                }
            }
        }

        if (boardHasClients) {
            sendError(clientSocket, 409, "Cannot delete a board while clients are connected.", "DELETE_BOARD");
            return false;
        }
        if (!boardListLoaded) {
            sendError(clientSocket, 500, "Could not check the board list.", "DELETE_BOARD");
            return false;
        }
        if (!boardExists) {
            sendError(clientSocket, 404, "Board does not exist.", "DELETE_BOARD");
            return false;
        }
        if (!boardDeleted) {
            sendError(clientSocket, 500, "Could not delete board.", "DELETE_BOARD");
            return false;
        }

        logServerMessage("INFO", "clientSocket=" + to_string(clientSocket) + " boardID=" +
                         to_string(boardID) + " request=DELETE_BOARD outcome=success");
        return handleListBoards(clientSocket, json::object().dump());
    } catch (const exception&) {
        sendError(clientSocket, 400, "Malformed JSON in DELETE_BOARD.", "DELETE_BOARD");
        return false;
    }
}

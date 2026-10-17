#include <iostream>
#include <cerrno>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cctype>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <system_error>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "BoardStructs.h"
#include "BoardDatabase.h"
#include "ServerBoardCommandProcessor.h"

using namespace std;
using json = nlohmann::json;

#define PORT 5432 // port we're listening on
#define MAX_LINE 256

fd_set master; // master file descriptor list
int listener;  // listening socket descriptor
int fdmax;
mutex masterMutex; // guards master/fdmax across threads

unordered_map<uint32_t, BoardState> activeBoards; // Maps boardID to boardState in memory
unordered_map<int, uint32_t> clientBoardMap; // Maps socket identifier to a active boardID
BoardDatabase boardDatabase;
mutex stateMutex;
mutex packetWriteMutex;

bool handleSyncBoard(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
void handleBoardCommand(int clientSocket, OpCode opcode, const std::string& payloadStr);

void broadcastUpdate(BoardState* board, int senderSocket, uint8_t msgType, const std::string& payloadStr, bool includeSender = false);
void broadcastToClients(const std::vector<int>& clients, int senderSocket, uint8_t msgType, const std::string& payloadStr, bool includeSender);
void cleanupClientSession(int clientSocket);
bool readExact(int sock, char* buffer, size_t bytesToRead);
bool sendPacket(int sock, uint8_t msgType, const std::string& payloadStr);
void sendError(int sock, int errCode, const std::string& errorMsg);
BoardState* getBoardForSocketLocked(int clientSocket);

// Reads packets from one client and dispatches each request
void ChildThread(long childSocket)
{
    char headerBuf[5];

    while (1)
    {
        // handle data from a client
        if (!readExact(childSocket, headerBuf, 5)) {
            cerr << "Client disconnected or header read failed. \n";
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
                cerr << "ERROR : Payload length exceeds 10MB. \n";
                sendError(childSocket, 413, "Packet payload exceeds 10 MB.");
                break;
            }
            else
            {
                vector<char> payloadBuf(payloadLen);
                if(payloadLen > 0 && !readExact(childSocket, payloadBuf.data(), payloadLen)) {
                    cerr << "ERROR : Failed to read full payload stream. \n";
                    break;
                }
                else
                {
                    string payloadStr(payloadBuf.begin(), payloadBuf.end());

                    switch (msgType) {
                        case static_cast<uint8_t>(OpCode::HEARTBEAT): // HEARTBEAT (ping/pong)
                        {
                            std::cout << "[SERVER] : Received HEARTBEAT\n";
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::AUTH_REQ): // AUTH_REQ
                        {
                            std::cout << "[SERVER] : Received AUTH_REQ: " << payloadStr << "\n";
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
                            BoardState* targetBoard = nullptr;
                            if(handleSyncBoard(childSocket, payloadStr, targetBoard))
                            {
                                json joinPayload = {{"childSocket", childSocket},
                                {"boardID", targetBoard->boardID}};
                                broadcastUpdate(targetBoard, childSocket, static_cast<uint8_t>(OpCode::CLIENT_CONNECTED), joinPayload.dump());
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DISCONNECT): // DISCONNECT
                        {
                            std::cout << "[SERVER] : Received explicit DISCONNECT from: " << childSocket << "\n";
                            cleanupClientSession(childSocket);
                            return;
                        }
                        default:
                        {
                            std::cout << "ERROR : Unknown Opcode: 0x" << hex << (int)msgType << "\n";
                            break;
                        }
                    }
                }
            }
        }
    }
    cleanupClientSession(childSocket);
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
void sendError(int sock, int errCode, const string& errorMsg) {
    const string jsonPayload = json{{"errCode", errCode}, {"errorMsg", errorMsg}}.dump();
    sendPacket(sock, 0xFF, jsonPayload);
}

// Starts the server listener and accepts client connections
int main(void)
{
    struct sockaddr_in myaddr;     // server address
    struct sockaddr_in remoteaddr; // client address
    int newfd;                     // newly accept()ed socket descriptor
    int yes = 1;                   // for setsockopt() SO_REUSEADDR, below
    socklen_t addrlen;

    if (!boardDatabase.open("opennote.db") || !boardDatabase.initialize()) {
        cerr << "[ERROR] : Could not initialize the board database.\n";
        return 1;
    }

    FD_ZERO(&master); // clear the master and temp sets

    // get the listener
    if ((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    // lose the pesky "address already in use" error message
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("setsockopt");
        exit(1);
    }

    // bind
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = INADDR_ANY;
    myaddr.sin_port = htons(PORT);
    memset(&(myaddr.sin_zero), '\0', 8);
    if (bind(listener, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    // listen
    if (listen(listener, 10) == -1)
    {
        perror("listen");
        exit(1);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    addrlen = sizeof(remoteaddr);

    // main loop
    for (;;)
    {
        // handle new connections
        if ((newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen)) == -1)
        {
            perror("accept");
            exit(1);
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
            std::cout << "multiThreadServer: new connection from "
                 << inet_ntoa(remoteaddr.sin_addr)
                 << " socket " << newfd << endl;

            try
            {
                thread cThread(ChildThread, (long)newfd);
                cThread.detach();
            }
            catch(const system_error &e)
            {
                cerr << "thread creation failed: " << e.what() << endl;
                exit(1);
            }
        }
    }
    return 0;
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

// Sends a board update to its connected clients, optionally including the sender
void broadcastUpdate(BoardState* board, int senderSocket, uint8_t msgType, const string& payloadStr, bool includeSender) {
    if (!board)
    {
        return;
    }

    broadcastToClients(board->connectedClients, senderSocket, msgType, payloadStr, includeSender);
}

// Sends a packet to selected clients, optionally including the sender
void broadcastToClients(const vector<int>& clients, int senderSocket, uint8_t msgType, const string& payloadStr, bool includeSender) {
    for (int clientSocket : clients) {
        if (!includeSender && clientSocket == senderSocket) {
            continue;
        }
        if (!sendPacket(clientSocket, msgType, payloadStr)) {
            cerr << "[ERROR] : Failed to broadcast update to socket " << clientSocket << "\n";
        }
    }

}

// Applies one board command, returns errors to its sender, and broadcasts successful updates
void handleBoardCommand(int clientSocket, OpCode opcode, const string& payloadStr) {
    ServerBoardCommandResult result{false, 403, "Client is not synced to an active board.", string{}};
    vector<int> clients;

    {
        lock_guard<mutex> lock(stateMutex);
        BoardState* board = getBoardForSocketLocked(clientSocket);
        if (board) {
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
        sendError(clientSocket, result.errorCode, result.errorMessage);
        return;
    }

    const bool includeSender = opcode == OpCode::CREATE_CARD || opcode == OpCode::CREATE_CONNECTOR;
    broadcastToClients(clients, clientSocket, static_cast<uint8_t>(opcode), result.updatePayload, includeSender);
}

// Removes a client from its board, notifies the other clients, and closes its socket
void cleanupClientSession(int clientSocket) {
    lock_guard<mutex> lock(stateMutex);

    auto boardIt = clientBoardMap.find(clientSocket);
    if (boardIt != clientBoardMap.end()) {
        uint32_t boardID = boardIt->second;

        auto activeBoardIt = activeBoards.find(boardID);
        if (activeBoardIt != activeBoards.end()) {
            BoardState& board = activeBoardIt->second;

            auto& clients = board.connectedClients;
            clients.erase(remove(clients.begin(), clients.end(), clientSocket), clients.end());

            // broadcast user left
            nlohmann::json leavePayload = {
                {"clientSocket", clientSocket},
                {"boardID", boardID}
            };

            broadcastUpdate(&board, -1, static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED), leavePayload.dump());

            std::cout << "[SERVER] : Client " << clientSocket << " removed from Board ID " << boardID << "\n";
        }

        clientBoardMap.erase(boardIt);
    }

    close(clientSocket);
}

// Joins a client to a board and sends the current board snapshot
bool handleSyncBoard(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);
        if (!jsonParsed.is_object()) {
            sendError(clientSocket, 400, "SYNC_BOARD payload must be a JSON object.");
            return false;
        }
        uint32_t boardID = jsonParsed.value("boardID", 0);

        if (boardID == 0) {
            sendError(clientSocket, 400, "Invalid board ID.");
            return false;
        }

        lock_guard<mutex> lockk(stateMutex);

        auto activeBoard = activeBoards.find(boardID);
        if (activeBoard == activeBoards.end()) {
            BoardState loadedBoard;
            bool boardFound = false;
            if (!boardDatabase.loadBoard(boardID, loadedBoard, boardFound)) {
                sendError(clientSocket, 500, "Could not load board from the database.");
                return false;
            }
            if (!boardFound && !boardDatabase.saveBoard(loadedBoard)) {
                sendError(clientSocket, 500, "Could not create board in the database.");
                return false;
            }
            activeBoard = activeBoards.emplace(boardID, loadedBoard).first;
        }

        const auto previousBoard = clientBoardMap.find(clientSocket);
        if (previousBoard != clientBoardMap.end() && previousBoard->second != boardID) {
            auto previous = activeBoards.find(previousBoard->second);
            if (previous != activeBoards.end()) {
                auto& previousClients = previous->second.connectedClients;
                previousClients.erase(remove(previousClients.begin(), previousClients.end(), clientSocket), previousClients.end());
            }
        }

        clientBoardMap[clientSocket] = boardID;

        outBoard = &activeBoard->second;

        auto& clients = outBoard->connectedClients;
        if (std::find(clients.begin(), clients.end(), clientSocket) == clients.end()) {
            clients.push_back(clientSocket);
        }

        // SYNC_BOARD replies with a complete snapshot on the same opcode
        if (!sendPacket(clientSocket, static_cast<uint8_t>(OpCode::SYNC_BOARD), ServerBoardCommandProcessor::boardSnapshotToJSON(*outBoard).dump())) {
            sendError(clientSocket, 500, "Failed to send board snapshot.");
            return false;
        }

        std::cout << "[SERVER] : Socket " << clientSocket << " successfully synced to Board ID " << boardID << "\n";

    } catch (const exception& e) {
        sendError(clientSocket, 400, "Malformed JSON in SYNC_BOARD.");
        return false;
    }
    return true;
}

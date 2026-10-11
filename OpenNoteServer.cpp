#include <iostream>
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
#include <nlohmann/json.hpp>

#include "BoardStructs.h"

using namespace std;
using json = nlohmann::json;

#define PORT 5432 // port we're listening on
#define MAX_LINE 256

fd_set master; // master file descriptor list
int listener;  // listening socket descriptor
int fdmax;
mutex masterMutex; // guards master/fdmax across threads

std::unordered_map<uint32_t, BoardState> activeBoards; // Maps boardID to boardState in memory
std::unordered_map<int, uint32_t> clientBoardMap; // Maps socket identifier to a active boardID
std::mutex stateMutex;

// the child thread
void ChildThread(long childSocket)
{
    char headerBuf[5];
    char buf[MAX_LINE];
    int nbytes;
    int j;

    while (1)
    {
        // handle data from a client
        if (!read(childSocket, headerBuf, 5)) {
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
            }
            else
            {
                std::vector<char> payloadBuf(payloadLen);
                if(payloadLen > 0 && !read(childSocket, payloadBuf.data(), payloadLen)) {
                    cerr << "ERROR : Failed to read full payload stream. \n";
                    break;
                }
                else
                {
                    string payloadStr(payloadBuf.begin(), payloadBuf.end());

                    switch (msgType) {
                        case 0x00: // HEARTBEAT (ping/pong)
                            cout << "[SERVER] : Received HEARTBEAT\n";
                            break;
                        case 0x01: // AUTH_REQ
                            cout << "[SERVER] : Received AUTH_REQ: " << payloadStr << "\n";
                            break;
                        case 0x02: // MOVE_CARD
                            cout << "[SERVER] : Received MOVE_CARD:" << payloadStr << "\n";
                            break;
                        case 0x03: // EDIT_CONTENT
                            cout << "[SERVER] : Received EDIT_CONTENT:" << payloadStr << "\n";
                            break;
                        case 0x04: // CREATE_CARD
                            handleCreateCard(childSocket, payloadStr);
                            break;
                        case 0x05: // DELETE_CARD
                            cout << "[SERVER] : Received DELETE_CARD:" << payloadStr << "\n";
                            break;
                        case 0x06: // CREATE_CONNECTOR
                            cout << "[SERVER] : Received CREATE_CONNECTOR:" << payloadStr << "\n";
                            break;
                        case 0x07: // EDIT_CONNECTOR
                            cout << "[SERVER] : Received EDIT_CONNECTOR:" << payloadStr << "\n";
                            break;
                        case 0x08: // DELETE_CONNECTOR
                            cout << "[SERVER] : Received DELETE_CONNECTOR:" << payloadStr << "\n";
                            break;
                        case 0x09: // SYNC_BOARD
                            cout << "[SERVER] : Received SYNC_BOARD:" << payloadStr << "\n";
                            break;
                        case 0x10: // DISCONNECT
                            cout << "[SERVER] : Received DISCONNECT from: " << childSocket << "\n";
                            close(childSocket);
                            return;
                        default:
                            cout << "ERROR : Unknown Opcode: 0x" << std::hex << (int)msgType << "\n";
                            break;
                    }
                }
            }
        }
    }
    close(childSocket);
}


// helper method to read exactly bytesToRead number of bytes from the socket
bool read(int sock, char* buffer, size_t bytesToRead) {
    size_t bytesRead = 0;
    while (bytesRead < bytesToRead) {
        ssize_t result = recv(sock, buffer + bytesRead, bytesToRead - bytesRead, 0);
        if(result <= 0) {
            // socket connection closed or erroring
            return false;
        }
        bytesRead += result;
    }
    return true;
}

// helper method to send error to client
void sendError(int sock, int errCode, const string& errorMsg) {
    string jsonPayload = "{\"errCode\":" + to_string(errCode) + 
                              ",\"errorMsg\":\"" + errorMsg + "\"}";
    
    uint32_t payloadLen = static_cast<uint32_t>(jsonPayload.size());
    
    char header[5];
    header[0] = static_cast<char>(0xFF);
    
    uint32_t networkLen = htonl(payloadLen);
    memcpy(&header[1], &networkLen, 4);
    
    send(sock, header, 5, 0);
    send(sock, jsonPayload.data(), payloadLen, 0);
}

int main(void)
{
    struct sockaddr_in myaddr;     // server address
    struct sockaddr_in remoteaddr; // client address
    int newfd;                     // newly accept()ed socket descriptor
    int yes = 1;                   // for setsockopt() SO_REUSEADDR, below
    socklen_t addrlen;

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
            cout << "multiThreadServer: new connection from "
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

// ADD: Save to SQL database & Broadcast to connected clients
void handleCreateCard(int clientSocket, const string& payloadStr) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        Card newCard;
        newCard.cardID = jsonParsed.value("cardID", 0);
        newCard.x = jsonParsed.value("x", 0.0f);
        newCard.y = jsonParsed.value("y", 0.0f);
        newCard.width = jsonParsed.value("width", 100.0f);
        newCard.height = jsonParsed.value("height", 100.0f);
        newCard.text = jsonParsed.value("text", "");

        if (!newCard.isValid()) {
            sendError(clientSocket, 400, "Invalid card structure or text length.");
            return;
        }

        std::lock_guard<std::mutex> lock(stateMutex);

        BoardState* board = getBoardForSocketLocked(clientSocket);
        if (!board) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return;
        }

        board->cards[newCard.cardID] = newCard;

        cout << "[SERVER] : Created card ID " << newCard.cardID << "in Board" << board->boardID << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in CREATE_CARD.");
    }
}

void handleDeleteCard(int clientSocket, const string& payloadStr) {
    
}

void handleEditCard(int clientSocket, const string& payloadStr) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        Card updatedCard;

        uint32_t cardID = jsonParsed.value("cardID", 0);
        if (cardID == 0) {
            sendError(clientSocket, 400, "Missing or invalid cardID for edit.");
            return;
        }

        std::lock_guard<std::mutex> lock(stateMutex);

        BoardState* board = getBoardForSocketLocked(clientSocket);
        if (!board) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return;
        }

        auto cardIt = board->cards.find(cardID);
        if (cardIt == board->cards.end()) {
            sendError(clientSocket, 404, "Card ID not found on this board.");
            return;
        }
        Card& card = cardIt->second;

        if (jsonParsed.contains("x")) {
            card.x = jsonParsed["x"];
        }
        if (jsonParsed.contains("y")) {
            card.y = jsonParsed["y"];
        }
        if (jsonParsed.contains("width")) {
            float newWidth = jsonParsed["width"];
            if (newWidth >= 10.0f) card.width = newWidth;
        }
        if (jsonParsed.contains("height")) {
            float newHeight = jsonParsed["height"];
            if (newHeight >= 10.0f) card.height = newHeight;
        }
        if (jsonParsed.contains("text")) {
            std::string newText = jsonParsed["text"];
            if (newText.length() <= 4096) card.text = newText;
        }
        if (jsonParsed.contains("zIndex")) {
            card.zIndex = jsonParsed["zIndex"];
        }
        if (jsonParsed.contains("mediaURL")) {
            card.mediaURL = jsonParsed["mediaURL"];
        }

        cout << "[SERVER] : Updated card " << cardID << "on Board" << board->boardID << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in EDIT_CARD.");
    }
}

// ADD: SQLITE FUNCTIONALITY FOR CHECKING & LOADING SAVED BOARDS
void handleSyncBoard(int clientSocket, const std::string& payloadStr) {
    try {
        auto jsonParsed = json::parse(payloadStr);
        uint32_t boardID = jsonParsed.value("boardID", 0);

        if (boardID == 0) {
            sendError(clientSocket, 400, "Invalid board IDI.");
            return;
        }

        std::lock_guard<std::mutex> lockk(stateMutex);

        clientBoardMap[clientSocket] = boardID;

        // Creat/load board in memory if does not eist
        if (activeBoards.find(boardID) == activeBoards.end()) {

            BoardState newBoard;
            newBoard.boardID = boardID;
            activeBoards[boardID] = newBoard;

            cout << "[SERVER] : Socket " << clientSocket << " successfully synced to Board ID " << boardID << "\n";
        }
    } catch (const std::exception& e) {
        sendError(clientSocket, 400, "Malformed JSON in SYNC_BOARD.");
    }
}
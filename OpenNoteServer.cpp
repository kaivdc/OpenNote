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
#include <string>
#include <unordered_map>
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

unordered_map<uint32_t, BoardState> activeBoards; // Maps boardID to boardState in memory
unordered_map<int, uint32_t> clientBoardMap; // Maps socket identifier to a active boardID
mutex stateMutex;

bool handleEditCard(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleCreateCard(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleDeleteCard(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleCreateConnector(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleEditConnector(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleDeleteConnector(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);
bool handleSyncBoard(int clientSocket, const std::string& payloadStr, BoardState*& outBoard);

void broadcastUpdate(BoardState* board, int senderSocket, uint8_t msgType, const std::string& payloadStr);
void cleanupClientSession(int clientSocket);

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
                vector<char> payloadBuf(payloadLen);
                if(payloadLen > 0 && !read(childSocket, payloadBuf.data(), payloadLen)) {
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
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleEditCard(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT): // EDIT_CARD_CONTENT
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleEditCard(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CARD): // CREATE_CARD
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleCreateCard(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CARD): // DELETE_CARD
                        {                            
                            BoardState* targetBoard = nullptr;
                            if(handleDeleteCard(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CONNECTOR): // CREATE_CONNECTOR
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleCreateConnector(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CONNECTOR): // EDIT_CONNECTOR
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleEditConnector(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CONNECTOR): // DELETE_CONNECTOR
                        {
                            BoardState* targetBoard = nullptr;
                            if(handleDeleteConnector(childSocket, payloadStr, targetBoard))
                            {
                                broadcastUpdate(targetBoard, childSocket, msgType, payloadStr);
                            }
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

// helper method for determining if and which board a socket is connected to
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

// helper method for broadcasting updates to all clients connected to the same board
void broadcastUpdate(BoardState* board, int senderSocket, uint8_t msgType, const string& payloadStr) {
    if (!board)
    {
        return;
    }

    // Construct header
    uint32_t payloadLen = static_cast<uint32_t>(payloadStr.size());
    char header[5];
    header[0] = static_cast<char>(msgType);
    uint32_t networkLen = htonl(payloadLen);
    memcpy(&header[1], &networkLen, 4);

    // Send to all clients on board
    for (int clientSocket : board->connectedClients) {
        if (clientSocket == senderSocket) {
            continue;
        }

        ssize_t headerSent = send(clientSocket, header, 5, 0);
        if (headerSent <= 0) {
            cerr << "[ERROR] : Failed to broadcast header to socket " << clientSocket << "\n";
            continue;
        }

        if (payloadLen > 0) {
            ssize_t payloadSent = send(clientSocket, payloadStr.data(), payloadLen, 0);
            if (payloadSent <= 0) {
                cerr << "[ERROR] : Failed to broadcast payload to socket " << clientSocket << "\n";
            }
        }
    }

}

// helper method to gracefully remove all references to a childsocket on closure
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

// ADD: Save to SQL database & Broadcast to connected clients
// ADD? zIndex autopopulates to top, with user changing in editCard?
bool handleCreateCard(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        Card newCard;
        newCard.cardID = jsonParsed.value("cardID", 0);
        newCard.x = jsonParsed.value("x", 0.0f);
        newCard.y = jsonParsed.value("y", 0.0f);
        newCard.width = jsonParsed.value("width", 100.0f);
        newCard.height = jsonParsed.value("height", 100.0f);
        newCard.zIndex = jsonParsed.value("zIndex", 0);
        newCard.text = jsonParsed.value("text", "");
        newCard.mediaURL = jsonParsed.value("mediaURL", "");
        if (jsonParsed.contains("cardColor")) {
            newCard.color.r = jsonParsed["cardColor"].value("r", 255);
            newCard.color.g = jsonParsed["cardColor"].value("g", 255);
            newCard.color.b = jsonParsed["cardColor"].value("b", 255);
            newCard.color.a = jsonParsed["cardColor"].value("a", 255);
        }
    

        if (!newCard.isValid()) {
            sendError(clientSocket, 400, "Invalid card structure or text length.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);

        BoardState* board = getBoardForSocketLocked(clientSocket);
        if (!board) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        board->cards[newCard.cardID] = newCard;

        std::cout << "[SERVER] : Created card ID " << newCard.cardID << "in Board" << board->boardID << " by client " << clientSocket << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in CREATE_CARD.");
        return false;
    }
    return true;
}

// ADD: Remove card from SQLite Database & Broadcast
bool handleDeleteCard(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        uint32_t cardID = jsonParsed.value("cardID", 0);
        if (cardID == 0) {
            sendError(clientSocket, 400, "Missing or invalid cardID for edit.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);
        outBoard = getBoardForSocketLocked(clientSocket);
        if (!outBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        auto cardIt = outBoard->cards.find(cardID);
        if (cardIt == outBoard->cards.end()) {
            sendError(clientSocket, 404, "Card ID not found on this board.");
            return false;
        }

        // Erase connnectors that reference this cardID as a source or target (prevent null references)
        for (auto it = outBoard->connectors.begin(); it != outBoard->connectors.end(); ) {
            if(it->second.sourceCardID == cardID || it->second.targetCardID == cardID) {
                it = outBoard->connectors.erase(it);
            } else {
                it++;
            }
        }

       outBoard-> cards.erase(cardID);

       std::cout << "[SERVER] : Card " << cardID << " deleted from Board " << outBoard->boardID << " by client " << clientSocket << "\n";
    } catch (const exception& e) {
        sendError(clientSocket, 400, "Malformed JSON payload in DELETE_CARD.");
        return false;
    }
    return true;
}

// ADD: EDIT SQLITE DATABASE & BROADCAST
bool handleEditCard(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        uint32_t cardID = jsonParsed.value("cardID", 0);
        if (cardID == 0) {
            sendError(clientSocket, 400, "Missing or invalid cardID for edit.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);

        outBoard = getBoardForSocketLocked(clientSocket);
        if (!outBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        auto cardIt = outBoard->cards.find(cardID);
        if (cardIt == outBoard->cards.end()) {
            sendError(clientSocket, 404, "Card ID not found on this board.");
            return false;
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
            if (newWidth >= 0.0f && newWidth <= 1000.0f) card.width = newWidth;
        }
        if (jsonParsed.contains("height")) {
            float newHeight = jsonParsed["height"];
            if (newHeight >= 0.0f && newHeight <= 1000.0f) card.height = newHeight;
        }
        if (jsonParsed.contains("text")) {
            string newText = jsonParsed["text"];
            if (newText.length() <= 4096) card.text = newText;
        }
        if (jsonParsed.contains("zIndex")) {
            card.zIndex = jsonParsed["zIndex"];
        }
        if (jsonParsed.contains("mediaURL")) {
            card.mediaURL = jsonParsed["mediaURL"];
        }

        std::cout << "[SERVER] : Updated card " << cardID << "on Board" << outBoard->boardID << " by client " << clientSocket << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in EDIT_CARD.");
        return false;
    }
    return true;
}

// ADD: SQLITE FUNCTIONALITY FOR CHECKING & LOADING SAVED BOARDS
bool handleSyncBoard(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);
        uint32_t boardID = jsonParsed.value("boardID", 0);

        if (boardID == 0) {
            sendError(clientSocket, 400, "Invalid board IDI.");
            return false;
        }

        lock_guard<mutex> lockk(stateMutex);

        clientBoardMap[clientSocket] = boardID;

        // Creat/load board in memory if does not exist
        if (activeBoards.find(boardID) == activeBoards.end()) {

            BoardState newBoard;
            newBoard.boardID = boardID;
            newBoard.theme = BoardTheme{};
            activeBoards[boardID] = newBoard;
        }

        outBoard = &activeBoards[boardID];

        auto& clients = outBoard->connectedClients;
        if (std::find(clients.begin(), clients.end(), clientSocket) == clients.end()) {
            clients.push_back(clientSocket);
        }

        std::cout << "[SERVER] : Socket " << clientSocket << " successfully synced to Board ID " << boardID << "\n";

    } catch (const exception& e) {
        sendError(clientSocket, 400, "Malformed JSON in SYNC_BOARD.");
        return false;
    }
    return true;
}

// ADD: Save to SQL database & Broadcast to connected clients
bool handleCreateConnector(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        Connector newConnector;
        newConnector.connectorID = jsonParsed.value("connectorID", 0);
        newConnector.sourceCardID = jsonParsed.value("sourceCardID", 0);
        newConnector.targetCardID = jsonParsed.value("targetCardID", 0);
        newConnector.thickness = jsonParsed.value("thickness", 2.0f);
        if (jsonParsed.contains("color")) {
            newConnector.color.r = jsonParsed["color"].value("r", 255);
            newConnector.color.g = jsonParsed["color"].value("g", 255);
            newConnector.color.b = jsonParsed["color"].value("b", 255);
            newConnector.color.a = jsonParsed["color"].value("a", 255);
        }

        if (!newConnector.isValid()) {
            sendError(clientSocket, 400, "Invalid connector structure.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);

        outBoard = getBoardForSocketLocked(clientSocket);
        if (!outBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        if (!outBoard->isConnectorValid(newConnector)) {
            sendError(clientSocket, 400, "Invalid connector, source or target does not exist.");
            return false;
        }

        if (outBoard->connectors.find(newConnector.connectorID) != outBoard->connectors.end()) {
            sendError(clientSocket, 400, "Invalid connector, connector ID already exists.");
            return false;
        }

        outBoard->connectors[newConnector.connectorID] = newConnector;

        std::cout << "[SERVER] : Created connnector ID " << newConnector.connectorID << "in Board" << outBoard->boardID << " by client " << clientSocket << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in CREATE_CONNECTOR.");
        return false;
    }
    return true;
}

// ADD: Remove connector from SQLite Database & Broadcast
bool handleDeleteConnector(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        uint32_t connectorID = jsonParsed.value("connectorID", 0);
        if (connectorID == 0) {
            sendError(clientSocket, 400, "Missing or invalid connectorID for edit.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);
        outBoard = getBoardForSocketLocked(clientSocket);
        if (!outBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        auto connectorIt = outBoard->connectors.find(connectorID);
        if (connectorIt == outBoard->connectors.end()) {
            sendError(clientSocket, 404, "Connector ID not found on this board.");
            return false;
        }

       outBoard-> connectors.erase(connectorID);

       std::cout << "[SERVER] : Connector " << connectorID << " deleted from Board " << outBoard->boardID << " by client " << clientSocket << "\n";
    } catch (const exception& e) {
        sendError(clientSocket, 400, "Malformed JSON payload in DELETE_CONNECTOR.");
        return false;
    }
    return true;
}

// ADD: EDIT SQLITE DATABASE & BROADCAST
// ADD? Edit source/target cardIDs (Drag connector to different card instead of making new one?)
bool handleEditConnector(int clientSocket, const string& payloadStr, BoardState*& outBoard) {
    try {
        auto jsonParsed = json::parse(payloadStr);

        uint32_t connectorID = jsonParsed.value("connectorID", 0);
        if (connectorID == 0) {
            sendError(clientSocket, 400, "Missing or invalid connectorID for edit.");
            return false;
        }

        lock_guard<mutex> lock(stateMutex);

        outBoard = getBoardForSocketLocked(clientSocket);
        if (!outBoard) {
            sendError(clientSocket, 403, "Client is not synced to an active board.");
            return false;
        }

        auto connectorIt = outBoard->connectors.find(connectorID);
        if (connectorIt == outBoard->connectors.end()) {
            sendError(clientSocket, 404, "Connector ID not found on this board.");
            return false;
        }
        Connector& connector = connectorIt->second;

        if (jsonParsed.contains("thickness")) {
            float t = jsonParsed["thickness"];
            if (t > 0.0f) connector.thickness = t;
        }
        if (jsonParsed.contains("color")) {
            connector.color.r = jsonParsed["color"].value("r", connector.color.r);
            connector.color.g = jsonParsed["color"].value("g", connector.color.g);
            connector.color.b = jsonParsed["color"].value("b", connector.color.b);
            connector.color.a = jsonParsed["color"].value("a", connector.color.a);
        }

        std::cout << "[SERVER] : Updated connector " << connectorID << "on Board" << outBoard->boardID << " by client " << clientSocket << "\n";
    } catch (const nlohmann::json::parse_error& e) {
        sendError(clientSocket, 400, "Malformed JSON in EDIT_CONNECTOR.");
        return false;
    }
    return true;
}
#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <bit>
#include <cerrno>
#include <exception>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include "BoardStructs.h"
#include "ClientRenderer.h"
#include "LocalBoardCommandHandler.h"
#include "ClientSettings.h"
#include "PacketCreation.h"

using namespace std;
using json = nlohmann::json;

constexpr int DEFAULT_SERVER_PORT = 5432; // default port when no port is provided
#define MAX_LINE 256
#define STDIN 0

struct ReceivedPacket {
    uint8_t opcode;
    string payload;
};

mutex receivedPacketsMutex;
queue<ReceivedPacket> receivedPackets;

bool readExact(int sock, char* buffer, size_t bytesToRead);
void queueReceivedPacket(uint8_t opcode, string payload);
queue<ReceivedPacket> takeReceivedPackets();
void clientReceivingLoop(long childSocket);
Card cardFromJSON(const json& cardJSON);
Connector connectorFromJSON(const json& connectorJSON);
bool replaceBoardFromSnapshot(BoardState& board, const json& snapshot);
void applyReceivedPacket(BoardState& board, LocalBoardCommandHandler& commandHandler,
                         ClientRenderer& renderer, const ReceivedPacket& packet);

// Identifies packet types that carry a board revision
bool isBoardMutationPacket(uint8_t opcode) {
    switch (static_cast<OpCode>(opcode)) {
        case OpCode::MOVE_CARD:
        case OpCode::EDIT_CARD_CONTENT:
        case OpCode::CREATE_CARD:
        case OpCode::DELETE_CARD:
        case OpCode::CREATE_CONNECTOR:
        case OpCode::EDIT_CONNECTOR:
        case OpCode::DELETE_CONNECTOR:
        case OpCode::EDIT_BOARD_THEME:
        case OpCode::DELETE_OBJECTS:
        case OpCode::BRING_CARD_TO_FRONT:
            return true;
        default:
            return false;
    }
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

// Adds one received packet to the queue for the main thread
void queueReceivedPacket(uint8_t opcode, string payload) {
    lock_guard<mutex> lock(receivedPacketsMutex);
    receivedPackets.push({opcode, std::move(payload)});
}

// Returns the packets waiting for the main thread to apply
queue<ReceivedPacket> takeReceivedPackets() {
    lock_guard<mutex> lock(receivedPacketsMutex);
    queue<ReceivedPacket> pendingPackets;
    pendingPackets.swap(receivedPackets);
    return pendingPackets;
}

// Reads packets from the server and queues them for the main thread
void clientReceivingLoop(long childSocket)
{
    char headerBuf[5];

    while (1)
    {
        if (!readExact(childSocket, headerBuf, 5)) {
            cerr << "[Client] : Disconnected or header read failed. \n";
            break;
        }
        else
        {
            uint8_t msgType = static_cast<uint8_t>(headerBuf[0]);

            uint32_t networkLen;
            memcpy(&networkLen, &headerBuf[1], 4);
            // network to host
            uint32_t payloadLen = ntohl(networkLen);

            if(payloadLen > MAX_PACKET_PAYLOAD_BYTES) // if > 10 MB
            {
                cerr << "[ERROR] : Payload length exceeds 10MB. \n";
                break;
            }
            else
            {
                vector<char> payloadBuf(payloadLen);
                if(payloadLen > 0 && !readExact(childSocket, payloadBuf.data(), payloadLen)) {
                    cerr << "[ERROR] : Failed to read full payload stream. \n";
                    break;
                }
                else
                {
                    string payloadStr(payloadBuf.begin(), payloadBuf.end());

                    switch (msgType) {
                        case static_cast<uint8_t>(OpCode::HEARTBEAT): // HEARTBEAT (ping/pong)
                        {
                          std::cout << "[Client] : Received HEARTBEAT\n";
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::MOVE_CARD): // MOVE_CARD
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT): // EDIT_CARD_CONTENT
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CARD): // CREATE_CARD
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CARD): // DELETE_CARD
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CONNECTOR): // CREATE_CONNECTOR
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CONNECTOR): // EDIT_CONNECTOR
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CONNECTOR): // DELETE_CONNECTOR
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_OBJECTS):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_BOARD_THEME):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::BRING_CARD_TO_FRONT):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::OBJECT_SELECTION):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::DISCONNECT): // DISCONNECT
                        {
                            std::cout << "[Client] : Received explicit DISCONNECT\n";
                            return;
                        }
                        case static_cast<uint8_t>(OpCode::SYNC_BOARD): // SYNC_BOARD
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::BOARD_SAVED):
                        case static_cast<uint8_t>(OpCode::BOARD_LIST):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CLIENT_CONNECTED): // CLIENT_CONNECTED
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED): // CLIENT_DISCONNECTED
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        case static_cast<uint8_t>(OpCode::ERROR):
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
                        }
                        default:
                        {
                            const json errorReport = {
                                {"errCode", 400},
                                {"errorMsg", "The server sent an unknown opcode."},
                                {"unknownOpcode", static_cast<unsigned int>(msgType)}
                            };
                            if (!sendPacket(childSocket, static_cast<uint8_t>(OpCode::ERROR), errorReport.dump())) {
                                std::cerr << "[ERROR] : Failed to report an unknown server opcode.\n";
                            }
                            std::cerr << "[ERROR] : Unknown opcode received from server: "
                                      << static_cast<unsigned int>(msgType) << "\n";
                            break;
                        }
                    }
                }
            }
        }
    }
}

// Reads a card from a full card packet
Card cardFromJSON(const json& cardJSON) {
    Card card{};
    card.cardID = cardJSON.at("cardID").get<uint32_t>();
    card.x = cardJSON.at("x").get<float>();
    card.y = cardJSON.at("y").get<float>();
    card.width = cardJSON.at("width").get<float>();
    card.height = cardJSON.at("height").get<float>();
    card.zIndex = cardJSON.at("zIndex").get<uint32_t>();
    card.text = cardJSON.at("text").get<string>();
    card.textRevision = cardJSON.value("textRevision", uint64_t{0});
    card.mediaURL = cardJSON.value("mediaURL", string{});
    card.color = cardJSON.at("color").get<Color>();
    return card;
}

// Reads a connector from a full connector packet
Connector connectorFromJSON(const json& connectorJSON) {
    Connector connector{};
    connector.connectorID = connectorJSON.at("connectorID").get<uint32_t>();
    connector.sourceCardID = connectorJSON.at("sourceCardID").get<uint32_t>();
    connector.targetCardID = connectorJSON.at("targetCardID").get<uint32_t>();
    connector.thickness = connectorJSON.at("thickness").get<float>();
    connector.color = connectorJSON.at("color").get<Color>();
    return connector;
}

// Replaces the local board with a server snapshot
bool replaceBoardFromSnapshot(BoardState& board, const json& snapshot) {
    if (!snapshot.is_object() || !snapshot.contains("cards") || !snapshot["cards"].is_array() ||
        !snapshot.contains("connectors") || !snapshot["connectors"].is_array()) {
        return false;
    }

    BoardState updatedBoard;
    updatedBoard.boardID = snapshot.at("boardID").get<uint32_t>();
    updatedBoard.revision = snapshot.value("revision", uint64_t{0});
    updatedBoard.boardName = snapshot.value("boardName", string{});
    updatedBoard.createdAt = snapshot.value("createdAt", string{});
    updatedBoard.lastEditedAt = snapshot.value("lastEditedAt", string{});
    updatedBoard.user_settings = board.user_settings;
    updatedBoard.theme = snapshot.at("theme").get<BoardTheme>();

    for (const json& cardJSON : snapshot.at("cards")) {
        Card card = cardFromJSON(cardJSON);
        updatedBoard.cards[card.cardID] = card;
    }
    for (const json& connectorJSON : snapshot.at("connectors")) {
        Connector connector = connectorFromJSON(connectorJSON);
        updatedBoard.connectors[connector.connectorID] = connector;
    }

    updatedBoard.updateCountersAfterSync();
    if (snapshot.contains("nextCardID")) {
        updatedBoard.nextCardID = snapshot.at("nextCardID").get<uint32_t>();
    }
    if (snapshot.contains("nextConnectorID")) {
        updatedBoard.nextConnectorID = snapshot.at("nextConnectorID").get<uint32_t>();
    }

    board = std::move(updatedBoard);
    return true;
}

// Applies a board snapshot and server status packets to the local board
void applyReceivedPacket(BoardState& board, LocalBoardCommandHandler& commandHandler,
                         ClientRenderer& renderer, const ReceivedPacket& packet) {
    try {
        const bool hasBoardRevision = isBoardMutationPacket(packet.opcode) ||
            packet.opcode == static_cast<uint8_t>(OpCode::BOARD_SAVED);
        uint64_t receivedRevision = 0;
        bool shouldApplyRevision = false;
        if (hasBoardRevision) {
            const json event = json::parse(packet.payload);
            if (!event.is_object()) {
                cerr << "[ERROR] : Board update was not a JSON object.\n";
                return;
            }
            if (isBoardMutationPacket(packet.opcode) && !event.contains("boardID")) {
                cerr << "[WARN] : Board update did not include a board ID. Requesting a fresh board snapshot.\n";
                commandHandler.handle(JoinBoardCommand{board.boardID});
                return;
            }
            if (event.contains("boardID") && event.at("boardID").get<uint32_t>() != board.boardID) {
                return;
            }
            if (isBoardMutationPacket(packet.opcode) && !event.contains("revision")) {
                cerr << "[WARN] : Board update did not include a revision. Requesting a fresh board snapshot.\n";
                commandHandler.handle(JoinBoardCommand{board.boardID});
                return;
            }
            if (event.contains("revision")) {
                receivedRevision = event.at("revision").get<uint64_t>();
                if (receivedRevision <= board.revision) return;
                if (receivedRevision != board.revision + 1) {
                    cerr << "[WARN] : Board update revision was missed. Requesting a fresh board snapshot.\n";
                    commandHandler.handle(JoinBoardCommand{board.boardID});
                    return;
                }
                shouldApplyRevision = true;
            }
        }

        switch (packet.opcode) {
            case static_cast<uint8_t>(OpCode::SYNC_BOARD): {
                json snapshot = json::parse(packet.payload);
                if (replaceBoardFromSnapshot(board, snapshot)) {
                    commandHandler.setBoardReady(true);
                    renderer.clearSelection();
                    renderer.clearRemoteSelections();
                    renderer.setBoardSelectionVisible(false);
                    cout << "[CLIENT] : Board snapshot loaded for Board ID " << board.boardID << "\n";
                } else {
                    cerr << "[ERROR] : Invalid board snapshot.\n";
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::BOARD_SAVED): {
                json payload = json::parse(packet.payload);
                board.boardName = payload.value("boardName", board.boardName);
                if (!payload.value("notification", false)) {
                    renderer.confirmBoardSave(board.boardName);
                }
                cout << "[CLIENT] : Board saved as " << board.boardName << ".\n";
                break;
            }
            case static_cast<uint8_t>(OpCode::BOARD_LIST): {
                json payload = json::parse(packet.payload);
                if (!payload.is_object() || !payload.contains("boards") || !payload["boards"].is_array()) {
                    cerr << "[ERROR] : Invalid board list from server.\n";
                    break;
                }

                BoardList boardList;
                for (const json& boardJSON : payload["boards"]) {
                    if (!boardJSON.is_object() || !boardJSON.contains("boardName") ||
                        !boardJSON["boardName"].is_string()) {
                        continue;
                    }
                    const string boardName = boardJSON["boardName"].get<string>();
                    BoardInfo boardInfo;
                    boardInfo.boardID = boardJSON.value("boardID", 0u);
                    boardInfo.createdAt = boardJSON.value("createdAt", string{});
                    boardInfo.lastEditedAt = boardJSON.value("lastEditedAt", string{});
                    if (boardJSON.contains("theme")) {
                        boardInfo.theme = boardJSON.at("theme").get<BoardTheme>();
                    }
                    if (!boardName.empty() && boardInfo.boardID > 0) {
                        boardList[boardName] = boardInfo;
                    }
                }
                renderer.setAvailableBoards(boardList);
                break;
            }
            case static_cast<uint8_t>(OpCode::CREATE_CARD): {
                Card card = cardFromJSON(json::parse(packet.payload));
                board.cards[card.cardID] = card;
                break;
            }
            case static_cast<uint8_t>(OpCode::CREATE_CONNECTOR): {
                Connector connector = connectorFromJSON(json::parse(packet.payload));
                board.connectors[connector.connectorID] = connector;
                break;
            }
            case static_cast<uint8_t>(OpCode::CLIENT_CONNECTED):  {
                if (json::parse(packet.payload).value("boardID", 0u) != board.boardID) break;
                cout << "[CLIENT] : Another client connected to the board.\n";
                break;
            }
            case static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED): {
                const json payload = json::parse(packet.payload);
                if (payload.value("boardID", 0u) != board.boardID) break;
                if (payload.contains("clientSocket") && payload["clientSocket"].is_number_integer()) {
                    renderer.clearRemoteSelections(payload["clientSocket"].get<int>());
                }
                cout << "[CLIENT] : Another client disconnected from the board.\n";
                break;
            }
            case static_cast<uint8_t>(OpCode::OBJECT_SELECTION): {
                const json payload = json::parse(packet.payload);
                if (payload.value("boardID", 0u) != board.boardID) break;
                const int clientSocket = payload.at("clientSocket").get<int>();
                const string objectType = payload.at("objectType").get<string>();
                const uint32_t objectID = payload.at("objectID").get<uint32_t>();
                const bool selected = payload.at("selected").get<bool>();
                const string clientName = payload.value("clientName", string("User"));
                const Color selectionColor = payload.at("selectionColor").get<Color>();
                MenuContextType selectionType;
                if (objectType == "card") {
                    selectionType = MenuContextType::Card;
                } else if (objectType == "connector") {
                    selectionType = MenuContextType::Connector;
                } else {
                    break;
                }
                renderer.updateRemoteSelection(clientSocket,
                    ContextMenuTarget{selectionType, objectID}, selected, clientName, selectionColor);
                break;
            }
            case static_cast<uint8_t>(OpCode::ERROR): {
                json payload = json::parse(packet.payload);
                const string errorMessage = payload.value("errorMsg", string("Unknown server error."));
                const string requestType = payload.value("requestType", string{});
                const bool saveBoardError = requestType == "SAVE_BOARD_AS";
                if (requestType == "EDIT_CARD_CONTENT" && payload.value("errCode", 0) == 409 &&
                    payload.contains("cardID") && payload.contains("currentText") &&
                    payload.contains("textRevision") && payload["currentText"].is_string()) {
                    const uint32_t cardID = payload.at("cardID").get<uint32_t>();
                    const string currentText = payload.at("currentText").get<string>();
                    const uint64_t textRevision = payload.at("textRevision").get<uint64_t>();
                    auto cardIt = board.cards.find(cardID);
                    if (cardIt != board.cards.end()) {
                        cardIt->second.text = currentText;
                        cardIt->second.textRevision = textRevision;
                        renderer.markTextConflict(cardID, currentText, textRevision);
                    }
                }
                cerr << "[ERROR] : Server rejected a command: " << errorMessage << "\n";
                renderer.showServerError(errorMessage, saveBoardError);
                if (requestType == "SYNC_BOARD" || requestType == "CREATE_BOARD") {
                    commandHandler.clearPendingBoardRequest();
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::MOVE_CARD):
            {
                json payload = json::parse(packet.payload);
                uint32_t cardID = payload.at("cardID").get<uint32_t>();
                float newX = payload.at("x").get<float>();
                float newY = payload.at("y").get<float>();
                float newWidth = payload.at("width").get<float>();
                float newHeight = payload.at("height").get<float>();
                uint32_t newZ = payload.at("zIndex").get<uint32_t>();
                if (board.cards.find(cardID) != board.cards.end()) 
                {
                    board.cards[cardID].x = newX;
                    board.cards[cardID].y = newY;
                    board.cards[cardID].width = newWidth;
                    board.cards[cardID].height = newHeight;
                    board.cards[cardID].zIndex = newZ;
                }
                if (payload.contains("connectors") && payload["connectors"].is_array()) {
                    for (const json& connectorUpdate : payload["connectors"]) {
                        const uint32_t connectorID = connectorUpdate.at("connectorID").get<uint32_t>();
                        auto connectorIt = board.connectors.find(connectorID);
                        if (connectorIt != board.connectors.end()) {
                            connectorIt->second.thickness = connectorUpdate.at("thickness").get<float>();
                        }
                    }
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT):
            {
              json payload = json::parse(packet.payload);
              uint32_t cardID = payload.at("cardID").get<uint32_t>();

              auto it = board.cards.find(cardID);
              if (it == board.cards.end()) 
              {
                cerr << "[ERROR] : EDIT_CARD_CONTENT for unknown card ID " << cardID << "\n";
                break;
              }

              Card& card = it->second;

              if (payload.contains("text"))
                card.text = payload.at("text").get<string>();
              if (payload.contains("textRevision"))
                card.textRevision = payload.at("textRevision").get<uint64_t>();
              if (payload.contains("mediaURL"))
                card.mediaURL = payload.at("mediaURL").get<string>();
              if (payload.contains("color"))
                card.color = payload.at("color").get<Color>();
              if (payload.contains("x"))
                card.x = payload.at("x").get<float>();
              if (payload.contains("y"))
                card.y = payload.at("y").get<float>();
              if (payload.contains("width"))
                card.width = payload.at("width").get<float>();
              if (payload.contains("height"))
                card.height = payload.at("height").get<float>();
              if (payload.contains("zIndex"))
                card.zIndex = payload.at("zIndex").get<uint32_t>();
              break;
            }
            case static_cast<uint8_t>(OpCode::DELETE_CARD):
            {
              json payload = json::parse(packet.payload);
              uint32_t cardID = payload.at("cardID").get<uint32_t>();
              if (!board.deleteCard(cardID))
              {
                cerr << "[ERROR] : DELETE_CARD for unknown card ID " << cardID << "\n";
              }
              break;
            }
            case static_cast<uint8_t>(OpCode::EDIT_CONNECTOR):
            {
              const json payload = json::parse(packet.payload);
              const uint32_t connectorID = payload.at("connectorID").get<uint32_t>();

              auto it = board.connectors.find(connectorID);
              if (it == board.connectors.end()) {
                cerr << "[ERROR] : EDIT_CONNECTOR for unknown connector ID "
                  << connectorID << "\n";
                break;
              }
              if (payload.contains("thickness")) {
                it->second.thickness = payload.at("thickness").get<float>();
              }
              if (payload.contains("color")) {
                const json& colorJSON = payload.at("color");
                Color updated = it->second.color;

                auto updateChannel = [&colorJSON](const char* name, uint8_t& channel) {
                  if (!colorJSON.contains(name)) return;

                  const int value = colorJSON.at(name).get<int>();
                  if (value < 0 || value > 255) {
                    throw runtime_error(string("Invalid color channel: ") + name);
                  }
                    channel = static_cast<uint8_t>(value);
                };

                updateChannel("r", updated.r);
                updateChannel("g", updated.g);
                updateChannel("b", updated.b);
                updateChannel("a", updated.a);

                it->second.color = updated;
              }
              break;
            }
            case static_cast<uint8_t>(OpCode::DELETE_CONNECTOR):
            {
              const json payload = json::parse(packet.payload);
              const uint32_t connectorID = payload.at("connectorID").get<uint32_t>();

              if (!board.deleteConnector(connectorID)) {
                cerr << "[ERROR] : DELETE_CONNECTOR for unknown connector ID " << connectorID << "\n";
              }
              break;
            }
            case static_cast<uint8_t>(OpCode::DELETE_OBJECTS):
            {
              const json payload = json::parse(packet.payload);
              const auto& objects = payload.at("objects");

              if (!objects.is_array()) {
                throw std::runtime_error("DELETE_OBJECTS objects must be an array");
              }
              for (const auto& object : objects) {
                if (object.at("type").get<string>() == "connector") {
                  const uint32_t id = object.at("id").get<uint32_t>();
                  if (!board.deleteConnector(id)) {
                    cerr << "[ERROR] : DELETE_OBJECTS referenced unknown connector ID " << id << "\n";
                  }
                }
              }
              for (const auto& object : objects) {
                if (object.at("type").get<string>() == "card") {
                  const uint32_t id = object.at("id").get<uint32_t>();
                  if (!board.deleteCard(id)) {
                    cerr << "[ERROR] : DELETE_OBJECTS referenced unknown card ID " << id << "\n";
                  }
                }
              }
              break;
            }
            case static_cast<uint8_t>(OpCode::EDIT_BOARD_THEME):
            {
              const json payload = json::parse(packet.payload);
              const string field = payload.at("field").get<string>();

              Color* target = nullptr;
              if (field == "boardBackgroundColor") target = &board.theme.boardBackgroundColor;
              else if (field == "cardColor") target = &board.theme.cardColor;
              else if (field == "connectorColor") target = &board.theme.connectorColor;
              else if (field == "accentColor") target = &board.theme.accentColor;
              else if (field == "importantElements") target = &board.theme.importantElements;

              if (!target) {
                throw runtime_error("Unknown board theme field: " + field);
              }

              const json& color = payload.at("color");
              auto updateChannel = [&color](const char* name, uint8_t& channel) {
                if (!color.contains(name)) return;

                const int value = color.at(name).get<int>();
                if (value < 0 || value > 255) {
                  throw runtime_error(string("Invalid color channel: ") + name);
                }
                channel = static_cast<uint8_t>(value);
              };

              updateChannel("r", target->r);
              updateChannel("g", target->g);
              updateChannel("b", target->b);
              updateChannel("a", target->a);
              break;
            }
            case static_cast<uint8_t>(OpCode::BRING_CARD_TO_FRONT):
            {
              const json payload = json::parse(packet.payload);
              const uint32_t cardID = payload.at("cardID").get<uint32_t>();

              if (board.cards.find(cardID) == board.cards.end()) {
                cerr << "[ERROR] : BRING_CARD_TO_FRONT for unknown card ID " << cardID << "\n";
                break;
              }

              board.bringCardToFront(cardID);
              break;
            }
            default:
                break;
        }
        if (shouldApplyRevision) board.revision = receivedRevision;
        renderer.confirmChange(packet.opcode, board);
    } catch (const exception& error) {
        cerr << "[ERROR] : Could not apply server packet: " << error.what() << "\n";
    }
}

int main(int argc, char *argv[])
{
  std::cout << "[DEBUG] Main started! argc = " << argc << "\n" << std::flush;
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <server_host> [port]\n";
    return 1;
  }

  int serverPort = DEFAULT_SERVER_PORT;
  if (argc == 3) {
    char* portEnd = nullptr;
    errno = 0;
    const long parsedPort = std::strtol(argv[2], &portEnd, 10);
    if (errno != 0 || portEnd == argv[2] || *portEnd != '\0' || parsedPort < 1 || parsedPort > 65535) {
      std::cerr << "[ERROR] : Port must be a number between 1 and 65535.\n";
      return 1;
    }
    serverPort = static_cast<int>(parsedPort);
  }

  struct addrinfo addressHints{};
  addressHints.ai_family = AF_INET;
  addressHints.ai_socktype = SOCK_STREAM;
  struct addrinfo* serverAddresses = nullptr;
  const string serverPortText = to_string(serverPort);
  const int lookupResult = getaddrinfo(argv[1], serverPortText.c_str(), &addressHints, &serverAddresses);
  if (lookupResult != 0) {
    std::cerr << "[ERROR] : Could not resolve server address: " << gai_strerror(lookupResult) << "\n";
    return 1;
  }

  int s = -1;
  int connectionError = 0;
  for (struct addrinfo* address = serverAddresses; address != nullptr; address = address->ai_next) {
    s = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (s < 0) {
      connectionError = errno;
      continue;
    }
    if (connect(s, address->ai_addr, address->ai_addrlen) == 0) {
      break;
    }
    connectionError = errno;
    close(s);
    s = -1;
  }
  freeaddrinfo(serverAddresses);
  if (s < 0) {
    errno = connectionError;
    perror("select client: connect");
    return 1;
  }
  std::cout << "[CLIENT] :  Connected to OpenNote Server successfully!\n";

    Settings user_settings;
    user_settings.loadFromFile("settings.json");

    // Initialize render window
    ClientRenderer renderer;
    GLFWwindow* window = renderer.initialize(1280, 720, "OpenNote");
    if (!window) {
        std::cerr << "[ERROR] :  Failed to init renderer window.\n";
        close(s);
        return 1;
    }

    // Initialize the local board while waiting for the server snapshot
    BoardState localBoard;
    localBoard.user_settings = user_settings;
    localBoard.boardID = 0;
    LocalBoardCommandHandler localCommandHandler(s);
    localCommandHandler.setBoardReady(false);
    
    // Start receiving thread
    std::thread receivingThread;
    try {
        receivingThread = std::thread(clientReceivingLoop, static_cast<long>(s));
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to create receiving thread: " << e.what() << "\n";
        close(s);
        renderer.shutdown();
        return 1;
    }

    // Ask the server for boards available to select
    if (!sendPacket(s, static_cast<uint8_t>(OpCode::LIST_BOARDS), json::object().dump())) {
        std::cerr << "[ERROR] : Failed to request board list.\n";
        shutdown(s, SHUT_RDWR);
        receivingThread.join();
        close(s);
        renderer.shutdown();
        return 1;
    }

    // Main rendering loop
    auto nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!renderer.shouldClose()) {
        queue<ReceivedPacket> pendingPackets = takeReceivedPackets();
        while (!pendingPackets.empty()) {
            applyReceivedPacket(localBoard, localCommandHandler, renderer, pendingPackets.front());
            pendingPackets.pop();
        }

        renderer.updateAndRender(localBoard, localCommandHandler);
        if (std::chrono::steady_clock::now() >= nextHeartbeat) {
            heartBeatPacket(s);
            nextHeartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
    }

    if (!sendPacket(s, static_cast<uint8_t>(OpCode::DISCONNECT), json::object().dump())) {
        std::cerr << "[WARN] : Failed to send disconnect packet.\n";
    }
    shutdown(s, SHUT_WR);
    if (receivingThread.joinable())
    {
      receivingThread.join();
    }
    close(s);

    if (!window) {
        return -1;
    }



    renderer.shutdown();
    return 0;
}

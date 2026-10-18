#include <iostream>
#include <stdio.h>
#include <strings.h>
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

#define SERVER_PORT 5432
#define MAX_LINE 256
#define STDIN 0
#define VERSION "0.0.1"

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
void applyReceivedPacket(BoardState& board, LocalBoardCommandHandler& commandHandler, const ReceivedPacket& packet);

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

            if(payloadLen > 10 * 1024 * 1024) // if > 10 MB
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
                        case 0xFF:
                        {
                            queueReceivedPacket(msgType, std::move(payloadStr));
                            break;
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
void applyReceivedPacket(BoardState& board, LocalBoardCommandHandler& commandHandler, const ReceivedPacket& packet) {
    try {
        switch (packet.opcode) {
            case static_cast<uint8_t>(OpCode::SYNC_BOARD): {
                json snapshot = json::parse(packet.payload);
                if (replaceBoardFromSnapshot(board, snapshot)) {
                    commandHandler.setBoardReady(true);
                    cout << "[CLIENT] : Board snapshot loaded for Board ID " << board.boardID << "\n";
                } else {
                    cerr << "[ERROR] : Invalid board snapshot.\n";
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::CLIENT_CONNECTED):
                cout << "[CLIENT] : Another client connected to the board.\n";
                break;
            case static_cast<uint8_t>(OpCode::CLIENT_DISCONNECTED):
                cout << "[CLIENT] : Another client disconnected from the board.\n";
                break;
            case 0xFF: {
                json payload = json::parse(packet.payload);
                cerr << "[ERROR] : Server rejected a command: "
                     << payload.value("errorMsg", string("Unknown server error.")) << "\n";
                break;
            }
            default:
                break;
        }
    } catch (const exception& error) {
        cerr << "[ERROR] : Could not apply server packet: " << error.what() << "\n";
    }
}

int main(int argc, char *argv[])
{
  std::cout << "[DEBUG] Main started! argc = " << argc << "\n" << std::flush;
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <server_ip>\n";
    return 1;
  }

  struct sockaddr_in sin;
  int s;

  /* active open */
  if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("select client: socket");
    exit(1);
  }

  /* build address data structure */
  bzero((char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(argv[1]);
  sin.sin_port = htons(SERVER_PORT);

  //connects to server
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
  {
    perror("select client: connect");
    close(s);
    exit(1);
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
    localBoard.boardID = 1;
    LocalBoardCommandHandler localCommandHandler(localBoard);
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

    // Ask the server for the board snapshot
    const string syncPayload = json{{"boardID", localBoard.boardID}}.dump();
    if (!sendPacket(s, static_cast<uint8_t>(OpCode::SYNC_BOARD), syncPayload)) {
        std::cerr << "[ERROR] : Failed to request board snapshot.\n";
        shutdown(s, SHUT_RDWR);
        receivingThread.join();
        close(s);
        renderer.shutdown();
        return 1;
    }

    // Main rendering loop
    while (!renderer.shouldClose()) {
        queue<ReceivedPacket> pendingPackets = takeReceivedPackets();
        while (!pendingPackets.empty()) {
            applyReceivedPacket(localBoard, localCommandHandler, pendingPackets.front());
            pendingPackets.pop();
        }

        renderer.updateAndRender(localBoard, localCommandHandler);
    }

    // Wake the thread if it is blocked in recv(), then wait for it to finish.
    shutdown(s, SHUT_RDWR);
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

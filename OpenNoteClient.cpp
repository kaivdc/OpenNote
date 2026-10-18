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

mutex stateMutex;

bool handleSyncBoard(int clientSocket, const std::string& payloadStr);
void handleBoardCommand(int clientSocket, OpCode opcode, const std::string& payloadStr);

void broadcastToClients(const std::vector<int>& clients, int senderSocket, uint8_t msgType, const std::string& payloadStr, bool includeSender);
void cleanupClientSession(int clientSocket);
bool readExact(int sock, char* buffer, size_t bytesToRead);
bool sendPacket(int sock, uint8_t msgType, const std::string& payloadStr);
void sendError(int sock, int errCode, const std::string& errorMsg);

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

// Read Packet -- Need to move later
void clientReceivingLoop(long childSocket)
{
    char headerBuf[5];

    while (1)
    {
        // handle data from a client
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
                          std::cout << "[Client] : Received HEARTBEAT\n";
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::MOVE_CARD): // MOVE_CARD
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT): // EDIT_CARD_CONTENT
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CARD): // CREATE_CARD
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CARD): // DELETE_CARD
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::CREATE_CONNECTOR): // CREATE_CONNECTOR
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_CONNECTOR): // EDIT_CONNECTOR
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_CONNECTOR): // DELETE_CONNECTOR
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::DELETE_OBJECTS):
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::EDIT_BOARD_THEME):
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::BRING_CARD_TO_FRONT):
                        {
                          break;
                        }
                        case static_cast<uint8_t>(OpCode::DISCONNECT): // DISCONNECT
                        {
                            std::cout << "[Client] : Received explicit DISCONNECT\n";
                            return;
                        }
                        case static_cast<uint8_t>(OpCode::SYNC_BOARD): // SYNC_BOARD
                        {
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

    // Initialize localBoard for render testing
    BoardState localBoard;
    localBoard.user_settings = user_settings;
    localBoard.boardID = 1;
    LocalBoardCommandHandler localCommandHandler(localBoard);
    
    // Start receiving thread
    std::thread receivingThread;
    try {
        receivingThread = std::thread(clientReceivingLoop, static_cast<long>(s));
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to create receiving thread: " << e.what() << "\n";
        close(s);
        return 1;
    }

    // Main rendering loop
    while (!renderer.shouldClose()) {

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
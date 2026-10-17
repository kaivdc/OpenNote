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

using namespace std;
using json = nlohmann::json;

#define SERVER_PORT 5432
#define MAX_LINE 256
#define STDIN 0
#define VERSION "0.0.1"

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload

// Read Packet -- Need to move later
void packetRead(int sock)
{
  char header[5];
  if (!readExact(sock, header, 5))
  {
    cerr << "Client disconnected or header read failed. \n";
    return;
  }
  // Extract the message type and payload length from the header
  uint8_t msgType = static_cast<uint8_t>(header[0]);
  uint32_t networkLen;
  memcpy(&networkLen, &header[1], 4);
  // network to host
  uint32_t payloadLen = ntohl(networkLen);
  // Check for payload length exceeding maximum allowed size
  if (payloadLen > MAX_PAYLOAD_SIZE)
  {
    cerr << "Payload length exceeds maximum allowed size.\n";
    return;
  }
  else
  {
    vector<char> payloadBuffer(payloadLen);
    if (!readExact(sock, payloadBuffer.data(), payloadLen))
    {
      cerr << "Client disconnected or payload read failed. \n";
      return;
    }
    string payloadStr(payloadBuffer.begin(), payloadBuffer.end());
    // Process the received packet based on msgType and payloadStr
    switch (msgType) {
      case static_cast<uint8_t>(OpCode::HEARTBEAT):
        cout << "[DEBUG] : Received HEARTBEAT packet.\n";
        break;
      case static_cast<uint8_t>(OpCode::MOVE_CARD):
        cout << "[DEBUG] : Received MOVE_CARD packet with payload: " << payloadStr << "\n";
        break;
      case static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT):
        cout << "[DEBUG] : Received EDIT_CARD_CONTENT packet with payload: " << payloadStr << "\n";
        break;
      case static_cast<uint8_t>(OpCode::CREATE_CARD):
        cout << "[DEBUG] : Received CREATE_CARD packet with payload: " << payloadStr << "\n";
        break;
      case static_cast<uint8_t>(OpCode::DELETE_CARD):
        cout << "[DEBUG] : Received DELETE_CARD packet with payload: " << payloadStr << "\n";
        break;
      default:
        cerr << "[ERROR] : Unknown message type received: " << static_cast<int>(msgType) << "\n";
    }
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

int main(int argc, char *argv[])
{
  std::cout << "[DEBUG] Main started! argc = " << argc << "\n" << std::flush;
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <server_ip>\n";
    return 1;
  }

  /*
  is the mast file descriptor list and the read file descriptor list needed for this client?
  */
  fd_set master;   // master file descriptor list
  fd_set read_fds; // temp file descriptor list for select()
  int fdmax;       // maximum file descriptor number

  struct sockaddr_in sin;
  char buf[MAX_LINE];
  int len;
  int s;

  FD_ZERO(&master); // clear the master and temp sets
  FD_ZERO(&read_fds);

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
    
    // Main rendering loop
    while (!renderer.shouldClose()) {
        renderer.updateAndRender(localBoard, localCommandHandler);

        //receive loop
        uint8_t result = recv(s, buf, sizeof(buf), 0);
        if (result < 0)
        {
            perror("recv");
            break;
        }
        else if (result == 0)
        {
            std::cout << "[CLIENT] : Server closed the connection.\n";
            break;
        }
        else
        {
            packetRead(s);
        }


    }

    if (!window) {
        return -1;
    }



    renderer.shutdown();
    return 0;
}

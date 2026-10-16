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
#include <vector>
#include <bit>
#include "BoardStructs.h"
#include "ClientRenderer.h"
#include "LocalBoardCommandHandler.h"
#include "ClientSettings.h"

using namespace std;
using buffer = vector<uint8_t>;
using json = nlohmann::json;

#define SERVER_PORT 5432
#define MAX_LINE 256
#define STDIN 0

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload


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
    }

    if (!window) {
        return -1;
    }



    renderer.shutdown();
    return 0;
}

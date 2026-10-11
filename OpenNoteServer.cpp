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

using namespace std;

#define PORT 5432 // port we're listening on
#define MAX_LINE 256

fd_set master; // master file descriptor list
int listener;  // listening socket descriptor
int fdmax;
mutex masterMutex; // guards master/fdmax across threads

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
                            cout << "[SERVER] : Received CREATE_CARD:" << payloadStr << "\n";
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
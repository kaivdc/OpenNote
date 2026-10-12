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

using namespace std;
using buffer = vector<uint8_t>;

#define SERVER_PORT 5432
#define MAX_LINE 256
#define STDIN 0

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload



/*
      TEMPORARY PLACEMENT OF APPEND FUNCTIONS
      WILL MOVE TO HEADER LATER
*/
void appendUint8(buffer& buf, uint8_t value)
{
  buf.push_back(value);
}

void appendUint32(buffer& buf, uint32_t value)
{
  uint32_t networkValue = htonl(value);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&networkValue);
  buf.insert(buf.end(), bytes, bytes + sizeof(networkValue));
}

void appendString(buffer& buf, const string& str)
{
  appendUint32(buf, static_cast<uint32_t>(str.size()));
  buf.insert(buf.end(), str.begin(), str.end());
}

buffer createPacket(OpCode opcode, const buffer& payload)
{
  buffer packet;
  packet.reserve(HEADER_SIZE + payload.size());
  //Byte 0: OpCode
  appendUint8(packet, static_cast<uint8_t>(opcode));
  //Bytes 1-4: Payload Length
  appendUint32(packet, static_cast<uint32_t>(payload.size()));
  //Payload
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

buffer heartBeatPacket()
{
  buffer payload;
  return createPacket(OpCode::HEARTBEAT, payload);
}

buffer AuthRequestPacket(string version, const uint32_t boardID, const string authToken)
{
  buffer payload;
  appendString(payload, version);
  appendUint32(payload, boardID);
  appendString(payload, authToken);
  return createPacket(OpCode::AUTH_REQ, payload);
}

buffer moveCardPacket(uint32_t cardID, float x, float y, float width, float height)
{
  buffer payload;
  appendUint32(payload, cardID);
}

int main(int argc, char *argv[])
{

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

  //Connection initialization

  // add the STDIN to the master set
  FD_SET(STDIN, &master);

  // add the listener to the master set
  FD_SET(s, &master);

  /* main loop; get and send lines of text */
  while (1)
  {
    
  }

  close(s);
}
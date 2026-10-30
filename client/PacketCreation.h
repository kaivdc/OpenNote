#ifndef PACKETCREATION_H
#define PACKETCREATION_H

#include "BoardStructs.h"

using namespace std;
using json = nlohmann::json;

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = MAX_PACKET_PAYLOAD_BYTES; // 10 MiB max payload

bool sendPacket(int sock, uint8_t msgType, const string& payloadStr);

void sendError(int sock, int errCode, const string& errorMsg);

void createPacket(OpCode opcode, const json& payload);

void heartBeatPacket(int sock);

void moveCardPacket(int sock,uint32_t cardID, float x, float y, float width, float height);

//overload for moveCardPacket with zIndex]
void moveCardPacket(int sock, uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height);

void editTextPacket(int sock, uint32_t cardID, const string& text);

void createCardPacket(int sock, float x, float y, float width, float height, const string& text);

//unsure if mediaURL needs its own overload
void createCardPacket(int sock, float x, float y, float width, float height, const string& text, const string& mediaURL);

void disconnectPacket(int sock);

void deleteCardPacket(int sock, uint32_t cardID);


#endif

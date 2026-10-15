#ifndef PACKETCREATION_H
#define PACKETCREATION_H

#include <cstdint>
#include <vector>
#include <string>
#include "BoardStructs.h"

using namespace std;
using buffer = vector<uint8_t>;

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload

void appendUint8(buffer& buf, uint8_t value);

void appendUint32(buffer& buf, uint32_t value);

void appendString(buffer& buf, const string& str);

void appendFloat(buffer& buf, float value);

buffer createPacket(OpCode opcode, const buffer& payload);

buffer heartBeatPacket();

buffer AuthRequestPacket(string version, const uint32_t boardID, const string authToken);

buffer moveCardPacket(uint32_t cardID, float x, float y, float width, float height);

//overload for moveCardPacket with zIndex]
buffer moveCardPacket(uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height);

buffer editTextPacket(uint32_t cardID, const string& text);

buffer createCardPacket(float x, float y, float width, float height, const string& text);

//unsure if mediaURL needs its own overload
buffer createCardPacket(float x, float y, float width, float height, const string& text, const string& mediaUrl);

buffer disconnectPacket();

buffer deleteCardPacket(uint32_t cardID);


#endif
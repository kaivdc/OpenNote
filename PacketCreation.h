#ifndef PACKETCREATION_H
#define PACKETCREATION_H

#include "BoardStructs.h"

using namespace std;
using json = nlohmann::json;

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload

json createPacket(OpCode opcode, const json& payload);

json heartBeatPacket();

json AuthRequestPacket(string version, const uint32_t boardID, const string authToken);

json moveCardPacket(uint32_t cardID, float x, float y, float width, float height);

//overload for moveCardPacket with zIndex]
json moveCardPacket(uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height);

json editTextPacket(uint32_t cardID, const string& text);

json createCardPacket(float x, float y, float width, float height, const string& text);

//unsure if mediaURL needs its own overload
json createCardPacket(float x, float y, float width, float height, const string& text, const string& mediaUrl);

json disconnectPacket();

json deleteCardPacket(uint32_t cardID);


#endif
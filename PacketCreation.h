#ifndef PACKETCREATION_H
#define PACKETCREATION_H

#include "BoardStructs.h"

using namespace std;
using json = nlohmann::json;

constexpr size_t HEADER_SIZE = 5; // 5 byte header: 1-byte msgType, 4-byte payloadLength
constexpr size_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10MB max payload

json createPacket(OpCode opcode, const json& payload);

json heartBeatPacket(int sock);

json AuthRequestPacket(int sock, const string version, const uint32_t boardID, const string authToken);

json moveCardPacket(int sock,uint32_t cardID, float x, float y, float width, float height);

//overload for moveCardPacket with zIndex]
json moveCardPacket(int sock, uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height);

json editTextPacket(int sock, uint32_t cardID, const string& text);

json createCardPacket(int sock, float x, float y, float width, float height, const string& text);

//unsure if mediaURL needs its own overload
json createCardPacket(int sock, float x, float y, float width, float height, const string& text, const string& mediaURL);

json disconnectPacket(int sock);

json deleteCardPacket(int sock, uint32_t cardID);


#endif

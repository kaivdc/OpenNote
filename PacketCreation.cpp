#include "PacketCreation.h"
#include <arpa/inet.h>
#include <string>
#include <cmath>
#include "BoardStructs.h"

using namespace std;

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

void appendFloat(buffer& buf, float value)
{
  static_assert(sizeof(float) == sizeof(uint32_t));
  static_assert(numeric_limits<float>::is_iec559, "Float must be IEEE 754 compliant");

  // Preserve the float's raw binary representation.
  uint32_t bits = std::bit_cast<uint32_t>(value);

  // Convert those bits to network byte order.
  uint32_t networkBits = htonl(bits);

  const auto* bytes = reinterpret_cast<const uint8_t*>(&networkBits);
  buf.insert(buf.end(), bytes, bytes + sizeof(networkBits));
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
  appendFloat(payload, x);
  appendFloat(payload, y);
  appendFloat(payload, width);
  appendFloat(payload, height);
  return createPacket(OpCode::MOVE_CARD, payload);
}

buffer moveCardPacket(uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height)
{
  buffer payload;
  appendUint32(payload, cardID);
  appendFloat(payload, x);
  appendFloat(payload, y);
  appendUint32(payload, zIndex);
  appendFloat(payload, width);
  appendFloat(payload, height);
  return createPacket(OpCode::MOVE_CARD, payload);
}

buffer editTextPacket(uint32_t cardID, const string& text)
{
  buffer payload;
  appendUint32(payload, cardID);
  appendString(payload, text);
  return createPacket(OpCode::EDIT_CARD_CONTENT, payload);
}

buffer createCardPacket(float x, float y, float width, float height, const string& text)
{
  buffer payload;
  appendFloat(payload, x);
  appendFloat(payload, y);
  appendFloat(payload, width);
  appendFloat(payload, height);
  appendString(payload, text);
  return createPacket(OpCode::CREATE_CARD, payload);
}

//unsure if mediaURL needs its own overload
buffer createCardPacket(float x, float y, float width, float height, const string& text, const string& mediaUrl)
{
  buffer payload;
  appendFloat(payload, x);
  appendFloat(payload, y);
  appendFloat(payload, width);
  appendFloat(payload, height);
  appendString(payload, text);
  appendString(payload, mediaUrl);
  return createPacket(OpCode::CREATE_CARD, payload);
}

buffer disconnectPacket()
{
  buffer payload;
  return createPacket(OpCode::DISCONNECT, payload);
}

buffer deleteCardPacket(uint32_t cardID)
{
  buffer payload;
  appendUint32(payload, cardID);
  return createPacket(OpCode::DELETE_CARD, payload);
}
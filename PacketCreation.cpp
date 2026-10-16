#include "PacketCreation.h"
#include <arpa/inet.h>
#include "BoardStructs.h"

using namespace std;

// Wraps an opcode and payload in a packet object
json createPacket(OpCode opcode, const json& payload)
{
  json header;
  header["OpCode"] = opcode;
  header["PayloadLength"] = payload.size();
  json packet;
  packet["Header"] = header;
  packet["Payload"] = payload;
  return packet;
}

// Creates a heartbeat packet
json heartBeatPacket()
{
  json payload;
  return createPacket(OpCode::HEARTBEAT, payload);
}

// Creates an authentication request with the client and board details
json AuthRequestPacket(string version, const uint32_t boardID, const string authToken)
{
  json payload;
  payload["version"] = version;
  payload["boardID"] = boardID;
  payload["authToken"] = authToken;

  return createPacket(OpCode::AUTH_REQ, payload);
}

// Creates a card movement packet with its position and size
json moveCardPacket(uint32_t cardID, float x, float y, float width, float height)
{
  json payload;
  payload["cardID"] = cardID;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  return createPacket(OpCode::MOVE_CARD, payload);
}

// Creates a card movement packet with its position, size, and draw order
json moveCardPacket(uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height)
{
  json payload;
  payload["cardID"] = cardID;
  payload["x"] = x;
  payload["y"] = y;
  payload["zIndex"] = zIndex;
  payload["width"] = width;
  payload["height"] = height;
  return createPacket(OpCode::MOVE_CARD, payload);
}

// Creates a packet that changes a card's text
json editTextPacket(uint32_t cardID, const string& text)
{
  json payload;
  payload["cardID"] = cardID;
  payload["text"] = text;
  return createPacket(OpCode::EDIT_CARD_CONTENT, payload);
}

// Creates a card packet with its position, size, and text
json createCardPacket(float x, float y, float width, float height, const string& text)
{
  json payload;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  payload["text"] = text;
  return createPacket(OpCode::CREATE_CARD, payload);
}

// Creates a card packet with its position, size, text, and image URL
json createCardPacket(float x, float y, float width, float height, const string& text, const string& mediaURL)
{
  json payload;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  payload["text"] = text;
  payload["mediaURL"] = mediaURL;
  return createPacket(OpCode::CREATE_CARD, payload);
}

// Creates a disconnect packet
json disconnectPacket()
{
  json payload;
  return createPacket(OpCode::DISCONNECT, payload);
}

// Creates a packet that deletes a card
json deleteCardPacket(uint32_t cardID)
{
  json payload;
  payload["cardID"] = cardID;
  return createPacket(OpCode::DELETE_CARD, payload);
}

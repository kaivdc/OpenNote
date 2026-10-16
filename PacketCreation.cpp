#include "PacketCreation.h"
#include <arpa/inet.h>
#include "BoardStructs.h"

using namespace std;

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

json heartBeatPacket()
{
  json payload;
  return createPacket(OpCode::HEARTBEAT, payload);
}

json AuthRequestPacket(string version, const uint32_t boardID, const string authToken)
{
  json payload;
  payload["version"] = version;
  payload["boardID"] = boardID;
  payload["authToken"] = authToken;

  return createPacket(OpCode::AUTH_REQ, payload);
}

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

json editTextPacket(uint32_t cardID, const string& text)
{
  json payload;
  payload["cardID"] = cardID;
  payload["text"] = text;
  return createPacket(OpCode::EDIT_CARD_CONTENT, payload);
}

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

//unsure if mediaURL needs its own overload
json createCardPacket(float x, float y, float width, float height, const string& text, const string& mediaUrl)
{
  json payload;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  payload["text"] = text;
  payload["mediaUrl"] = mediaUrl;
  return createPacket(OpCode::CREATE_CARD, payload);
}

json disconnectPacket()
{
  json payload;
  return createPacket(OpCode::DISCONNECT, payload);
}

json deleteCardPacket(uint32_t cardID)
{
  json payload;
  payload["cardID"] = cardID;
  return createPacket(OpCode::DELETE_CARD, payload);
}
#include "PacketCreation.h"
#include <arpa/inet.h>
#include "BoardStructs.h"

using namespace std;

mutex packetWriteMutex;

// Creates heartbeat json payload
void heartBeatPacket(int sock)
{
  json payload;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::HEARTBEAT), jsonPayload);
}

// Creates a card movement packet with its position and size
void moveCardPacket(int sock, uint32_t cardID, float x, float y, float width, float height)
{
  json payload;
  payload["cardID"] = cardID;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::MOVE_CARD), jsonPayload);
}

// Creates a card movement packet with its position, size, and draw order
void moveCardPacket(int sock, uint32_t cardID, float x, float y, uint32_t zIndex, float width, float height)
{
  json payload;
  payload["cardID"] = cardID;
  payload["x"] = x;
  payload["y"] = y;
  payload["zIndex"] = zIndex;
  payload["width"] = width;
  payload["height"] = height;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::MOVE_CARD), jsonPayload);
}

// Creates a packet that changes a card's text
void editTextPacket(int sock, uint32_t cardID, const string& text)
{
  json payload;
  payload["cardID"] = cardID;
  payload["text"] = text;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::EDIT_CARD_CONTENT), jsonPayload);
}

// Creates a card packet with its position, size, and text
void createCardPacket(int sock, float x, float y, float width, float height, const string& text)
{
  json payload;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  payload["text"] = text;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::CREATE_CARD), jsonPayload);
}

// Creates a card packet with its position, size, text, and image URL
void createCardPacket(int sock, float x, float y, float width, float height, const string& text, const string& mediaURL)
{
  json payload;
  payload["x"] = x;
  payload["y"] = y;
  payload["width"] = width;
  payload["height"] = height;
  payload["text"] = text;
  payload["mediaUrl"] = mediaURL;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::CREATE_CARD), jsonPayload);
}

// Creates a disconnect packet
void disconnectPacket(int sock)
{
  json payload;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::DISCONNECT), jsonPayload);
}

// Creates a packet that deletes a card
void deleteCardPacket(int sock, uint32_t cardID)
{
  json payload;
  payload["cardID"] = cardID;
  const string jsonPayload = payload.dump();
  sendPacket(sock, static_cast<uint8_t>(OpCode::DELETE_CARD), jsonPayload);
}

// Sends every byte in a buffer before returning
bool sendAll(int sock, const char* buffer, size_t bytesToSend) {
    size_t bytesSent = 0;
    while (bytesSent < bytesToSend) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t result = send(sock, buffer + bytesSent, bytesToSend - bytesSent, flags);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        bytesSent += static_cast<size_t>(result);
    }
    return true;
}

// Sends one framed packet with its opcode and payload
bool sendPacket(int sock, uint8_t msgType, const string& payloadStr) {
    if (payloadStr.size() > 10 * 1024 * 1024) {
        return false;
    }
    lock_guard<mutex> lock(packetWriteMutex);
    char header[5];
    header[0] = static_cast<char>(msgType);
    const uint32_t networkLen = htonl(static_cast<uint32_t>(payloadStr.size()));
    memcpy(&header[1], &networkLen, sizeof(networkLen));
    return sendAll(sock, header, sizeof(header)) &&
           (payloadStr.empty() || sendAll(sock, payloadStr.data(), payloadStr.size()));
}

// Sends an error packet through the client connection
void sendError(int sock, int errCode, const string& errorMsg) {
    const string jsonPayload = json{{"errCode", errCode}, {"errorMsg", errorMsg}}.dump();
    sendPacket(sock, static_cast<uint8_t>(OpCode::ERROR), jsonPayload);
}

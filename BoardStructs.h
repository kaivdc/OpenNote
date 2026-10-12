#ifndef BOARD_STRUCTS_H
#define BOARD_STRUCTS_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

using namespace std;

enum class OpCode : uint8_t 
{
HEARTBEAT = 0x00,
AUTH_REQ = 0x01,
MOVE_CARD = 0x02,
EDIT_CARD_CONTENT = 0x03,
CREATE_CARD = 0x04,
DELETE_CARD = 0x05,
CREATE_CONNECTOR = 0x06,
EDIT_CONNECTOR = 0x07,
DELETE_CONNECTOR =  0x08,
SYNC_BOARD =  0x09,
DISCONNECT =  0x10,
CLIENT_DISCONNECTED = 0x0B,
CLIENT_CONNECTED = 0x0A
};

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct Card {
    uint32_t cardID;
    float x;
    float y;
    float width; // > 0 but <= 10
    float height; // > 0 but <= 10
    uint32_t zIndex;
    string text; // < 4096
    string mediaURL;
    Color cardColor;
    Color textColor;

    bool isValid() const {
        return (cardID > 0 && width >= 10.0f && height >= 10.0f && text.length() <= 4096);
    }
};

struct Connector {
    uint32_t connectorID;
    uint32_t sourceCardID;
    uint32_t targetCardID;
    float thickness; // > 0
    Color color;

    bool isValid() const {
        return (connectorID > 0 && sourceCardID > 0 && targetCardID > 0 && thickness > 0.0f);
    }
};

struct BoardState {
    uint32_t boardID;
    unordered_map<uint32_t, Card> cards;
    unordered_map<uint32_t, Connector> connectors;
    vector<int> connectedClients;

    bool isValid() const {
        return (boardID > 0);
    }

    bool isConnectorValid(const Connector& connector) const {
        if(!connector.isValid()) 
        {
            return false;
        }

        bool sourceExists = cards.find(connector.sourceCardID) != cards.end();
        bool targetExists = cards.find(connector.targetCardID) != cards.end();

        return sourceExists && targetExists;
    }
};

#endif
#ifndef BOARD_STRUCTS_H
#define BOARD_STRUCTS_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

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
    float width;
    float height;
    uint32_t zIndex;
    std::string text;
    std::string mediaURL;
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
    float thickness;
    Color color;

    bool isValid() const {
        return (connectorID > 0 && sourceCardID > 0 && targetCardID > 0 && thickness > 0.0f);
    }
};

struct BoardState {
    uint32_t boardID;
    std::unordered_map<uint32_t, Card> cards;
    std::unordered_map<uint32_t, Connector> connectors;
    std::vector<int> connectedClients;

    bool isValid() const {
        return (boardID > 0);
    }
};

#endif
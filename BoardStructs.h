#ifndef BOARD_STRUCTS_H
#define BOARD_STRUCTS_H

#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include "ClientSettings.h"

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
CLIENT_CONNECTED = 0x0A,
EDIT_BOARD_THEME = 0x11,
DELETE_OBJECTS = 0x12,
BRING_CARD_TO_FRONT = 0x13
};

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

inline void to_json(nlohmann::json& j, const Color& color) {
    j = nlohmann::json{{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
}

inline void from_json(const nlohmann::json& j, Color& color) {
    j.at("r").get_to(color.r);
    j.at("g").get_to(color.g);
    j.at("b").get_to(color.b);
    j.at("a").get_to(color.a);
}

struct BoardTheme {
    Color boardBackgroundColor = {30, 30, 35, 255};
    Color cardColor = {86, 66, 86, 255};
    Color connectorColor = {150, 150, 170, 255};
    Color accentColor = {252, 129, 74, 255};
    Color importantElements = {255, 255, 255, 255};
};

inline void to_json(nlohmann::json& j, const BoardTheme& theme) {
    j = nlohmann::json{
        {"boardBackgroundColor", theme.boardBackgroundColor},
        {"cardColor", theme.cardColor},
        {"connectorColor", theme.connectorColor},
        {"accentColor", theme.accentColor},
        {"importantElements", theme.importantElements}
    };
}

inline void from_json(const nlohmann::json& j, BoardTheme& theme) {
    j.at("boardBackgroundColor").get_to(theme.boardBackgroundColor);
    j.at("cardColor").get_to(theme.cardColor);
    j.at("connectorColor").get_to(theme.connectorColor);
    j.at("accentColor").get_to(theme.accentColor);
    if (j.contains("importantElements")) j.at("importantElements").get_to(theme.importantElements);
}

namespace ThemeUtils {
    inline uint32_t toImColor(const Color& color) {
        return ((uint32_t)color.a << 24) | ((uint32_t)color.b << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.r;
    }

    inline Color shift(const Color& color, int dr, int dg, int db, int da = 0) {
        auto clamp = [](int val) -> uint8_t {
            return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
        };
        return { clamp(color.r + dr), clamp(color.g + dg), clamp(color.b + db), clamp(color.a + da) };
    }

    inline Color getCardBorder(const Color& cardColor) {
        return shift(cardColor, 40, 40, 40);
    }

    inline Color getConnectorBorder(const Color& connectorColor) {
        return shift(connectorColor, 30, 30, 30);
    }

    inline Color getGridLineColor(const Color& bg) {
        return shift(bg, 15, 15, 18);
    }

    inline Color getTextColor(const Color& bg) {
        auto linearize = [](uint8_t channel) -> double {
            double c = static_cast<double>(channel) / 255.0;
            return (c <= 0.03928) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
        };

        double rLinear = linearize(bg.r);
        double gLinear = linearize(bg.g);
        double bLinear = linearize(bg.b);

        double luminance = 0.2126 * rLinear + 0.7152 * gLinear + 0.0722 * bLinear;

        if (luminance > 0.179) {
            return {15, 15, 15, 255};
        } else {
            return {255, 255, 255, 255};
        }
    }
}

struct Card {
    uint32_t cardID;
    float x;
    float y;
    float width; // > 0 but <= 1000
    float height; // > 0 but <= 1000
    uint32_t zIndex;
    string text; // < 4096
    string mediaURL;
    Color color;

Color getTextColor() const {
        return ThemeUtils::getTextColor(color);
    }

    Color getBorderColor() const {
        return ThemeUtils::getCardBorder(color);
    }

    bool isValid() const {
        return (cardID > 0 && width >= 0.0f && width <= 1000.0f && height >= 0.0f && height <= 1000.0f && text.length() <= 4096);
    }
};

struct Connector {
    uint32_t connectorID;
    uint32_t sourceCardID;
    uint32_t targetCardID;
    float thickness; // > 0
    Color color;

Color getBorderColor() const {
        return ThemeUtils::getConnectorBorder(color);
    }

    bool isValid() const {
        return (connectorID > 0 && sourceCardID > 0 && targetCardID > 0 && thickness > 0.0f);
    }
};

struct BoardState {
    uint32_t boardID;
    unordered_map<uint32_t, Card> cards;
    unordered_map<uint32_t, Connector> connectors;
    vector<int> connectedClients;
    BoardTheme theme;
    Settings user_settings;

    uint32_t nextCardID = 1;
    uint32_t nextConnectorID = 1;

    bool isValid() const {
        return (boardID > 0);
    }

    vector<uint32_t> getCardIDsInZOrder() const {
        vector<uint32_t> orderedIDs;
        orderedIDs.reserve(cards.size());
        for (const auto& [id, card] : cards) {
            orderedIDs.push_back(id);
        }
        sort(orderedIDs.begin(), orderedIDs.end(), [this](uint32_t leftID, uint32_t rightID) {
            const Card& left = cards.at(leftID);
            const Card& right = cards.at(rightID);
            if (left.zIndex != right.zIndex) {
                return left.zIndex < right.zIndex;
            }
            return leftID < rightID;
        });
        return orderedIDs;
    }

    // Raises one card and normalizes the remaining indices to avoid gaps or overflow.
    void bringCardToFront(uint32_t cardID) {
        auto topCard = cards.find(cardID);
        if (topCard == cards.end()) {
            return;
        }

        vector<uint32_t> otherIDs;
        otherIDs.reserve(cards.size() - 1);
        for (const auto& [id, card] : cards) {
            if (id != cardID) {
                otherIDs.push_back(id);
            }
        }
        sort(otherIDs.begin(), otherIDs.end(), [this](uint32_t leftID, uint32_t rightID) {
            const Card& left = cards.at(leftID);
            const Card& right = cards.at(rightID);
            if (left.zIndex != right.zIndex) {
                return left.zIndex < right.zIndex;
            }
            return leftID < rightID;
        });

        uint32_t nextZIndex = 1;
        for (uint32_t id : otherIDs) {
            cards.at(id).zIndex = nextZIndex++;
        }
        topCard->second.zIndex = nextZIndex;
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

    // Helper method checking whether a connector already links the two given cards (in either direction).
    bool connectorExistsBetween(uint32_t sourceCardID, uint32_t targetCardID) const {
        for (const auto& [id, connector] : connectors) {
            bool forward = connector.sourceCardID == sourceCardID && connector.targetCardID == targetCardID;
            bool backward = connector.sourceCardID == targetCardID && connector.targetCardID == sourceCardID;
            if (forward || backward) {
                return true;
            }
        }
        return false;
    }

    // Factory method returning pointer to connector + adding it to board state.
    Card* createCardAt(float x, float y, const string& text = "New Card", std::optional<Color> color = std::nullopt, float width = 0.0f, float height = 0.0f) {
        uint32_t assignedID = nextCardID;

        float finalWidth = (width > 0.0f) ? width : user_settings.defaultCardWidth;
        float finalHeight = (height > 0.0f) ? height : user_settings.defaultCardHeight;

        Card card;
        card.cardID = assignedID;
        card.width = finalWidth;
        card.height = finalHeight;
        card.x = x - (finalWidth / 2.0f);
        card.y = y - (finalHeight / 2.0f);
        card.zIndex = 1;
        card.text = text;
        
        card.color = color.value_or(theme.cardColor);

        if (!card.isValid()) {
            cerr << "[ERROR] : Card creation failed (size or text length out of bounds).\n";
            return nullptr;
        }

        nextCardID++;

        cards[assignedID] = card;
        bringCardToFront(assignedID);
        return &cards[assignedID];
    }

    // Factory method returning pointer to connector for success/fail + adding to boardstate.
    Connector* createConnector(uint32_t sourceCardID, uint32_t targetCardID, float thickness = 0.0f, std::optional<Color> color = std::nullopt) {
        if (cards.find(sourceCardID) == cards.end() || cards.find(targetCardID) == cards.end()) {
            cerr << "[ERROR] : Connector creation failed, sourceCardID or targetCardID does not exist.\n";
            return nullptr;
        }

        // Reject duplicate connectors between the same pair of cards
        if (connectorExistsBetween(sourceCardID, targetCardID)) {
            cerr << "[ERROR] : Connector creation failed, a connector between these cards already exists.\n";
            return nullptr;
        }
        
        uint32_t assignedID = nextConnectorID;

        Connector connector;
        connector.connectorID = assignedID;
        connector.sourceCardID = sourceCardID;
        connector.targetCardID = targetCardID;
        connector.thickness = (thickness > 0.0f) ? thickness : user_settings.defaultConnectorThickness;

        connector.color = color.value_or(theme.connectorColor);

        if (!connector.isValid()) {
            cerr << "[ERROR] : Connector creation failed (Thickness out of bounds).\n";
            return nullptr;
        }

        nextConnectorID++;

        connectors[assignedID] = connector;
        return &connectors[assignedID];
    }

    bool deleteCard(uint32_t cardID) {
        auto it = cards.find(cardID);
        if (it == cards.end()) {
            cerr << "[ERROR] : Card deletion failed, cardID does not exist.\n";
            return false;
        }

        // Delete associated connectors
        for (auto connIt = connectors.begin(); connIt != connectors.end(); ) {
            if (connIt->second.sourceCardID == cardID || connIt->second.targetCardID == cardID) {
                connIt = connectors.erase(connIt);
            } else {
                ++connIt;
            }
        }

        cards.erase(it);
        return true;
    }

    // Destructor method for connector
    bool deleteConnector(uint32_t connectorID) {
        auto it = connectors.find(connectorID);
        if (it == connectors.end()) {
            cerr << "[ERROR] : Connecter deletion failed, connectorID does not exist.";
            return false;
        }
        connectors.erase(it);
        return true;
    }

    // Helper method to update local board state ID tracking on sync.
    void updateCountersAfterSync() {
        uint32_t maxCard = 0;
        for (const auto& [id, _] : cards) {
            if (id > maxCard) maxCard = id;
        }
        nextCardID = maxCard + 1;

        uint32_t maxConn = 0;
        for (const auto& [id, _] : connectors) {
            if (id > maxConn) maxConn = id;
        }
        nextConnectorID = maxConn + 1;
    }
};

#endif

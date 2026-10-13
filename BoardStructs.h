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

inline void to_json(nlohmann::json& json, const Color& color) {
    json = nlohmann::json{{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
}

inline void from_json(const nlohmann::json& json, Color& color) {
    json.at("r").get_to(color.r);
    json.at("g").get_to(color.g);
    json.at("b").get_to(color.b);
    json.at("a").get_to(color.a);
}

struct BoardTheme {
    Color boardBackGroundColor = {30, 30, 35, 255};
    Color cardColor = {86, 66, 86, 255};
    Color connectorColor = {150, 150, 170, 255};
    Color accentColor = {252, 129, 74, 255};
};

inline void to_json(nlohmann::json& json, const BoardTheme& theme) {
    json = nlohmann::json{
        {"boardBackGroundColor", theme.boardBackGroundColor},
        {"cardColor", theme.cardColor},
        {"connectorColor", theme.connectorColor},
        {"accentColor", theme.accentColor}
    };
}

inline void from_json(const nlohmann::json& json, BoardTheme& theme) {
    json.at("boardBackGroundColor").get_to(theme.boardBackGroundColor);
    json.at("cardColor").get_to(theme.cardColor);
    json.at("connectorColor").get_to(theme.connectorColor);
    json.at("accentColor").get_to(theme.accentColor);
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
            return (c <= 0.03928) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
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
    float width; // > 0 but <= 10
    float height; // > 0 but <= 10
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
        return (cardID > 0 && width >= 10.0f && height >= 10.0f && text.length() <= 4096);
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
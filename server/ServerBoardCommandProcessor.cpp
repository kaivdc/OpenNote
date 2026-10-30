#include "ServerBoardCommandProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

using nlohmann::json;
using std::string;
using std::vector;

namespace {

// Builds an error result for a rejected board command
ServerBoardCommandResult commandError(int errorCode, const string& errorMessage) {
    return {false, errorCode, errorMessage, string{}};
}

// Builds a successful result with the update payload to broadcast
ServerBoardCommandResult commandSuccess(const string& updatePayload) {
    return {true, 0, string{}, updatePayload};
}

// Applies valid red, green, blue, and alpha values from JSON to a color
bool applyColorJSON(const json& colorJSON, Color& targetColor) {
    if (!colorJSON.is_object()) return false;

    int colorComponentCount = 0;
    if (colorJSON.contains("r")) {
        if (!colorJSON["r"].is_number_integer()) return false;
        const int redValue = colorJSON["r"].get<int>();
        if (redValue < 0 || redValue > 255) return false;
        targetColor.r = static_cast<uint8_t>(redValue);
        colorComponentCount++;
    }
    if (colorJSON.contains("g")) {
        if (!colorJSON["g"].is_number_integer()) return false;
        const int greenValue = colorJSON["g"].get<int>();
        if (greenValue < 0 || greenValue > 255) return false;
        targetColor.g = static_cast<uint8_t>(greenValue);
        colorComponentCount++;
    }
    if (colorJSON.contains("b")) {
        if (!colorJSON["b"].is_number_integer()) return false;
        const int blueValue = colorJSON["b"].get<int>();
        if (blueValue < 0 || blueValue > 255) return false;
        targetColor.b = static_cast<uint8_t>(blueValue);
        colorComponentCount++;
    }
    if (colorJSON.contains("a")) {
        if (!colorJSON["a"].is_number_integer()) return false;
        const int alphaValue = colorJSON["a"].get<int>();
        if (alphaValue < 0 || alphaValue > 255) return false;
        targetColor.a = static_cast<uint8_t>(alphaValue);
        colorComponentCount++;
    }
    return colorComponentCount > 0;
}

// Applies a finite JSON number when it falls within the allowed range
bool applyFloatJSON(const json& jsonParsed, const char* fieldName, float& targetValue, float minValue, float maxValue) {
    if (!jsonParsed.contains(fieldName)) return true;
    if (!jsonParsed[fieldName].is_number()) return false;

    const double parsedValue = jsonParsed[fieldName].get<double>();
    if (!std::isfinite(parsedValue) || parsedValue < minValue || parsedValue > maxValue) return false;
    targetValue = static_cast<float>(parsedValue);
    return true;
}

// Converts a color into its JSON representation
json colorToJSON(const Color& targetColor) {
    return {{"r", targetColor.r}, {"g", targetColor.g}, {"b", targetColor.b}, {"a", targetColor.a}};
}

// Converts a card into its JSON representation
json cardToJSON(const Card& card) {
    return {{"cardID", card.cardID}, {"x", card.x}, {"y", card.y},
            {"width", card.width}, {"height", card.height}, {"zIndex", card.zIndex},
            {"text", card.text}, {"mediaURL", card.mediaURL}, {"color", colorToJSON(card.color)}};
}

// Converts a connector into its JSON representation
json connectorToJSON(const Connector& connector) {
    return {{"connectorID", connector.connectorID},
            {"sourceCardID", connector.sourceCardID}, {"targetCardID", connector.targetCardID},
            {"thickness", connector.thickness}, {"color", colorToJSON(connector.color)}};
}

// Creates a card and returns its assigned ID and state for broadcasting
ServerBoardCommandResult createCard(BoardState& board, const json& jsonParsed) {
    if (!jsonParsed.is_object()) {
        return commandError(400, "CREATE_CARD payload must be a JSON object.");
    }

    const float cardWidth = jsonParsed.value("width", board.user_settings.defaultCardWidth);
    const float cardHeight = jsonParsed.value("height", board.user_settings.defaultCardHeight);
    if (!std::isfinite(cardWidth) || !std::isfinite(cardHeight) || cardWidth < 20.0f || cardHeight < 20.0f ||
        cardWidth > 1000.0f || cardHeight > 1000.0f) {
        return commandError(400, "Card dimensions must be between 20 and 1000.");
    }

    const bool hasX = jsonParsed.contains("x");
    const bool hasY = jsonParsed.contains("y");
    if (hasX != hasY) {
        return commandError(400, "CREATE_CARD must provide both x and y.");
    }

    float x = 0.0f;
    float y = 0.0f;
    if (hasX) {
        x = jsonParsed.at("x").get<float>();
        y = jsonParsed.at("y").get<float>();
    } else {
        x = cardWidth / 2.0f;
        y = cardHeight / 2.0f;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return commandError(400, "Card position must be finite.");
    }

    const string cardText = jsonParsed.value("text", string("New Card"));
    if (cardText.size() > MAX_CARD_TEXT_BYTES) {
        return commandError(400, "Card text exceeds 4096 bytes.");
    }

    Color cardColor = board.theme.cardColor;
    const char* cardColorField = jsonParsed.contains("color") ? "color" : nullptr;
    if (cardColorField && !applyColorJSON(jsonParsed[cardColorField], cardColor)) {
        return commandError(400, "Invalid card color.");
    }

    string mediaURL;
    const char* mediaURLField = jsonParsed.contains("mediaURL") ? "mediaURL" : nullptr;
    if (mediaURLField) {
        if (!jsonParsed[mediaURLField].is_string()) {
            return commandError(400, "Card mediaURL must be a string.");
        }
        mediaURL = jsonParsed[mediaURLField].get<string>();
    }

    Card* newCard = board.createCardAt(x, y, cardText, cardColor, cardWidth, cardHeight);
    if (!newCard) {
        return commandError(400, "Card creation failed validation.");
    }
    newCard->mediaURL = mediaURL;

    return commandSuccess(cardToJSON(*newCard).dump());
}

// Deletes a card and all connectors attached to it
ServerBoardCommandResult deleteCard(BoardState& board, const json& jsonParsed) {
    const uint32_t cardID = jsonParsed.value("cardID", 0u);
    if (cardID == 0) {
        return commandError(400, "Missing or invalid cardID for edit.");
    }
    if (board.cards.find(cardID) == board.cards.end()) {
        return commandError(404, "Card ID not found on this board.");
    }

    board.deleteCard(cardID);
    return commandSuccess(string{});
}

// Updates a card's geometry, text, image, or color and returns its updated state
ServerBoardCommandResult editCard(BoardState& board, const json& jsonParsed) {
    if (!jsonParsed.is_object()) {
        return commandError(400, "Card edit payload must be a JSON object.");
    }
    const uint32_t cardID = jsonParsed.value("cardID", 0u);
    if (cardID == 0) {
        return commandError(400, "Missing or invalid cardID for edit.");
    }

    auto cardIt = board.cards.find(cardID);
    if (cardIt == board.cards.end()) {
        return commandError(404, "Card ID not found on this board.");
    }

    Card updatedCard = cardIt->second;
    bool cardChanged = jsonParsed.contains("x") || jsonParsed.contains("y") ||
                       jsonParsed.contains("width") || jsonParsed.contains("height");
    if (!applyFloatJSON(jsonParsed, "x", updatedCard.x, -10000000.0f, 10000000.0f) ||
        !applyFloatJSON(jsonParsed, "y", updatedCard.y, -10000000.0f, 10000000.0f) ||
        !applyFloatJSON(jsonParsed, "width", updatedCard.width, 20.0f, 1000.0f) ||
        !applyFloatJSON(jsonParsed, "height", updatedCard.height, 20.0f, 1000.0f)) {
        return commandError(400, "Card geometry is invalid.");
    }
    if (jsonParsed.contains("text")) {
        if (!jsonParsed["text"].is_string()) {
            return commandError(400, "Card text must be a string.");
        }
        updatedCard.text = jsonParsed["text"].get<string>();
        if (updatedCard.text.size() > MAX_CARD_TEXT_BYTES) {
            return commandError(400, "Card text exceeds 4096 bytes.");
        }
        cardChanged = true;
    }
    if (jsonParsed.contains("zIndex")) {
        updatedCard.zIndex = jsonParsed["zIndex"].get<uint32_t>();
        cardChanged = true;
    }

    const char* mediaURLField = jsonParsed.contains("mediaURL") ? "mediaURL" : nullptr;
    if (mediaURLField) {
        if (!jsonParsed[mediaURLField].is_string()) {
            return commandError(400, "Card media URL must be a string.");
        }
        updatedCard.mediaURL = jsonParsed[mediaURLField].get<string>();
        cardChanged = true;
    }

    const char* cardColorField = jsonParsed.contains("color") ? "color" : nullptr;
    if (cardColorField) {
        if (!applyColorJSON(jsonParsed[cardColorField], updatedCard.color)) {
            return commandError(400, "Invalid card color.");
        }
        cardChanged = true;
    }
    if (!cardChanged) {
        return commandError(400, "EDIT_CARD must include geometry, text, image, or color.");
    }

    cardIt->second = updatedCard;
    board.bringCardToFront(cardID);

    json updatedPayload = jsonParsed;
    updatedPayload["cardID"] = cardID;
    updatedPayload["zIndex"] = board.cards.at(cardID).zIndex;
    if (mediaURLField) {
        updatedPayload["mediaURL"] = board.cards.at(cardID).mediaURL;
    }
    if (cardColorField) {
        updatedPayload["color"] = colorToJSON(board.cards.at(cardID).color);
    }

    return commandSuccess(updatedPayload.dump());
}

// Creates a connector between two cards and returns its assigned ID and state
ServerBoardCommandResult createConnector(BoardState& board, const json& jsonParsed) {
    const uint32_t sourceCardID = jsonParsed.value("sourceCardID", 0u);
    const uint32_t targetCardID = jsonParsed.value("targetCardID", 0u);
    if (sourceCardID == 0 || targetCardID == 0) {
        return commandError(400, "CREATE_CONNECTOR requires sourceCardID and targetCardID.");
    }

    const float connectorThickness = jsonParsed.value("thickness", board.user_settings.defaultConnectorThickness);
    if (!std::isfinite(connectorThickness) || connectorThickness < 0.1f || connectorThickness > 1000.0f) {
        return commandError(400, "Connector thickness must be between 0.1 and 1000.");
    }

    Color connectorColor = board.theme.connectorColor;
    if (jsonParsed.contains("color") && !applyColorJSON(jsonParsed["color"], connectorColor)) {
        return commandError(400, "Invalid connector color.");
    }

    Connector* newConnector = board.createConnector(sourceCardID, targetCardID, connectorThickness, connectorColor);
    if (!newConnector) {
        return commandError(400, "Connector endpoints must exist and may only be connected once.");
    }

    return commandSuccess(connectorToJSON(*newConnector).dump());
}

// Deletes a connector from the board
ServerBoardCommandResult deleteConnector(BoardState& board, const json& jsonParsed) {
    const uint32_t connectorID = jsonParsed.value("connectorID", 0u);
    if (connectorID == 0) {
        return commandError(400, "Missing or invalid connectorID for edit.");
    }
    if (board.connectors.find(connectorID) == board.connectors.end()) {
        return commandError(404, "Connector ID not found on this board.");
    }

    board.connectors.erase(connectorID);
    return commandSuccess(string{});
}

// Updates a connector's thickness or color
ServerBoardCommandResult editConnector(BoardState& board, const json& jsonParsed) {
    const uint32_t connectorID = jsonParsed.value("connectorID", 0u);
    if (connectorID == 0) {
        return commandError(400, "Missing or invalid connectorID for edit.");
    }

    auto connectorIt = board.connectors.find(connectorID);
    if (connectorIt == board.connectors.end()) {
        return commandError(404, "Connector ID not found on this board.");
    }

    Connector updatedConnector = connectorIt->second;
    bool connectorChanged = false;
    if (jsonParsed.contains("thickness")) {
        if (!jsonParsed["thickness"].is_number()) {
            return commandError(400, "Connector thickness must be numeric.");
        }
        const float connectorThickness = jsonParsed["thickness"].get<float>();
        if (!std::isfinite(connectorThickness) || connectorThickness < 0.1f || connectorThickness > 1000.0f) {
            return commandError(400, "Connector thickness must be between 0.1 and 1000.");
        }
        updatedConnector.thickness = connectorThickness;
        connectorChanged = true;
    }
    if (jsonParsed.contains("color")) {
        if (!applyColorJSON(jsonParsed["color"], updatedConnector.color)) {
            return commandError(400, "Invalid connector color.");
        }
        connectorChanged = true;
    }
    if (!connectorChanged) {
        return commandError(400, "EDIT_CONNECTOR requires thickness or color.");
    }

    connectorIt->second = updatedConnector;
    return commandSuccess(string{});
}

// Validates and deletes a selection of cards and connectors
ServerBoardCommandResult deleteObjects(BoardState& board, const json& jsonParsed) {
    if (!jsonParsed.is_object() || !jsonParsed.contains("objects") || !jsonParsed["objects"].is_array() ||
        jsonParsed["objects"].empty()) {
        return commandError(400, "DELETE_OBJECTS requires a non-empty objects array.");
    }

    vector<uint32_t> cardIDs;
    vector<uint32_t> connectorIDs;
    for (const json& selectedObjectJSON : jsonParsed["objects"]) {
        if (!selectedObjectJSON.is_object()) {
            return commandError(400, "Each selected object must contain a type and id.");
        }
        if (!selectedObjectJSON.contains("type") || !selectedObjectJSON["type"].is_string() ||
            !selectedObjectJSON.contains("id")) {
            return commandError(400, "Each selected object must contain a type and id.");
        }
        const string objectType = selectedObjectJSON["type"].get<string>();
        const uint32_t selectedObjectID = selectedObjectJSON["id"].get<uint32_t>();
        if (selectedObjectID == 0) {
            return commandError(400, "Each selected object must have a positive id.");
        }
        if (objectType == "card") {
            if (std::find(cardIDs.begin(), cardIDs.end(), selectedObjectID) == cardIDs.end()) {
                cardIDs.push_back(selectedObjectID);
            }
        } else if (objectType == "connector") {
            if (std::find(connectorIDs.begin(), connectorIDs.end(), selectedObjectID) == connectorIDs.end()) {
                connectorIDs.push_back(selectedObjectID);
            }
        } else {
            return commandError(400, "Selected object type must be card or connector.");
        }
    }

    for (const uint32_t cardID : cardIDs) {
        if (board.cards.find(cardID) == board.cards.end()) {
            return commandError(404, "A selected card was not found on this board.");
        }
    }
    for (const uint32_t connectorID : connectorIDs) {
        if (board.connectors.find(connectorID) == board.connectors.end()) {
            return commandError(404, "A selected connector was not found on this board.");
        }
    }

    // Checks the full selection before deleting anything
    for (const uint32_t connectorID : connectorIDs) board.connectors.erase(connectorID);
    for (const uint32_t cardID : cardIDs) board.deleteCard(cardID);
    return commandSuccess(string{});
}

// Changes one of the board's theme colors
ServerBoardCommandResult editBoardTheme(BoardState& board, const json& jsonParsed) {
    if (!jsonParsed.is_object() || !jsonParsed.contains("field") || !jsonParsed["field"].is_string() ||
        !jsonParsed.contains("color")) {
        return commandError(400, "EDIT_BOARD_THEME requires a field and color.");
    }

    const string themeField = jsonParsed["field"].get<string>();

    Color* selectedThemeColor = nullptr;
    if (themeField == "boardBackgroundColor") {
        selectedThemeColor = &board.theme.boardBackgroundColor;
    } else if (themeField == "cardColor") {
        selectedThemeColor = &board.theme.cardColor;
    } else if (themeField == "connectorColor") {
        selectedThemeColor = &board.theme.connectorColor;
    } else if (themeField == "accentColor") {
        selectedThemeColor = &board.theme.accentColor;
    } else if (themeField == "importantElements") {
        selectedThemeColor = &board.theme.importantElements;
    }

    Color updatedThemeColor = selectedThemeColor ? *selectedThemeColor : Color{};
    if (!selectedThemeColor || !applyColorJSON(jsonParsed["color"], updatedThemeColor)) {
        return commandError(400, "Unsupported theme color field or invalid color.");
    }
    *selectedThemeColor = updatedThemeColor;
    return commandSuccess(string{});
}

// Moves a card to the front of the board's draw order
ServerBoardCommandResult bringCardToFront(BoardState& board, const json& jsonParsed) {
    const uint32_t cardID = jsonParsed.value("cardID", 0u);
    if (cardID == 0) {
        return commandError(400, "BRING_CARD_TO_FRONT requires a positive cardID.");
    }
    if (board.cards.find(cardID) == board.cards.end()) {
        return commandError(404, "Card ID not found on this board.");
    }
    board.bringCardToFront(cardID);
    return commandSuccess(string{});
}

// Returns the error text used when a command payload cannot be parsed
string malformedPayloadMessage(OpCode commandOpcode) {
    switch (commandOpcode) {
        case OpCode::CREATE_CARD: return "Malformed JSON in CREATE_CARD.";
        case OpCode::MOVE_CARD:
        case OpCode::EDIT_CARD_CONTENT: return "Malformed JSON in EDIT_CARD.";
        case OpCode::DELETE_CARD: return "Malformed JSON payload in DELETE_CARD.";
        case OpCode::CREATE_CONNECTOR: return "Malformed JSON in CREATE_CONNECTOR.";
        case OpCode::DELETE_CONNECTOR: return "Malformed JSON payload in DELETE_CONNECTOR.";
        case OpCode::EDIT_CONNECTOR: return "Malformed JSON in EDIT_CONNECTOR.";
        case OpCode::DELETE_OBJECTS: return "Malformed JSON payload in DELETE_OBJECTS.";
        case OpCode::EDIT_BOARD_THEME: return "Malformed JSON payload in EDIT_BOARD_THEME.";
        case OpCode::BRING_CARD_TO_FRONT: return "Malformed JSON payload in BRING_CARD_TO_FRONT.";
        default: return "Malformed JSON payload.";
    }
}

}

// Applies one board command and returns an error or the update to broadcast
ServerBoardCommandResult ServerBoardCommandProcessor::process(
    BoardState& board,
    OpCode commandOpcode,
    const std::string& payloadStr,
    int) {
    json jsonParsed;
    try {
        jsonParsed = json::parse(payloadStr);
    } catch (const std::exception&) {
        return commandError(400, malformedPayloadMessage(commandOpcode));
    }

    try {
        ServerBoardCommandResult commandResult{false, 400, "Unsupported board command.", string{}};
        switch (commandOpcode) {
            case OpCode::CREATE_CARD:
                commandResult = createCard(board, jsonParsed);
                break;
            case OpCode::MOVE_CARD:
            case OpCode::EDIT_CARD_CONTENT:
                commandResult = editCard(board, jsonParsed);
                break;
            case OpCode::DELETE_CARD:
                commandResult = deleteCard(board, jsonParsed);
                break;
            case OpCode::CREATE_CONNECTOR:
                commandResult = createConnector(board, jsonParsed);
                break;
            case OpCode::EDIT_CONNECTOR:
                commandResult = editConnector(board, jsonParsed);
                break;
            case OpCode::DELETE_CONNECTOR:
                commandResult = deleteConnector(board, jsonParsed);
                break;
            case OpCode::DELETE_OBJECTS:
                commandResult = deleteObjects(board, jsonParsed);
                break;
            case OpCode::EDIT_BOARD_THEME:
                commandResult = editBoardTheme(board, jsonParsed);
                break;
            case OpCode::BRING_CARD_TO_FRONT:
                commandResult = bringCardToFront(board, jsonParsed);
                break;
            default:
                break;
        }
        if (commandResult.success && commandResult.updatePayload.empty()) {
            commandResult.updatePayload = payloadStr;
        }
        return commandResult;
    } catch (const std::exception&) {
        return commandError(400, malformedPayloadMessage(commandOpcode));
    }
}

// Builds a snapshot containing the board theme, cards, connectors, and next IDs
nlohmann::json ServerBoardCommandProcessor::boardSnapshotToJSON(const BoardState& board) {
    json cards = json::array();
    const vector<uint32_t> cardIDsInZOrder = board.getCardIDsInZOrder();
    for (const uint32_t cardID : cardIDsInZOrder) {
        cards.push_back(cardToJSON(board.cards.at(cardID)));
    }

    vector<uint32_t> connectorIDs;
    connectorIDs.reserve(board.connectors.size());
    for (const auto& entry : board.connectors) connectorIDs.push_back(entry.first);
    std::sort(connectorIDs.begin(), connectorIDs.end());

    json connectors = json::array();
    for (const uint32_t connectorID : connectorIDs) {
        connectors.push_back(connectorToJSON(board.connectors.at(connectorID)));
    }

    return {{"syncType", "snapshot"}, {"boardID", board.boardID}, {"revision", board.revision},
            {"boardName", board.boardName},
            {"createdAt", board.createdAt}, {"lastEditedAt", board.lastEditedAt}, {"theme", board.theme},
            {"cards", cards}, {"connectors", connectors},
            {"nextCardID", board.nextCardID}, {"nextConnectorID", board.nextConnectorID}};
}

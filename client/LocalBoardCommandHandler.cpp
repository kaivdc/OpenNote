#include "LocalBoardCommandHandler.h"
#include "PacketCreation.h"

#include <iostream>

namespace {

// Sends one board command to the server
bool sendBoardCommand(int socket, OpCode opcode, const json& payload) {
    if (!sendPacket(socket, static_cast<uint8_t>(opcode), payload.dump())) {
        std::cerr << "[ERROR] : Failed to send board command.\n";
        return false;
    }
    return true;
}

// Sends a save request with an optional board name
bool sendSaveBoardRequest(int socket, const string& boardName) {
    json payload = json::object();
    if (!boardName.empty()) {
        payload["boardName"] = boardName;
    }
    return sendBoardCommand(socket, OpCode::SAVE_BOARD, payload);
}

}

// Connects the handler to the server socket
LocalBoardCommandHandler::LocalBoardCommandHandler(int socket) : socket(socket) {}

// Sends the requested board command to the server
void LocalBoardCommandHandler::handle(const BoardCommand& command) {
    if (!boardReady && !std::holds_alternative<JoinBoardCommand>(command) &&
        !std::holds_alternative<RefreshBoardListCommand>(command) &&
        !std::holds_alternative<CreateBoardCommand>(command) &&
        !std::holds_alternative<DeleteBoardCommand>(command)) return;
    std::visit([this](const auto& typedCommand) { apply(typedCommand); }, command);
}

// Enables board commands after the server snapshot is loaded
void LocalBoardCommandHandler::setBoardReady(bool ready) {
    boardReady = ready;
    if (ready) boardRequestPending = false;
}

// Clears a failed board request without enabling board commands
void LocalBoardCommandHandler::clearPendingBoardRequest() {
    boardRequestPending = false;
}

// Sends a save-as request when the active board is ready
bool LocalBoardCommandHandler::sendSaveBoardAs(const string& boardName) {
    if (!boardReady) return false;
    return sendSaveBoardRequest(socket, boardName);
}

// Sends a card creation command so the server can assign its ID
void LocalBoardCommandHandler::apply(const CreateCardCommand& command) {
    json payload;
    payload["x"] = command.x;
    payload["y"] = command.y;
    payload["text"] = command.text;
    payload["width"] = command.width;
    payload["height"] = command.height;
    sendBoardCommand(socket, OpCode::CREATE_CARD, payload);
}

// Sends a request to delete a card from the board
void LocalBoardCommandHandler::apply(const DeleteCardCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    sendBoardCommand(socket, OpCode::DELETE_CARD, payload);
}

// Sends a connector creation command so the server can assign its ID
void LocalBoardCommandHandler::apply(const CreateConnectorCommand& command) {
    json payload;
    payload["sourceCardID"] = command.sourceCardID;
    payload["targetCardID"] = command.targetCardID;
    payload["thickness"] = command.thickness;
    sendBoardCommand(socket, OpCode::CREATE_CONNECTOR, payload);
}

// Sends a user's selected object and highlight color to the server
void LocalBoardCommandHandler::apply(const SetObjectSelectionCommand& command) {
    json payload;
    payload["objectType"] = command.objectType;
    payload["objectID"] = command.objectID;
    payload["selected"] = command.selected;
    payload["clientName"] = command.clientName;
    payload["selectionColor"] = command.selectionColor;
    sendBoardCommand(socket, OpCode::OBJECT_SELECTION, payload);
}

// Sends a request to delete a connector from the board
void LocalBoardCommandHandler::apply(const DeleteConnectorCommand& command) {
    json payload;
    payload["connectorID"] = command.connectorID;
    sendBoardCommand(socket, OpCode::DELETE_CONNECTOR, payload);
}

// Sends a request to delete the selected connectors and cards
void LocalBoardCommandHandler::apply(const DeleteObjectsCommand& command) {
    json payload;
    payload["objects"] = json::array();
    for (const BoardObjectRef& object : command.objects) {
        const string objectType = object.kind == BoardObjectRef::Kind::Connector ? "connector" : "card";
        payload["objects"].push_back({{"type", objectType}, {"id", object.id}});
    }
    sendBoardCommand(socket, OpCode::DELETE_OBJECTS, payload);
}

// Sends a request to update a card's position and size
void LocalBoardCommandHandler::apply(const SetCardGeometryCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    payload["x"] = command.x;
    payload["y"] = command.y;
    payload["width"] = command.width;
    payload["height"] = command.height;
    sendBoardCommand(socket, OpCode::MOVE_CARD, payload);
}

// Sends a request to update a card's text
void LocalBoardCommandHandler::apply(const SetCardTextCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    payload["text"] = command.text;
    payload["expectedTextRevision"] = command.expectedTextRevision;
    sendBoardCommand(socket, OpCode::EDIT_CARD_CONTENT, payload);
}

// Sends a request to update a card's image source
void LocalBoardCommandHandler::apply(const SetCardImageCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    payload["mediaURL"] = command.imageSource;
    sendBoardCommand(socket, OpCode::EDIT_CARD_CONTENT, payload);
}

// Sends a request to update a card's color
void LocalBoardCommandHandler::apply(const SetCardColorCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    payload["color"] = command.color;
    sendBoardCommand(socket, OpCode::EDIT_CARD_CONTENT, payload);
}

// Sends a request to update a connector's thickness
void LocalBoardCommandHandler::apply(const SetConnectorThicknessCommand& command) {
    json payload;
    payload["connectorID"] = command.connectorID;
    payload["thickness"] = command.thickness;
    sendBoardCommand(socket, OpCode::EDIT_CONNECTOR, payload);
}

// Sends a request to update a connector's color
void LocalBoardCommandHandler::apply(const SetConnectorColorCommand& command) {
    json payload;
    payload["connectorID"] = command.connectorID;
    payload["color"] = command.color;
    sendBoardCommand(socket, OpCode::EDIT_CONNECTOR, payload);
}

// Sends a request to update a board theme color
void LocalBoardCommandHandler::apply(const SetBoardThemeColorCommand& command) {
    string fieldName;
    switch (command.field) {
        case BoardThemeField::BoardBackground: fieldName = "boardBackgroundColor"; break;
        case BoardThemeField::CardColor: fieldName = "cardColor"; break;
        case BoardThemeField::ConnectorColor: fieldName = "connectorColor"; break;
        case BoardThemeField::AccentColor: fieldName = "accentColor"; break;
        case BoardThemeField::ImportantElements: fieldName = "importantElements"; break;
        default: return;
    }

    json payload;
    payload["field"] = fieldName;
    payload["color"] = command.color;
    sendBoardCommand(socket, OpCode::EDIT_BOARD_THEME, payload);
}

// Sends a request to move a card to the front of the draw order
void LocalBoardCommandHandler::apply(const BringCardToFrontCommand& command) {
    json payload;
    payload["cardID"] = command.cardID;
    sendBoardCommand(socket, OpCode::BRING_CARD_TO_FRONT, payload);
}

// Requests that the server save the active board
void LocalBoardCommandHandler::apply(const SaveBoardCommand& command) {
    sendSaveBoardRequest(socket, command.boardName);
}

// Requests a board snapshot and waits for the server to load it
void LocalBoardCommandHandler::apply(const JoinBoardCommand& command) {
    if (boardRequestPending) return;
    json payload;
    payload["boardID"] = command.boardID;
    if (sendBoardCommand(socket, OpCode::SYNC_BOARD, payload)) {
        boardReady = false;
        boardRequestPending = true;
    }
}

// Requests the names, IDs, and dates for boards available on the server
void LocalBoardCommandHandler::apply(const RefreshBoardListCommand&) {
    sendBoardCommand(socket, OpCode::LIST_BOARDS, json::object());
}

// Requests a new board so the server can assign its ID
void LocalBoardCommandHandler::apply(const CreateBoardCommand&) {
    if (!boardRequestPending && sendBoardCommand(socket, OpCode::CREATE_BOARD, json::object())) {
        boardReady = false;
        boardRequestPending = true;
    }
}

// Requests the server to delete one board
void LocalBoardCommandHandler::apply(const DeleteBoardCommand& command) {
    json payload;
    payload["boardID"] = command.boardID;
    sendBoardCommand(socket, OpCode::DELETE_BOARD, payload);
}

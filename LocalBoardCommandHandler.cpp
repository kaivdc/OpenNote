#include "LocalBoardCommandHandler.h"
#include "PacketCreation.h"

#include <iostream>

// Connects the handler to the board it updates
LocalBoardCommandHandler::LocalBoardCommandHandler(BoardState& board, int socket) : board(board), socket(socket) {}

// Applies a command locally or sends it to the server
void LocalBoardCommandHandler::handle(const BoardCommand& command) {
    if (!boardReady) return;
    std::visit([this](const auto& typedCommand) { apply(typedCommand); }, command);
}

// Enables board commands after the server snapshot is loaded
void LocalBoardCommandHandler::setBoardReady(bool ready) {
    boardReady = ready;
}

// Sends a card creation command so the server can assign its ID
void LocalBoardCommandHandler::apply(const CreateCardCommand& command) {
    json payload;
    payload["x"] = command.x;
    payload["y"] = command.y;
    payload["text"] = command.text;

    if (!sendPacket(socket, static_cast<uint8_t>(OpCode::CREATE_CARD), payload.dump())) {
        std::cerr << "[ERROR] : Failed to send CREATE_CARD command.\n";
    }
}

// Deletes a card from the local board
void LocalBoardCommandHandler::apply(const DeleteCardCommand& command) {
    board.deleteCard(command.cardID);
}

// Sends a connector creation command so the server can assign its ID
void LocalBoardCommandHandler::apply(const CreateConnectorCommand& command) {
    json payload;
    payload["sourceCardID"] = command.sourceCardID;
    payload["targetCardID"] = command.targetCardID;

    if (!sendPacket(socket, static_cast<uint8_t>(OpCode::CREATE_CONNECTOR), payload.dump())) {
        std::cerr << "[ERROR] : Failed to send CREATE_CONNECTOR command.\n";
    }
}

// Deletes a connector from the local board
void LocalBoardCommandHandler::apply(const DeleteConnectorCommand& command) {
    board.deleteConnector(command.connectorID);
}

// Deletes the selected connectors and cards from the local board
void LocalBoardCommandHandler::apply(const DeleteObjectsCommand& command) {
    for (const BoardObjectRef& object : command.objects) {
        if (object.kind == BoardObjectRef::Kind::Connector) {
            board.deleteConnector(object.id);
        }
    }
    for (const BoardObjectRef& object : command.objects) {
        if (object.kind == BoardObjectRef::Kind::Card) {
            board.deleteCard(object.id);
        }
    }
}

// Updates a card's position and size on the local board
void LocalBoardCommandHandler::apply(const SetCardGeometryCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    Card& card = it->second;
    card.x = command.x;
    card.y = command.y;
    card.width = command.width;
    card.height = command.height;
    board.bringCardToFront(command.cardID);
}

// Updates a card's text on the local board
void LocalBoardCommandHandler::apply(const SetCardTextCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.text = command.text;
    board.bringCardToFront(command.cardID);
}

// Updates a card's image source on the local board
void LocalBoardCommandHandler::apply(const SetCardImageCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.mediaURL = command.imageSource;
    board.bringCardToFront(command.cardID);
}

// Updates a card's color on the local board
void LocalBoardCommandHandler::apply(const SetCardColorCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.color = command.color;
    board.bringCardToFront(command.cardID);
}

// Updates a connector's thickness on the local board
void LocalBoardCommandHandler::apply(const SetConnectorThicknessCommand& command) {
    auto it = board.connectors.find(command.connectorID);
    if (it != board.connectors.end()) {
        it->second.thickness = command.thickness;
    }
}

// Updates a connector's color on the local board
void LocalBoardCommandHandler::apply(const SetConnectorColorCommand& command) {
    auto it = board.connectors.find(command.connectorID);
    if (it != board.connectors.end()) {
        it->second.color = command.color;
    }
}

// Returns the theme color selected by the given field
Color& LocalBoardCommandHandler::themeColor(BoardTheme& theme, BoardThemeField field) {
    switch (field) {
        case BoardThemeField::BoardBackground:   return theme.boardBackgroundColor;
        case BoardThemeField::CardColor:         return theme.cardColor;
        case BoardThemeField::ConnectorColor:    return theme.connectorColor;
        case BoardThemeField::AccentColor:       return theme.accentColor;
        case BoardThemeField::ImportantElements: return theme.importantElements;
    }
    return theme.boardBackgroundColor;
}

// Updates a board theme color on the local board
void LocalBoardCommandHandler::apply(const SetBoardThemeColorCommand& command) {
    themeColor(board.theme, command.field) = command.color;
}

// Moves a card to the front of the local board's draw order
void LocalBoardCommandHandler::apply(const BringCardToFrontCommand& command) {
    board.bringCardToFront(command.cardID);
}

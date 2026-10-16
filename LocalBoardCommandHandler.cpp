#include "LocalBoardCommandHandler.h"

// Connects the handler to the board it updates
LocalBoardCommandHandler::LocalBoardCommandHandler(BoardState& board) : board(board) {}

// Sends a board command to the matching local handler
void LocalBoardCommandHandler::handle(const BoardCommand& command) {
    std::visit([this](const auto& typedCommand) { apply(typedCommand); }, command);
}

// Creates a card on the local board
void LocalBoardCommandHandler::apply(const CreateCardCommand& command) {
    board.createCardAt(command.x, command.y, command.text);
}

// Deletes a card from the local board
void LocalBoardCommandHandler::apply(const DeleteCardCommand& command) {
    board.deleteCard(command.cardID);
}

// Creates a connector between two cards on the local board
void LocalBoardCommandHandler::apply(const CreateConnectorCommand& command) {
    board.createConnector(command.sourceCardID, command.targetCardID);
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

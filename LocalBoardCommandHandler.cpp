#include "LocalBoardCommandHandler.h"

LocalBoardCommandHandler::LocalBoardCommandHandler(BoardState& board) : board(board) {}

void LocalBoardCommandHandler::handle(const BoardCommand& command) {
    std::visit([this](const auto& typedCommand) { apply(typedCommand); }, command);
}

void LocalBoardCommandHandler::apply(const CreateCardCommand& command) {
    board.createCardAt(command.centerX, command.centerY, command.text);
}

void LocalBoardCommandHandler::apply(const DeleteCardCommand& command) {
    board.deleteCard(command.cardID);
}

void LocalBoardCommandHandler::apply(const CreateConnectorCommand& command) {
    board.createConnector(command.sourceCardID, command.targetCardID);
}

void LocalBoardCommandHandler::apply(const DeleteConnectorCommand& command) {
    board.deleteConnector(command.connectorID);
}

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

void LocalBoardCommandHandler::apply(const SetCardTextCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.text = command.text;
    board.bringCardToFront(command.cardID);
}

void LocalBoardCommandHandler::apply(const SetCardImageCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.mediaURL = command.source;
    board.bringCardToFront(command.cardID);
}

void LocalBoardCommandHandler::apply(const SetCardColorCommand& command) {
    auto it = board.cards.find(command.cardID);
    if (it == board.cards.end()) return;
    it->second.color = command.color;
    board.bringCardToFront(command.cardID);
}

void LocalBoardCommandHandler::apply(const SetConnectorThicknessCommand& command) {
    auto it = board.connectors.find(command.connectorID);
    if (it != board.connectors.end()) {
        it->second.thickness = command.thickness;
    }
}

void LocalBoardCommandHandler::apply(const SetConnectorColorCommand& command) {
    auto it = board.connectors.find(command.connectorID);
    if (it != board.connectors.end()) {
        it->second.color = command.color;
    }
}

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

void LocalBoardCommandHandler::apply(const SetBoardThemeColorCommand& command) {
    themeColor(board.theme, command.field) = command.color;
}

void LocalBoardCommandHandler::apply(const BringCardToFrontCommand& command) {
    board.bringCardToFront(command.cardID);
}

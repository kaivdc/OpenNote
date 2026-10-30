#ifndef BOARD_COMMANDS_H
#define BOARD_COMMANDS_H

#include "BoardStructs.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct CreateCardCommand {
    float x;
    float y;
    std::string text;
};

struct DeleteCardCommand {
    uint32_t cardID;
};

struct CreateConnectorCommand {
    uint32_t sourceCardID;
    uint32_t targetCardID;
};

struct DeleteConnectorCommand {
    uint32_t connectorID;
};

struct BoardObjectRef {
    enum class Kind {
        Card,
        Connector
    };

    Kind kind;
    uint32_t id;
};

struct DeleteObjectsCommand {
    std::vector<BoardObjectRef> objects;
};

struct SetCardGeometryCommand {
    uint32_t cardID;
    float x;
    float y;
    float width;
    float height;
};

struct SetCardTextCommand {
    uint32_t cardID;
    std::string text;
};

struct SetCardImageCommand {
    uint32_t cardID;
    std::string imageSource;
};

struct SetCardColorCommand {
    uint32_t cardID;
    Color color;
};

struct SetConnectorThicknessCommand {
    uint32_t connectorID;
    float thickness;
};

struct SetConnectorColorCommand {
    uint32_t connectorID;
    Color color;
};

enum class BoardThemeField {
    BoardBackground,
    CardColor,
    ConnectorColor,
    AccentColor,
    ImportantElements
};

struct SetBoardThemeColorCommand {
    BoardThemeField field;
    Color color;
};

struct BringCardToFrontCommand {
    uint32_t cardID;
};

struct SaveBoardCommand {
    std::string boardName;
};

struct JoinBoardCommand {
    uint32_t boardID;
};

struct RefreshBoardListCommand {};
struct CreateBoardCommand {};

struct DeleteBoardCommand {
    uint32_t boardID;
};

using BoardCommand = std::variant<
    CreateCardCommand,
    DeleteCardCommand,
    CreateConnectorCommand,
    DeleteConnectorCommand,
    DeleteObjectsCommand,
    SetCardGeometryCommand,
    SetCardTextCommand,
    SetCardImageCommand,
    SetCardColorCommand,
    SetConnectorThicknessCommand,
    SetConnectorColorCommand,
    SetBoardThemeColorCommand,
    BringCardToFrontCommand,
    SaveBoardCommand,
    JoinBoardCommand,
    RefreshBoardListCommand,
    CreateBoardCommand,
    DeleteBoardCommand>;

class BoardCommandHandler {
public:
    // Allows derived command handlers to be deleted through the base class
    virtual ~BoardCommandHandler() = default;
    // Applies a board command using the active board handler
    virtual void handle(const BoardCommand& command) = 0;
    // Sends a save-as request and reports whether it reached the socket
    virtual bool sendSaveBoardAs(const std::string& boardName) = 0;
};

#endif

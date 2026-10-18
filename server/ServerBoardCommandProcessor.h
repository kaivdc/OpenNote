#ifndef SERVER_BOARD_COMMAND_PROCESSOR_H
#define SERVER_BOARD_COMMAND_PROCESSOR_H

#include "BoardStructs.h"

#include <cstdint>
#include <string>

struct ServerBoardCommandResult {
    bool success;
    int errorCode;
    std::string errorMessage;
    std::string updatePayload;
};

class ServerBoardCommandProcessor {
public:
    // Validates one board command, updates the board, and returns its result
    static ServerBoardCommandResult process(
        BoardState& board,
        OpCode commandOpcode,
        const std::string& payloadStr,
        int clientSocket);

    // Converts the current board state into a sync snapshot
    static nlohmann::json boardSnapshotToJSON(const BoardState& board);
};

#endif

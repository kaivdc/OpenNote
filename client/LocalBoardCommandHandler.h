#ifndef LOCAL_BOARD_COMMAND_HANDLER_H
#define LOCAL_BOARD_COMMAND_HANDLER_H

#include "BoardCommands.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

class LocalBoardCommandHandler final : public BoardCommandHandler {
public:
    explicit LocalBoardCommandHandler(int socket);
    ~LocalBoardCommandHandler();
    void handle(const BoardCommand& command) override;
    void setBoardReady(bool ready);
    void clearPendingBoardRequest();
    bool sendSaveBoardAs(const std::string& boardName) override;
    bool sendHeartbeat();
    bool sendDisconnect();
    bool takeSaveAsSendFailure();
    void stopSending();

private:
    struct OutgoingPacket {
        uint8_t opcode;
        std::string payload;
        bool isSaveAsRequest;
    };

    int socket;
    bool boardReady = true;
    bool boardRequestPending = false;
    std::mutex outgoingQueueMutex;
    std::condition_variable outgoingQueueChanged;
    std::queue<OutgoingPacket> outgoingPackets;
    std::thread sendingThread;
    bool stoppingSender = false;
    bool saveAsSendFailed = false;

    bool queueBoardCommand(OpCode opcode, const nlohmann::json& payload, bool isSaveAsRequest = false);
    bool queuePacket(uint8_t opcode, std::string payload, bool isSaveAsRequest = false);
    bool sendSaveBoardRequest(const std::string& boardName, bool isSaveAsRequest = false);
    void sendingLoop();

    void apply(const CreateCardCommand& command);
    void apply(const DeleteCardCommand& command);
    void apply(const CreateConnectorCommand& command);
    void apply(const SetObjectSelectionCommand& command);
    void apply(const DeleteConnectorCommand& command);
    void apply(const DeleteObjectsCommand& command);
    void apply(const SetCardGeometryCommand& command);
    void apply(const SetCardTextCommand& command);
    void apply(const SetCardImageCommand& command);
    void apply(const SetCardColorCommand& command);
    void apply(const SetConnectorThicknessCommand& command);
    void apply(const SetConnectorColorCommand& command);
    void apply(const SetBoardThemeColorCommand& command);
    void apply(const BringCardToFrontCommand& command);
    void apply(const SaveBoardCommand& command);
    void apply(const JoinBoardCommand& command);
    void apply(const RefreshBoardListCommand& command);
    void apply(const CreateBoardCommand& command);
    void apply(const DeleteBoardCommand& command);
};

#endif

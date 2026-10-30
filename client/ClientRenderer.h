#ifndef CLIENT_RENDERER_H
#define CLIENT_RENDERER_H

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <GLFW/glfw3.h>
#include "BoardStructs.h"
#include "BoardCommands.h"
#include "BoardInspector.h"
#include "BoardUIState.h"
#include "ClientImageCache.h"
#include "imgui.h"
#include <string>
#include <vector>

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
};

// Enum for which edge/corner of a card is being resized
enum class ResizeHandle {
    None,
    N, S, E, W,
    NE, NW, SE, SW
};

class ClientRenderer {
private:
    struct CardGeometry {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    GLFWwindow* window = nullptr;
    Camera camera;
    uint32_t draggedCardID = 0;
    uint32_t resizingCardID = 0;
    uint32_t geometryPreviewCardID = 0;
    CardGeometry geometryPreview;
    std::vector<BoardCommand> pendingChangePreviews;
    ResizeHandle activeResizeHandle = ResizeHandle::None;
    ImVec2 contextMenuWorldPos = {0.0f, 0.0f};
    ContextMenuTarget currentMenuTarget;
    ContextMenuTarget selection;
    std::vector<ContextMenuTarget> selectedObjects;
    bool selectionSidebarOpen = false;

    bool isSelectionSelecting = false;
    bool selectionHasMoved = false;
    ImVec2 selectionStart = {0.0f, 0.0f};
    ImVec2 selectionEnd = {0.0f, 0.0f};

    bool isRightDragging = false;
    bool isMiddleDragging = false;

    bool isConnecting = false;
    uint32_t connectingSourceCardID = 0;

    ClientImageCache imageCache;
    BoardInspector inspector;
    uint32_t hoveredCardID = 0;
    uint32_t dropPreviewCardID = 0;
    std::vector<std::string> droppedPaths;
    BoardList availableBoards;
    bool showingBoardSelection = true;
    char saveBoardName[MAX_BOARD_NAME_LENGTH + 1] = {};
    bool openSaveBoardAsPopup = false;
    bool saveBoardNameError = false;
    std::string saveBoardNameErrorMessage;
    bool saveBoardAsRequestPending = false;
    bool closeSaveBoardAsPopup = false;
    std::string serverErrorMessage;
    uint32_t pendingDeleteBoardID = 0;
    std::string pendingDeleteBoardName;
    bool openDeleteBoardPopup = false;

public:
    GLFWwindow* initialize(int width, int height, const char* title);
    void updateAndRender(const BoardState& board, BoardCommandHandler& commandHandler);
    // Replaces the boards shown by the board selection screen
    void setAvailableBoards(const BoardList& boardList);
    // Shows or hides the board selection screen
    void setBoardSelectionVisible(bool visible);
    // Clears selected objects and selection controls
    void clearSelection();
    // Shows an error from the server in the client window
    void showServerError(const std::string& message, bool saveBoardAsError = false);
    // Updates the board name after a successful save response
    void confirmBoardSave(const std::string& boardName);
    // Adds a local preview for a board command while the server processes it
    void previewChange(const BoardCommand& command);
    // Updates local previews after a server packet changes the board
    void confirmChange(uint8_t opcode, const BoardState& board);
    void shutdown();

    bool shouldClose() const { return window && glfwWindowShouldClose(window); }

private:
    void handleInput(const BoardState& board, BoardCommandHandler& commandHandler, ImVec2 boardCenter, bool hovered);
    // Draws the context menu for the current target and handles board saving
    void handleContextMenu(const BoardState& board, BoardCommandHandler& commandHandler, const ContextMenuTarget& context);
    // Draws the server boards and lets the client request one
    void drawBoardSelectionScreen(BoardCommandHandler& commandHandler);
    void drawImageTexture(ImDrawList* drawList, GLuint texture, ImVec2 min, ImVec2 max) const;
    void processDroppedFiles(const BoardState& board, BoardCommandHandler& commandHandler);
    static void onFilesDropped(GLFWwindow* window, int pathCount, const char* paths[]);

    // Coordinate conversion between world space and screen space
    ImVec2 worldToScreen(float worldX, float worldY, ImVec2 boardCenter) const;
    ImVec2 screenToWorld(ImVec2 screenPos, ImVec2 boardCenter) const;

    // Hit detection (invisible buttons for cards, distance-to-segment for connectors)
    uint32_t submitCardHitboxes(const BoardState& board, BoardCommandHandler& commandHandler, ImVec2 boardCenter);
    uint32_t hitTestConnector(const BoardState& localBoard, ImVec2 screenPos, ImVec2 boardCenter) const;
    float distanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b) const;

    // Card resize handles (anchored scaling from edges/corners)
    CardGeometry geometryForCard(const Card& card) const;
    ResizeHandle hitTestResizeHandle(const CardGeometry& geometry, ImVec2 mousePos, ImVec2 boardCenter) const;
    CardGeometry applyCardResize(const CardGeometry& geometry, ImVec2 mouseDelta) const;
    void drawResizeHandles(ImDrawList* draw_list, const CardGeometry& geometry, const BoardTheme& theme, ImVec2 boardCenter);

    // Selection helpers
    bool isSelected(MenuContextType type, uint32_t id) const;
    // Checks whether an object is waiting for server confirmation of deletion
    bool isPendingDelete(MenuContextType type, uint32_t id) const;
    // Checks whether a card geometry update is waiting for server confirmation
    bool hasPendingGeometry(uint32_t cardID) const;
    void setSelection(const ContextMenuTarget& target);
    void setMultipleSelection(const std::vector<ContextMenuTarget>& targets);
    void syncSelection(const BoardState& localBoard);
    void updateSelectionSelection(const BoardState& board, ImVec2 boardCenter, bool backgroundHovered);
    void selectObjectsInSelection(const BoardState& localBoard, ImVec2 boardCenter);
    void deleteSelectedObjects(BoardCommandHandler& commandHandler);

    // Input sub-handlers
    void handleCameraControls(bool hovered);
    void handleObjectSelection(uint32_t hoveredCardID, uint32_t hoveredConnectorID, bool backgroundHovered);
    void handleConnectingMode(const BoardState& board, BoardCommandHandler& commandHandler, uint32_t hoveredCardID);
    void handleKeyboardShortcuts(BoardCommandHandler& commandHandler);

    void drawGrid(ImDrawList* draw_list, const BoardState& board, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter);
    void drawConnectors(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter);
    void drawConnectionPreview(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter);
    void drawCards(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter);

};

#endif

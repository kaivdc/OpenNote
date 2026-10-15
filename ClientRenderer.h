#ifndef CLIENT_RENDERER_H
#define CLIENT_RENDERER_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "BoardStructs.h"
#include "imgui.h"

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
};

enum class MenuContextType {
    Background,
    Card,
    Connector
};

struct ContextMenuTarget {
    MenuContextType type = MenuContextType::Background;
    uint32_t targetID = 0;
};

// Enum for which edge/corner of a card is being resized
enum class ResizeHandle {
    None,
    N, S, E, W,
    NE, NW, SE, SW
};

class ClientRenderer {
private:
    GLFWwindow* window = nullptr;
    Camera camera;
    uint32_t draggedCardID = 0;
    uint32_t resizingCardID = 0;
    ResizeHandle activeResizeHandle = ResizeHandle::None;
    ImVec2 contextMenuWorldPos = {0.0f, 0.0f};
    ContextMenuTarget currentMenuTarget;
    ContextMenuTarget selection;
    bool selectionSidebarOpen = false;

    bool isRightDragging = false;
    bool isMiddleDragging = false;

    bool isConnecting = false;
    uint32_t connectingSourceCardID = 0;

public:
    GLFWwindow* initialize(int width, int height, const char* title);
    void updateAndRender(BoardState& localBoard);
    void shutdown();

    bool shouldClose() const { return window && glfwWindowShouldClose(window); }

private:
    void handleInput(BoardState& localBoard, ImVec2 boardCenter, bool hovered);
    void handleContextMenu(BoardState& localBoard, ImVec2 boardCenter, const ContextMenuTarget& context);
    void changeBackgroundColor();

    // Coordinate conversion between world space and screen space
    ImVec2 worldToScreen(float worldX, float worldY, ImVec2 boardCenter) const;
    ImVec2 screenToWorld(ImVec2 screenPos, ImVec2 boardCenter) const;

    // Hit detection (invisible buttons for cards, distance-to-segment for connectors)
    uint32_t submitCardHitboxes(BoardState& localBoard, ImVec2 boardCenter);
    uint32_t hitTestConnector(const BoardState& localBoard, ImVec2 screenPos, ImVec2 boardCenter) const;
    float distanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b) const;

    // Card resize handles (anchored scaling from edges/corners)
    ResizeHandle hitTestResizeHandle(const Card& card, ImVec2 mousePos, ImVec2 boardCenter) const;
    void applyCardResize(Card& card);
    void drawResizeHandles(ImDrawList* draw_list, const Card& card, const BoardTheme& theme, ImVec2 boardCenter);

    // Selection helpers
    bool isSelected(MenuContextType type, uint32_t id) const;
    void syncSelection(const BoardState& localBoard);

    // Input sub-handlers
    void handleCameraControls(bool hovered);
    void handleObjectSelection(uint32_t hoveredCardID, uint32_t hoveredConnectorID, bool backgroundHovered);
    void handleConnectingMode(BoardState& localBoard, uint32_t hoveredCardID);
    void handleKeyboardShortcuts(BoardState& localBoard);

    void drawGrid(ImDrawList* draw_list, BoardState& localBoard, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter);
    void drawConnectors(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);
    void drawConnectionPreview(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);
    void drawCards(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);

    // Hook for the selection sidebar (per-object editing options)
    void drawSelectionSidebar(BoardState& localBoard);
    void drawCardOptions(BoardState& localBoard);
    void drawConnectorOptions(BoardState& localBoard);
    const char* getSelectionTitle(MenuContextType type) const;

    // Applies the board theme to the global ImGui style colors
    void applyTheme(const BoardTheme& theme);
};

#endif
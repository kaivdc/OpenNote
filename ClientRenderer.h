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

class ClientRenderer {
private:
    GLFWwindow* window = nullptr;
    Camera camera;
    uint32_t draggedCardID = 0;
    ImVec2 contextMenuWorldPos = {0.0f, 0.0f};
    ContextMenuTarget currentMenuTarget;

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
    void handleInput(BoardState& localBoard, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter, bool hovered, bool active);
    void handleContextMenu(BoardState& localBoard, ImVec2 boardCenter, const ContextMenuTarget& context);
    void drawGrid(ImDrawList* draw_list, BoardState& localBoard, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter);
    void drawConnectors(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);
    void drawConnectionPreview(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);
    void drawCards(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter);
};

#endif
#include "ClientRenderer.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include "BoardStructs.h"
#include <cmath>

using namespace std;

static Camera boardCamera;

GLFWwindow* ClientRenderer::initialize(int width, int height, const char* title) {
    if (!glfwInit()) {
        cerr << "[ERROR] : Failed to initialize GLFW\n";
        return nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        cerr << "[ERROR] : Failed to create GLFW window\n";
        glfwTerminate();
        return nullptr;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE; 
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "[WARNING] GLEW init returned: " << glewGetErrorString(err) 
                  << " (Code: " << err << "). Continuing anyway for WSLg...\n";
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGui::StyleColorsDark();

    return window;
}

void ClientRenderer::updateAndRender(BoardState& localBoard) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Board window set to display size
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoTitleBar |                     
        ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoResize |             
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoBringToFrontOnFocus |         
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("board Root", nullptr, window_flags);

    // Info bar
    if (ImGui::BeginChild("Toolbar", ImVec2(0, 45), true)) {
        ImGui::Text("Board ID: %u | Cards: %zu | Connectors: %zu | Zoom: %.2fx", 
                    localBoard.boardID, localBoard.cards.size(), localBoard.connectors.size(), camera.zoom);
        ImGui::SameLine();

        if (ImGui::Button("Reset View")) {
            camera.x = 0.0f;
            camera.y = 0.0f;
            camera.zoom = 1.0f;
        }
        ImGui::EndChild();
    }

    ImVec2 board_p0 = ImGui::GetCursorScreenPos();
    ImVec2 board_sz = ImGui::GetContentRegionAvail();
    ImVec2 board_p1 = ImVec2(board_p0.x + board_sz.x, board_p0.y + board_sz.y);
    ImVec2 boardCenter = ImVec2(board_p0.x + board_sz.x * 0.5f, board_p0.y + board_sz.y * 0.5f);

    ImGui::InvisibleButton("board", board_sz, 
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    // Update
    handleInput(localBoard, board_p0, board_p1, boardCenter, isHovered, isActive);

    // Render
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(board_p0, board_p1, true);

    // Background
    uint32_t bgColIm = ThemeUtils::toImColor(localBoard.theme.boardBackgroundColor);
    draw_list->AddRectFilled(board_p0, board_p1, bgColIm);

    drawGrid(draw_list, localBoard, board_p0, board_p1, boardCenter);
    drawConnectors(draw_list, localBoard, boardCenter);
    drawConnectionPreview(draw_list, localBoard, boardCenter);
    drawCards(draw_list, localBoard, boardCenter);

    draw_list->PopClipRect();
    ImGui::End();

    // Render to screen
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.12f, 0.12f, 0.14f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void ClientRenderer::handleInput(BoardState& localBoard, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter, bool hovered, bool active) {
    // Board Panning (Middle Mouse always, Right Mouse if dragged)
    // dragThreshold represents number of pixels required for movement to be considered a drag when right clicking
    float dragThreshold = 3.0f; 
    bool isMiddleDragging = active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
    bool isRightDragActive = ImGui::IsMouseDragging(ImGuiMouseButton_Right, dragThreshold); 

    if (isRightDragActive) {
        isRightDragging = true;
    }

    if (isMiddleDragging || isRightDragging) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        camera.x -= delta.x / camera.zoom;
        camera.y -= delta.y / camera.zoom;
    }

    // Zoom with scroll wheel
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            camera.zoom += wheel * 0.1f;
            if (camera.zoom < 0.2f) camera.zoom = 0.2f;
            if (camera.zoom > 3.0f) camera.zoom = 3.0f;
        }
    }

    // Reset card dragging state
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggedCardID = 0;
    }

    // Update Card Positions on drag
    for (auto& [id, card] : localBoard.cards) {
        float screenX = boardCenter.x + (card.x - camera.x) * camera.zoom;
        float screenY = boardCenter.y + (card.y - camera.y) * camera.zoom;
        float screenW = card.width * camera.zoom;
        float screenH = card.height * camera.zoom;

        ImVec2 cardMin(screenX, screenY);
        ImVec2 cardMax(screenX + screenW, screenY + screenH);

        ImVec2 mousePos = ImGui::GetIO().MousePos;
        bool isMouseOverCard = (mousePos.x >= cardMin.x && mousePos.x <= cardMax.x &&
                                mousePos.y >= cardMin.y && mousePos.y <= cardMax.y);

        if (isMouseOverCard && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && draggedCardID == 0) {
            draggedCardID = card.cardID;
        }

        if (draggedCardID == card.cardID) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            card.x += delta.x / camera.zoom;
            card.y += delta.y / camera.zoom;
        }
    }

    // Right click release detection
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        // If the user released the right button without dragging, defer logic to context menu
        if (!isRightDragging && hovered) {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            
            // Convert screen coordinates to world space
            contextMenuWorldPos.x = camera.x + (mousePos.x - boardCenter.x) / camera.zoom;
            contextMenuWorldPos.y = camera.y + (mousePos.y - boardCenter.y) / camera.zoom;

            // Check if right-clicked on a specific card
            bool clickedOnCard = false;
            uint32_t hitCardID = 0;

            for (const auto& [id, card] : localBoard.cards) {
                float screenX = boardCenter.x + (card.x - camera.x) * camera.zoom;
                float screenY = boardCenter.y + (card.y - camera.y) * camera.zoom;
                float screenW = card.width * camera.zoom;
                float screenH = card.height * camera.zoom;

                if (mousePos.x >= screenX && mousePos.x <= screenX + screenW &&
                    mousePos.y >= screenY && mousePos.y <= screenY + screenH) {
                    clickedOnCard = true;
                    hitCardID = card.cardID;
                    break;
                }
            }

            // Set context based on what was hit
            if (clickedOnCard) {
                currentMenuTarget = { MenuContextType::Card, hitCardID };
            } else {
                currentMenuTarget = { MenuContextType::Background, 0 };
            }

            ImGui::OpenPopup("CanvasContextMenu");
        }

        // Reset drag tracking state when the button is released
        isRightDragging = false;
    }

    // Process context menu logic
    handleContextMenu(localBoard, boardCenter, currentMenuTarget);

    if (isConnecting) {
        // Error detection for deleted source card
        if (localBoard.cards.find(connectingSourceCardID) == localBoard.cards.end()) {
            isConnecting = false;
        }
        // Cancel if user hits escape key
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            isConnecting = false;
        }
        // Check for left click on card
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            uint32_t clickedTargetCardID = 0;
            bool clickedACard = false;

            // Hit-test all cards
            for (const auto& [id, card] : localBoard.cards) {
                float screenX = boardCenter.x + (card.x - camera.x) * camera.zoom;
                float screenY = boardCenter.y + (card.y - camera.y) * camera.zoom;
                float screenW = card.width * camera.zoom;
                float screenH = card.height * camera.zoom;

                if (mousePos.x >= screenX && mousePos.x <= screenX + screenW &&
                    mousePos.y >= screenY && mousePos.y <= screenY + screenH) {
                    clickedTargetCardID = card.cardID;
                    clickedACard = true;
                    break;
                }
            }

            // create connector if valid second card is selected & exit connecting mode
            if (clickedACard) {
                localBoard.createConnector(connectingSourceCardID, clickedTargetCardID);
                isConnecting = false;
            }
        }
    }
}

void ClientRenderer::handleContextMenu(BoardState& localBoard, ImVec2 boardCenter, const ContextMenuTarget& context) {
    if (ImGui::BeginPopup("CanvasContextMenu")) {
        
        // Background/general right click menu
        if (context.type == MenuContextType::Background) {

            if (ImGui::MenuItem("Create Card")) {
                float spawnX = contextMenuWorldPos.x;
                float spawnY = contextMenuWorldPos.y;
                localBoard.createCardAt(spawnX, spawnY, "Enter text...");
            }
        }
        
        // Card right click menu
        else if (context.type == MenuContextType::Card) {
            ImGui::TextDisabled("Card Options (ID: %u)", context.targetID);
            ImGui::Separator();

            // Connection drawing option
            if (ImGui::MenuItem("Connect to...")) {
                isConnecting = true;
                connectingSourceCardID = context.targetID;
            }

            // Delete card option (new options can be expanded under here)
            if (ImGui::MenuItem("Delete Card")) {
                localBoard.deleteCard(context.targetID);
            }
        }

        // Connector right click menu
        else if (context.type == MenuContextType::Connector) {
            ImGui::TextDisabled("Connector Options");
            ImGui::Separator();
        }

        ImGui::EndPopup();
    }
}

void ClientRenderer::drawGrid(ImDrawList* draw_list, BoardState& localBoard, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter) {
    Color gridColor = ThemeUtils::getGridLineColor(localBoard.theme.boardBackgroundColor);
    uint32_t gridColIm = ThemeUtils::toImColor(gridColor);
    float gridSpacingWorld = 50.0f;
    float zoom = camera.zoom;

    float minWorldX = camera.x - (boardCenter.x - board_p0.x) / zoom;
    float maxWorldX = camera.x + (board_p1.x - boardCenter.x) / zoom;
    float minWorldY = camera.y - (boardCenter.y - board_p0.y) / zoom;
    float maxWorldY = camera.y + (board_p1.y - boardCenter.y) / zoom;

    float startWorldX = floorf(minWorldX / gridSpacingWorld) * gridSpacingWorld;
    float startWorldY = floorf(minWorldY / gridSpacingWorld) * gridSpacingWorld;

    for (float wx = startWorldX; wx <= maxWorldX; wx += gridSpacingWorld) {
        float screenX = boardCenter.x + (wx - camera.x) * zoom;
        draw_list->AddLine(ImVec2(screenX, board_p0.y), ImVec2(screenX, board_p1.y), gridColIm, 1.0f);
    }

    for (float wy = startWorldY; wy <= maxWorldY; wy += gridSpacingWorld) {
        float screenY = boardCenter.y + (wy - camera.y) * zoom;
        draw_list->AddLine(ImVec2(board_p0.x, screenY), ImVec2(board_p1.x, screenY), gridColIm, 1.0f);
    }
}

void ClientRenderer::drawConnectors(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter) {
    for (const auto& [id, conn] : localBoard.connectors) {
        if (localBoard.cards.find(conn.sourceCardID) != localBoard.cards.end() &&
            localBoard.cards.find(conn.targetCardID) != localBoard.cards.end()) {
            
            const Card& src = localBoard.cards.at(conn.sourceCardID);
            const Card& tgt = localBoard.cards.at(conn.targetCardID);

            ImVec2 srcCenter(
                boardCenter.x + (src.x + src.width / 2.0f - camera.x) * camera.zoom,
                boardCenter.y + (src.y + src.height / 2.0f - camera.y) * camera.zoom
            );
            ImVec2 tgtCenter(
                boardCenter.x + (tgt.x + tgt.width / 2.0f - camera.x) * camera.zoom,
                boardCenter.y + (tgt.y + tgt.height / 2.0f - camera.y) * camera.zoom
            );

            uint32_t connColorIm = ThemeUtils::toImColor(conn.color);
            draw_list->AddLine(srcCenter, tgtCenter, connColorIm, conn.thickness * camera.zoom);
        }
    }
}

void ClientRenderer::drawConnectionPreview(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter) {
    if (!isConnecting) return;

    // Verify source card still exists before drawing preview
    if (localBoard.cards.find(connectingSourceCardID) != localBoard.cards.end()) {
        const Card& srcCard = localBoard.cards.at(connectingSourceCardID);
        
        ImVec2 srcCenter(
            boardCenter.x + (srcCard.x + srcCard.width / 2.0f - camera.x) * camera.zoom,
            boardCenter.y + (srcCard.y + srcCard.height / 2.0f - camera.y) * camera.zoom
        );
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        uint32_t previewColor = ThemeUtils::toImColor(localBoard.theme.connectorColor);
        draw_list->AddLine(srcCenter, mousePos, previewColor, 10.0f * camera.zoom);
    } else {
        // Fallback in case card was deleted in the split second we rendered (stupid)
        isConnecting = false;
    }
}

void ClientRenderer::drawCards(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter) {
    for (const auto& [id, card] : localBoard.cards) {
        float screenX = boardCenter.x + (card.x - camera.x) * camera.zoom;
        float screenY = boardCenter.y + (card.y - camera.y) * camera.zoom;
        float screenW = card.width * camera.zoom;
        float screenH = card.height * camera.zoom;

        ImVec2 cardMin(screenX, screenY);
        ImVec2 cardMax(screenX + screenW, screenY + screenH);

        uint32_t bgCol = ThemeUtils::toImColor(card.color);
        uint32_t borderCol = ThemeUtils::toImColor(card.getBorderColor());
        uint32_t textCol = ThemeUtils::toImColor(card.getTextColor());

        draw_list->AddRectFilled(cardMin, cardMax, bgCol, 6.0f);
        draw_list->AddRect(cardMin, cardMax, borderCol, 6.0f, 0, 2.0f);
        draw_list->AddText(ImVec2(cardMin.x + 10, cardMin.y + 10), textCol, card.text.c_str());
    }
}

// Shutdown method (run this on closing renderer/app)
void ClientRenderer::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}
#include "ClientRenderer.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include "BoardStructs.h"
#include <cmath>
#include <cstdio>

using namespace std;

static Camera boardCamera;

// Card resize tuning (world units for sizes, screen pixels for handle grab zones)
static constexpr float kMinCardSize = 20.0f;
static constexpr float kMaxCardSize = 1000.0f;
static constexpr float kResizeHandleSize = 10.0f;
static constexpr float kResizeSideHandleLength = kResizeHandleSize * 4.0f;

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

    // Apply the board theme to the ImGui elements
    applyTheme(localBoard.theme);

    // Board window set to display size
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoTitleBar |                     
        ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoResize |             
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoBringToFrontOnFocus |         
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

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

    // Allow the per-card hitboxes submitted later to overlap and take priority over this full-board hitbox
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("board", board_sz, 
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);

    // Update
    handleInput(localBoard, boardCenter, isHovered);

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

    // Selection sidebar hook (per-object editing options)
    drawSelectionSidebar(localBoard);

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

void ClientRenderer::handleInput(BoardState& localBoard, ImVec2 boardCenter, bool hovered) {
    // Camera panning + zoom
    handleCameraControls(hovered);

    // Remember if a resize was in progress so releasing outside the card keeps the selection
    bool wasResizing = (resizingCardID != 0);

    // Reset card dragging / resizing state once the left button is released
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggedCardID = 0;
        resizingCardID = 0;
        activeResizeHandle = ResizeHandle::None;
    }

    uint32_t hoveredCardID = submitCardHitboxes(localBoard, boardCenter);

    uint32_t hoveredConnectorID = 0;
    if (hoveredCardID == 0) {
        hoveredConnectorID = hitTestConnector(localBoard, ImGui::GetIO().MousePos, boardCenter);
    }

    // Empty board space is only "background" when no object sits under the cursor
    bool backgroundHovered = hovered && hoveredCardID == 0 && hoveredConnectorID == 0;

    // Update the current selection (panning + zooming never clear it)
    handleObjectSelection(hoveredCardID, hoveredConnectorID, backgroundHovered && !wasResizing);

    // Double clicking an object opens its editing sidebar for that object
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (hoveredCardID != 0) {
            selection = { MenuContextType::Card, hoveredCardID };
            selectionSidebarOpen = true;
        } else if (hoveredConnectorID != 0) {
            selection = { MenuContextType::Connector, hoveredConnectorID };
            selectionSidebarOpen = true;
        }
    }

    // Right click without dragging opens the context menu for whatever is under the cursor
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !isRightDragging && hovered) {
        // Convert screen coordinates to world space
        contextMenuWorldPos = screenToWorld(ImGui::GetIO().MousePos, boardCenter);

        // Set context based on what was hit
        if (hoveredCardID != 0) {
            currentMenuTarget = { MenuContextType::Card, hoveredCardID };
        } else if (hoveredConnectorID != 0) {
            currentMenuTarget = { MenuContextType::Connector, hoveredConnectorID };
        } else {
            currentMenuTarget = { MenuContextType::Background, 0 };
        }

        ImGui::OpenPopup("BoardContextMenu");
    }

    // Reset drag tracking state when the button is released
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        isRightDragging = false;
    }

    // Process context menu logic
    handleContextMenu(localBoard, boardCenter, currentMenuTarget);

    // Clear a selection that points at an object which no longer exists
    syncSelection(localBoard);

    // Connection (link drawing) mode
    handleConnectingMode(localBoard, hoveredCardID);
}

void ClientRenderer::handleCameraControls(bool hovered) {
    // Board Panning (Middle Mouse always, Right Mouse if dragged)
    // dragThreshold represents number of pixels required for movement to be considered a drag when right clicking
    float dragThreshold = 3.0f;
    bool isMiddleDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
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
}

uint32_t ClientRenderer::submitCardHitboxes(BoardState& localBoard, ImVec2 boardCenter) {
    uint32_t hoveredCardID = 0;

    // A click on a resize handle of the selected card starts a resize, even on the part outside the card
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selection.type == MenuContextType::Card) {
        auto it = localBoard.cards.find(selection.targetID);
        if (it != localBoard.cards.end()) {
            ResizeHandle handle = hitTestResizeHandle(it->second, ImGui::GetIO().MousePos, boardCenter);
            if (handle != ResizeHandle::None) {
                resizingCardID = selection.targetID;
                activeResizeHandle = handle;
            }
        }
    }

    for (auto& [id, card] : localBoard.cards) {
        ImVec2 screenPos = worldToScreen(card.x, card.y, boardCenter);
        ImVec2 screenSize(card.width * camera.zoom, card.height * camera.zoom);
        if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
            continue;
        }

        ImGui::PushID(static_cast<int>(card.cardID));
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::InvisibleButton("##cardHitbox", screenSize, ImGuiButtonFlags_MouseButtonLeft);

        if (ImGui::IsItemHovered()) {
            hoveredCardID = card.cardID;
        }

        // A left click on a card grabs it for dragging (only one card at a time)
        if (ImGui::IsItemActive()) {
            if (draggedCardID == 0 && resizingCardID == 0) {
                draggedCardID = card.cardID;
            }
            selection = { MenuContextType::Card, card.cardID };
        }
        ImGui::PopID();
    }

    // Resize the grabbed card, anchoring the opposite edge
    if (resizingCardID != 0) {
        auto it = localBoard.cards.find(resizingCardID);
        if (it != localBoard.cards.end()) {
            applyCardResize(it->second);
        }
    }

    // Move the grabbed card by the mouse delta
    if (draggedCardID != 0) {
        auto it = localBoard.cards.find(draggedCardID);
        if (it != localBoard.cards.end()) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            it->second.x += delta.x / camera.zoom;
            it->second.y += delta.y / camera.zoom;
        }
    }

    return hoveredCardID;
}

// Finds which resize handle (if any) sits under the mouse for a given card (screen scale adjusted)
ResizeHandle ClientRenderer::hitTestResizeHandle(const Card& card, ImVec2 mousePos, ImVec2 boardCenter) const {
    ImVec2 p0 = worldToScreen(card.x, card.y, boardCenter);
    ImVec2 p1 = worldToScreen(card.x + card.width, card.y + card.height, boardCenter);

    float half = kResizeHandleSize * 0.5f;
    float left = p0.x, top = p0.y;
    float right = p1.x, bottom = p1.y;
    float midX = (left + right) * 0.5f;
    float midY = (top + bottom) * 0.5f;

    // Corners take priority over edges when zones overlap
    auto nearCorner = [&](float hx, float hy) {
        return fabsf(mousePos.x - hx) <= half && fabsf(mousePos.y - hy) <= half;
    };

    auto inRect = [&](float rMinX, float rMinY, float rMaxX, float rMaxY) {
        return mousePos.x >= rMinX && mousePos.x <= rMaxX && mousePos.y >= rMinY && mousePos.y <= rMaxY;
    };
    float halfLen = kResizeSideHandleLength * 0.5f;

    if (nearCorner(left, top))     return ResizeHandle::NW;
    if (nearCorner(right, top))    return ResizeHandle::NE;
    if (nearCorner(left, bottom))  return ResizeHandle::SW;
    if (nearCorner(right, bottom)) return ResizeHandle::SE;

    // North/South edges
    if (inRect(midX - halfLen, top - half, midX + halfLen, top + half))    return ResizeHandle::N;
    if (inRect(midX - halfLen, bottom - half, midX + halfLen, bottom + half)) return ResizeHandle::S;
    // West/East edges
    if (inRect(left - half, midY - halfLen, left + half, midY + halfLen))    return ResizeHandle::W;
    if (inRect(right - half, midY - halfLen, right + half, midY + halfLen))  return ResizeHandle::E;

    return ResizeHandle::None;
}

// Resizes a card by the mouse delta, anchoring the edge opposite the grabbed handle.
void ClientRenderer::applyCardResize(Card& card) {
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    float dx = delta.x / camera.zoom;
    float dy = delta.y / camera.zoom;

    float left = card.x;
    float top = card.y;
    float right = card.x + card.width;
    float bottom = card.y + card.height;

    switch (activeResizeHandle) {
        case ResizeHandle::NW: left += dx;  top += dy;  break;
        case ResizeHandle::N:  top += dy;                break;
        case ResizeHandle::NE: right += dx; top += dy;   break;
        case ResizeHandle::E:  right += dx;              break;
        case ResizeHandle::SE: right += dx; bottom += dy; break;
        case ResizeHandle::S:  bottom += dy;             break;
        case ResizeHandle::SW: left += dx;  bottom += dy; break;
        case ResizeHandle::W:  left += dx;               break;
        default: return;
    }

    // Clamp the moving edge to keep the card within the valid size range
    if (right - left < kMinCardSize) {
        if (activeResizeHandle == ResizeHandle::W || activeResizeHandle == ResizeHandle::NW || activeResizeHandle == ResizeHandle::SW)
            left = right - kMinCardSize;
        else
            right = left + kMinCardSize;
    } else if (right - left > kMaxCardSize) {
        if (activeResizeHandle == ResizeHandle::W || activeResizeHandle == ResizeHandle::NW || activeResizeHandle == ResizeHandle::SW)
            left = right - kMaxCardSize;
        else
            right = left + kMaxCardSize;
    }

    if (bottom - top < kMinCardSize) {
        if (activeResizeHandle == ResizeHandle::N || activeResizeHandle == ResizeHandle::NW || activeResizeHandle == ResizeHandle::NE)
            top = bottom - kMinCardSize;
        else
            bottom = top + kMinCardSize;
    } else if (bottom - top > kMaxCardSize) {
        if (activeResizeHandle == ResizeHandle::N || activeResizeHandle == ResizeHandle::NW || activeResizeHandle == ResizeHandle::NE)
            top = bottom - kMaxCardSize;
        else
            bottom = top + kMaxCardSize;
    }

    card.x = left;
    card.y = top;
    card.width = right - left;
    card.height = bottom - top;
}

// Draws the resize handles and sets the matching cursor for a card
void ClientRenderer::drawResizeHandles(ImDrawList* draw_list, const Card& card, const BoardTheme& theme, ImVec2 boardCenter) {
    ImVec2 p0 = worldToScreen(card.x, card.y, boardCenter);
    ImVec2 p1 = worldToScreen(card.x + card.width, card.y + card.height, boardCenter);

    float half = kResizeHandleSize * 0.5f;
    float left = p0.x, top = p0.y;
    float right = p1.x, bottom = p1.y;
    float midX = (left + right) * 0.5f;
    float midY = (top + bottom) * 0.5f;

    // All handles use the theme's importantElements color
    uint32_t handleCol = ThemeUtils::toImColor(theme.importantElements);

    // Corners are square
    struct HandlePoint { float x, y; };
    HandlePoint corners[4] = {
        {left, top}, {right, top}, {left, bottom}, {right, bottom}
    };
    for (const auto& h : corners) {
        draw_list->AddRectFilled(
            ImVec2(h.x - half, h.y - half),
            ImVec2(h.x + half, h.y + half),
            handleCol, 2.0f);
    }

    // Side handles are rectangular and 4x longer than a corner
    float halfLen = kResizeSideHandleLength * 0.5f;

    // North/South resize handles
    draw_list->AddRectFilled(ImVec2(midX - halfLen, top - half), ImVec2(midX + halfLen, top + half), handleCol, 2.0f);
    draw_list->AddRectFilled(ImVec2(midX - halfLen, bottom - half), ImVec2(midX + halfLen, bottom + half), handleCol, 2.0f);
    // West/East resize handles
    draw_list->AddRectFilled(ImVec2(left - half, midY - halfLen), ImVec2(left + half, midY + halfLen), handleCol, 2.0f);
    draw_list->AddRectFilled(ImVec2(right - half, midY - halfLen), ImVec2(right + half, midY + halfLen), handleCol, 2.0f);

    // Cursor parity with resize handles
    ResizeHandle hovered = hitTestResizeHandle(card, ImGui::GetIO().MousePos, boardCenter);
    switch (hovered) {
        case ResizeHandle::N:
        case ResizeHandle::S:    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
        case ResizeHandle::E:
        case ResizeHandle::W:    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
        case ResizeHandle::NE:
        case ResizeHandle::SW:   ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
        case ResizeHandle::NW:
        case ResizeHandle::SE:   ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
        default: break;
    }
}

uint32_t ClientRenderer::hitTestConnector(const BoardState& localBoard, ImVec2 screenPos, ImVec2 boardCenter) const {
    // Keep a small screen-space padding so thin connectors stay clickable, then
    // grow the hit area with the connector's on-screen thickness.
    const float hitPadding = 6.0f;
    uint32_t bestConnectorID = 0;
    float bestDistance = 1e30f;

    for (const auto& [id, conn] : localBoard.connectors) {
        auto srcIt = localBoard.cards.find(conn.sourceCardID);
        auto tgtIt = localBoard.cards.find(conn.targetCardID);
        if (srcIt == localBoard.cards.end() || tgtIt == localBoard.cards.end()) {
            continue;
        }

        const Card& src = srcIt->second;
        const Card& tgt = tgtIt->second;

        ImVec2 srcCenter = worldToScreen(src.x + src.width / 2.0f, src.y + src.height / 2.0f, boardCenter);
        ImVec2 tgtCenter = worldToScreen(tgt.x + tgt.width / 2.0f, tgt.y + tgt.height / 2.0f, boardCenter);

        float halfWidth = conn.thickness * camera.zoom * 0.5f + hitPadding;
        float distance = distanceToSegment(screenPos, srcCenter, tgtCenter);
        if (distance <= halfWidth && distance < bestDistance) {
            bestDistance = distance;
            bestConnectorID = conn.connectorID;
        }
    }

    return bestConnectorID;
}

float ClientRenderer::distanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b) const {
    ImVec2 segment(b.x - a.x, b.y - a.y);
    ImVec2 toPoint(point.x - a.x, point.y - a.y);

    // Project the point onto the segment, clamped to the [a, b] range
    float lengthSq = segment.x * segment.x + segment.y * segment.y;
    float t = (lengthSq > 0.0f) ? ((toPoint.x * segment.x + toPoint.y * segment.y) / lengthSq) : 0.0f;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);

    ImVec2 projection(a.x + segment.x * t, a.y + segment.y * t);
    float dx = point.x - projection.x;
    float dy = point.y - projection.y;
    return sqrtf(dx * dx + dy * dy);
}

void ClientRenderer::handleObjectSelection(uint32_t hoveredCardID, uint32_t hoveredConnectorID, bool backgroundHovered) {
    // Selection is committed on release so right/middle panning drags never clear it
    bool leftClick = ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !isConnecting;
    bool rightClick = ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !isRightDragging;
    if (!leftClick && !rightClick) {
        return;
    }

    if (hoveredCardID != 0) {
        selection = { MenuContextType::Card, hoveredCardID };
    } else if (hoveredConnectorID != 0) {
        selection = { MenuContextType::Connector, hoveredConnectorID };
    } else if (backgroundHovered) {
        selection = { MenuContextType::Background, 0 };
    }
}

void ClientRenderer::handleConnectingMode(BoardState& localBoard, uint32_t hoveredCardID) {
    if (!isConnecting) {
        return;
    }

    // Error detection for deleted source card
    if (localBoard.cards.find(connectingSourceCardID) == localBoard.cards.end()) {
        isConnecting = false;
    }
    // Cancel if user hits escape key
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        isConnecting = false;
    }
    // Left click on a card creates the connector and exits connecting mode
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredCardID != 0) {
        // Leave on success so user can select another target after invalid
        if (localBoard.createConnector(connectingSourceCardID, hoveredCardID) != nullptr) {
            isConnecting = false;
        }
    }
}

bool ClientRenderer::isSelected(MenuContextType type, uint32_t id) const {
    return selection.type == type && selection.targetID == id;
}

void ClientRenderer::syncSelection(const BoardState& localBoard) {
    if (selection.type == MenuContextType::Card && localBoard.cards.find(selection.targetID) == localBoard.cards.end()) {
        selection = { MenuContextType::Background, 0 };
    } else if (selection.type == MenuContextType::Connector && localBoard.connectors.find(selection.targetID) == localBoard.connectors.end()) {
        selection = { MenuContextType::Background, 0 };
    }
}

void ClientRenderer::handleContextMenu(BoardState& localBoard, ImVec2 boardCenter, const ContextMenuTarget& context) {
    if (ImGui::BeginPopup("BoardContextMenu")) {
        
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

            // Delete connector option (new options can be expanded under here)
            if (ImGui::MenuItem("Delete Connector")) {
                localBoard.deleteConnector(context.targetID);
            }
        }

        ImGui::EndPopup();
    }
}

ImVec2 ClientRenderer::worldToScreen(float worldX, float worldY, ImVec2 boardCenter) const {
    return ImVec2(
        boardCenter.x + (worldX - camera.x) * camera.zoom,
        boardCenter.y + (worldY - camera.y) * camera.zoom
    );
}

ImVec2 ClientRenderer::screenToWorld(ImVec2 screenPos, ImVec2 boardCenter) const {
    return ImVec2(
        camera.x + (screenPos.x - boardCenter.x) / camera.zoom,
        camera.y + (screenPos.y - boardCenter.y) / camera.zoom
    );
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
        if (localBoard.cards.find(conn.sourceCardID) == localBoard.cards.end() ||
            localBoard.cards.find(conn.targetCardID) == localBoard.cards.end()) {
            continue;
        }

        const Card& src = localBoard.cards.at(conn.sourceCardID);
        const Card& tgt = localBoard.cards.at(conn.targetCardID);

        ImVec2 srcCenter = worldToScreen(src.x + src.width / 2.0f, src.y + src.height / 2.0f, boardCenter);
        ImVec2 tgtCenter = worldToScreen(tgt.x + tgt.width / 2.0f, tgt.y + tgt.height / 2.0f, boardCenter);

        // Selected connectors get accent colored border (from theme)
        if (isSelected(MenuContextType::Connector, conn.connectorID)) {
            uint32_t accentColorIm = ThemeUtils::toImColor(localBoard.theme.accentColor);
            draw_list->AddLine(srcCenter, tgtCenter, accentColorIm, (conn.thickness + 4.0f) * camera.zoom);
        }

        uint32_t connColorIm = ThemeUtils::toImColor(conn.color);
        draw_list->AddLine(srcCenter, tgtCenter, connColorIm, conn.thickness * camera.zoom);
    }
}

void ClientRenderer::drawConnectionPreview(ImDrawList* draw_list, BoardState& localBoard, ImVec2 boardCenter) {
    if (!isConnecting) return;

    // Verify source card still exists before drawing preview
    if (localBoard.cards.find(connectingSourceCardID) != localBoard.cards.end()) {
        const Card& srcCard = localBoard.cards.at(connectingSourceCardID);
        
        ImVec2 srcCenter = worldToScreen(srcCard.x + srcCard.width / 2.0f, srcCard.y + srcCard.height / 2.0f, boardCenter);
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
        ImVec2 cardMin = worldToScreen(card.x, card.y, boardCenter);
        ImVec2 cardSize(card.width * camera.zoom, card.height * camera.zoom);
        ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);

        uint32_t bgCol = ThemeUtils::toImColor(card.color);
        uint32_t textCol = ThemeUtils::toImColor(card.getTextColor());

        // Selected cards are outlined with the theme accent color
        bool selected = isSelected(MenuContextType::Card, card.cardID);
        uint32_t borderCol = selected
            ? ThemeUtils::toImColor(localBoard.theme.accentColor)
            : ThemeUtils::toImColor(card.getBorderColor());
        float borderThickness = selected ? 4.0f : 2.0f;

        draw_list->AddRectFilled(cardMin, cardMax, bgCol, 6.0f);
        draw_list->AddRect(cardMin, cardMax, borderCol, 6.0f, 0, borderThickness);

        // Scale font size by camera zoom
        float fontSize = ImGui::GetFontSize() * camera.zoom;
        float textPadding = 10.0f * camera.zoom;

        // Wrap the text to the card's inner width so it never spills horizontally
        float wrapWidth = cardMax.x - (cardMin.x + textPadding) - textPadding;

        // Backup clip text in case wrap fails
        draw_list->PushClipRect(cardMin, cardMax, true);
        draw_list->AddText(ImGui::GetFont(), fontSize, ImVec2(cardMin.x + textPadding, cardMin.y + textPadding), textCol, card.text.c_str(), NULL, wrapWidth);
        draw_list->PopClipRect();
    }

    // Draw resize handles last so they sit on top of every card
    if (selection.type == MenuContextType::Card) {
        auto it = localBoard.cards.find(selection.targetID);
        if (it != localBoard.cards.end()) {
            drawResizeHandles(draw_list, it->second, localBoard.theme, boardCenter);
        }
    }
}

// Opens selection sidebar for object selected on right side of the screen
void ClientRenderer::drawSelectionSidebar(BoardState& localBoard) {
    // Return if an object was not selected 
    if (!selectionSidebarOpen || selection.type == MenuContextType::Background) {
        return;
    }

    // Set panel to right side of screen
    const float sidebarWidth = 320.0f;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - sidebarWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, displaySize.y), ImGuiCond_Always);

    if (ImGui::Begin(getSelectionTitle(selection.type), &selectionSidebarOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushID(static_cast<int>(selection.targetID));

        // Per-selected object method so we can scale if we need/want to
        switch (selection.type) {
            case MenuContextType::Card:
                drawCardOptions(localBoard);
                break;
            case MenuContextType::Connector:
                drawConnectorOptions(localBoard);
                break;
            default:
                break;
        }

        ImGui::PopID();
    }
    ImGui::End();
}

// Title shown on the sidebar window for a given object type.
const char* ClientRenderer::getSelectionTitle(MenuContextType type) const {
    switch (type) {
        case MenuContextType::Card:      return "Card";
        case MenuContextType::Connector: return "Connector";
        default:                         return "Selection";
    }
}

// Card editor options
void ClientRenderer::drawCardOptions(BoardState& localBoard) {
    auto cardIt = localBoard.cards.find(selection.targetID);
    if (cardIt == localBoard.cards.end()) {
        return;
    }
    Card& card = cardIt->second;

    // Text content editor
    ImGui::TextUnformatted("Text:");
    char textBuffer[4096];
    snprintf(textBuffer, sizeof(textBuffer), "%s", card.text.c_str());
    ImVec2 textSize(ImGui::GetContentRegionAvail().x, 100.0f);
    if (ImGui::InputTextMultiline("##text", textBuffer, sizeof(textBuffer), textSize, ImGuiInputTextFlags_WordWrap)) {
        card.text = textBuffer;
    }

    // Card size editors
    ImGui::TextUnformatted("Size X:");
    ImGui::InputFloat("##sizeX", &card.width, 1.0f, 1.0f, "%.2f");
    if (!ImGui::IsItemActive()) {
        if (card.width < kMinCardSize) card.width = kMinCardSize;
        if (card.width > kMaxCardSize) card.width = kMaxCardSize;
    }

    ImGui::TextUnformatted("Size Y:");
    ImGui::InputFloat("##sizeY", &card.height, 1.0f, 1.0f, "%.2f");
    if (!ImGui::IsItemActive()) {
        if (card.height < kMinCardSize) card.height = kMinCardSize;
        if (card.height > kMaxCardSize) card.height = kMaxCardSize;
    }

    // Need color editor
}

// Connector editor options
void ClientRenderer::drawConnectorOptions(BoardState& localBoard) {
    auto connIt = localBoard.connectors.find(selection.targetID);
    if (connIt == localBoard.connectors.end()) {
        return;
    }
    Connector& connector = connIt->second;

    // Thickness editor with a reset-to-default button
    ImGui::TextUnformatted("Thickness:");
    float resetButtonWidth = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputFloat("##thickness", &connector.thickness, 1.0f, 1.0f, "%.2f");

    // Keep thickness positive once editing finishes so the connector stays valid
    if (!ImGui::IsItemActive() && connector.thickness < 0.1f) {
        connector.thickness = 0.1f;
    }

    ImGui::SameLine();
    if (ImGui::Button("R")) {
        connector.thickness = localBoard.user_settings.defaultConnectorThickness;
    }
}

// Force theme colors on ImGUI elements
void ClientRenderer::applyTheme(const BoardTheme& theme) {
    ImGuiStyle& style = ImGui::GetStyle();

    auto toVec4 = [](const Color& c) {
        return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
    };

    const ImVec4 base = toVec4(theme.cardColor);
    const ImVec4 hovered = toVec4(ThemeUtils::shift(theme.cardColor, 20, 20, 20));
    const ImVec4 active = toVec4(ThemeUtils::shift(theme.cardColor, -20, -20, -20));
    const ImVec4 text = toVec4(ThemeUtils::getTextColor(theme.cardColor));

    // Labels & text elements
    style.Colors[ImGuiCol_Text] = text;
    // Input fields
    style.Colors[ImGuiCol_FrameBg] = base;
    style.Colors[ImGuiCol_FrameBgHovered] = hovered;
    style.Colors[ImGuiCol_FrameBgActive] = active;
    // Buttons
    style.Colors[ImGuiCol_Button] = base;
    style.Colors[ImGuiCol_ButtonHovered] = hovered;
    style.Colors[ImGuiCol_ButtonActive] = active;
    // Sliders & checkmarks
    style.Colors[ImGuiCol_SliderGrab] = hovered;
    style.Colors[ImGuiCol_SliderGrabActive] = active;
    style.Colors[ImGuiCol_CheckMark] = text;
    // Window title bars
    style.Colors[ImGuiCol_TitleBg] = base;
    style.Colors[ImGuiCol_TitleBgActive] = hovered;
    style.Colors[ImGuiCol_TitleBgCollapsed] = base;
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
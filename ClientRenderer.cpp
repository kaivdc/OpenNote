#include "ClientRenderer.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include "BoardStructs.h"
#include <algorithm>
#include <cmath>
#include "BoardCommands.h"
#include "BoardInspector.h"
#include "LocalBoardCommandHandler.h"

using namespace std;

// Card resize tuning (world units for sizes, screen pixels for handle grab zones)
static constexpr float kMinCardSize = 20.0f;
static constexpr float kMaxCardSize = 1000.0f;
static constexpr float kResizeHandleSize = 10.0f;
static constexpr float kResizeSideHandleLength = kResizeHandleSize * 4.0f;
static constexpr float kSelectionDragThreshold = 4.0f;

// Converts between the board's byte Color and ImGui's 0..1 float color
static ImVec4 toImVec4(const Color& c) {
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

static bool rectanglesOverlap(ImVec2 aMin, ImVec2 aMax, ImVec2 bMin, ImVec2 bMax) {
    return aMin.x <= bMax.x && aMax.x >= bMin.x &&
           aMin.y <= bMax.y && aMax.y >= bMin.y;
}

static bool segmentIntersectsRectangle(ImVec2 a, ImVec2 b, ImVec2 rectMin, ImVec2 rectMax) {
    float tMin = 0.0f;
    float tMax = 1.0f;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;

    auto clip = [&](float p, float q) {
        if (fabsf(p) < 1e-6f) {
            return q >= 0.0f;
        }

        float t = q / p;
        if (p < 0.0f) {
            if (t > tMax) return false;
            if (t > tMin) tMin = t;
        } else {
            if (t < tMin) return false;
            if (t < tMax) tMax = t;
        }
        return true;
    };

    return clip(-dx, a.x - rectMin.x) &&
           clip(dx, rectMax.x - a.x) &&
           clip(-dy, a.y - rectMin.y) &&
           clip(dy, rectMax.y - a.y);
}

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
    glfwSetWindowUserPointer(window, this);
    glfwSetDropCallback(window, &ClientRenderer::onFilesDropped);

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

    imageCache.initializeUploadIcon("resources/icons/upload_icon.png");

    return window;
}

void ClientRenderer::updateAndRender(const BoardState& board, BoardCommandHandler& commandHandler) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Apply the board theme to the ImGui elements
    inspector.applyTheme(inspector.displayTheme(board));

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
                    board.boardID, board.cards.size(), board.connectors.size(), camera.zoom);
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

    // Theme color picker. Drawn before input so a request made this frame opens
    // on the next frame (avoids the click that requested it instantly closing it).
    inspector.drawColorPicker(board, commandHandler);

    // Update
    handleInput(board, commandHandler, boardCenter, isHovered);
    processDroppedFiles(board, commandHandler);
    imageCache.loadForBoard(board);
    if (dropPreviewCardID != 0) {
        auto previewCard = board.cards.find(dropPreviewCardID);
        const ClientImageCache::Image* image = previewCard == board.cards.end()
            ? nullptr
            : imageCache.find(previewCard->second.mediaURL);
        if (!image || (!image->loading && (image->texture != 0 || image->failed))) {
            dropPreviewCardID = 0;
        }
    }

    // Render
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(board_p0, board_p1, true);

    // Background
    uint32_t bgColIm = ThemeUtils::toImColor(inspector.displayTheme(board).boardBackgroundColor);
    draw_list->AddRectFilled(board_p0, board_p1, bgColIm);

    drawGrid(draw_list, board, board_p0, board_p1, boardCenter);
    drawConnectors(draw_list, board, boardCenter);
    drawConnectionPreview(draw_list, board, boardCenter);
    drawCards(draw_list, board, boardCenter);

    if (isSelectionSelecting && selectionHasMoved) {
        const ImVec2 rectMin(
            (selectionStart.x < selectionEnd.x) ? selectionStart.x : selectionEnd.x,
            (selectionStart.y < selectionEnd.y) ? selectionStart.y : selectionEnd.y);
        const ImVec2 rectMax(
            (selectionStart.x > selectionEnd.x) ? selectionStart.x : selectionEnd.x,
            (selectionStart.y > selectionEnd.y) ? selectionStart.y : selectionEnd.y);
        ImVec4 fillColor = toImVec4(inspector.displayTheme(board).accentColor);
        fillColor.w = 0.16f;
        draw_list->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(fillColor));
        draw_list->AddRect(rectMin, rectMax, ThemeUtils::toImColor(inspector.displayTheme(board).accentColor), 0.0f, 0, 1.5f);
    }

    draw_list->PopClipRect();
    ImGui::End();

    // Selection sidebar hook (per-object editing options)
    inspector.drawSidebar(board, imageCache, commandHandler, selection, selectionSidebarOpen);

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

void ClientRenderer::onFilesDropped(GLFWwindow* droppedWindow, int pathCount, const char* paths[]) {
    auto* renderer = static_cast<ClientRenderer*>(glfwGetWindowUserPointer(droppedWindow));
    if (!renderer) {
        return;
    }
    for (int i = 0; i < pathCount; ++i) {
        if (paths[i]) {
            renderer->droppedPaths.emplace_back(paths[i]);
        }
    }
}

void ClientRenderer::processDroppedFiles(const BoardState& board, BoardCommandHandler& commandHandler) {
    if (droppedPaths.empty()) return;

    std::vector<std::string> paths;
    paths.swap(droppedPaths);
    dropPreviewCardID = 0;

    auto cardIt = board.cards.find(hoveredCardID);
    if (hoveredCardID == 0 || cardIt == board.cards.end()) {
        cerr << "[WARNING] Drop an image onto a card to attach it.\n";
        return;
    }

    for (const std::string& droppedPath : paths) {
        std::string error;
        const std::string imageSource = ClientImageCache::sourceFromDrop(droppedPath, error);
        if (imageSource.empty()) {
            cerr << "[WARNING] Could not attach '" << droppedPath << "': " << error << "\n";
            continue;
        }

        commandHandler.handle(SetCardImageCommand{cardIt->second.cardID, imageSource});
        dropPreviewCardID = cardIt->second.cardID;
        inspector.resetImageSourceEditor();
        return;
    }
}

void ClientRenderer::drawImageTexture(ImDrawList* drawList, GLuint texture, ImVec2 min, ImVec2 max) const {
    if (texture != 0) {
        drawList->AddImage(static_cast<ImTextureID>(texture), min, max);
    }
}

void ClientRenderer::handleInput(const BoardState& board, BoardCommandHandler& commandHandler, ImVec2 boardCenter, bool hovered) {
    // Camera panning + zoom
    handleCameraControls(hovered);

    // Remember if a resize was in progress so releasing outside the card keeps the selection
    bool wasResizing = (resizingCardID != 0);

    // Commit a geometry change once on release. The renderer keeps the preview
    // local during the gesture, so a future command sink can send one update.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && geometryPreviewCardID != 0) {
        if (resizingCardID != 0) {
            geometryPreview = applyCardResize(geometryPreview, ImGui::GetIO().MouseDelta);
        } else if (draggedCardID != 0) {
            geometryPreview.x += ImGui::GetIO().MouseDelta.x / camera.zoom;
            geometryPreview.y += ImGui::GetIO().MouseDelta.y / camera.zoom;
        }

        if (board.cards.find(geometryPreviewCardID) != board.cards.end()) {
            commandHandler.handle(SetCardGeometryCommand{
                geometryPreviewCardID,
                geometryPreview.x,
                geometryPreview.y,
                geometryPreview.width,
                geometryPreview.height});
        }
        draggedCardID = 0;
        resizingCardID = 0;
        geometryPreviewCardID = 0;
        activeResizeHandle = ResizeHandle::None;
    }

    uint32_t hoveredCardID = submitCardHitboxes(board, commandHandler, boardCenter);
    this->hoveredCardID = hoveredCardID;

    uint32_t hoveredConnectorID = 0;
    if (hoveredCardID == 0) {
        hoveredConnectorID = hitTestConnector(board, ImGui::GetIO().MousePos, boardCenter);
    }

    // Empty board space is only "background" when no object sits under the cursor
    bool backgroundHovered = hovered && hoveredCardID == 0 && hoveredConnectorID == 0;

    // Update the current selection (panning + zooming never clear it)
    handleObjectSelection(hoveredCardID, hoveredConnectorID, backgroundHovered && !wasResizing);
    updateSelectionSelection(board, boardCenter, backgroundHovered && !wasResizing);

    // Double clicking an object opens its editing sidebar for that object
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (hoveredCardID != 0) {
            setSelection({ MenuContextType::Card, hoveredCardID });
            selectionSidebarOpen = true;
        } else if (hoveredConnectorID != 0) {
            setSelection({ MenuContextType::Connector, hoveredConnectorID });
            selectionSidebarOpen = true;
        }
    }

    // Right click without dragging opens the context menu for whatever is under the cursor
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !isRightDragging && hovered) {
        // Convert screen coordinates to world space
        contextMenuWorldPos = screenToWorld(ImGui::GetIO().MousePos, boardCenter);

        // Set context based on what was hit
        if (hoveredCardID != 0) {
            currentMenuTarget = selectedObjects.size() > 1 && isSelected(MenuContextType::Card, hoveredCardID)
                ? ContextMenuTarget{ MenuContextType::MultipleSelection, 0 }
                : ContextMenuTarget{ MenuContextType::Card, hoveredCardID };
        } else if (hoveredConnectorID != 0) {
            currentMenuTarget = selectedObjects.size() > 1 && isSelected(MenuContextType::Connector, hoveredConnectorID)
                ? ContextMenuTarget{ MenuContextType::MultipleSelection, 0 }
                : ContextMenuTarget{ MenuContextType::Connector, hoveredConnectorID };
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
    handleContextMenu(commandHandler, currentMenuTarget);

    // Keyboard shortcut handler
    handleKeyboardShortcuts(commandHandler);

    // Clear a selection that points at an object which no longer exists
    syncSelection(board);

    // Connection (link drawing) mode
    handleConnectingMode(board, commandHandler, hoveredCardID);
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

uint32_t ClientRenderer::submitCardHitboxes(const BoardState& board, BoardCommandHandler& commandHandler, ImVec2 boardCenter) {
    uint32_t hoveredCardID = 0;

    // A click on a resize handle of the selected card starts a resize, even on the part outside the card
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selection.type == MenuContextType::Card) {
        auto it = board.cards.find(selection.targetID);
        if (it != board.cards.end()) {
            CardGeometry geometry = geometryForCard(it->second);
            ResizeHandle handle = hitTestResizeHandle(geometry, ImGui::GetIO().MousePos, boardCenter);
            if (handle != ResizeHandle::None) {
                resizingCardID = selection.targetID;
                activeResizeHandle = handle;
                geometryPreviewCardID = resizingCardID;
                geometryPreview = geometry;
                commandHandler.handle(BringCardToFrontCommand{resizingCardID});
            }
        }
    }

    for (uint32_t cardID : board.getCardIDsInZOrder()) {
        auto cardIt = board.cards.find(cardID);
        if (cardIt == board.cards.end()) {
            continue;
        }
        const Card& card = cardIt->second;
        const CardGeometry geometry = geometryForCard(card);
        ImVec2 screenPos = worldToScreen(geometry.x, geometry.y, boardCenter);
        ImVec2 screenSize(geometry.width * camera.zoom, geometry.height * camera.zoom);
        if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
            continue;
        }

        ImGui::PushID(static_cast<int>(card.cardID));
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##cardHitbox", screenSize, ImGuiButtonFlags_MouseButtonLeft);

        if (ImGui::IsItemHovered()) {
            hoveredCardID = card.cardID;
        }

        // A left click on a card grabs it for dragging (only one card at a time)
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (draggedCardID == 0 && resizingCardID == 0) {
                draggedCardID = card.cardID;
                geometryPreviewCardID = draggedCardID;
                geometryPreview = geometry;
                commandHandler.handle(BringCardToFrontCommand{draggedCardID});
            }
            setSelection({ MenuContextType::Card, card.cardID });
        }
        ImGui::PopID();
    }

    // Resize the grabbed card, anchoring the opposite edge
    if (geometryPreviewCardID != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (resizingCardID != 0) {
            geometryPreview = applyCardResize(geometryPreview, ImGui::GetIO().MouseDelta);
        } else if (draggedCardID != 0) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            geometryPreview.x += delta.x / camera.zoom;
            geometryPreview.y += delta.y / camera.zoom;
        }
    }

    return hoveredCardID;
}

ClientRenderer::CardGeometry ClientRenderer::geometryForCard(const Card& card) const {
    if (geometryPreviewCardID == card.cardID) {
        return geometryPreview;
    }
    return {card.x, card.y, card.width, card.height};
}

// Finds which resize handle (if any) sits under the mouse for a given card (screen scale adjusted)
ResizeHandle ClientRenderer::hitTestResizeHandle(const CardGeometry& geometry, ImVec2 mousePos, ImVec2 boardCenter) const {
    ImVec2 p0 = worldToScreen(geometry.x, geometry.y, boardCenter);
    ImVec2 p1 = worldToScreen(geometry.x + geometry.width, geometry.y + geometry.height, boardCenter);

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

    if (nearCorner(left, top)) return ResizeHandle::NW;
    if (nearCorner(right, top)) return ResizeHandle::NE;
    if (nearCorner(left, bottom)) return ResizeHandle::SW;
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
ClientRenderer::CardGeometry ClientRenderer::applyCardResize(const CardGeometry& geometry, ImVec2 mouseDelta) const {
    float dx = mouseDelta.x / camera.zoom;
    float dy = mouseDelta.y / camera.zoom;

    float left = geometry.x;
    float top = geometry.y;
    float right = geometry.x + geometry.width;
    float bottom = geometry.y + geometry.height;

    switch (activeResizeHandle) {
        case ResizeHandle::NW:
            left += dx;
            top += dy;
            break;
        case ResizeHandle::N:
            top += dy;
            break;
        case ResizeHandle::NE:
            right += dx;
            top += dy;
            break;
        case ResizeHandle::E:
            right += dx;
            break;
        case ResizeHandle::SE:
            right += dx;
            bottom += dy;
            break;
        case ResizeHandle::S:
            bottom += dy;
            break;
        case ResizeHandle::SW:
            left += dx;
            bottom += dy;
            break;
        case ResizeHandle::W:
            left += dx;
            break;
        default:
            return geometry;
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

    return {left, top, right - left, bottom - top};
}

// Draws the resize handles and sets the matching cursor for a card
void ClientRenderer::drawResizeHandles(ImDrawList* draw_list, const CardGeometry& geometry, const BoardTheme& theme, ImVec2 boardCenter) {
    ImVec2 p0 = worldToScreen(geometry.x, geometry.y, boardCenter);
    ImVec2 p1 = worldToScreen(geometry.x + geometry.width, geometry.y + geometry.height, boardCenter);

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
    ResizeHandle hovered = hitTestResizeHandle(geometry, ImGui::GetIO().MousePos, boardCenter);
    switch (hovered) {
        case ResizeHandle::N:
        case ResizeHandle::S:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            break;
        case ResizeHandle::E:
        case ResizeHandle::W:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            break;
        case ResizeHandle::NE:
        case ResizeHandle::SW:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
            break;
        case ResizeHandle::NW:
        case ResizeHandle::SE:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            break;
        default:
            break;
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
        const CardGeometry srcGeometry = geometryForCard(src);
        const CardGeometry tgtGeometry = geometryForCard(tgt);

        ImVec2 srcCenter = worldToScreen(srcGeometry.x + srcGeometry.width / 2.0f, srcGeometry.y + srcGeometry.height / 2.0f, boardCenter);
        ImVec2 tgtCenter = worldToScreen(tgtGeometry.x + tgtGeometry.width / 2.0f, tgtGeometry.y + tgtGeometry.height / 2.0f, boardCenter);

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
        if (!(rightClick && selectedObjects.size() > 1 && isSelected(MenuContextType::Card, hoveredCardID))) {
            setSelection({ MenuContextType::Card, hoveredCardID });
        }
    } else if (hoveredConnectorID != 0) {
        if (!(rightClick && selectedObjects.size() > 1 && isSelected(MenuContextType::Connector, hoveredConnectorID))) {
            setSelection({ MenuContextType::Connector, hoveredConnectorID });
        }
    } else if (backgroundHovered) {
        setSelection({ MenuContextType::Background, 0 });
    }
}

void ClientRenderer::setSelection(const ContextMenuTarget& target) {
    selection = target;
    selectedObjects.clear();
    if (target.type == MenuContextType::Card || target.type == MenuContextType::Connector) {
        selectedObjects.push_back(target);
    }
}

void ClientRenderer::setMultipleSelection(const std::vector<ContextMenuTarget>& targets) {
    selectionSidebarOpen = false;
    selectedObjects.clear();
    selectedObjects.reserve(targets.size());

    for (const ContextMenuTarget& target : targets) {
        if (target.type != MenuContextType::Card && target.type != MenuContextType::Connector) {
            continue;
        }

        bool alreadySelected = false;
        for (const ContextMenuTarget& selected : selectedObjects) {
            if (selected.type == target.type && selected.targetID == target.targetID) {
                alreadySelected = true;
                break;
            }
        }
        if (!alreadySelected) {
            selectedObjects.push_back(target);
        }
    }

    if (selectedObjects.empty()) {
        selection = { MenuContextType::Background, 0 };
    } else if (selectedObjects.size() == 1) {
        selection = selectedObjects.front();
    } else {
        selection = { MenuContextType::MultipleSelection, 0 };
    }
}

void ClientRenderer::updateSelectionSelection(const BoardState& board, ImVec2 boardCenter, bool backgroundHovered) {
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && backgroundHovered && !isConnecting) {
        isSelectionSelecting = true;
        selectionHasMoved = false;
        selectionStart = mousePos;
        selectionEnd = mousePos;
    }

    if (!isSelectionSelecting) {
        return;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        selectionEnd = mousePos;
        const float dx = selectionEnd.x - selectionStart.x;
        const float dy = selectionEnd.y - selectionStart.y;
        if (dx * dx + dy * dy >= kSelectionDragThreshold * kSelectionDragThreshold) {
            selectionHasMoved = true;
        }
    }

    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        return;
    }

    if (selectionHasMoved) {
        selectObjectsInSelection(board, boardCenter);
    } else {
        setSelection({ MenuContextType::Background, 0 });
    }
    isSelectionSelecting = false;
    selectionHasMoved = false;
}

void ClientRenderer::selectObjectsInSelection(const BoardState& localBoard, ImVec2 boardCenter) {
    const ImVec2 selectionMin(
        (selectionStart.x < selectionEnd.x) ? selectionStart.x : selectionEnd.x,
        (selectionStart.y < selectionEnd.y) ? selectionStart.y : selectionEnd.y);
    const ImVec2 selectionMax(
        (selectionStart.x > selectionEnd.x) ? selectionStart.x : selectionEnd.x,
        (selectionStart.y > selectionEnd.y) ? selectionStart.y : selectionEnd.y);
    std::vector<ContextMenuTarget> targets;

    for (uint32_t id : localBoard.getCardIDsInZOrder()) {
        const auto cardIt = localBoard.cards.find(id);
        if (cardIt == localBoard.cards.end()) {
            continue;
        }
        const Card& card = cardIt->second;
        const CardGeometry geometry = geometryForCard(card);
        const ImVec2 cardMin = worldToScreen(geometry.x, geometry.y, boardCenter);
        const ImVec2 cardMax = worldToScreen(geometry.x + geometry.width, geometry.y + geometry.height, boardCenter);
        if (rectanglesOverlap(selectionMin, selectionMax, cardMin, cardMax)) {
            targets.push_back({ MenuContextType::Card, id });
        }
    }

    for (const auto& [id, connector] : localBoard.connectors) {
        const auto srcIt = localBoard.cards.find(connector.sourceCardID);
        const auto tgtIt = localBoard.cards.find(connector.targetCardID);
        if (srcIt == localBoard.cards.end() || tgtIt == localBoard.cards.end()) {
            continue;
        }

        const Card& source = srcIt->second;
        const Card& target = tgtIt->second;
        const CardGeometry sourceGeometry = geometryForCard(source);
        const CardGeometry targetGeometry = geometryForCard(target);
        const ImVec2 sourceCenter = worldToScreen(
            sourceGeometry.x + sourceGeometry.width / 2.0f,
            sourceGeometry.y + sourceGeometry.height / 2.0f, boardCenter);
        const ImVec2 targetCenter = worldToScreen(
            targetGeometry.x + targetGeometry.width / 2.0f,
            targetGeometry.y + targetGeometry.height / 2.0f, boardCenter);
        const float padding = connector.thickness * camera.zoom * 0.5f;
        const ImVec2 paddedMin(selectionMin.x - padding, selectionMin.y - padding);
        const ImVec2 paddedMax(selectionMax.x + padding, selectionMax.y + padding);
        if (segmentIntersectsRectangle(sourceCenter, targetCenter, paddedMin, paddedMax)) {
            targets.push_back({ MenuContextType::Connector, id });
        }
    }

    setMultipleSelection(targets);
}

void ClientRenderer::deleteSelectedObjects(BoardCommandHandler& commandHandler) {
    std::vector<BoardObjectRef> objects;
    objects.reserve(selectedObjects.size());
    for (const ContextMenuTarget& target : selectedObjects) {
        if (target.type == MenuContextType::Card) {
            objects.push_back({BoardObjectRef::Kind::Card, target.targetID});
        } else if (target.type == MenuContextType::Connector) {
            objects.push_back({BoardObjectRef::Kind::Connector, target.targetID});
        }
    }
    commandHandler.handle(DeleteObjectsCommand{std::move(objects)});

    setSelection({ MenuContextType::Background, 0 });
    selectionSidebarOpen = false;
}

void ClientRenderer::handleConnectingMode(const BoardState& board, BoardCommandHandler& commandHandler, uint32_t hoveredCardID) {
    if (!isConnecting) {
        return;
    }

    // Error detection for deleted source card
    if (board.cards.find(connectingSourceCardID) == board.cards.end()) {
        isConnecting = false;
    }
    // Cancel if user hits escape key
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        isConnecting = false;
    }
    // Left click on a card creates the connector and exits connecting mode
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredCardID != 0) {
        // Leave on success so user can select another target after invalid
        if (board.cards.find(hoveredCardID) != board.cards.end() &&
            !board.connectorExistsBetween(connectingSourceCardID, hoveredCardID)) {
            commandHandler.handle(CreateConnectorCommand{connectingSourceCardID, hoveredCardID});
            isConnecting = false;
        }
    }
}

// Deletes whichever object is currently selected when the Delete key is pressed
void ClientRenderer::handleKeyboardShortcuts(BoardCommandHandler& commandHandler) {
    // Skip shortcuts while a text field has keyboard focus
    if (ImGui::GetIO().WantTextInput) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (!selectedObjects.empty()) {
            deleteSelectedObjects(commandHandler);
        }
    }
}

bool ClientRenderer::isSelected(MenuContextType type, uint32_t id) const {
    for (const ContextMenuTarget& selected : selectedObjects) {
        if (selected.type == type && selected.targetID == id) {
            return true;
        }
    }
    return false;
}

void ClientRenderer::syncSelection(const BoardState& localBoard) {
    selectedObjects.erase(
        std::remove_if(selectedObjects.begin(), selectedObjects.end(), [&](const ContextMenuTarget& target) {
            if (target.type == MenuContextType::Card) {
                return localBoard.cards.find(target.targetID) == localBoard.cards.end();
            }
            if (target.type == MenuContextType::Connector) {
                return localBoard.connectors.find(target.targetID) == localBoard.connectors.end();
            }
            return true;
        }),
        selectedObjects.end());

    if (!selectedObjects.empty()) {
        selection = selectedObjects.size() == 1
            ? selectedObjects.front()
            : ContextMenuTarget{ MenuContextType::MultipleSelection, 0 };
    } else if (selection.type == MenuContextType::Card ||
               selection.type == MenuContextType::Connector ||
               selection.type == MenuContextType::MultipleSelection) {
        selection = { MenuContextType::Background, 0 };
    }
}

void ClientRenderer::handleContextMenu(BoardCommandHandler& commandHandler, const ContextMenuTarget& context) {
    if (ImGui::BeginPopup("BoardContextMenu")) {

        // Background/general right click menu
        if (context.type == MenuContextType::Background) {

            if (ImGui::MenuItem("Create Card")) {
                float spawnX = contextMenuWorldPos.x;
                float spawnY = contextMenuWorldPos.y;
                commandHandler.handle(CreateCardCommand{spawnX, spawnY, "Enter text..."});
            }

            if (ImGui::MenuItem("Edit theme...")) {
                setSelection({ MenuContextType::Theme, 0 });
                selectionSidebarOpen = true;
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
                commandHandler.handle(DeleteCardCommand{context.targetID});
            }
        }

        // Connector right click menu
        else if (context.type == MenuContextType::Connector) {

            // Delete connector option (new options can be expanded under here)
            if (ImGui::MenuItem("Delete Connector")) {
                commandHandler.handle(DeleteConnectorCommand{context.targetID});
            }
        }

        // Multi-selection menu. Add future selection-wide actions here.
        else if (context.type == MenuContextType::MultipleSelection) {
            if (ImGui::MenuItem("Delete Selected")) {
                deleteSelectedObjects(commandHandler);
            }
        }

        ImGui::EndPopup();
    }
}

// Maps a theme color selector to the matching BoardTheme field
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

void ClientRenderer::drawGrid(ImDrawList* draw_list, const BoardState& board, ImVec2 board_p0, ImVec2 board_p1, ImVec2 boardCenter) {
    Color gridColor = ThemeUtils::getGridLineColor(inspector.displayTheme(board).boardBackgroundColor);
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

void ClientRenderer::drawConnectors(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter) {
    const BoardTheme theme = inspector.displayTheme(board);
    for (const auto& [id, conn] : board.connectors) {
        if (board.cards.find(conn.sourceCardID) == board.cards.end() ||
            board.cards.find(conn.targetCardID) == board.cards.end()) {
            continue;
        }

        const Card& src = board.cards.at(conn.sourceCardID);
        const Card& tgt = board.cards.at(conn.targetCardID);
        const CardGeometry srcGeometry = geometryForCard(src);
        const CardGeometry tgtGeometry = geometryForCard(tgt);

        ImVec2 srcCenter = worldToScreen(srcGeometry.x + srcGeometry.width / 2.0f, srcGeometry.y + srcGeometry.height / 2.0f, boardCenter);
        ImVec2 tgtCenter = worldToScreen(tgtGeometry.x + tgtGeometry.width / 2.0f, tgtGeometry.y + tgtGeometry.height / 2.0f, boardCenter);

        // Selected connectors get accent colored border (from theme)
        if (isSelected(MenuContextType::Connector, conn.connectorID)) {
            uint32_t accentColorIm = ThemeUtils::toImColor(theme.accentColor);
            draw_list->AddLine(srcCenter, tgtCenter, accentColorIm, (conn.thickness + 4.0f) * camera.zoom);
        }

        uint32_t connColorIm = ThemeUtils::toImColor(inspector.displayConnectorColor(conn));
        draw_list->AddLine(srcCenter, tgtCenter, connColorIm, conn.thickness * camera.zoom);
    }
}

void ClientRenderer::drawConnectionPreview(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter) {
    if (!isConnecting) return;

    // Verify source card still exists before drawing preview
    if (board.cards.find(connectingSourceCardID) != board.cards.end()) {
        const Card& srcCard = board.cards.at(connectingSourceCardID);
        const CardGeometry geometry = geometryForCard(srcCard);

        ImVec2 srcCenter = worldToScreen(geometry.x + geometry.width / 2.0f, geometry.y + geometry.height / 2.0f, boardCenter);
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        uint32_t previewColor = ThemeUtils::toImColor(inspector.displayTheme(board).connectorColor);
        draw_list->AddLine(srcCenter, mousePos, previewColor, 10.0f * camera.zoom);
    } else {
        // Fallback in case card was deleted in the split second we rendered (stupid)
        isConnecting = false;
    }
}

void ClientRenderer::drawCards(ImDrawList* draw_list, const BoardState& board, ImVec2 boardCenter) {
    const BoardTheme theme = inspector.displayTheme(board);
    for (uint32_t cardID : board.getCardIDsInZOrder()) {
        auto cardIt = board.cards.find(cardID);
        if (cardIt == board.cards.end()) {
            continue;
        }
        const Card& card = cardIt->second;
        const CardGeometry geometry = geometryForCard(card);
        const Color cardColor = inspector.displayCardColor(card);
        ImVec2 cardMin = worldToScreen(geometry.x, geometry.y, boardCenter);
        ImVec2 cardSize(geometry.width * camera.zoom, geometry.height * camera.zoom);
        ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);

        uint32_t bgCol = ThemeUtils::toImColor(cardColor);
        uint32_t textCol = ThemeUtils::toImColor(ThemeUtils::getTextColor(cardColor));

        const bool showingDropPreview = card.cardID == dropPreviewCardID;
        bool selected = isSelected(MenuContextType::Card, card.cardID);
        uint32_t borderCol = (selected || showingDropPreview)
            ? ThemeUtils::toImColor(theme.accentColor)
            : ThemeUtils::toImColor(ThemeUtils::getCardBorder(cardColor));
        float borderThickness = (selected || showingDropPreview) ? 4.0f : 2.0f;

        draw_list->AddRectFilled(cardMin, cardMax, bgCol, 6.0f);

        const ClientImageCache::Image* image = card.mediaURL.empty() ? nullptr : imageCache.find(card.mediaURL);
        const bool hasLoadedImage = image && image->texture != 0;
        if (hasLoadedImage) {
            const float padding = std::min(10.0f * camera.zoom, std::min(cardSize.x, cardSize.y) * 0.05f);
            const float availableWidth = std::max(0.0f, cardSize.x - padding * 2.0f);
            const float availableHeight = std::max(0.0f, cardSize.y - padding * 2.0f);
            const float scale = std::min(availableWidth / image->width, availableHeight / image->height);
            if (scale > 0.0f) {
                const float imageWidth = image->width * scale;
                const float imageHeight = image->height * scale;
                const ImVec2 imageMin(cardMin.x + (cardSize.x - imageWidth) * 0.5f,
                                      cardMin.y + (cardSize.y - imageHeight) * 0.5f);
                const ImVec2 imageMax(imageMin.x + imageWidth, imageMin.y + imageHeight);
                draw_list->PushClipRect(cardMin, cardMax, true);
                drawImageTexture(draw_list, image->texture, imageMin, imageMax);
                draw_list->PopClipRect();
            }
        }

        // Selected cards and active image drops use the accent outline.
        draw_list->AddRect(cardMin, cardMax, borderCol, 6.0f, 0, borderThickness);

        if (showingDropPreview) {
            const GLuint uploadIconTexture = imageCache.uploadIconTexture();
            const int uploadIconWidth = imageCache.uploadIconWidth();
            const int uploadIconHeight = imageCache.uploadIconHeight();
            if (uploadIconTexture != 0 && uploadIconWidth > 0 && uploadIconHeight > 0) {
                const float iconMaxSize = std::min(cardSize.x, cardSize.y) * 0.3f;
                const float iconScale = std::min(iconMaxSize / uploadIconWidth, iconMaxSize / uploadIconHeight);
                const float iconWidth = uploadIconWidth * iconScale;
                const float iconHeight = uploadIconHeight * iconScale;
                const ImVec2 iconMin(cardMin.x + (cardSize.x - iconWidth) * 0.5f,
                                     cardMin.y + (cardSize.y - iconHeight) * 0.5f);
                drawImageTexture(draw_list, uploadIconTexture, iconMin, ImVec2(iconMin.x + iconWidth, iconMin.y + iconHeight));
            }
        } else if (card.mediaURL.empty()) {
            // Scale font size by camera zoom
            float fontSize = ImGui::GetFontSize() * camera.zoom;
            float textPadding = 10.0f * camera.zoom;

            // Wrap the text to the card's inner width so it never spills horizontally
            float wrapWidth = cardMax.x - (cardMin.x + textPadding) - textPadding;

            draw_list->PushClipRect(cardMin, cardMax, true);
            draw_list->AddText(ImGui::GetFont(), fontSize, ImVec2(cardMin.x + textPadding, cardMin.y + textPadding), textCol, card.text.c_str(), NULL, wrapWidth);
            draw_list->PopClipRect();
        }
    }

    // Draw resize handles last so they sit on top of every card
    if (selection.type == MenuContextType::Card) {
        auto it = board.cards.find(selection.targetID);
        if (it != board.cards.end()) {
            drawResizeHandles(draw_list, geometryForCard(it->second), theme, boardCenter);
        }
    }
}

// Opens selection sidebar for object selected on right side of the screen
// Shutdown method (run this on closing renderer/app)
void ClientRenderer::shutdown() {
    imageCache.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

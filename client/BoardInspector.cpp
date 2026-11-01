#include "BoardInspector.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr float kMinCardSize = 20.0f;
constexpr float kMaxCardSize = 1000.0f;
constexpr double kTextEditDelaySeconds = 0.2;

ImVec4 toImVec4(const Color& color) {
    return ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

Color fromImVec4(const ImVec4& value) {
    auto toByte = [](float channel) -> uint8_t {
        float scaled = channel * 255.0f;
        if (scaled < 0.0f) scaled = 0.0f;
        if (scaled > 255.0f) scaled = 255.0f;
        return static_cast<uint8_t>(scaled + 0.5f);
    };
    return {toByte(value.x), toByte(value.y), toByte(value.z), toByte(value.w)};
}

bool startsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string trimWhitespace(std::string value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
} // namespace
static const Color* themeColorPtr(const BoardTheme& theme, ThemeColor field) {
    switch (field) {
        case ThemeColor::BoardBackground:   return &theme.boardBackgroundColor;
        case ThemeColor::CardColor:         return &theme.cardColor;
        case ThemeColor::ConnectorColor:    return &theme.connectorColor;
        case ThemeColor::AccentColor:       return &theme.accentColor;
        case ThemeColor::ImportantElements: return &theme.importantElements;
    }
    return &theme.boardBackgroundColor;
}

// Default value for a theme color, used by the picker's Reset button
static Color themeColorDefault(ThemeColor field) {
    BoardTheme defaults;
    switch (field) {
        case ThemeColor::BoardBackground:   return defaults.boardBackgroundColor;
        case ThemeColor::CardColor:         return defaults.cardColor;
        case ThemeColor::ConnectorColor:    return defaults.connectorColor;
        case ThemeColor::AccentColor:       return defaults.accentColor;
        case ThemeColor::ImportantElements: return defaults.importantElements;
    }
    return defaults.boardBackgroundColor;
}

// Stages a theme color and requests the picker popup
void BoardInspector::openColorPicker(ThemeColor field, const Color& current) {
    editingThemeColor = field;
    editingObjectColorType = ObjectColorType::None;
    editingObjectID = 0;
    pendingColor = toImVec4(current);
    colorPickerRequested = true;
}

void BoardInspector::openObjectColorPicker(ObjectColorType type, uint32_t objectID, const Color& current) {
    editingThemeColor = ThemeColor::CardColor;
    editingObjectColorType = type;
    editingObjectID = objectID;
    pendingColor = toImVec4(current);
    colorPickerRequested = true;
}

// Keep the pending color as a view-only preview until the user confirms it.
void BoardInspector::drawColorPicker(const BoardState& board, BoardCommandHandler& commandHandler) {
    // Open here (not in the sidebar) so the popup ID matches this window
    if (colorPickerRequested) {
        colorPickerRequested = false;
        if (editingObjectColorType == ObjectColorType::Card) {
            if (board.cards.find(editingObjectID) == board.cards.end()) {
                editingObjectColorType = ObjectColorType::None;
            }
        } else if (editingObjectColorType == ObjectColorType::Connector) {
            if (board.connectors.find(editingObjectID) == board.connectors.end()) {
                editingObjectColorType = ObjectColorType::None;
            }
        }
        ImGui::OpenPopup("ThemeColorPicker");
    }

    const Color* target = nullptr;
    if (editingObjectColorType == ObjectColorType::None) {
        target = themeColorPtr(board.theme, editingThemeColor);
    } else if (editingObjectColorType == ObjectColorType::Card) {
        auto it = board.cards.find(editingObjectID);
        if (it != board.cards.end()) {
            target = &it->second.color;
        }
    } else if (editingObjectColorType == ObjectColorType::Connector) {
        auto it = board.connectors.find(editingObjectID);
        if (it != board.connectors.end()) {
            target = &it->second.color;
        }
    }

    if (ImGui::BeginPopup("ThemeColorPicker")) {
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueBar |
                                    ImGuiColorEditFlags_NoSidePreview;
        ImGui::ColorPicker4("##themeColor", (float*)&pendingColor, flags);

        // Enter confirms, unless a color text field is being edited
        bool enterPressed = !ImGui::GetIO().WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));

        if (ImGui::Button("Confirm") || enterPressed) {
            if (target) {
                const Color committedColor = fromImVec4(pendingColor);
                if (editingObjectColorType == ObjectColorType::None) {
                    BoardThemeField field = BoardThemeField::BoardBackground;
                    switch (editingThemeColor) {
                        case ThemeColor::BoardBackground:   field = BoardThemeField::BoardBackground; break;
                        case ThemeColor::CardColor:         field = BoardThemeField::CardColor; break;
                        case ThemeColor::ConnectorColor:    field = BoardThemeField::ConnectorColor; break;
                        case ThemeColor::AccentColor:       field = BoardThemeField::AccentColor; break;
                        case ThemeColor::ImportantElements: field = BoardThemeField::ImportantElements; break;
                    }
                    commandHandler.handle(SetBoardThemeColorCommand{field, committedColor});
                } else if (editingObjectColorType == ObjectColorType::Card) {
                    commandHandler.handle(SetCardColorCommand{editingObjectID, committedColor});
                } else if (editingObjectColorType == ObjectColorType::Connector) {
                    commandHandler.handle(SetConnectorColorCommand{editingObjectID, committedColor});
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            if (editingObjectColorType == ObjectColorType::None) {
                pendingColor = toImVec4(themeColorDefault(editingThemeColor));
            } else if (editingObjectColorType == ObjectColorType::Card) {
                pendingColor = toImVec4(board.theme.cardColor);
            } else if (editingObjectColorType == ObjectColorType::Connector) {
                pendingColor = toImVec4(board.theme.connectorColor);
            } else {
                pendingColor = toImVec4(themeColorDefault(editingThemeColor));
            }
        }

        ImGui::EndPopup();
        colorPopupWasOpen = true;
    } else {
        // Preview colors live in renderer state, so closing without confirming
        // simply discards the preview and leaves the board untouched.
        colorPopupWasOpen = false;
    }
}

// Clears the image source editor when its selected card changes
void BoardInspector::resetImageSourceEditor() {
    imageUrlEditCardID = 0;
    std::memset(imageUrlEditBuffer, 0, sizeof(imageUrlEditBuffer));
    imageSourceError.clear();
}


void BoardInspector::drawSidebar(const BoardState& board, const ClientImageCache& imageCache, BoardCommandHandler& commandHandler,
                                 const ContextMenuTarget& selection, bool& sidebarOpen) {
    if (textEditCardID != 0 && (!sidebarOpen || selection.type != MenuContextType::Card ||
                                selection.targetID != textEditCardID)) {
        flushPendingText(board, commandHandler);
    }

    // Return if an object was not selected 
    if (!sidebarOpen || selection.type == MenuContextType::Background ||
        selection.type == MenuContextType::MultipleSelection) {
        return;
    }
    currentSelectionID = selection.targetID;

    // Set panel to right side of screen
    const float sidebarWidth = 320.0f;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - sidebarWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, displaySize.y), ImGuiCond_Always);

    if (ImGui::Begin(getSelectionTitle(selection.type), &sidebarOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushID(static_cast<int>(selection.targetID));

        // Per-selected object method so we can scale if we need/want to
        switch (selection.type) {
            case MenuContextType::Card:
                drawCardOptions(board, imageCache, commandHandler);
                break;
            case MenuContextType::Connector:
                drawConnectorOptions(board, commandHandler);
                break;
            case MenuContextType::Theme:
                drawThemeOptions(board);
                break;
            default:
                break;
        }

        ImGui::PopID();
    }
    ImGui::End();
}

// Sends a pending text edit before leaving the selected card
void BoardInspector::flushPendingText(const BoardState& board, BoardCommandHandler& commandHandler) {
    auto editIt = textEdits.find(textEditCardID);
    if (editIt == textEdits.end() || !editIt->second.pending || editIt->second.conflict ||
        editIt->second.awaitingServerUpdate) {
        return;
    }

    auto cardIt = board.cards.find(textEditCardID);
    TextEditState& edit = editIt->second;
    if (cardIt == board.cards.end()) {
        edit.pending = false;
        return;
    }
    if (cardIt->second.textRevision != edit.baseRevision) {
        markTextConflict(textEditCardID, cardIt->second.text, cardIt->second.textRevision);
        return;
    }
    if (cardIt->second.text == edit.text) {
        edit.pending = false;
        return;
    }

    commandHandler.handle(SetCardTextCommand{textEditCardID, edit.text, edit.baseRevision});
    edit.sentText = edit.text;
    edit.pending = false;
    edit.awaitingServerUpdate = true;
}

// Keeps a rejected text draft and displays the latest server version
void BoardInspector::markTextConflict(uint32_t cardID, const std::string& serverText, uint64_t textRevision) {
    auto editIt = textEdits.find(cardID);
    if (editIt == textEdits.end()) {
        return;
    }

    TextEditState& edit = editIt->second;
    edit.conflictText = serverText;
    edit.conflictRevision = textRevision;
    edit.pending = false;
    edit.awaitingServerUpdate = false;
    edit.conflict = true;
    edit.widgetRevision++;
}

// Title shown on the sidebar window for a given object type.
const char* BoardInspector::getSelectionTitle(MenuContextType type) const {
    switch (type) {
        case MenuContextType::Card:      return "Card";
        case MenuContextType::Connector: return "Connector";
        case MenuContextType::Theme:     return "Theme";
        default:                         return "Selection";
    }
}

// Card editor options
void BoardInspector::drawCardOptions(const BoardState& board, const ClientImageCache& imageCache, BoardCommandHandler& commandHandler) {
    auto cardIt = board.cards.find(currentSelectionID);
    if (cardIt == board.cards.end()) {
        return;
    }
    const Card& card = cardIt->second;

    textEditCardID = card.cardID;
    TextEditState& edit = textEdits[card.cardID];
    if (!edit.initialized) {
        edit.text = card.text;
        edit.baseRevision = card.textRevision;
        edit.initialized = true;
    } else {
        if (card.textRevision != edit.baseRevision) {
            if (edit.awaitingServerUpdate && card.text == edit.sentText) {
                edit.awaitingServerUpdate = false;
                edit.baseRevision = card.textRevision;
                if (!edit.pending) {
                    edit.text = card.text;
                }
            } else if (!edit.pending && !edit.awaitingServerUpdate && !edit.conflict) {
                edit.text = card.text;
                edit.baseRevision = card.textRevision;
                edit.widgetRevision++;
            } else if (!edit.conflict) {
                markTextConflict(card.cardID, card.text, card.textRevision);
            } else {
                edit.conflictText = card.text;
                edit.conflictRevision = card.textRevision;
            }
        }
    }

    const bool hasEmbeddedImage = startsWith(card.mediaURL, "data:image/");
    if (imageUrlEditCardID != card.cardID) {
        imageUrlEditCardID = card.cardID;
        std::memset(imageUrlEditBuffer, 0, sizeof(imageUrlEditBuffer));
        if (!hasEmbeddedImage) {
            snprintf(imageUrlEditBuffer, sizeof(imageUrlEditBuffer), "%s", card.mediaURL.c_str());
        }
    }

    if (hasEmbeddedImage) {
        ImGui::TextUnformatted("Image attached from file");
    } else {
        ImGui::TextUnformatted("Image URL or local path:");
        ImGui::SetNextItemWidth(-1.0f);
        bool submitted = ImGui::InputText("##imageURL", imageUrlEditBuffer, sizeof(imageUrlEditBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        if (submitted || deactivatedAfterEdit) {
            const std::string newImageSource = trimWhitespace(imageUrlEditBuffer);
            if (newImageSource != card.mediaURL) {
                std::string error;
                const std::string sharedImageSource = newImageSource.empty()
                    ? std::string{}
                    : ClientImageCache::sourceFromDrop(newImageSource, error);
                if (!error.empty()) {
                    imageSourceError = error;
                } else {
                    imageSourceError.clear();
                    commandHandler.handle(SetCardImageCommand{card.cardID, sharedImageSource});
                }
            }
        }
    }
    if (!imageSourceError.empty()) {
        ImGui::TextWrapped("%s", imageSourceError.c_str());
    }

    if (!card.mediaURL.empty() && ImGui::Button("Remove Image##cardImage")) {
        std::memset(imageUrlEditBuffer, 0, sizeof(imageUrlEditBuffer));
        commandHandler.handle(SetCardImageCommand{card.cardID, ""});
    }
    if (!card.mediaURL.empty()) {
        const ClientImageCache::Image* image = imageCache.find(card.mediaURL);
        if (image && image->loading) {
            ImGui::TextUnformatted("Loading image...");
        } else if (image && image->failed) {
            ImGui::TextUnformatted("Image could not be loaded.");
        }
    }
    ImGui::Separator();

    // Text content editor
    ImGui::TextUnformatted("Text:");
    char textBuffer[MAX_CARD_TEXT_BYTES + 1] = {};
    snprintf(textBuffer, sizeof(textBuffer), "%s", edit.text.c_str());
    ImVec2 textSize(ImGui::GetContentRegionAvail().x, 100.0f);
    ImGui::PushID(edit.widgetRevision);
    ImGuiInputTextFlags textFlags = ImGuiInputTextFlags_WordWrap;
    if (edit.conflict) {
        textFlags |= ImGuiInputTextFlags_ReadOnly;
    }
    bool textChanged = ImGui::InputTextMultiline("##text", textBuffer, sizeof(textBuffer), textSize, textFlags);
    bool textInputDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    if (textChanged) {
        edit.text = textBuffer;
        edit.pending = true;
        edit.changedAt = ImGui::GetTime();
    }
    if (edit.conflict) {
        ImGui::TextWrapped("This card changed while you were editing. Your draft is saved.");
        ImGui::TextWrapped("Latest server text: %s", edit.conflictText.c_str());
        if (ImGui::Button("Use latest text")) {
            edit.text = edit.conflictText;
            edit.baseRevision = edit.conflictRevision;
            edit.pending = false;
            edit.awaitingServerUpdate = false;
            edit.conflict = false;
            edit.widgetRevision++;
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace with my draft")) {
            edit.baseRevision = edit.conflictRevision;
            edit.sentText = edit.text;
            edit.pending = false;
            edit.awaitingServerUpdate = true;
            edit.conflict = false;
            commandHandler.handle(SetCardTextCommand{card.cardID, edit.text, edit.conflictRevision});
            edit.widgetRevision++;
        }
    }
    if (edit.pending && !edit.conflict && !edit.awaitingServerUpdate &&
        (textInputDeactivatedAfterEdit || ImGui::GetTime() - edit.changedAt >= kTextEditDelaySeconds)) {
        flushPendingText(board, commandHandler);
    }

    // Card size editors with reset-to-default buttons (width matches the connector input)
    float resetButtonWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float sizeInputWidth = ImGui::GetContentRegionAvail().x - resetButtonWidth - ImGui::GetStyle().ItemSpacing.x;

    ImGui::TextUnformatted("Size X:");
    ImGui::SetNextItemWidth(sizeInputWidth);
    float width = card.width;
    bool widthChanged = ImGui::InputFloat("##sizeX", &width, 1.0f, 1.0f, "%.2f");
    if (!ImGui::IsItemActive()) {
        width = std::clamp(width, kMinCardSize, kMaxCardSize);
    }
    if (widthChanged || width != card.width) {
        commandHandler.handle(SetCardGeometryCommand{card.cardID, card.x, card.y, width, card.height});
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##sizeX")) {
        commandHandler.handle(SetCardGeometryCommand{
            card.cardID, card.x, card.y, board.user_settings.defaultCardWidth, card.height});
    }

    ImGui::TextUnformatted("Size Y:");
    ImGui::SetNextItemWidth(sizeInputWidth);
    float height = card.height;
    bool heightChanged = ImGui::InputFloat("##sizeY", &height, 1.0f, 1.0f, "%.2f");
    if (!ImGui::IsItemActive()) {
        height = std::clamp(height, kMinCardSize, kMaxCardSize);
    }
    if (heightChanged || height != card.height) {
        commandHandler.handle(SetCardGeometryCommand{card.cardID, card.x, card.y, card.width, height});
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##sizeY")) {
        commandHandler.handle(SetCardGeometryCommand{
            card.cardID, card.x, card.y, card.width, board.user_settings.defaultCardHeight});
    }

    // Card color editor with swatch opening the shared color picker
    {
        const float swatchWidth = 60.0f;
        ImGui::TextUnformatted("Color:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - swatchWidth);
        const Color currentColor = displayCardColor(card);
        if (ImGui::ColorButton("##cardColor", toImVec4(currentColor), ImGuiColorEditFlags_NoAlpha, ImVec2(swatchWidth, 0.0f))) {
            openObjectColorPicker(ObjectColorType::Card, card.cardID, currentColor);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##cardColorReset")) {
            commandHandler.handle(SetCardColorCommand{card.cardID, board.theme.cardColor});
        }
    }
}

// Connector editor options
void BoardInspector::drawConnectorOptions(const BoardState& board, BoardCommandHandler& commandHandler) {
    auto connIt = board.connectors.find(currentSelectionID);
    if (connIt == board.connectors.end()) {
        return;
    }
    const Connector& connector = connIt->second;

    // Thickness editor with a reset-to-default button
    ImGui::TextUnformatted("Thickness:");
    float resetButtonWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    float thickness = connector.thickness;
    bool thicknessChanged = ImGui::InputFloat("##thickness", &thickness, 1.0f, 1.0f, "%.2f");

    // Keep thickness positive once editing finishes so the connector stays valid
    if (!ImGui::IsItemActive() && thickness < 0.1f) {
        thickness = 0.1f;
    }
    if (thicknessChanged || thickness != connector.thickness) {
        commandHandler.handle(SetConnectorThicknessCommand{connector.connectorID, thickness});
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        commandHandler.handle(SetConnectorThicknessCommand{
            connector.connectorID, board.user_settings.defaultConnectorThickness});
    }

    // Connector color editor with swatch opening the shared color picker
    {
        const float swatchWidth = 60.0f;
        ImGui::TextUnformatted("Color:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - swatchWidth);
        const Color currentColor = displayConnectorColor(connector);
        if (ImGui::ColorButton("##connectorColor", toImVec4(currentColor), ImGuiColorEditFlags_NoAlpha, ImVec2(swatchWidth, 0.0f))) {
            openObjectColorPicker(ObjectColorType::Connector, connector.connectorID, currentColor);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##connColorReset")) {
            commandHandler.handle(SetConnectorColorCommand{connector.connectorID, board.theme.connectorColor});
        }
    }
}

// Theme editor: one row per theme color, each with a clickable preview swatch
void BoardInspector::drawThemeOptions(const BoardState& board) {
    const BoardTheme theme = displayTheme(board);

    auto colorRow = [&](const char* label, const char* id, ThemeColor field, const Color& color) {
        const float swatchWidth = 60.0f;
        ImGui::TextUnformatted(label);
        // Right-align the swatch within the content region
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - swatchWidth);
        if (ImGui::ColorButton(id, toImVec4(color), ImGuiColorEditFlags_NoAlpha, ImVec2(swatchWidth, 0.0f))) {
            openColorPicker(field, color);
        }
    };

    colorRow("Board Background", "##themeBoardBackground", ThemeColor::BoardBackground, theme.boardBackgroundColor);
    colorRow("Cards", "##themeCard", ThemeColor::CardColor, theme.cardColor);
    colorRow("Connectors", "##themeConnector", ThemeColor::ConnectorColor, theme.connectorColor);
    colorRow("Accent", "##themeAccent", ThemeColor::AccentColor, theme.accentColor);
    colorRow("Important Elements", "##themeImportant", ThemeColor::ImportantElements, theme.importantElements);
}

// Force theme colors on ImGUI elements
void BoardInspector::applyTheme(const BoardTheme& theme) {
    ImGuiStyle& style = ImGui::GetStyle();

    const ImVec4 base = toImVec4(theme.cardColor);
    const ImVec4 hovered = toImVec4(ThemeUtils::shift(theme.cardColor, 20, 20, 20));
    const ImVec4 active = toImVec4(ThemeUtils::shift(theme.cardColor, -20, -20, -20));
    const ImVec4 text = toImVec4(ThemeUtils::getTextColor(theme.cardColor));

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

BoardTheme BoardInspector::displayTheme(const BoardState& board) const {
    BoardTheme theme = board.theme;
    if (!colorPopupWasOpen || editingObjectColorType != ObjectColorType::None) {
        return theme;
    }

    const Color preview = fromImVec4(pendingColor);
    switch (editingThemeColor) {
        case ThemeColor::BoardBackground:   theme.boardBackgroundColor = preview; break;
        case ThemeColor::CardColor:         theme.cardColor = preview; break;
        case ThemeColor::ConnectorColor:    theme.connectorColor = preview; break;
        case ThemeColor::AccentColor:       theme.accentColor = preview; break;
        case ThemeColor::ImportantElements: theme.importantElements = preview; break;
    }
    return theme;
}

Color BoardInspector::displayCardColor(const Card& card) const {
    if (colorPopupWasOpen && editingObjectColorType == ObjectColorType::Card &&
        editingObjectID == card.cardID) {
        return fromImVec4(pendingColor);
    }
    return card.color;
}

Color BoardInspector::displayConnectorColor(const Connector& connector) const {
    if (colorPopupWasOpen && editingObjectColorType == ObjectColorType::Connector &&
        editingObjectID == connector.connectorID) {
        return fromImVec4(pendingColor);
    }
    return connector.color;
}


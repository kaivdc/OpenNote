#ifndef BOARD_INSPECTOR_H
#define BOARD_INSPECTOR_H

#include "BoardCommands.h"
#include "BoardUIState.h"
#include "ClientImageCache.h"
#include "imgui.h"
#include <string>

class BoardInspector {
public:
    void drawColorPicker(const BoardState& board, BoardCommandHandler& commandHandler);
    void drawSidebar(const BoardState& board, const ClientImageCache& imageCache, BoardCommandHandler& commandHandler,
                     const ContextMenuTarget& selection, bool& sidebarOpen);
    void applyTheme(const BoardTheme& theme);
    void resetImageSourceEditor();

    BoardTheme displayTheme(const BoardState& board) const;
    Color displayCardColor(const Card& card) const;
    Color displayConnectorColor(const Connector& connector) const;

private:
    ThemeColor editingThemeColor = ThemeColor::BoardBackground;
    ObjectColorType editingObjectColorType = ObjectColorType::None;
    uint32_t editingObjectID = 0;
    uint32_t currentSelectionID = 0;
    ImVec4 pendingColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool colorPickerRequested = false;
    bool colorPopupWasOpen = false;
    uint32_t imageUrlEditCardID = 0;
    char imageUrlEditBuffer[2048] = {};
    std::string imageSourceError;

    void openColorPicker(ThemeColor field, const Color& current);
    void openObjectColorPicker(ObjectColorType type, uint32_t objectID, const Color& current);
    void drawCardOptions(const BoardState& board, const ClientImageCache& imageCache, BoardCommandHandler& commandHandler);
    void drawConnectorOptions(const BoardState& board, BoardCommandHandler& commandHandler);
    void drawThemeOptions(const BoardState& board);
    const char* getSelectionTitle(MenuContextType type) const;
};

#endif

#ifndef BOARD_UI_STATE_H
#define BOARD_UI_STATE_H

#include <cstdint>

enum class MenuContextType {
    Background,
    Card,
    Connector,
    MultipleSelection,
    Theme
};

enum class ThemeColor {
    BoardBackground,
    CardColor,
    ConnectorColor,
    AccentColor,
    ImportantElements
};

enum class ObjectColorType {
    None,
    Card,
    Connector
};

struct ContextMenuTarget {
    MenuContextType type = MenuContextType::Background;
    uint32_t targetID = 0;
};

#endif

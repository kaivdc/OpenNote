#ifndef CLIENT_COLOR_PICKER_H
#define CLIENT_COLOR_PICKER_H

#include <algorithm>
#include "BoardStructs.h"
#include "imgui.h"

struct ColorPickerResult {
    bool isOpen = false;
    bool confirmed = false;
};

// Converts a board color into ImGui's float color format
inline ImVec4 colorToImVec4(const Color& color) {
    return ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

// Converts ImGui's float color into a board color
inline Color colorFromImVec4(const ImVec4& value) {
    auto toByte = [](float channel) -> uint8_t {
        const float scaled = std::clamp(channel * 255.0f, 0.0f, 255.0f);
        return static_cast<uint8_t>(scaled + 0.5f);
    };
    return {toByte(value.x), toByte(value.y), toByte(value.z), toByte(value.w)};
}

// Draws a right-aligned color swatch row and reports when it is clicked
inline bool drawColorSwatchRow(const char* label, const char* widgetID, const Color& color) {
    const float swatchWidth = 60.0f;
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - swatchWidth);
    return ImGui::ColorButton(widgetID, colorToImVec4(color), ImGuiColorEditFlags_NoAlpha,
                              ImVec2(swatchWidth, 0.0f));
}

// Draws the shared picker controls and reports whether the color was confirmed
inline ColorPickerResult drawColorPickerPopup(const char* popupID, ImVec4& pendingColor,
                                               const Color& resetColor) {
    if (!ImGui::BeginPopup(popupID)) {
        return {};
    }

    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_NoSidePreview;
    ImGui::ColorPicker4("##colorPicker", (float*)&pendingColor, flags);

    const bool enterPressed = !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    const bool confirmed = ImGui::Button("Confirm") || enterPressed;
    if (confirmed) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        pendingColor = colorToImVec4(resetColor);
    }

    ImGui::EndPopup();
    return {true, confirmed};
}

#endif

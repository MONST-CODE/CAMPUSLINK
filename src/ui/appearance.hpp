#pragma once
#include "campuslink/ui_preferences.hpp"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <array>
#include <string>

namespace campuslink::ui {
constexpr ImVec4 color(unsigned rgb, float alpha = 1) {
    return {float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, alpha};
}
struct Palette {
    ImVec4 canvas, surface, raised, text, muted, line, key, button, hover, selected, caution;
};
inline constexpr Palette light_palette{color(0xF5F8FC), color(0xFFFFFF), color(0xEFF5FA), color(0x102238),
    color(0x465D72), color(0xD4E1EC), color(0x075C8E), color(0xD7EDFC), color(0xBDE4FA), color(0xA5D9F5), color(0x8E460B)};
inline constexpr Palette dark_palette{color(0x0C111A), color(0x121C2B), color(0x1B2A3B), color(0xEDF5FF),
    color(0xADC2D4), color(0x2C4258), color(0x80D4FF), color(0x203B52), color(0x294B68), color(0x325D80), color(0xFFBE73)};
inline const Palette& palette(Theme theme) { return theme == Theme::light ? light_palette : dark_palette; }
inline void apply_appearance(const Preferences& p, float dpi) {
    const auto& b = palette(p.theme);
    auto& s = ImGui::GetStyle();
    s = ImGuiStyle{}; // start from unscaled geometry, never multiply the previous monitor's scale
    if (p.theme == Theme::light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    s.FontSizeBase = static_cast<float>(p.size);
    s.FontScaleDpi = dpi;
    s.WindowPadding = p.compact ? ImVec2(12, 10) : ImVec2(18, 16);
    s.FramePadding = p.compact ? ImVec2(8, 4) : ImVec2(11, 7);
    s.ItemSpacing = p.compact ? ImVec2(8, 6) : ImVec2(12, 10);
    s.CellPadding = p.compact ? ImVec2(9, 6) : ImVec2(11, 8);
    s.WindowBorderSize = 0; s.ChildBorderSize = 1; s.PopupBorderSize = 1;
    s.FrameRounding = 6; s.ChildRounding = 10; s.PopupRounding = 9; s.ScrollbarRounding = 8;
    const auto set = [&](ImVec4 v, std::initializer_list<ImGuiCol> ids) { for (auto id : ids) s.Colors[id] = v; };
    set(b.text, {ImGuiCol_Text}); set(b.muted, {ImGuiCol_TextDisabled});
    set(b.canvas, {ImGuiCol_WindowBg, ImGuiCol_ScrollbarBg});
    set(b.surface, {ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_TableRowBg, ImGuiCol_TitleBg});
    set(b.raised, {ImGuiCol_FrameBg, ImGuiCol_TableHeaderBg, ImGuiCol_TableRowBgAlt, ImGuiCol_MenuBarBg});
    set(b.line, {ImGuiCol_Border, ImGuiCol_TableBorderLight, ImGuiCol_TableBorderStrong, ImGuiCol_Separator, ImGuiCol_ScrollbarGrab});
    set(b.button, {ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_TitleBgActive});
    set(b.hover, {ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered, ImGuiCol_FrameBgHovered, ImGuiCol_ResizeGripHovered, ImGuiCol_PlotLinesHovered});
    set(b.selected, {ImGuiCol_ButtonActive, ImGuiCol_HeaderActive, ImGuiCol_FrameBgActive, ImGuiCol_TextSelectedBg});
    set(b.key, {ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_PlotLines, ImGuiCol_PlotHistogram, ImGuiCol_NavCursor});
    s.ScaleAllSizes(dpi);
}
struct Fonts {
    ImFont* inter{};
    ImFont* mono{};
    ImFont* fallback{};
    void load() {
        auto& atlas = *ImGui::GetIO().Fonts;
        fallback = atlas.AddFontDefault();
        const auto load_file = [&](const char* relative, const ImFontConfig* config = nullptr, const ImWchar* ranges = nullptr) -> ImFont* {
            const auto* root = SDL_GetBasePath();
            if (!root) return nullptr;
            const auto file = std::string(root) + relative;
            size_t length{};
            auto* data = SDL_LoadFile(file.c_str(), &length);
            if (!data) return nullptr;
            if (!length || length > 4 * 1024 * 1024) { SDL_free(data); return nullptr; }
            auto* owned = IM_ALLOC(length);
            SDL_memcpy(owned, data, length); SDL_free(data);
            return atlas.AddFontFromMemoryTTF(owned, static_cast<int>(length), 16, config, ranges);
        };
        inter = load_file("assets/fonts/Inter.ttf");
        mono = load_file("assets/fonts/JetBrainsMono.ttf");
        // Keep the embedded fallback usable for French even outside the bitmap's character coverage.
        if (inter) {
            static constexpr ImWchar french_glyphs[]{0x0080, 0x017f, 0x2000, 0x206f, 0};
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.DstFont = fallback;
            load_file("assets/fonts/Inter.ttf", &merge, french_glyphs);
        }
    }
    ImFont* choose(Typeface face) const {
        const auto selected = face == Typeface::mono ? mono : face == Typeface::inter ? inter : fallback;
        return selected ? selected : fallback;
    }
};
}

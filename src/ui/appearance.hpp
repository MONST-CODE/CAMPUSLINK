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
// Design tokens. Each theme is specified on its own (not an inversion of the other):
// light uses tinted whites and a sky-700 accent (5.9:1 under white labels); dark uses blue-black
// surfaces, off-white text and a luminous sky-400 accent. Semantic tones have a strong
// foreground (text/icons) and a soft background (banners, badges).
struct Tone { ImVec4 strong, soft; };
struct Palette {
    ImVec4 canvas, surface, raised, sunken, text, muted, faint, line, line_strong;
    ImVec4 accent, accent_hover, accent_active, accent_soft, accent_text, on_accent;
    Tone success, warning, danger, info, neutral;
    // Legacy roles used by lab panels.
    ImVec4 key, button, hover, selected, caution;
};
inline constexpr Palette light_palette{
    color(0xF3F6FA), color(0xFFFFFF), color(0xF7F9FC), color(0xEEF3F8),
    color(0x0F1E2E), color(0x52637A), color(0x8393A6), color(0xDDE5EE), color(0xC4D0DD),
    color(0x0369A1), color(0x075985), color(0x0C4A6E), color(0xE0F2FE), color(0x075985), color(0xFFFFFF),
    {color(0x15803D), color(0xDCFCE7)}, {color(0xB45309), color(0xFEF3C7)}, {color(0xB91C1C), color(0xFEE2E2)},
    {color(0x0369A1), color(0xE0F2FE)}, {color(0x52637A), color(0xEEF3F8)},
    color(0x075985), color(0xEAF1F8), color(0xDCEAF6), color(0xCFE6F8), color(0xB45309)};
inline constexpr Palette dark_palette{
    color(0x0A0F16), color(0x111922), color(0x16212D), color(0x0D141C),
    color(0xE7EEF6), color(0x9AABBD), color(0x6A7C90), color(0x1F2D3C), color(0x2C3F53),
    color(0x38BDF8), color(0x7DD3FC), color(0xBAE6FD), color(0x0B2A3D), color(0x7DD3FC), color(0x04131D),
    {color(0x4ADE80), color(0x0E2A1A)}, {color(0xFBBF24), color(0x2C2109)}, {color(0xF87171), color(0x2E1416)},
    {color(0x7DD3FC), color(0x0B2A3D)}, {color(0x9AABBD), color(0x16212D)},
    color(0x7DD3FC), color(0x1A2836), color(0x223548), color(0x274462), color(0xFBBF24)};
inline const Palette& palette(Theme theme) { return theme == Theme::light ? light_palette : dark_palette; }

// Spacing scale (unscaled px; multiplied by the DPI scale at use).
namespace space { inline constexpr float xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32; }
// Type scale relative to the user's body size.
namespace type { inline constexpr float caption = 0.86f, body = 1.f, heading = 1.13f, title = 1.3f, display = 1.6f; }

inline void apply_appearance(const Preferences& p, float dpi) {
    const auto& b = palette(p.theme);
    auto& s = ImGui::GetStyle();
    s = ImGuiStyle{}; // start from unscaled geometry, never multiply the previous monitor's scale
    if (p.theme == Theme::light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    s.FontSizeBase = static_cast<float>(p.size);
    s.FontScaleDpi = dpi;
    s.WindowPadding = p.compact ? ImVec2(14, 12) : ImVec2(20, 18);
    s.FramePadding = p.compact ? ImVec2(10, 5) : ImVec2(12, 7);
    s.ItemSpacing = p.compact ? ImVec2(8, 6) : ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = p.compact ? ImVec2(8, 5) : ImVec2(10, 7);
    s.ScrollbarSize = 12; s.GrabMinSize = 12;
    s.WindowBorderSize = 0; s.ChildBorderSize = 1; s.PopupBorderSize = 1; s.FrameBorderSize = 0;
    s.FrameRounding = 7; s.ChildRounding = 10; s.PopupRounding = 10; s.ScrollbarRounding = 8; s.GrabRounding = 7;
    s.WindowRounding = 12; s.TabRounding = 7;
    s.SeparatorTextBorderSize = 1; s.SeparatorTextPadding = ImVec2(0, 4);
    s.DisabledAlpha = 0.45f;
    const auto set = [&](ImVec4 v, std::initializer_list<ImGuiCol> ids) { for (auto id : ids) s.Colors[id] = v; };
    set(b.text, {ImGuiCol_Text}); set(b.faint, {ImGuiCol_TextDisabled});
    set(b.canvas, {ImGuiCol_WindowBg, ImGuiCol_MenuBarBg});
    set(b.surface, {ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TableRowBg});
    set(b.raised, {ImGuiCol_TableRowBgAlt, ImGuiCol_TableHeaderBg});
    set(b.sunken, {ImGuiCol_FrameBg, ImGuiCol_ScrollbarBg});
    set(b.line, {ImGuiCol_Border, ImGuiCol_TableBorderLight, ImGuiCol_Separator});
    set(b.line_strong, {ImGuiCol_TableBorderStrong, ImGuiCol_ScrollbarGrab, ImGuiCol_SeparatorHovered});
    set(b.muted, {ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive});
    set(b.button, {ImGuiCol_Button, ImGuiCol_Header});
    set(b.hover, {ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered, ImGuiCol_FrameBgHovered, ImGuiCol_ResizeGripHovered});
    set(b.selected, {ImGuiCol_ButtonActive, ImGuiCol_HeaderActive, ImGuiCol_FrameBgActive});
    set(b.accent_soft, {ImGuiCol_TextSelectedBg});
    set(b.accent, {ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_PlotLines, ImGuiCol_PlotHistogram,
        ImGuiCol_NavCursor, ImGuiCol_SeparatorActive, ImGuiCol_ResizeGripActive});
    s.Colors[ImGuiCol_ModalWindowDimBg] = p.theme == Theme::light ? color(0x0F1E2E, 0.28f) : color(0x000000, 0.55f);
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

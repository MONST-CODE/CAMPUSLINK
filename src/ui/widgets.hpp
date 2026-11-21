#pragma once
// Small, stateless presentation components built on Dear ImGui and the design tokens.
// They draw; they never decide. Every state they show comes from the caller.
#include "appearance.hpp"
#include "imgui.h"
#include <algorithm>
#include <span>
#include <string_view>

namespace campuslink::ui::widget {
inline float px(float v) { return v * ImGui::GetStyle().FontScaleDpi; }
inline float body_size() { return ImGui::GetStyle().FontSizeBase; }

inline void text(std::string_view s) { ImGui::TextUnformatted(s.data(), s.data() + s.size()); }
inline void wrapped(std::string_view s) { ImGui::PushTextWrapPos(0); text(s); ImGui::PopTextWrapPos(); }
inline void colored(const ImVec4& c, std::string_view s, bool wrap = true) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (wrap) wrapped(s); else text(s);
    ImGui::PopStyleColor();
}
inline void sized(float scale, std::string_view s, const ImVec4* c = nullptr, bool wrap = true) {
    ImGui::PushFont(nullptr, body_size() * scale);
    if (c) colored(*c, s, wrap); else if (wrap) wrapped(s); else text(s);
    ImGui::PopFont();
}
inline void display(const Palette& p, std::string_view s) { sized(type::display, s, &p.text); }
inline void title(const Palette& p, std::string_view s) { sized(type::title, s, &p.text); }
inline void heading(const Palette& p, std::string_view s) { sized(type::heading, s, &p.text); }
inline void caption(const Palette& p, std::string_view s, bool wrap = true) { sized(type::caption, s, &p.muted, wrap); }
inline void gap(float v = space::md) { ImGui::Dummy({0, px(v)}); }

// Section label: small muted caption with spacing above.
inline void section(const Palette& p, std::string_view s) {
    gap(space::sm);
    caption(p, s, false);
    ImGui::Dummy({0, px(2)});
}

// Card container. Auto-height unless a height is given. Returns the visible flag of BeginChild.
inline bool begin_card(const Palette& p, const char* id, float height = 0, bool tint = false) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint ? p.raised : p.surface);
    ImGui::PushStyleColor(ImGuiCol_Border, p.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(space::lg), px(space::lg - 2)});
    const auto flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (height <= 0 ? ImGuiChildFlags_AutoResizeY : 0);
    const bool open = ImGui::BeginChild(id, {0, height > 0 ? height : 0}, flags, height > 0 ? 0 : ImGuiWindowFlags_NoScrollbar);
    return open;
}
inline void end_card() { ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(2); }

enum class Kind { primary, secondary, ghost, danger, link };
inline bool button(const Palette& p, const char* label, Kind kind = Kind::secondary, ImVec2 size = {0, 0}) {
    ImVec4 bg = p.button, hover = p.hover, active = p.selected, fg = p.text;
    switch (kind) {
    case Kind::primary: bg = p.accent; hover = p.accent_hover; active = p.accent_active; fg = p.on_accent; break;
    case Kind::danger: bg = p.danger.soft; hover = p.danger.soft; active = p.danger.soft; fg = p.danger.strong; break;
    case Kind::ghost: bg = {0, 0, 0, 0}; hover = p.button; active = p.hover; fg = p.accent_text; break;
    case Kind::link: bg = hover = active = {0, 0, 0, 0}; fg = p.accent_text; break;
    case Kind::secondary: break;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, bg); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active); ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kind == Kind::link ? ImVec2{0, px(2)} : ImVec2{px(space::lg), px(space::sm + 1)});
    const bool pressed = ImGui::Button(label, size);
    if (kind == Kind::link && ImGui::IsItemHovered()) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine({a.x, b.y - px(1)}, {b.x, b.y - px(1)}, ImGui::GetColorU32(fg), px(1));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
    return pressed;
}

// Pill badge on the current line.
inline void badge(const Tone& t, std::string_view label) {
    ImGui::PushFont(nullptr, body_size() * type::caption);
    const auto size = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const ImVec2 pad{px(8), px(3)};
    const auto pos = ImGui::GetCursorScreenPos();
    const ImVec2 box{size.x + pad.x * 2, size.y + pad.y * 2};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + box.x, pos.y + box.y}, ImGui::GetColorU32(t.soft), box.y * 0.5f);
    draw->AddText({pos.x + pad.x, pos.y + pad.y}, ImGui::GetColorU32(t.strong), label.data(), label.data() + label.size());
    ImGui::Dummy(box);
    ImGui::PopFont();
}

// Four bars from scan RSSI (dBm). Reported signal, not throughput.
inline int bars_from_rssi(int rssi) { return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0; }
inline void signal_bars(const Palette& p, int rssi, float height = 0) {
    const float h = height > 0 ? height : ImGui::GetTextLineHeight();
    const float w = px(3.5f), spacing = px(2.f);
    const auto pos = ImGui::GetCursorScreenPos();
    const int lit = bars_from_rssi(rssi);
    auto* draw = ImGui::GetWindowDrawList();
    const auto on = ImGui::GetColorU32(lit >= 3 ? p.accent : lit == 2 ? p.warning.strong : p.danger.strong);
    const auto off = ImGui::GetColorU32(p.line_strong);
    for (int i = 0; i < 4; ++i) {
        const float bh = h * (0.35f + 0.65f * (i + 1) / 4.f);
        const ImVec2 a{pos.x + i * (w + spacing), pos.y + h - bh}, b{a.x + w, pos.y + h};
        draw->AddRectFilled(a, b, i < lit ? on : off, px(1));
    }
    ImGui::Dummy({4 * w + 3 * spacing, h});
}

// Status banner with an accent rule on the left.
inline void banner(const Tone& t, const Palette& p, std::string_view headline, std::string_view body = {}) {
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    auto* draw = ImGui::GetWindowDrawList();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    ImGui::Indent(px(space::lg));
    ImGui::Dummy({0, px(space::sm + 2)});
    ImGui::PushTextWrapPos(start.x + width - px(space::lg) - ImGui::GetWindowPos().x);
    sized(body.empty() ? type::body : type::heading, headline, &t.strong, true);
    if (!body.empty()) { ImGui::Dummy({0, px(2)}); ImGui::PushStyleColor(ImGuiCol_Text, p.text); text(body); ImGui::PopStyleColor(); }
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, px(space::sm + 2)});
    ImGui::Unindent(px(space::lg));
    const auto end = ImGui::GetCursorScreenPos();
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(start, {start.x + width, end.y}, ImGui::GetColorU32(t.soft), px(8));
    draw->AddRectFilled(start, {start.x + px(4), end.y}, ImGui::GetColorU32(t.strong), px(8), ImDrawFlags_RoundCornersLeft);
    draw->ChannelsMerge();
    ImGui::Dummy({0, px(2)});
}

// Label/value rows aligned in two columns.
inline bool begin_fields(const char* id, float label_width = 150) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, px(label_width));
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
inline void field(const Palette& p, std::string_view label, std::string_view value, bool mono = false, ImFont* mono_font = nullptr) {
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    colored(p.muted, label, false);
    ImGui::TableNextColumn();
    if (mono && mono_font) ImGui::PushFont(mono_font, body_size() * 0.95f);
    wrapped(value);
    if (mono && mono_font) ImGui::PopFont();
}
inline void end_fields() { ImGui::EndTable(); }

// Horizontal progress steps. `current` is the active step; `done` steps before it are complete.
enum class StepState { pending, active, done, failed };
inline void stepper(const Palette& p, std::span<const char* const> labels, std::span<const StepState> states) {
    const float width = ImGui::GetContentRegionAvail().x;
    const auto n = static_cast<float>(labels.size());
    const float slot = width / n, r = px(7);
    const auto origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float cy = origin.y + r + px(2);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const float cx = origin.x + slot * (static_cast<float>(i) + 0.5f);
        if (i + 1 < labels.size()) {
            const bool filled = states[i] == StepState::done;
            draw->AddLine({cx + r + px(4), cy}, {cx + slot - r - px(4), cy}, ImGui::GetColorU32(filled ? p.accent : p.line_strong), px(2));
        }
        switch (states[i]) {
        case StepState::done: draw->AddCircleFilled({cx, cy}, r, ImGui::GetColorU32(p.accent)); break;
        case StepState::active:
            draw->AddCircleFilled({cx, cy}, r, ImGui::GetColorU32(p.accent_soft));
            draw->AddCircle({cx, cy}, r, ImGui::GetColorU32(p.accent), 0, px(2.5f));
            draw->AddCircleFilled({cx, cy}, r * 0.4f, ImGui::GetColorU32(p.accent));
            break;
        case StepState::failed: draw->AddCircleFilled({cx, cy}, r, ImGui::GetColorU32(p.danger.strong)); break;
        case StepState::pending: draw->AddCircle({cx, cy}, r, ImGui::GetColorU32(p.line_strong), 0, px(2)); break;
        }
        ImGui::PushFont(nullptr, body_size() * type::caption);
        const char* label = labels[i];
        const auto size = ImGui::CalcTextSize(label, nullptr, false, slot - px(6));
        const auto tone = states[i] == StepState::pending ? p.muted : states[i] == StepState::failed ? p.danger.strong : p.text;
        ImGui::SetCursorScreenPos({cx - std::min(size.x, slot - px(6)) * 0.5f, cy + r + px(6)});
        ImGui::PushStyleColor(ImGuiCol_Text, tone);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + slot - px(6));
        ImGui::TextUnformatted(label);
        ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, r * 2 + px(8) + ImGui::GetTextLineHeight() * 2.1f});
}
}

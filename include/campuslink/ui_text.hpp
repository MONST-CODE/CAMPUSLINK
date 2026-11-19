#pragma once
#include "campuslink/ui_preferences.hpp"
#include <array>
#include <string>
#include <string_view>

namespace campuslink::ui {
enum class Key {
#define CL_TEXT_KEY(key, en, fr) key,
#include "campuslink/ui_strings.inc"
#undef CL_TEXT_KEY
};
struct Translation { const char* en; const char* fr; };
inline constexpr auto catalog = std::array{
#define CL_TEXT_KEY(key, en, fr) Translation{en, fr},
#include "campuslink/ui_strings.inc"
#undef CL_TEXT_KEY
};
inline constexpr const char* english(Key key) { return catalog[static_cast<unsigned>(key)].en; }
inline constexpr const char* translate(Language lang, Key key) {
    const auto& item = catalog[static_cast<unsigned>(key)];
    return lang == Language::fr ? item.fr : item.en;
}
inline constexpr bool catalog_complete() {
    for (const auto& text : catalog) if (!*text.en || !*text.fr) return false;
    return true;
}
static_assert(catalog_complete());
// Only pass controlled diagnostic/status strings here, never SSIDs, profile names or file paths.
inline std::string localize(Language lang, std::string_view value) {
    if (lang == Language::en || value.empty()) return std::string(value);
    for (const auto& item : catalog) if (value == item.en) return item.fr;
    if (value.ends_with(english(Key::Running))) {
        const auto previous = value.substr(0, value.size() - std::string_view(english(Key::Running)).size());
        for (const auto& item : catalog) if (previous == item.en) return std::string(item.fr) + translate(lang, Key::Running);
    }
    // Compound diagnostics consist of fixed fragments around counts or native status codes.
    auto result = std::string(value);
    for (const auto& item : catalog) {
        const std::string_view en = item.en;
        if (en.ends_with(' ') || en.starts_with(' ') || en.starts_with(';')) {
            const auto at = result.find(en);
            if (at != result.npos) result.replace(at, en.size(), item.fr);
        }
    }
    if (const auto separator = value.find("; rollback"); separator != value.npos) {
        const auto before = value.substr(0, separator);
        // Localize the leading diagnosis without modifying the native API name.
        for (const auto& item : catalog) if (before == item.en) { result.replace(0, before.size(), item.fr); break; }
    }
    if (value.starts_with(english(Key::SimulationCapacity))) {
        const auto state = value.substr(std::string_view(english(Key::SimulationCapacity)).size());
        return std::string(translate(lang, Key::SimulationCapacity)) + localize(lang, state);
    }
    return result;
}
}

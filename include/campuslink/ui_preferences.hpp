#pragma once
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace campuslink::ui {
enum class Language { fr, en };
enum class Theme { light, dark };
enum class Typeface { inter, mono, fallback };
struct Preferences {
    Language language{Language::fr};
    Theme theme{Theme::light};
    Typeface font{Typeface::inter};
    int size{16};
    bool compact{};
    int width{1280}, height{800};
    bool operator==(const Preferences&) const = default;
};
inline std::string encode(const Preferences& value) {
    constexpr const char* fonts[]{"inter", "mono", "fallback"};
    return std::string("CAMPUSLINK_UI_V1\n") + (value.language == Language::fr ? "fr\n" : "en\n") +
        (value.theme == Theme::light ? "light\n" : "dark\n") + fonts[static_cast<int>(value.font)] + '\n' +
        std::to_string(value.size) + '\n' + (value.compact ? "1\n" : "0\n") +
        std::to_string(value.width) + '\n' + std::to_string(value.height) + '\n';
}
inline std::optional<Preferences> decode(std::string_view text) {
    const auto take = [&]() -> std::string_view {
        const auto i = text.find('\n');
        if (i == text.npos) return {};
        auto row = text.substr(0, i); text.remove_prefix(i + 1); return row;
    };
    const auto integer = [&](int& value, int lo, int hi) {
        const auto field = take();
        if (field.empty()) return false;
        const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
        return result.ec == std::errc{} && result.ptr == field.data() + field.size() && value >= lo && value <= hi;
    };
    Preferences prefs;
    if (take() != "CAMPUSLINK_UI_V1") return {};
    const auto language = take(), theme = take(), font = take();
    if (language == "en") prefs.language = Language::en; else if (language != "fr") return {};
    if (theme == "dark") prefs.theme = Theme::dark; else if (theme != "light") return {};
    if (font == "mono") prefs.font = Typeface::mono; else if (font == "fallback") prefs.font = Typeface::fallback; else if (font != "inter") return {};
    int compact{};
    if (!integer(prefs.size, 13, 24) || !integer(compact, 0, 1) || !integer(prefs.width, 720, 7680) || !integer(prefs.height, 480, 4320) || !text.empty()) return {};
    prefs.compact = compact != 0;
    return prefs;
}
struct LoadedPreferences { Preferences value; bool invalid{}; };
inline LoadedPreferences load_preferences(const std::filesystem::path& file) {
    if (file.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return {{}, bool(ec)};
    std::ifstream input(file, std::ios::binary);
    if (!input) return {{}, true};
    char buffer[257]{};
    input.read(buffer, sizeof buffer);
    if (input.bad() || input.gcount() == sizeof buffer) return {{}, true};
    const auto data = decode(std::string_view(buffer, static_cast<std::size_t>(input.gcount())));
    return {data.value_or(Preferences{}), !data};
}
inline bool save_preferences(const std::filesystem::path& file, const Preferences& prefs) {
    if (file.empty()) return false;
    auto temp = file; temp += ".tmp";
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    output << encode(prefs); output.close();
    std::error_code ec;
    if (output) {
        std::filesystem::rename(temp, file, ec);
        if (!ec) return true;
    }
    std::filesystem::remove(temp, ec);
    return false;
}
}

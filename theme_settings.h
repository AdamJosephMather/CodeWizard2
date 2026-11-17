#pragma once

#include "json.hpp"
#include "theme_style.h"
#include <cmath>
#include <string>

// Portable appearance settings, independent of the window and file backends.
namespace ThemeSettings {
using Json = nlohmann::json;
inline constexpr int fileVersion = 1;
inline constexpr std::array<const char*, 11> syntaxKeys = {
    "c_strings_color", "c_comments_color", "c_vars_color", "c_types_color",
    "c_functs_color", "c_keywords_color", "c_punctuation_color", "c_literals_color",
    "c_operator_color", "c_preproc_color", "c_invalid_color"
};

inline std::string italicKey(std::string key) {
    return key.substr(0, key.size()-6) + "_italic";
}

inline Json defaults(ThemePreset preset = ThemePreset::Existing, bool dark = true) {
    Json result = {{"c_theme", themePresetName(preset)}, {"dark_mode", dark},
        {"c_tint_color", "255,255,255"}, {"c_saturation", 0.6f},
        {"use_transparency", false}, {"opacity", 0.8f}};
    constexpr std::array<const char*, 11> colors = {
        "127,173,94", "127,132,142", "245,91,102", "85,169,237", "199,157,78",
        "176,95,199", "127,132,142", "194,127,64", "194,79,64", "64,194,120", "255,0,0"
    };
    for (size_t i = 0; i < syntaxKeys.size(); ++i) {
        result[syntaxKeys[i]] = colors[i];
        result[italicKey(syntaxKeys[i])] = i == 3 || i == 5 || i == 7 || i == 9 || i == 10;
    }
    return result;
}

inline bool normalizeColor(const std::string& text, std::string& result) {
    std::array<int,3> channels{};
    size_t pos = 0;
    auto spaces = [&] { while (pos < text.size() && (text[pos]==' ' || text[pos]=='\t')) ++pos; };
    for (int i = 0; i < 3; ++i) {
        spaces();
        const size_t start = pos;
        while (pos < text.size() && text[pos]>='0' && text[pos]<='9') {
            channels[i] = channels[i]*10 + text[pos++]-'0';
            if (channels[i]>255) return false;
        }
        if (pos == start) return false;
        spaces();
        if (i < 2 && (pos == text.size() || text[pos++] != ',')) return false;
    }
    if (pos != text.size()) return false;
    result = std::to_string(channels[0])+","+std::to_string(channels[1])+","+std::to_string(channels[2]);
    return true;
}

// Strict imports are validated completely before callers change live settings.
// Startup uses tolerant mode to fall back only for malformed saved fields.
inline bool normalize(const Json& input, Json& output, std::string& error,
                      ThemePreset fallback = ThemePreset::Existing, bool dark = true,
                      bool strict = true) {
    if (!input.is_object()) { error = "Theme must be a JSON object."; return false; }
    if (input.contains("theme_version") && (!input["theme_version"].is_number_integer() ||
            input["theme_version"] != fileVersion)) {
        error = "Unsupported theme file version."; return false;
    }
    Json candidate = defaults(fallback, dark);
    for (auto it = input.begin(); it != input.end(); ++it) {
        const std::string key = it.key();
        if (!candidate.contains(key)) continue; // Never import unrelated application settings.
        const auto& value = it.value();
        bool valid = false;
        if (key == "c_theme") {
            ThemePreset preset;
            valid = value.is_string() && parseThemePreset(value.get<std::string>(), preset);
            if (valid) candidate[key] = themePresetName(preset);
        } else if (candidate[key].is_boolean()) {
            valid = value.is_boolean();
            if (valid) candidate[key] = value;
        } else if (candidate[key].is_number()) {
            valid = value.is_number() && std::isfinite(value.get<double>()) &&
                    value.get<double>() >= 0.0 && value.get<double>() <= 1.0;
            if (valid) candidate[key] = value;
        } else {
            std::string color;
            valid = value.is_string() && normalizeColor(value.get<std::string>(), color);
            if (valid) candidate[key] = color;
        }
        if (!valid && strict) { error = "Invalid theme value for " + key + "."; return false; }
    }
    output = std::move(candidate);
    error.clear();
    return true;
}

inline bool parse(const std::string& text, Json& output, std::string& error) {
    try { return normalize(Json::parse(text), output, error); }
    catch (const Json::exception&) { error = "Theme file is not valid JSON."; return false; }
}

inline std::string serialize(Json values) {
    values["theme_version"] = fileVersion;
    return values.dump(2);
}
}

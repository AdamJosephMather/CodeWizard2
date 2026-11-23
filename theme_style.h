#pragma once

#include <algorithm>
#include <array>
#include <string_view>

// Defaults for installations without saved theme settings. Runtime choices
// and imported themes take precedence and persist across launches.
enum class ThemePreset { Existing, Square, Modern, Bubble };
inline constexpr std::array<ThemePreset, 4> themePresets = {
    ThemePreset::Existing, ThemePreset::Square, ThemePreset::Modern, ThemePreset::Bubble
};

inline constexpr const char* themePresetName(ThemePreset preset) {
    switch (preset) {
    case ThemePreset::Existing: return "Existing";
    case ThemePreset::Square: return "Square";
    case ThemePreset::Modern: return "Modern";
    case ThemePreset::Bubble: return "Bubble";
    }
    return "Modern";
}

inline bool parseThemePreset(std::string_view name, ThemePreset& result) {
    for (auto preset : themePresets) {
        const std::string_view expected = themePresetName(preset);
        if (name.size() != expected.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size(); ++i) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; };
            if (lower(name[i]) != lower(expected[i])) { same = false; break; }
        }
        if (same) { result = preset; return true; }
    }
    return false;
}
namespace ThemeBuild {
    inline constexpr ThemePreset preset = ThemePreset::Modern;
    inline constexpr bool build_dark_mode = true;
}

struct ThemeIntensities {
    float main_background;
    float extras_background;
    float hover_background;
    float darker_background;
    float overlay_background;
    float main_text;
    float lesser_text;
    float border;
    float active;
};

struct ThemeStyle {
    ThemeIntensities dark;
    ThemeIntensities light;
    float rounding; // Multiples of the classic font-scaled corner radius.
    float saturation_scale;
    
    float cornerRadius(float width, float height, float font_width, float font_height) const {
        // Match the original integer radius without coupling it to text padding.
        const float classic_radius = static_cast<int>(std::min(font_width, font_height) * 0.5f);
        return std::clamp(classic_radius * rounding, 0.0f,
                          std::max(0.0f, std::min(width, height) * 0.5f));
    }
};

inline constexpr ThemeStyle themeStyle(ThemePreset preset) {
    const ThemeStyle classic = {
        {0.098039f, 0.164706f, 0.26f, 0.05f, 0.12f, 1.0f, 0.392157f, 0.35f, 0.7f},
        {0.7f, 0.9f, 0.8f, 1.0f, 0.95f, 0.0f, 0.5f, 0.6f, 0.0f},
        1.0f, 1.0f
    };
    switch (preset) {
    case ThemePreset::Square: {
        ThemeStyle square = classic;
        square.rounding = 0.0f;
        return square;
    }
    case ThemePreset::Modern:
        return {
            {0.115f, 0.14f, 0.19f, 0.095f, 0.16f, 0.9f, 0.4f, 0.24f, 0.7f},
            {0.96f, 0.93f, 0.88f, 1.0f, 0.98f, 0.12f, 0.43f, 0.78f, 0.28f},
            2.0f, 0.4f
        };
    case ThemePreset::Bubble:
        return {
            {0.12f, 0.18f, 0.28f, 0.08f, 0.15f, 0.94f, 0.6f, 0.32f, 0.8f},
            {0.92f, 0.98f, 0.85f, 1.0f, 0.96f, 0.1f, 0.4f, 0.7f, 0.28f},
            4.0f, 0.65f
        };
    case ThemePreset::Existing:
    default:
        return classic;
    }
}

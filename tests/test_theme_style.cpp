#include "theme_style.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    const auto classic = themeStyle(ThemePreset::Existing);
    const auto square = themeStyle(ThemePreset::Square);
    const auto modern = themeStyle(ThemePreset::Modern);
    require(classic.cornerRadius(100, 30, 11, 20) == 5, "classic radius changed");
    require(square.cornerRadius(100, 30, 11, 20) == 0, "square button has rounding");
    require(modern.cornerRadius(100, 30, 11, 20) == 10, "modern radius is not larger");
    require(modern.saturation_scale < classic.saturation_scale, "modern tint is not softer");
    const auto bubble = themeStyle(ThemePreset::Bubble);
    require(bubble.cornerRadius(100, 30, 11, 20) == 15, "Bubble controls are not pill-shaped");

    for (auto preset : themePresets) {
        const auto style = themeStyle(preset);
        for (bool dark : {false, true}) {
            const auto& palette = dark ? style.dark : style.light;
            for (float value : {palette.main_background, palette.extras_background,
                    palette.hover_background, palette.darker_background,
                    palette.overlay_background, palette.main_text, palette.lesser_text,
                    palette.border, palette.active}) {
                require(std::isfinite(value) && value >= 0 && value <= 1, "invalid palette intensity");
            }
            require(dark ? palette.main_text > palette.main_background
                         : palette.main_text < palette.main_background, "incorrect text polarity");
        }
        for (float width : {-1.0f, 0.0f, 1.0f, 4.0f, 100.0f}) {
            for (float height : {-1.0f, 0.0f, 1.0f, 4.0f, 30.0f}) {
                const float radius = style.cornerRadius(width, height, 11, 20);
                require(radius >= 0 && radius <= std::max(0.0f, std::min(width, height) / 2),
                        "radius exceeds widget bounds");
            }
        }
        require(style.cornerRadius(100, 30, 0, 0) == 0, "zero font metrics have rounding");
    }
    require(modern.dark.hover_background - modern.dark.darker_background <
            classic.dark.hover_background - classic.dark.darker_background, "modern dark contrast is not softer");
    require(modern.light.darker_background - modern.light.hover_background <
            classic.light.darker_background - classic.light.hover_background, "modern light contrast is not softer");
    std::cout << "All eight theme palettes and corner bounds passed\n";
}

#include "theme_settings.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace ThemeSettings;
    for (auto preset : themePresets) {
        for (bool dark : {false, true}) {
            auto original = defaults(preset, dark);
            original["c_tint_color"] = "12,34,56";
            original["c_saturation"] = 0.37;
            original["opacity"] = 0.42;
            original["use_transparency"] = true;
            original["c_types_color"] = "9,8,7";
            original["c_types_italic"] = false;
            original["c_comments_italic"] = true;
            Json imported;
            std::string error;
            require(parse(serialize(original), imported, error), "theme round-trip failed");
            require(imported == original, "theme lost appearance settings during round-trip");
            Json restarted;
            require(normalize(imported, restarted, error, ThemePreset::Existing, !dark, false), "restart normalization failed");
            require(restarted == original, "saved selection or mode lost on restart");
            ThemePreset parsed{};
            require(parseThemePreset(imported["c_theme"].get<std::string>(), parsed) && parsed == preset, "wrong restored preset");
            for (const auto* key : syntaxKeys) {
                require(imported.contains(key) && imported.contains(italicKey(key)), "incomplete syntax export");
            }
        }
    }
    std::string error;
    Json output;
    require(parse(R"({"dark_mode":false,"c_strings_color":"1, 2, 3"})", output, error), "legacy theme rejected");
    require(output["c_theme"] == "Existing" && output["dark_mode"] == false, "legacy mode/preset incorrect");
    require(output["c_strings_color"] == "1,2,3", "RGB was not normalized");
    require(output["c_comments_color"] == defaults()["c_comments_color"], "legacy defaults were inherited from current theme");
    require(parse(R"({"c_theme":"mOdErN","font_path":"unrelated","current_folder":"unrelated"})",output,error), "case-insensitive preset rejected");
    require(output["c_theme"] == "Modern" && !output.contains("font_path") && !output.contains("current_folder"), "import included unrelated settings");

    const Json unchanged = output;
    for (const char* invalid : {
        "not json", "[]", "null", R"({"theme_version":2})", R"({"theme_version":"1"})",
        R"({"c_theme":"Unknown"})", R"({"c_theme":2})", R"({"dark_mode":"false"})",
        R"({"c_saturation":1.1})", R"({"c_saturation":-0.1})", R"({"opacity":null})",
        R"({"c_tint_color":"256,0,0"})", R"({"c_types_color":",0,0"})",
        R"({"c_types_color":"0,0,0,0"})", R"({"c_types_color":"999999999999999999999,0,0"})",
        R"({"c_types_italic":"yes"})"
    }) {
        require(!parse(invalid, output, error), "invalid theme accepted");
        require(!error.empty() && output == unchanged, "failed import partially changed settings");
    }
    Json bad = {{"c_saturation", std::numeric_limits<double>::infinity()}};
    require(!normalize(bad, output, error), "non-finite saturation accepted");
    require(normalize({{"c_theme","unknown"},{"dark_mode",false},{"c_saturation",0.4}},
        output,error,ThemePreset::Modern,true,false), "saved settings fallback failed");
    require(output["c_theme"]=="Modern" && output["dark_mode"]==false && output["c_saturation"]==0.4,
        "invalid saved field discarded valid preferences");
    std::cout << "Eight theme save/load/restart round-trips, defaults, legacy imports and atomic validation passed\n";
}

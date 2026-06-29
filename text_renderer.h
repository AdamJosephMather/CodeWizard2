#pragma once

#include "MonoString.cpp"
#include "helper_types.h"
#include <vector>
#include <functional>

// Font rendering system
class TextRenderer {
public:
	// Initialize the font system with a font file
	static bool init_font(const char* fontPath);
	
	// Draw text at specified position with per-glyph colors
	static void draw_text(float x, float y, const MST::MonoString& text, const std::vector<Color*>& colors, bool renderEmojis=true);
	static void draw_text(float x, float y, const MST::MonoString& text, Color* color, bool renderEmojis=true);
	static void draw_text(float x, float y, const MST::MonoString& text, uint8_t r, uint8_t g, uint8_t b, bool renderEmojis=true);
	static float draw_text_substring(float x, float y, const MST::MonoString& text, size_t start, size_t end, const std::vector<Color*>& colors, bool use_color_substring, bool renderEmojis=true);
	
	// Cleanup resources
	static void cleanup();
	
	// Set font size (call before init_font)
	static void set_font_size(float size);
	
	// Helpers
	static int get_text_width(int text_len);
	static int get_text_height();
	
	static std::function<void()> after_font_change;
};
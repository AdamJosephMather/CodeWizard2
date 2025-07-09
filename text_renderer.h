#pragma once

#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include "helper_types.h"
#include <vector>

// Font rendering system
class TextRenderer {
public:
	// Initialize the font system with a font file
	static bool init_font(const char* fontPath);
	
	// Draw text at specified position with per-glyph colors
	static void draw_text(float x, float y, const icu::UnicodeString& unicodeStr, const std::vector<Color*>& colors);
	
	// Cleanup resources
	static void cleanup();
	
	// Set font size (call before init_font)
	static void set_font_size(float size);
	
	// Helpers
	static int get_text_width(int text_len);
	static int get_text_height();
};
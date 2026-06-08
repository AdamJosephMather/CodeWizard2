#pragma once

#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include "EmojiRenderer.h"
#include "helper_types.h"
#include <vector>
#include <functional>

// Font rendering system
class TextRenderer {
public:
	// Initialize the font system with a font file
	static bool init_font(const char* fontPath);
	
	// Draw text at specified position with per-glyph colors
	static void draw_text(float x, float y, const icu::UnicodeString& unicodeStr, const std::vector<Color*>& colors, bool renderEmojis=true);
	static void draw_text(float x, float y, const icu::UnicodeString& unicodeStr, Color* color, bool renderEmojis=true);
	static void draw_text(float x, float y, const icu::UnicodeString& unicodeStr, uint8_t r, uint8_t g, uint8_t b, bool renderEmojis=true);
	
	static bool try_get_emoji_sequence(const icu::UnicodeString& str, int32_t index, icu::UnicodeString& out_sequence);
	static bool try_get_keycap_sequence(const icu::UnicodeString& str, int32_t index, icu::UnicodeString& out_sequence);
	
	// Cleanup resources
	static void cleanup();
	
	// Set font size (call before init_font)
	static void set_font_size(float size);
	
	// Helpers
	static int get_text_width(int text_len);
	static int get_text_height();
	
	static std::function<void()> after_font_change;
};
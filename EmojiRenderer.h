#pragma once

#include <unicode/unistr.h>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

#include <GL/gl.h>

struct EmojiTexture {
	GLuint id = 0;
	int width = 0;
	int height = 0;
};

class EmojiRenderer {
public:
	EmojiRenderer();
	~EmojiRenderer();

	// Renders the emoji at the specified coordinates
	void draw_emoji(const icu::UnicodeString& emojiSeq, float x, float y, float size);

private:
	// Fetches from cache, or asks the OS to render it
	EmojiTexture get_or_create_texture(const icu::UnicodeString& emojiSeq, float size);
	
	std::unordered_map<std::string, EmojiTexture> texture_cache;

#ifdef _WIN32
	// Windows COM Pointers for offscreen rendering
	Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory;
	Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory;
	Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;

	bool init_windows_api();
#endif
};
#include "EmojiRenderer.h"
#include <iostream>

#ifdef _WIN32
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

EmojiRenderer::EmojiRenderer() {
#ifdef _WIN32
	init_windows_api();
#endif
}

EmojiRenderer::~EmojiRenderer() {
	for (auto& pair : texture_cache) {
		glDeleteTextures(1, &pair.second.id);
	}
	texture_cache.clear();
}

void EmojiRenderer::draw_emoji(const icu::UnicodeString& emojiSeq, float x, float y, float size) {
	EmojiTexture tex = get_or_create_texture(emojiSeq, size);
	if (tex.id == 0) return;

	// Save OpenGL state
	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex.id);
	
	glEnable(GL_BLEND);
	// Use pre-multiplied alpha blending because Windows WIC renders PBGRA
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); 

	// Draw the textured quad
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Ensure color doesn't tint the emoji
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(x + size, y);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(x + size, y + size);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + size);
	glEnd();

	// Restore state
	if (!wasBlendEnabled) glDisable(GL_BLEND);
	else glBlendFunc(oldBlendSrc, oldBlendDst);
	if (!wasTexEnabled) glDisable(GL_TEXTURE_2D);
}

EmojiTexture EmojiRenderer::get_or_create_texture(const icu::UnicodeString& emojiSeq, float size) {
	// Generate a cache key (UTF-8 string + size)
	std::string key;
	emojiSeq.toUTF8String(key);
	key += "_" + std::to_string((int)size);

	auto it = texture_cache.find(key);
	if (it != texture_cache.end()) {
		return it->second;
	}

	EmojiTexture newTex;
	newTex.width = (int)size;
	newTex.height = (int)size;

#ifdef _WIN32
	if (!d2d_factory || !dwrite_factory || !wic_factory) {
		return newTex; // API failed to init
	}

	// 1. Convert ICU string to Windows UTF-16
	std::wstring utf16Str(reinterpret_cast<const wchar_t*>(emojiSeq.getBuffer()), emojiSeq.length());

	// 2. Create an offscreen WIC Bitmap (Premultiplied BGRA)
	Microsoft::WRL::ComPtr<IWICBitmap> wic_bitmap;
	wic_factory->CreateBitmap(newTex.width, newTex.height, 
							  GUID_WICPixelFormat32bppPBGRA, 
							  WICBitmapCacheOnDemand, &wic_bitmap);

	// 3. Create D2D Render Target mapping to the WIC Bitmap
	D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT
	);

	Microsoft::WRL::ComPtr<ID2D1RenderTarget> render_target;
	d2d_factory->CreateWicBitmapRenderTarget(wic_bitmap.Get(), &rtProps, &render_target);

	// 4. Set up the Font (Segoe UI Emoji is the Windows standard for color emojis)
	Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format;
	dwrite_factory->CreateTextFormat(
		L"Segoe UI Emoji", nullptr,
		DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		size * 0.85f, // Slight scale down to ensure it fits the box
		L"en-us", &text_format
	);
	
	text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
	render_target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush);

	// 5. Draw the Emoji!
	render_target->BeginDraw();
	render_target->Clear(D2D1::ColorF(0, 0, 0, 0)); // Transparent background
	
	D2D1_RECT_F layoutRect = D2D1::RectF(0.0f, 0.0f, (float)newTex.width, (float)newTex.height);
	render_target->DrawTextW(
		utf16Str.c_str(), utf16Str.length(),
		text_format.Get(), layoutRect, brush.Get(),
		D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT // CRITICAL for emojis
	);
	render_target->EndDraw();

	// 6. Extract the raw pixels from the WIC Bitmap
	WICRect rect = { 0, 0, newTex.width, newTex.height };
	std::vector<uint8_t> pixels(newTex.width * newTex.height * 4);
	wic_bitmap->CopyPixels(&rect, newTex.width * 4, pixels.size(), pixels.data());

	// 7. Upload to OpenGL
	glGenTextures(1, &newTex.id);
	glBindTexture(GL_TEXTURE_2D, newTex.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	// Note: WIC outputs BGRA, so we tell OpenGL to expect GL_BGRA_EXT
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, newTex.width, newTex.height, 
				 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());
#endif

	texture_cache[key] = newTex;
	return newTex;
}

#ifdef _WIN32
bool EmojiRenderer::init_windows_api() {
	HRESULT hr;

	hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		d2d_factory.GetAddressOf()
	);
	if (FAILED(hr)) return false;

	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf())
	);
	if (FAILED(hr)) return false;

	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(wic_factory.GetAddressOf())
	);
	if (FAILED(hr)) return false;

	return true;
}
#endif
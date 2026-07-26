#include "EmojiRenderer.h"
#include <vector>

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
#ifdef _WIN32
	if (wic_factory) wic_factory->Release();
	if (dwrite_factory) dwrite_factory->Release();
	if (d2d_factory) d2d_factory->Release();
#endif
}

void EmojiRenderer::draw_emoji(const std::string& emojiSeq, float x, float y, float size) {
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

EmojiTexture EmojiRenderer::get_or_create_texture(std::string emojiUtf8, float size) {
	// Cache key = UTF-8 emoji sequence + size
	std::string cacheKey = emojiUtf8 + "_" + std::to_string((int)size);

	auto it = texture_cache.find(cacheKey);
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

	// 1. Convert UTF-8 std::string to Windows UTF-16
	int utf16Len = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		emojiUtf8.data(),
		(int)emojiUtf8.size(),
		nullptr,
		0
	);

	if (utf16Len <= 0) {
		return newTex; // Invalid UTF-8 or conversion failed
	}

	std::wstring utf16Str;
	utf16Str.resize(utf16Len);

	MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		emojiUtf8.data(),
		(int)emojiUtf8.size(),
		utf16Str.data(),
		utf16Len
	);

	// 2. Create an offscreen WIC Bitmap (Premultiplied BGRA)
	IWICBitmap* wic_bitmap = nullptr;
	HRESULT hr = wic_factory->CreateBitmap(
		newTex.width,
		newTex.height,
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapCacheOnDemand,
		&wic_bitmap
	);
	if (FAILED(hr) || !wic_bitmap) return newTex;

	// 3. Create D2D Render Target mapping to the WIC Bitmap
	D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED
		),
		0.0f,
		0.0f,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	);

	ID2D1RenderTarget* render_target = nullptr;
	hr = d2d_factory->CreateWicBitmapRenderTarget(
		wic_bitmap,
		&rtProps,
		&render_target
	);
	if (FAILED(hr) || !render_target) {
		wic_bitmap->Release();
		return newTex;
	}

	// 4. Set up the Font
	IDWriteTextFormat* text_format = nullptr;
	hr = dwrite_factory->CreateTextFormat(
		L"Segoe UI Emoji",
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		size * 0.85f,
		L"en-us",
		&text_format
	);
	if (FAILED(hr) || !text_format) {
		render_target->Release();
		wic_bitmap->Release();
		return newTex;
	}

	text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	ID2D1SolidColorBrush* brush = nullptr;
	hr = render_target->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF::White),
		&brush
	);
	if (FAILED(hr) || !brush) {
		text_format->Release();
		render_target->Release();
		wic_bitmap->Release();
		return newTex;
	}

	// 5. Draw the Emoji
	render_target->BeginDraw();
	render_target->Clear(D2D1::ColorF(0, 0, 0, 0));

	D2D1_RECT_F layoutRect = D2D1::RectF(
		0.0f,
		0.0f,
		(float)newTex.width,
		(float)newTex.height
	);

	render_target->DrawTextW(
		utf16Str.c_str(),
		(UINT32)utf16Str.length(),
		text_format,
		layoutRect,
		brush,
		D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
	);

	render_target->EndDraw();

	// 6. Extract the raw pixels from the WIC Bitmap
	WICRect rect = { 0, 0, newTex.width, newTex.height };
	std::vector<uint8_t> pixels(newTex.width * newTex.height * 4);

	wic_bitmap->CopyPixels(
		&rect,
		newTex.width * 4,
		(UINT)pixels.size(),
		pixels.data()
	);

	brush->Release();
	text_format->Release();
	render_target->Release();
	wic_bitmap->Release();

	// 7. Upload to OpenGL
	glGenTextures(1, &newTex.id);
	glBindTexture(GL_TEXTURE_2D, newTex.id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// WIC outputs BGRA, so tell OpenGL to expect BGRA input.
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA,
		newTex.width,
		newTex.height,
		0,
		GL_BGRA_EXT,
		GL_UNSIGNED_BYTE,
		pixels.data()
	);
#endif

	texture_cache[cacheKey] = newTex;
	return newTex;
}

#ifdef _WIN32
bool EmojiRenderer::init_windows_api() {
	HRESULT hr;

	hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		&d2d_factory
	);
	if (FAILED(hr)) return false;

	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(&dwrite_factory)
	);
	if (FAILED(hr)) return false;

	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&wic_factory)
	);
	if (FAILED(hr)) return false;

	return true;
}
#endif

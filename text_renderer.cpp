#include "text_renderer.h"
#include "EmojiRenderer.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <ft2build.h>
#include FT_FREETYPE_H

std::function<void()> TextRenderer::after_font_change = nullptr;

struct PackedChar {
	unsigned short x0, y0, x1, y1; // bounding box in atlas
	float xoff, yoff, xadvance;    // layout metrics (PX)
};

struct AlignedQuad {
	float x0, y0, x1, y1; // pixel coords
	float s0, t0, s1, t1; // tex coords
};

static void GetPackedQuad(const std::vector<PackedChar>& pc,
						  int tex_w, int tex_h,
						  int char_index,
						  float* x, float* y,
						  AlignedQuad* q,
						  bool align_to_integer)
{
	const PackedChar& b = pc[char_index];
	float ipw = 1.0f / tex_w;
	float iph = 1.0f / tex_h;

	float rx = *x + b.xoff;
	float ry = *y + b.yoff;
	if (align_to_integer) {
		rx = std::floor(rx + 0.5f);
		ry = std::floor(ry + 0.5f);
	}

	float w = b.x1 - b.x0;
	float h = b.y1 - b.y0;

	q->x0 = rx;      q->y0 = ry;
	q->x1 = rx + w;  q->y1 = ry + h;

	q->s0 = b.x0 * ipw; q->t0 = b.y0 * iph;
	q->s1 = b.x1 * ipw; q->t1 = b.y1 * iph;

	*x += b.xadvance; // advance cursor
}

struct RangeInfo {
	int first;   // first Unicode code‑point
	int count;   // number of code‑points
	int offset;  // starting index in cdata[]
};

static FT_Library ftLib = nullptr;     // shared FreeType handle
static FT_Face    ftFace = nullptr;    // font face currently active
static float      font_size = 19.0f;   // px
static std::vector<PackedChar> cdataRegular;
static std::vector<PackedChar> cdataItalic;

static GLuint fontTexRegular = 0;
static GLuint fontTexItalic  = 0;

static std::vector<RangeInfo> packedRanges;

const int TEX_W = 2048, TEX_H = 2048;
int TEXT_WIDTH  = 1;  // monospace char cell
int TEXT_HEIGHT = 1;
int ascent_px   = 0;
EmojiRenderer* emoji_renderer;

static std::unordered_map<int, int> packedIndexByCp;

static int lookup_packedchar_index(int cp)
{
	auto it = packedIndexByCp.find(cp);
	if (it == packedIndexByCp.end()) return -1;
	return it->second;
}

void TextRenderer::set_font_size(float sz) { font_size = sz; }

static bool build_font_atlas(bool italic,
							 std::vector<PackedChar>& outCData,
							 GLuint& outTex) {
	std::vector<unsigned char> atlas(TEX_W * TEX_H, 0);

	struct Range { int first, count; };
	std::vector<Range> ranges = {
		{ 0x0020, 0x0060 },
		{ 0x00A0, 0x0060 },
		{ 0x0100, 0x0080 },
		{ 0x0180, 0x00D0 },
		{ 0x0300, 0x0070 },
		{ 0x2000, 0x0080 },
		{ 0x2E00, 0x0080 },
		{ 0x20A0, 0x0030 },
		{ 0x2100, 0x0050 },
		{ 0x2150, 0x0040 },
		{ 0x2190, 0x00F0 },
		{ 0x27F0, 0x0010 },
		{ 0x2900, 0x0080 },
		{ 0x2B00, 0x0100 },
		{ 0x2200, 0x0100 },
		{ 0x2300, 0x0100 },
		{ 0x2580, 0x0020 },
		{ 0x2500, 0x0080 },
		{ 0x2460, 0x009F },
		{ 0x2070, 0x0030 },
		{ 0x2600, 0x0100 },
		{ 0x2700, 0x00C0 },
		{ 0x0400, 0x0100 },
		{ 0x0370, 0x0090 },
		{ 0xFFFD, 0x0001 },
		{ 0x25A0, 0x0060 },
	};

	int totalCandidateGlyphs = 0;
	for (auto& r : ranges) {
		totalCandidateGlyphs += r.count;
	}

	outCData.clear();
	outCData.reserve(totalCandidateGlyphs);

	if (!italic) {
		packedIndexByCp.clear();
		packedIndexByCp.reserve(totalCandidateGlyphs);
	}

	int pen_x = 2;
	int pen_y = 2;
	int row_h = 0;

	for (const auto& r : ranges) {
		for (int cp = r.first; cp < r.first + r.count; ++cp) {
			if (cp >= 0xFE00 && cp <= 0xFE0F) {
				continue;
			}

			FT_UInt glyph_index = FT_Get_Char_Index(ftFace, static_cast<FT_ULong>(cp));
			if (glyph_index == 0) {
				continue;
			}

			FT_Matrix matrix;
			matrix.xx = 1L << 16;
			matrix.xy = italic ? static_cast<FT_Fixed>(0.25f * 65536.0f) : 0;
			matrix.yx = 0;
			matrix.yy = 1L << 16;

			FT_Set_Transform(ftFace, &matrix, nullptr);

			if (FT_Load_Glyph(ftFace, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) {
				continue;
			}

			FT_GlyphSlot g = ftFace->glyph;

			int gw = g->bitmap.width;
			int gh = g->bitmap.rows;

			if (gw > 0 && gh > 0) {
				if (pen_x + gw + 2 >= TEX_W) {
					pen_x = 2;
					pen_y += row_h + 2;
					row_h = 0;
				}

				if (pen_y + gh + 2 >= TEX_H) {
					std::cerr << "Font atlas overflow - increase TEX_W/H or trim ranges\n";
					FT_Set_Transform(ftFace, nullptr, nullptr);
					return false;
				}
			}

			PackedChar pc = {};

			if (gw > 0 && gh > 0) {
				for (int y = 0; y < gh; ++y) {
					unsigned char* dst = &atlas[(pen_y + y) * TEX_W + pen_x];
					const unsigned char* src = g->bitmap.buffer + y * g->bitmap.pitch;
					std::memcpy(dst, src, gw);
				}

				pc.x0 = static_cast<unsigned short>(pen_x);
				pc.y0 = static_cast<unsigned short>(pen_y);
				pc.x1 = static_cast<unsigned short>(pen_x + gw);
				pc.y1 = static_cast<unsigned short>(pen_y + gh);

				pen_x += gw + 2;
				if (gh > row_h) {
					row_h = gh;
				}
			} else {
				pc.x0 = static_cast<unsigned short>(pen_x);
				pc.y0 = static_cast<unsigned short>(pen_y);
				pc.x1 = static_cast<unsigned short>(pen_x);
				pc.y1 = static_cast<unsigned short>(pen_y);
			}

			pc.xoff = static_cast<float>(g->bitmap_left);
			pc.yoff = static_cast<float>(-g->bitmap_top);

			// Keep monospaced layout stable. Use the original advance.
			pc.xadvance = static_cast<float>(g->advance.x >> 6);

			int dstIdx = static_cast<int>(outCData.size());
			outCData.push_back(pc);

			// Only build the cp->index map once. Both atlases are generated in same order.
			if (!italic) {
				packedIndexByCp[cp] = dstIdx;
			}
		}
	}

	FT_Set_Transform(ftFace, nullptr, nullptr);

	std::vector<unsigned char> rgba(TEX_W * TEX_H * 4);
	for (int i = 0; i < TEX_W * TEX_H; ++i) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = atlas[i];
	}

	if (outTex == 0) {
		glGenTextures(1, &outTex);
	}

	glBindTexture(GL_TEXTURE_2D, outTex);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA,
		TEX_W,
		TEX_H,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		rgba.data()
	);

	// Important. NEAREST makes transformed or subpixel-positioned text look rough.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	return true;
}

bool TextRenderer::init_font(const char* fontPath) {
	std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
	if (!fontFile) {
		std::cerr << "Failed to load font\n";
		return false;
	}
	std::streamsize fsize = fontFile.tellg();
	fontFile.seekg(0, std::ios::beg);
	std::vector<unsigned char> buffer(fsize);
	if (!fontFile.read(reinterpret_cast<char*>(buffer.data()), fsize)) {
		std::cerr << "Failed to read font file\n";
		return false;
	}

	if (!ftLib) {
		if (FT_Init_FreeType(&ftLib) != 0) {
			std::cerr << "FT_Init_FreeType failed\n";
			return false;
		}
	}

	if (ftFace) {
		FT_Done_Face(ftFace);
		ftFace = nullptr;
	}
	if (FT_New_Memory_Face(ftLib, buffer.data(), static_cast<FT_Long>(buffer.size()), 0, &ftFace) != 0) {
		std::cerr << "FT_New_Memory_Face failed\n";
		return false;
	}

	// Request pixel size (height)
	if (FT_Set_Pixel_Sizes(ftFace, 0, static_cast<FT_UInt>(font_size)) != 0) {
		std::cerr << "FT_Set_Pixel_Sizes failed\n";
		return false;
	}

	if (!build_font_atlas(false, cdataRegular, fontTexRegular)) {
	return false;
}

	if (!build_font_atlas(true, cdataItalic, fontTexItalic)) {
		return false;
	}
	
	int sampleIdx = lookup_packedchar_index('Q');
	if (sampleIdx < 0) {
		sampleIdx = 0;
	}
	
	TEXT_WIDTH = static_cast<int>(cdataRegular[sampleIdx].xadvance + 0.5f);
	
	FT_Size_Metrics m = ftFace->size->metrics;
	ascent_px  = static_cast<int>(m.ascender  >> 6);
	int descent_px = static_cast<int>(m.descender >> 6);
	int line_gap_px = static_cast<int>((m.height - (m.ascender - m.descender)) >> 6);
	TEXT_HEIGHT = ascent_px - descent_px + line_gap_px;
	
	if (after_font_change) {
		after_font_change();
	}
	
	if (emoji_renderer) {
		delete emoji_renderer;
	}
	emoji_renderer = new EmojiRenderer();
	
	return true;
}

int TextRenderer::get_text_width(int len) { return TEXT_WIDTH * len; }
int TextRenderer::get_text_height()       { return TEXT_HEIGHT; }

void TextRenderer::draw_text(float x, float y,
							 const MST::MonoString& text,
							 const std::vector<Color*>& colors,
							 bool renderEmojis,
							 bool forceItalics) {
	y += ascent_px;

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	bool currentItalic = false;
	glBindTexture(GL_TEXTURE_2D, fontTexRegular);

	float cursorX = x;
	float cursorY = y;

	glBegin(GL_QUADS);

	for (int32_t i = 0; i < text.length; i++) {
		if (MST::skipIdx(text, i)) {
			cursorX += TEXT_WIDTH;
			continue;
		} else if (MST::isEmoji(text, i)) {
			float dist_right = 2 * TEXT_WIDTH;

			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT);

				glEnd();

				emoji_renderer->draw_emoji(
					MST::getEmoji(text, i),
					cursorX + (dist_right - emojiSize) / 2,
					cursorY - ascent_px + (TEXT_HEIGHT - emojiSize) / 2,
					emojiSize
				);

				glBindTexture(GL_TEXTURE_2D, currentItalic ? fontTexItalic : fontTexRegular);
				glBegin(GL_QUADS);
			}

			cursorX += TEXT_WIDTH;
			continue;
		}

		MST::u32 cp = MST::char32At(text, i);

		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);
		}

		if (idx >= 0) {
			const Color* col = colors[i];

			bool wantItalic = (col && col->italic) || forceItalics;
			if (wantItalic != currentItalic) {
				glEnd();

				currentItalic = wantItalic;
				glBindTexture(GL_TEXTURE_2D, currentItalic ? fontTexItalic : fontTexRegular);

				glBegin(GL_QUADS);
			}

			const std::vector<PackedChar>& glyphs = currentItalic ? cdataItalic : cdataRegular;

			AlignedQuad q;

			float cx_temp = cursorX;
			GetPackedQuad(glyphs, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);

			glColor4f(col->r, col->g, col->b, 1.0f);

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}

		cursorX += TEXT_WIDTH;
	}

	glEnd();

	if (!wasBlendEnabled) {
		glDisable(GL_BLEND);
	} else {
		glBlendFunc(oldBlendSrc, oldBlendDst);
	}

	if (!wasTexEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
}

float TextRenderer::draw_text_substring(float x, float y,
							 const MST::MonoString& text,
							 size_t start, size_t end,
							 const std::vector<Color*>& colors,
							 bool use_color_substring,
							 bool renderEmojis,
							 bool forceItalics) {
	if (start >= text.length) {
		return x;
	}

	if (text.length < end) {
		end = text.length;
	}

	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	bool currentItalic = false;
	glBindTexture(GL_TEXTURE_2D, fontTexRegular);

	float cursorX = x;
	float cursorY = y;

	glBegin(GL_QUADS);

	for (int32_t i = static_cast<int32_t>(start); i < static_cast<int32_t>(end); i++) {
		if (MST::skipIdx(text, i)) {
			cursorX += TEXT_WIDTH;
			continue;
		} else if (MST::isEmoji(text, i)) {
			float dist_right = 2 * TEXT_WIDTH;

			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT);

				glEnd();

				emoji_renderer->draw_emoji(
					MST::getEmoji(text, i),
					cursorX + (dist_right - emojiSize) / 2,
					cursorY - ascent_px + (TEXT_HEIGHT - emojiSize) / 2,
					emojiSize
				);

				glBindTexture(GL_TEXTURE_2D, currentItalic ? fontTexItalic : fontTexRegular);
				glBegin(GL_QUADS);
			}

			cursorX += TEXT_WIDTH;
			continue;
		}

		MST::u32 cp = MST::char32At(text, i);

		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);
		}

		if (idx >= 0) {
			const Color* col = colors[use_color_substring ? i : i - start];

			bool wantItalic = (col && col->italic) || forceItalics;
			if (wantItalic != currentItalic) {
				glEnd();

				currentItalic = wantItalic;
				glBindTexture(GL_TEXTURE_2D, currentItalic ? fontTexItalic : fontTexRegular);

				glBegin(GL_QUADS);
			}

			const std::vector<PackedChar>& glyphs = currentItalic ? cdataItalic : cdataRegular;

			AlignedQuad q;

			float cx_temp = cursorX;
			GetPackedQuad(glyphs, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);

			glColor4f(col->r, col->g, col->b, 1.0f);

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}

		cursorX += TEXT_WIDTH; // enforces monospaced
	}

	glEnd();

	if (!wasBlendEnabled) {
		glDisable(GL_BLEND);
	} else {
		glBlendFunc(oldBlendSrc, oldBlendDst);
	}

	if (!wasTexEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	return cursorX;
}

void TextRenderer::draw_text(float x, float y,
							 const MST::MonoString& text,
							 Color* color,
							 bool renderEmojis,
							 bool forceItalics) {
	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	bool useItalic = (color && color->italic) || forceItalics;
	glBindTexture(GL_TEXTURE_2D, useItalic ? fontTexItalic : fontTexRegular);

	const std::vector<PackedChar>& glyphs = useItalic ? cdataItalic : cdataRegular;

	float cursorX = x;
	float cursorY = y;

	glColor4f(color->r, color->g, color->b, 1.0f);

	glBegin(GL_QUADS);

	for (int32_t i = 0; i < text.length; i++) {
		if (MST::skipIdx(text, i)) {
			cursorX += TEXT_WIDTH;
			continue;
		} else if (MST::isEmoji(text, i)) {
			float dist_right = 2 * TEXT_WIDTH;

			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT);

				glEnd();

				emoji_renderer->draw_emoji(
					MST::getEmoji(text, i),
					cursorX + (dist_right - emojiSize) / 2,
					cursorY - ascent_px + (TEXT_HEIGHT - emojiSize) / 2,
					emojiSize
				);

				glBindTexture(GL_TEXTURE_2D, useItalic ? fontTexItalic : fontTexRegular);
				glColor4f(color->r, color->g, color->b, 1.0f);
				glBegin(GL_QUADS);
			}

			cursorX += TEXT_WIDTH;
			continue;
		}

		MST::u32 cp = MST::char32At(text, i);

		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);
		}

		if (idx >= 0) {
			AlignedQuad q;

			float cx_temp = cursorX;
			GetPackedQuad(glyphs, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}

		cursorX += TEXT_WIDTH; // enforces monospaced
	}

	glEnd();

	if (!wasBlendEnabled) {
		glDisable(GL_BLEND);
	} else {
		glBlendFunc(oldBlendSrc, oldBlendDst);
	}

	if (!wasTexEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
}

void TextRenderer::draw_text(float x, float y,
							 const MST::MonoString& text,
							 uint8_t r,
							 uint8_t g,
							 uint8_t b,
							 bool renderEmojis,
							 bool forceItalics) {
	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	if (forceItalics) {
		glBindTexture(GL_TEXTURE_2D, fontTexItalic);
	}else{
		glBindTexture(GL_TEXTURE_2D, fontTexRegular);
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float cursorX = x;
	float cursorY = y;

	glColor4f((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);

	glBegin(GL_QUADS);

	for (int32_t i = 0; i < text.length; i++) {
		if (MST::skipIdx(text, i)) {
			cursorX += TEXT_WIDTH;
			continue;
		} else if (MST::isEmoji(text, i)) {
			float dist_right = 2 * TEXT_WIDTH;

			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT);

				glEnd();

				emoji_renderer->draw_emoji(
					MST::getEmoji(text, i),
					cursorX + (dist_right - emojiSize) / 2,
					cursorY - ascent_px + (TEXT_HEIGHT - emojiSize) / 2,
					emojiSize
				);
				
				if (forceItalics) {
					glBindTexture(GL_TEXTURE_2D, fontTexItalic);
				}else{
					glBindTexture(GL_TEXTURE_2D, fontTexRegular);
				}
				glColor4f((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
				glBegin(GL_QUADS);
			}

			cursorX += TEXT_WIDTH;
			continue;
		}

		MST::u32 cp = MST::char32At(text, i);

		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);
		}

		if (idx >= 0) {
			AlignedQuad q;

			float cx_temp = cursorX;
			if (forceItalics) {
				GetPackedQuad(cdataItalic, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);
			}else{
				GetPackedQuad(cdataRegular, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);
			}

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}

		cursorX += TEXT_WIDTH; // enforces monospaced
	}

	glEnd();

	if (!wasBlendEnabled) {
		glDisable(GL_BLEND);
	} else {
		glBlendFunc(oldBlendSrc, oldBlendDst);
	}

	if (!wasTexEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
}

void TextRenderer::cleanup() {
	if (fontTexRegular) {
		glDeleteTextures(1, &fontTexRegular);
		fontTexRegular = 0;
	}

	if (fontTexItalic) {
		glDeleteTextures(1, &fontTexItalic);
		fontTexItalic = 0;
	}

	cdataRegular.clear();
	cdataItalic.clear();

	packedRanges.clear();
	packedIndexByCp.clear();

	if (emoji_renderer) {
		delete emoji_renderer;
		emoji_renderer = nullptr;
	}

	if (ftFace) {
		FT_Done_Face(ftFace);
		ftFace = nullptr;
	}
}
#include "text_renderer.h"
#include <unicode/brkiter.h>
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
static std::vector<PackedChar> cdata;  // packed glyph metadata
static std::vector<RangeInfo> packedRanges;
static GLuint fontTex = 0;

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

	std::vector<unsigned char> atlas(TEX_W * TEX_H, 0); // 8‑bit alpha

	struct Range { int first, count; };
	std::vector<Range> ranges = {
		{ 0x0020, 0x0060 }, // ASCII printable
		{ 0x00A0, 0x0060 }, // Latin-1 Supplement
		{ 0x0100, 0x0080 }, // Latin Extended-A
		{ 0x0180, 0x00D0 }, // Latin Extended-B
		{ 0x0300, 0x0070 }, // Combining Diacritical Marks
		{ 0x2000, 0x0080 }, // General Punctuation
		{ 0x2E00, 0x0080 }, // Supplemental Punctuation
		{ 0x20A0, 0x0030 }, // Currency Symbols
		{ 0x2100, 0x0050 }, // Letterlike Symbols (™, ℉, ♯, etc.)
		{ 0x2150, 0x0040 }, // Number Forms (⅓, ½, Ⅻ, etc.)
		{ 0x2190, 0x00F0 }, // Arrows
		{ 0x27F0, 0x0010 }, // Supplemental Arrows-A
		{ 0x2900, 0x0080 }, // Supplemental Arrows-B
		{ 0x2B00, 0x0100 }, // Miscellaneous Symbols & Arrows
		{ 0x2200, 0x0100 }, // Mathematical Operators
		{ 0x2300, 0x0100 }, // Miscellaneous Technical (⌂, ⌘, ⌚…)
		{ 0x2580, 0x0020 }, // Block Elements
		{ 0x2500, 0x0080 }, // Box Drawing
		{ 0x2460, 0x009F }, // Enclosed Alphanumerics (①, ②…Ⓐ, Ⓑ…)
		{ 0x2070, 0x0030 }, // Superscripts & Subscripts
		{ 0x2600, 0x0100 }, // Miscellaneous Symbols (☀, ♫, ❤…)
		{ 0x2700, 0x00C0 }, // Dingbats (✂, ✔, ✉…)
		{ 0x0400, 0x0100 }, // Cyrillic
		{ 0x0370, 0x0090 }, // Greek and Coptic
		{ 0xFFFD, 0x0001 }, // Replacement Character
		{ 0x25A0, 0x0060 }, // Geometric Shapes (U+25A0–U+25FF), includes □ (U+25A1)
	};

	int totalCandidateGlyphs = 0;
	for (auto& r : ranges) totalCandidateGlyphs += r.count;
	
	cdata.clear();
	cdata.reserve(totalCandidateGlyphs);
	
	packedIndexByCp.clear();
	packedIndexByCp.reserve(totalCandidateGlyphs);

	// Simple skyline/shelf packing ‑ not optimal but robust for mono‑sized glyphs
	int pen_x = 1; // 1‑pixel padding around glyphs
	int pen_y = 1;
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
	
			if (FT_Load_Glyph(ftFace, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) {
				continue;
			}
	
			FT_GlyphSlot g = ftFace->glyph;
	
			int gw = g->bitmap.width;
			int gh = g->bitmap.rows;
	
			// Row wrap. Zero-size glyphs like space are valid and should still be stored.
			if (gw > 0 && gh > 0) {
				if (pen_x + gw + 1 >= TEX_W) {
					pen_x = 1;
					pen_y += row_h + 1;
					row_h = 0;
				}
	
				if (pen_y + gh + 1 >= TEX_H) {
					std::cerr << "Font atlas overflow - increase TEX_W/H or trim ranges\n";
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
	
				pen_x += gw + 1;
				if (gh > row_h) row_h = gh;
			} else {
				// Valid glyph with no bitmap, e.g. space.
				pc.x0 = static_cast<unsigned short>(pen_x);
				pc.y0 = static_cast<unsigned short>(pen_y);
				pc.x1 = static_cast<unsigned short>(pen_x);
				pc.y1 = static_cast<unsigned short>(pen_y);
			}
	
			pc.xoff = static_cast<float>(g->bitmap_left);
			pc.yoff = static_cast<float>(-g->bitmap_top);
			pc.xadvance = static_cast<float>(g->advance.x >> 6);
	
			int dstIdx = static_cast<int>(cdata.size());
			cdata.push_back(pc);
			packedIndexByCp[cp] = dstIdx;
		}
	}

	std::vector<unsigned char> rgba(TEX_W * TEX_H * 4);
	for (int i = 0; i < TEX_W * TEX_H; ++i) {
		rgba[i*4+0] = 255;
		rgba[i*4+1] = 255;
		rgba[i*4+2] = 255;
		rgba[i*4+3] = atlas[i];
	}

	if (fontTex == 0) glGenTextures(1, &fontTex);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// --------------------------------------------------------------------------
	// Debug: write atlas PPM (hella slow)
	// --------------------------------------------------------------------------
//	{
//		std::ofstream ppm("font_atlas_debug.ppm");
//		ppm << "P3\n" << TEX_W << " " << TEX_H << "\n255\n";
//		for (int y = 0; y < TEX_H; ++y) {
//			for (int x = 0; x < TEX_W; ++x) {
//				int val = atlas[y * TEX_W + x];
//				ppm << val << " " << val << " " << val << " ";
//			}
//			ppm << "\n";
//		}
//	}

	int sampleIdx = lookup_packedchar_index('Q'); // pick representative char
	if (sampleIdx < 0) sampleIdx = 0;
	TEXT_WIDTH = static_cast<int>(cdata[sampleIdx].xadvance + 0.5f);

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

static bool is_keycap_base(UChar32 cp) {
	return (cp >= U'0' && cp <= U'9') || cp == U'#' || cp == U'*';
}

bool TextRenderer::try_get_keycap_sequence(const icu::UnicodeString& str,
										   int32_t index,
										   icu::UnicodeString& out_sequence) {
	if (index >= str.length())
		return false;

	int32_t i = index;

	UChar32 base = str.char32At(i);
	if (!is_keycap_base(base))
		return false;

	i += U16_LENGTH(base);

	// Optional variation selector:
	// 0034 FE0F 20E3 = fully-qualified keycap
	// 0033      20E3 = unqualified keycap
	if (i < str.length()) {
		UChar32 maybeVS16 = str.char32At(i);
		if (maybeVS16 == 0xFE0F) {
			i += U16_LENGTH(maybeVS16);
		}
	}

	if (i >= str.length())
		return false;

	UChar32 maybeKeycap = str.char32At(i);
	if (maybeKeycap != 0x20E3)
		return false;

	i += U16_LENGTH(maybeKeycap);

	out_sequence = str.tempSubStringBetween(index, i);
	return true;
}

bool TextRenderer::try_get_emoji_sequence(const icu::UnicodeString& str,
										  int32_t index,
										  icu::UnicodeString& out_sequence) {
	if (index >= str.length()) {
		return false;
	}

	// Keycap sequences start with ASCII digits/#/*, so the normal emoji
	// property gate below will reject them unless handled first.
	if (try_get_keycap_sequence(str, index, out_sequence)) {
		return true;
	}

	UChar32 cp = str.char32At(index);

	const bool looksLikeEmoji =
		u_hasBinaryProperty(cp, UCHAR_EXTENDED_PICTOGRAPHIC) ||
		u_hasBinaryProperty(cp, UCHAR_EMOJI_PRESENTATION)    ||
		(cp >= 0x1F1E0 && cp <= 0x1F1FF);

	if (!looksLikeEmoji) {
		return false;
	}

	UErrorCode status = U_ZERO_ERROR;
	std::unique_ptr<icu::BreakIterator> brk(
		icu::BreakIterator::createCharacterInstance(
			icu::Locale::getDefault(), status));

	if (U_FAILURE(status)) {
		return false;
	}

	brk->setText(str);

	int32_t end = brk->following(index);
	if (end == icu::BreakIterator::DONE || end <= index) {
		return false;
	}

	out_sequence = str.tempSubStringBetween(index, end);
	return true;
}

void TextRenderer::draw_text(float x, float y,
							 const icu::UnicodeString& unicodeStr,
							 const std::vector<Color*>& colors,
							 bool renderEmojis) {
	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float cursorX = x;
	float cursorY = y;
	int glyphIdx  = 0;

	glBegin(GL_QUADS);
	for (int32_t i = 0; i < unicodeStr.length(); ) {
		UChar32 cp = unicodeStr.char32At(i);
		
		icu::UnicodeString emojiSeq;
		if (try_get_emoji_sequence(unicodeStr, i, emojiSeq)) {
			int32_t seqLen = emojiSeq.length();   // UTF-16 code units
			float dist_right = 2*TEXT_WIDTH;
			
			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT); // Or whatever size mapping you prefer
				
				// 1. Break the current quad batch
				glEnd(); 
				
				// 2. Render the emoji (this will bind its own texture)
				emoji_renderer->draw_emoji(emojiSeq, cursorX + (dist_right - emojiSize)/2, cursorY - ascent_px + (TEXT_HEIGHT - emojiSize)/2, emojiSize);
				
				// 3. Restore state and resume the quad batch for the rest of your font
				glBindTexture(GL_TEXTURE_2D, fontTex);
				glBegin(GL_QUADS); 
			}
			
			cursorX  += dist_right;
			i        += seqLen;
			glyphIdx += seqLen;
			continue;
		}
		
		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);    // unknown non-emoji fallback
		}
		
		if (glyphIdx < static_cast<int>(colors.size()) && idx >= 0) {
			AlignedQuad q;
			
			float cx_temp = cursorX;
			
			GetPackedQuad(cdata, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);

			const Color* col = colors[glyphIdx];
			glColor4f(col->r, col->g, col->b, 1.0f);

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}
		
		int codepoints_16 = U16_LENGTH(cp);
		cursorX += TEXT_WIDTH*codepoints_16; // enforces monospacing

		i = unicodeStr.moveIndex32(i, 1);
		++glyphIdx;
	}
	glEnd();

	if (!wasBlendEnabled) glDisable(GL_BLEND);
	else glBlendFunc(oldBlendSrc, oldBlendDst);
	if (!wasTexEnabled) glDisable(GL_TEXTURE_2D);
}

void TextRenderer::draw_text(float x, float y,
							 const icu::UnicodeString& unicodeStr,
							 Color* color,
							 bool renderEmojis) {
	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float cursorX = x;
	float cursorY = y;
	
	glColor4f(color->r, color->g, color->b, 1.0f);

	glBegin(GL_QUADS);
	for (int32_t i = 0; i < unicodeStr.length(); ) {
		UChar32 cp = unicodeStr.char32At(i);
		
		icu::UnicodeString emojiSeq;
		if (try_get_emoji_sequence(unicodeStr, i, emojiSeq)) {
			int32_t seqLen = emojiSeq.length();   // UTF-16 code units
			float dist_right = 2*TEXT_WIDTH;
			
			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT); // Or whatever size mapping you prefer
				
				// 1. Break the current quad batch
				glEnd(); 
				
				// 2. Render the emoji (this will bind its own texture)
				emoji_renderer->draw_emoji(emojiSeq, cursorX + (dist_right - emojiSize)/2, cursorY - ascent_px + (TEXT_HEIGHT - emojiSize)/2, emojiSize);
				
				// 3. Restore state and resume the quad batch for the rest of your font
				glBindTexture(GL_TEXTURE_2D, fontTex);
				glColor4f(color->r, color->g, color->b, 1.0f);
				glBegin(GL_QUADS); 
			}
			
			cursorX  += dist_right;
			i        += seqLen;
			continue;
		}
		
		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);    // unknown non-emoji fallback
		}
		
		if (idx >= 0) {
			AlignedQuad q;
			
			float cx_temp = cursorX;
			
			GetPackedQuad(cdata, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);
	
			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}
		
		int codepoints_16 = U16_LENGTH(cp);
		cursorX += TEXT_WIDTH*codepoints_16; // enforces monospaced

		i = unicodeStr.moveIndex32(i, 1);
	}
	glEnd();

	if (!wasBlendEnabled) glDisable(GL_BLEND);
	else glBlendFunc(oldBlendSrc, oldBlendDst);
	if (!wasTexEnabled) glDisable(GL_TEXTURE_2D);
}

void TextRenderer::draw_text(float x, float y,
							 const icu::UnicodeString& unicodeStr,
							 uint8_t r,
							 uint8_t g,
							 uint8_t b,
							 bool renderEmojis) {
	y += ascent_px; // baseline adjustment

	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float cursorX = x;
	float cursorY = y;
	
	glColor4f((float)r/255.0f, (float)g/255.0f, (float)b/255.0f, 1.0f);

	glBegin(GL_QUADS);
	for (int32_t i = 0; i < unicodeStr.length(); ) {
		UChar32 cp = unicodeStr.char32At(i);
		
		icu::UnicodeString emojiSeq;
		if (try_get_emoji_sequence(unicodeStr, i, emojiSeq)) {
			int32_t seqLen = emojiSeq.length();   // UTF-16 code units
			float dist_right = 2*TEXT_WIDTH;
			
			if (renderEmojis) {
				float emojiSize = std::fmin(dist_right, TEXT_HEIGHT); // Or whatever size mapping you prefer
				
				// 1. Break the current quad batch
				glEnd(); 
				
				// 2. Render the emoji (this will bind its own texture)
				emoji_renderer->draw_emoji(emojiSeq, cursorX + (dist_right - emojiSize)/2, cursorY - ascent_px + (TEXT_HEIGHT - emojiSize)/2, emojiSize);
				
				// 3. Restore state and resume the quad batch for the rest of your font
				glBindTexture(GL_TEXTURE_2D, fontTex);
				glColor4f((float)r/255.0f, (float)g/255.0f, (float)b/255.0f, 1.0f);
				glBegin(GL_QUADS); 
			}
			
			cursorX  += dist_right;
			i        += seqLen;
			continue;
		}
		
		int idx = lookup_packedchar_index(cp);
		if (idx < 0 && cp != 0xFFFF && cp != U'\t') {
			idx = lookup_packedchar_index(0xFFFD);    // unknown non-emoji fallback
		}
		
		if (idx >= 0) {
			AlignedQuad q;
			
			float cx_temp = cursorX;
			
			GetPackedQuad(cdata, TEX_W, TEX_H, idx, &cx_temp, &cursorY, &q, true);
	
			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		}
		
		int codepoints_16 = U16_LENGTH(cp);
		cursorX += TEXT_WIDTH*codepoints_16;

		i = unicodeStr.moveIndex32(i, 1);
	}
	glEnd();

	if (!wasBlendEnabled) glDisable(GL_BLEND);
	else glBlendFunc(oldBlendSrc, oldBlendDst);
	if (!wasTexEnabled) glDisable(GL_TEXTURE_2D);
}

void TextRenderer::cleanup() {
	if (fontTex) {
		glDeleteTextures(1, &fontTex);
		fontTex = 0;
	}
	cdata.clear();
	packedRanges.clear();
	if (ftFace) {
		FT_Done_Face(ftFace);
		ftFace = nullptr;
	}
}
#include "text_renderer.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct RangeInfo {
	int first;     // first Unicode code-point in this range
	int count;     // number of code-points in this range
	int offset;    // starting index in cdata[]
};

static std::vector<stbtt_packedchar> cdata;
static GLuint fontTex = 0;
static float font_size = 19.0f;
static std::vector<RangeInfo> packedRanges;
const int TEX_W = 1024, TEX_H = 1024;

int TEXT_WIDTH = 1;
int TEXT_HEIGHT = 1;

int ascent_px = 0;

static int lookup_packedchar_index(int cp) {
	for (const auto& pr : packedRanges) {
		if (cp >= pr.first && cp < pr.first + pr.count) {
			return pr.offset + (cp - pr.first);
		}
	}
	return -1;
}

void TextRenderer::set_font_size(float size) {
	font_size = size;
}

bool TextRenderer::init_font(const char* fontPath) {
	std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
	if (!fontFile) {
		std::cerr << "Failed to load font\n";
		return false;
	}

	std::streamsize size = fontFile.tellg();
	fontFile.seekg(0, std::ios::beg);

	std::vector<unsigned char> fontBuffer(size);
	if (!fontFile.read((char*)fontBuffer.data(), size)) {
		std::cerr << "Failed to read font file\n";
		return false;
	}
	
	std::vector<unsigned char> atlas(TEX_W * TEX_H, 0);

	// Define Unicode ranges to pack
	struct Range { int first, count; };
	std::vector<Range> ranges = {
		// ─── Already have these ────────────────────────────────────
		{ 0x0020, 0x0060 },   // ASCII printable (space through ~) :contentReference[oaicite:0]{index=0}
		{ 0x00A0, 0x0060 },   // Latin-1 supplement :contentReference[oaicite:1]{index=1}
		{ 0x2000, 0x0080 },   // General Punctuation :contentReference[oaicite:2]{index=2}
		{ 0x25A0, 0x0060 },   // Geometric Shapes (▲ ■ ●, etc.) :contentReference[oaicite:3]{index=3}
	
		// ─── Extended Latin ───────────────────────────────────────
		{ 0x0100, 0x0080 },   // Latin Extended-A (ĀāĂă …)  
		{ 0x0180, 0x00D0 },   // Latin Extended-B (ƀƁƂƃ …)
	
		// ─── Other alphabets (optional) ──────────────────────────
		{ 0x0370, 0x0090 },   // Greek and Coptic (αβγ …)  
		{ 0x0400, 0x0100 },   // Cyrillic (АБВ …)
		
		// ─── Programming-friendly symbols ────────────────────────
		{ 0x0300, 0x0070 },   // Combining Diacritical Marks (́̊̈ etc.)  
		{ 0x20A0, 0x0030 },   // Currency Symbols (€, ₤, ₨, etc.)  
		{ 0x2190, 0x00F0 },   // Arrows (← ↑ → ↓ ↔ etc.)  
		{ 0x2200, 0x0100 },   // Mathematical Operators (≤ ≥ ≈ ± ∑ ∞ etc.)  
		{ 0x2500, 0x0080 },   // Box Drawing (─ │ ┌ ┐ ░ ▒ ▓)  
		{ 0x2E00, 0x0080 },   // Supplemental Punctuation (‥…‧)
		
		// ─── Specials ──────────────────────────────────────────────
		{ 0xFFFD, 0x0001 },   // REPLACEMENT CHARACTER (U+FFFD)
	};

	// Calculate total glyphs and resize cdata
	int totalGlyphs = 0;
	for (const auto& r : ranges) totalGlyphs += r.count;
	cdata.resize(totalGlyphs);

	// Build packedRanges with running offsets
	packedRanges.clear();
	int runningOffset = 0;
	for (const auto& r : ranges) {
		packedRanges.push_back({ r.first, r.count, runningOffset });
		runningOffset += r.count;
	}
	
	// Initialize packing context
	stbtt_pack_context pc;
	if (!stbtt_PackBegin(&pc, atlas.data(), TEX_W, TEX_H, 0, 1, nullptr)) {
		std::cerr << "Failed to initialize font packing\n";
		return false;
	}

	// Pack each range
	int dstIndex = 0;
	for (const auto& r : ranges) {
		stbtt_pack_range pr = { 0 };
		pr.first_unicode_codepoint_in_range = r.first;
		pr.num_chars = r.count;
		pr.chardata_for_range = &cdata[dstIndex];
		pr.font_size = font_size;
		pr.h_oversample = 1;
		pr.v_oversample = 1;
		
		if (!stbtt_PackFontRanges(&pc, fontBuffer.data(), 0, &pr, 1)) {
			std::cerr << "Failed to pack font range starting at " << r.first << "\n";
		}
		
		dstIndex += r.count;
	}

	stbtt_PackEnd(&pc);

	// Debug: Check if atlas has any content
	bool hasContent = false;
	for (int i = 0; i < TEX_W * TEX_H; i++) {
		if (atlas[i] > 0) {
			hasContent = true;
			break;
		}
	}
	std::cout << "Atlas has content: " << (hasContent ? "YES" : "NO") << std::endl;
	
	// Create RGBA texture - WHITE text with alpha from atlas
	std::vector<unsigned char> rgba(TEX_W * TEX_H * 4);
	for (int i = 0; i < TEX_W * TEX_H; i++) {
		rgba[i*4+0] = 255; // R - white
		rgba[i*4+1] = 255; // G - white  
		rgba[i*4+2] = 255; // B - white
		rgba[i*4+3] = atlas[i]; // A - use atlas alpha
	}

	// Upload texture to OpenGL
	glGenTextures(1, &fontTex);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H,
				 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Debug: Save atlas as PPM file to inspect visually
	std::ofstream ppm("font_atlas_debug.ppm");
	ppm << "P3\n" << TEX_W << " " << TEX_H << "\n255\n";
	for (int y = 0; y < TEX_H; y++) {
		for (int x = 0; x < TEX_W; x++) {
			unsigned char val = atlas[y * TEX_W + x];
			ppm << (int)val << " " << (int)val << " " << (int)val << " ";
		}
		ppm << "\n";
	}
	ppm.close();
	std::cout << "Debug atlas saved as font_atlas_debug.ppm" << std::endl;
	
	stbtt_packedchar& sp = cdata[lookup_packedchar_index(0x0051)];
	// pixel width of one character cell:
	TEXT_WIDTH  = static_cast<int>(sp.xadvance + 0.5f);
	
	// assuming monospace - it's a code editor bruv
	
	stbtt_fontinfo fontInfo;
	if (!stbtt_InitFont(&fontInfo, fontBuffer.data(), 0)) {
		std::cerr << "Failed to init font info\n";
		return false;
	}
	
	float scale = stbtt_ScaleForPixelHeight(&fontInfo, font_size);
	
	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
	
	TEXT_HEIGHT = static_cast<int>((ascent - descent + lineGap) * scale + 0.5f);
	
	ascent_px = static_cast<int>(ascent * scale);
	
	return true;
}

int TextRenderer::get_text_width(int text_len){
	return TEXT_WIDTH*text_len;
}

int TextRenderer::get_text_height(){
	return TEXT_HEIGHT;
}

void TextRenderer::draw_text(float x, float y, const icu::UnicodeString& unicodeStr, const std::vector<Color*>& colors) {
	y += ascent_px;
	
	GLboolean wasBlendEnabled = glIsEnabled(GL_BLEND);
	GLboolean wasTexEnabled   = glIsEnabled(GL_TEXTURE_2D);
	
	GLint oldBlendSrc, oldBlendDst;
	glGetIntegerv(GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv(GL_BLEND_DST, &oldBlendDst);
	
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float currentX = x, currentY = y;
	int glyphIdx = 0;
	
	glBegin(GL_QUADS);

	// Properly iterate through Unicode string
	for (int32_t i = 0; i < unicodeStr.length(); ) {
		UChar32 cp = unicodeStr.char32At(i);  // Get proper Unicode codepoint
		
		// Find the packed character index for this codepoint
		int idx = lookup_packedchar_index(cp);
		
		if (idx >= 0 && glyphIdx < (int)colors.size()) {
			stbtt_aligned_quad q;
			float tricks = currentX;
			stbtt_GetPackedQuad(cdata.data(), TEX_W, TEX_H,
							   idx, &tricks, &currentY, &q, 1);


			// Set color for this glyph - modulate with white texture
			Color* col = colors[glyphIdx];
			glColor4f(col->r, col->g, col->b, 1.0f);
			
			// Draw the quad with proper texture coordinates
			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
		} else if (idx < 0) {
			std::cout << "Character not found in atlas: U+" << std::hex << cp << std::dec << "\n";
			// Advance position even for missing characters
			// You could implement a default advance width here
		}
		
		currentX += TEXT_WIDTH; // like I said... tricks (enforces mono-spaced fontage. I know...)
		
		// Move to next character (handle multi-unit Unicode properly)
		i = unicodeStr.moveIndex32(i, 1);
		glyphIdx++;
	}

	glEnd();
	
	if (!wasBlendEnabled) glDisable(GL_BLEND);
	else glBlendFunc(oldBlendSrc, oldBlendDst); // Restore original blend func if needed
	
	if (!wasTexEnabled) glDisable(GL_TEXTURE_2D);
}

void TextRenderer::cleanup() {
	if (fontTex != 0) {
		glDeleteTextures(1, &fontTex);
		fontTex = 0;
	}
	cdata.clear();
	packedRanges.clear();
}
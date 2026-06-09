#include "label.h"
#include "text_renderer.h"
#include "application.h"
#include "helper_types.h"
#include <unicode/brkiter.h>
#include <unicode/uchar.h>

Label::Label(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Label");
	
	fulltext = icu::UnicodeString::fromUTF8("");
	drawlines = {};
}

inline bool is_keycap_base_fast(UChar c) {
	return (c >= '0' && c <= '9') || c == '#' || c == '*';
}

int32_t Label::get_emoji_sequence_length(const icu::UnicodeString& str, int32_t index) {
	const int32_t len = str.length();
	if (index >= len) {
		return 0;
	}

	// 1. FAST PATH: Keycap Sequence Detection
	// Since all keycap components fit into single UTF-16 code units, 
	// we can use direct array subscripting for O(1) evaluation.
	const UChar first = str[index];
	if (is_keycap_base_fast(first)) {
		if (index + 1 < len) {
			const UChar second = str[index + 1];
			// Unqualified keycap (e.g., "3" + Combining Enclosing Keycap)
			if (second == 0x20E3) {
				return 2; 
			}
			// Fully-qualified keycap (e.g., "3" + VS16 + Combining Enclosing Keycap)
			if (second == 0xFE0F && index + 2 < len && str[index + 2] == 0x20E3) {
				return 3;
			}
		}
		return 0; // Valid base character, but doesn't form a keycap emoji sequence
	}

	// 2. Property Check for General Emojis
	const UChar32 cp = str.char32At(index);
	const bool looksLikeEmoji =
		u_hasBinaryProperty(cp, UCHAR_EXTENDED_PICTOGRAPHIC) ||
		u_hasBinaryProperty(cp, UCHAR_EMOJI_PRESENTATION)    ||
		(cp >= 0x1F1E0 && cp <= 0x1F1FF);

	if (!looksLikeEmoji) {
		return 0;
	}

	// 3. SLOW PATH: Multi-grapheme Emoji Boundaries (Flags, ZWJ sequences, Modifiers)
	// We use thread_local to instantiate the BreakIterator EXACTLY ONCE per thread.
	thread_local std::unique_ptr<icu::BreakIterator> brk = []() {
		UErrorCode status = U_ZERO_ERROR;
		auto iterator = std::unique_ptr<icu::BreakIterator>(
			icu::BreakIterator::createCharacterInstance(icu::Locale::getDefault(), status));
		return U_SUCCESS(status) ? std::move(iterator) : nullptr;
	}();

	if (!brk) {
		return 0; // Safety fallback if ICU fails to initialize the iterator
	}

	// setText() is lightweight; it points the iterator to the existing buffer without copying
	brk->setText(str);
	
	const int32_t end = brk->following(index);
	if (end == icu::BreakIterator::DONE || end <= index) {
		return 0;
	}

	// Returns the total number of UTF-16 code units spanning the emoji sequence
	return end - index;
}

void Label::setFullText(icu::UnicodeString text, std::vector<MarkdownSpan> spans) {
	std::lock_guard<std::mutex> lock(positioning);
	
	fulltext = text;
	old_width = -1;
	handlingColor = (spans.size() != 0);
	colorSpans = spans;
}

icu::UnicodeString Label::getFullText() {
	return fulltext;
}

bool Label::on_mouse_button_event(int button, int action, int mods) {
	if (action != GLFW_PRESS || button != GLFW_MOUSE_BUTTON_LEFT) {
		return Widget::on_mouse_button_event(button, action, mods);
	}
	
	if (t_x < App::mouseX && App::mouseX < t_x+t_w && t_y < App::mouseY && App::mouseY < t_y+t_h) {
		if (fulltext.length() != 0) {
			std::string text;
			fulltext.toUTF8String(text);
			SetClipboardText(text);
			App::displayToast(icu::UnicodeString::fromUTF8("Coppied to clipboard."));
		}else{
			App::displayToast(icu::UnicodeString::fromUTF8("Nothing to copy."));
		}
		
		return true;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}

void Label::render() {
	if (rect) {
		App::DrawRect(t_x, t_y, t_w, t_h, background_color);
	}
	if (border) {
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
	
	int ypos = t_y+App::text_padding;
	
	for (int i = 0; i < drawlines.size(); i++) {
		if (handlingColor) {
			TextRenderer::draw_text(t_x+App::text_padding, ypos, drawlines[i], drawColors[i]);
		}else{
			TextRenderer::draw_text(t_x+App::text_padding, ypos, drawlines[i], App::theme.main_text_color);
		}
		ypos += TextRenderer::get_text_height();
	}
	
	Widget::render();
}

void Label::position(int x, int y, int w, int h) {
	std::lock_guard<std::mutex> lock(positioning);
	
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	if (POSITIONER) {
		POSITIONER(this);
	}
	
	const int mx = App::mouseX;
	const int my = App::mouseY;
	if (cursor_in_this) {
		App::expectedCursorType = 3;
	}
	
	if (old_width == t_w) { return; }
	old_width = t_w;
	
	drawlines.clear();
	drawColors.clear();
	
	icu::UnicodeString curline = icu::UnicodeString();
	
	should_be_h = App::text_padding*2;
	int linewidth = 0;
	
	int most_allowed = t_w-App::text_padding*2;
	
	int curspan = 0;
	std::vector<Color*> curlineColor = {};
	Color* thisColor;
	
	int byteIndex = 0;
	
	for (auto ci = 0; ci < fulltext.length(); ci++) {
		char16_t c = fulltext.charAt(ci);
		
		int bytes = 0;
		int emojiUtf16Count = get_emoji_sequence_length(fulltext, ci);
		if (emojiUtf16Count == 0) {
			bytes = U8_LENGTH(c);
		}else{
			bytes = emojiUtf16Count*2;
		}
		
		if (handlingColor) {
			thisColor = App::theme.main_text_color;
			if (curspan < colorSpans.size()) { // spans are always ordered so that we can only look at one at a time (mucho faster)
				if (byteIndex >= colorSpans[curspan].start && byteIndex < colorSpans[curspan].end) {
					auto t = colorSpans[curspan].type;
					if (t == MarkdownElem::Header) {
						thisColor = App::theme.equal_diff;
					}else if (t == MarkdownElem::Bold) {
						thisColor = App::theme.add_diff;
					}else if (t == MarkdownElem::Italic) {
						thisColor = App::theme.warning_color;
					}else if (t == MarkdownElem::Link) {
						thisColor = App::theme.equal_diff;
					}else if (t == MarkdownElem::Code) {
						thisColor = App::theme.warning_color;
					}
				}
				
				if (byteIndex == colorSpans[curspan].end) {
					curspan ++;
				}
			}
		}
		
		byteIndex += bytes;
		
		int num_chr = 1;
		if (c == U'\t') {
			num_chr = App::settings->getValue("tab_width", 4);
			c = ' ';
		}
		
		int newlen = linewidth + TextRenderer::get_text_width(num_chr);
		
		if (emojiUtf16Count != 0) {
			newlen += TextRenderer::get_text_width(1);
		}
		
		if (c == U'\n' || newlen > most_allowed) {
			drawlines.push_back(curline);
			curline = "";
			
			if (handlingColor) {
				drawColors.push_back(curlineColor);
				curlineColor = {};
			}
			
			should_be_h += TextRenderer::get_text_height();
			
			linewidth = 0;
			newlen = linewidth + TextRenderer::get_text_width(num_chr);
		}
		
		if (c != U'\n') {
			if (emojiUtf16Count != 0) {
				for (int i = 0; i < emojiUtf16Count; i++) {
					curline.append(fulltext.charAt(ci+i));
					if (handlingColor) {
						curlineColor.push_back(thisColor);
					}
				}
			}else{
				for (int rep = 0; rep < num_chr; rep++) {
					curline += c;
					if (handlingColor) {
						curlineColor.push_back(thisColor);
					}
				}
			}
		}
		
		linewidth = newlen;
		
		if (emojiUtf16Count != 0) {
			ci += emojiUtf16Count-1;
		}
	}
	
	drawlines.push_back(curline);
	if (handlingColor) {
		drawColors.push_back(curlineColor);
	}
	should_be_h += TextRenderer::get_text_height();
	
	if (t_x < App::mouseX && App::mouseX < t_x+t_w && t_y < App::mouseY && App::mouseY < t_y+t_h) {
		if (App::expectedCursorType == -1 && cursor_in_this) { // only set cursor if expected to be arrow right now
			App::expectedCursorType = 3; // hand cursor
		}
	}
}
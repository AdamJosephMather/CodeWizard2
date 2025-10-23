#include "terminalwidget.h"
#include "application.h"
#include "terminal.h"
#include "text_renderer.h"

TerminalWidget::TerminalWidget(Widget* parent)  : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Terminal");
	
	std::thread([&]() { run(); }).detach();
}

void TerminalWidget::run() {
	int width_cells = std::max((t_w-App::text_padding*2) / TextRenderer::get_text_width(1), 1);
	int height_cells = std::max((t_h-App::text_padding*2) / TextRenderer::get_text_height(), 1);
	
	const std::wstring shell = L"cmd.exe";
	term = new Terminal(width_cells, height_cells);
	
	if (term->start(shell)) {
		term->enableMouseTracking(true);
	}else {
		std::cerr << "Failed to create terminal..." << std::endl;
		return;
	}
	
	settingup = false;
}

void TerminalWidget::request_close(close_callback_type callback) {
	term->stop();
	Widget::request_close(callback);
}

void TerminalWidget::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
	
	if (term == nullptr) {
		return;
	}
	
	int width_cells = std::max((w-App::text_padding*2) / TextRenderer::get_text_width(1), 1);
	int height_cells = std::max((h-App::text_padding*2) / TextRenderer::get_text_height(), 1);
	
	if (!settingup && (prev_w_cells != width_cells || prev_h_cells != height_cells)) {
		term->resize(width_cells, height_cells);
		prev_w_cells = width_cells;
		prev_h_cells = height_cells;
	}
}

void TerminalWidget::render() {
	if (!is_visible) { return; }
	
	if (term == nullptr || settingup) {
		return;
	}
	
	UChar32 empty = U' ';
	
	auto ci = term->getCursorInfo();
	int c_wid = TextRenderer::get_text_width(1)/4;
	
	for (int r = 0; r < prev_h_cells; r++) {
		for (int c = 0; c < prev_w_cells; c++) {
			OurCell cell = term->getCell(r, c);
			
			int x = t_x+App::text_padding+TextRenderer::get_text_width(c);
			int y = t_y+App::text_padding+TextRenderer::get_text_height()*r;
			
			App::DrawRect(x, y, TextRenderer::get_text_width(prev_w_cells)+App::text_padding*2, TextRenderer::get_text_height()*prev_h_cells+App::text_padding*2, cell.bg_red, cell.bg_green, cell.bg_blue);
			
			if (ci.row == r && ci.col == c && ci.visible) {
				if (ci.shape == 1) { // block
					App::DrawRect(x, y, TextRenderer::get_text_width(1), TextRenderer::get_text_height(), App::theme.white);
					cell.fg_red = 255-cell.fg_red;
					cell.fg_green = 255-cell.fg_green;
					cell.fg_blue = 255-cell.fg_blue;
				}else if (ci.shape == 2) { // underline
					App::DrawRect(x, y+TextRenderer::get_text_height()-c_wid, TextRenderer::get_text_width(1), c_wid, App::theme.white);
				}else { // bar left
					App::DrawRect(x, y, c_wid, TextRenderer::get_text_height(), App::theme.white);
				}
			}
			
			if (cell.c != empty) {
				TextRenderer::draw_text(x, y, cell.c, cell.fg_red, cell.fg_green, cell.fg_blue);
			}
		}
	}
}

static inline void mods_to_bools(int mods, bool& shift, bool& alt, bool& ctrl) {
	shift = (mods & GLFW_MOD_SHIFT) != 0;
	alt   = (mods & GLFW_MOD_ALT)   != 0;
	ctrl  = (mods & GLFW_MOD_CONTROL) != 0;
}

static inline bool map_special_key(int key, Terminal::SpecialKey& out) {
	switch (key) {
		case GLFW_KEY_UP:         out = Terminal::SpecialKey::Up;       return true;
		case GLFW_KEY_DOWN:       out = Terminal::SpecialKey::Down;     return true;
		case GLFW_KEY_LEFT:       out = Terminal::SpecialKey::Left;     return true;
		case GLFW_KEY_RIGHT:      out = Terminal::SpecialKey::Right;    return true;
		case GLFW_KEY_HOME:       out = Terminal::SpecialKey::Home;     return true;
		case GLFW_KEY_END:        out = Terminal::SpecialKey::End;      return true;
		case GLFW_KEY_INSERT:     out = Terminal::SpecialKey::InsertKey;return true;
		case GLFW_KEY_DELETE:     out = Terminal::SpecialKey::DeleteKey;return true;
		case GLFW_KEY_PAGE_UP:    out = Terminal::SpecialKey::PageUp;   return true;
		case GLFW_KEY_PAGE_DOWN:  out = Terminal::SpecialKey::PageDown; return true;
		case GLFW_KEY_F1:         out = Terminal::SpecialKey::F1;       return true;
		case GLFW_KEY_F2:         out = Terminal::SpecialKey::F2;       return true;
		case GLFW_KEY_F3:         out = Terminal::SpecialKey::F3;       return true;
		case GLFW_KEY_F4:         out = Terminal::SpecialKey::F4;       return true;
		case GLFW_KEY_F5:         out = Terminal::SpecialKey::F5;       return true;
		case GLFW_KEY_F6:         out = Terminal::SpecialKey::F6;       return true;
		case GLFW_KEY_F7:         out = Terminal::SpecialKey::F7;       return true;
		case GLFW_KEY_F8:         out = Terminal::SpecialKey::F8;       return true;
		case GLFW_KEY_F9:         out = Terminal::SpecialKey::F9;       return true;
		case GLFW_KEY_F10:        out = Terminal::SpecialKey::F10;      return true;
		case GLFW_KEY_F11:        out = Terminal::SpecialKey::F11;      return true;
		case GLFW_KEY_F12:        out = Terminal::SpecialKey::F12;      return true;
		default: return false;
	}
}

inline void TerminalWidget::cell_from_cursor(int& row, int& col) {
	int mx = App::mouseX;
	int my = App::mouseY;

	if (mx < t_x || my < t_y || mx > t_x+t_w || my > t_y+t_h) {
		row = -1;
		col = -1;
		return;
	}
	
	col = (mx-t_x-App::text_padding)/TextRenderer::get_text_width(1);
	row = (my-t_y-App::text_padding)/TextRenderer::get_text_height();
}

// Track dragging state locally (no header change)
static bool s_dragging = false;

bool TerminalWidget::on_key_event(int key, int /*scancode*/, int action, int mods) {
	if (!is_visible) { return false; }
	
	if (!term || settingup) return false;
	if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

	bool shift=false, alt=false, ctrl=false;
	mods_to_bools(mods, shift, alt, ctrl);

	// Enter / Backspace / Tab / Escape
	if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
		return term->sendEnter();
	} else if (key == GLFW_KEY_BACKSPACE) {
		return term->sendBackspace();
	} else if (key == GLFW_KEY_TAB) {
		return term->sendText("\t");
	} else if (key == GLFW_KEY_ESCAPE) {
		return term->sendText("\x1b");
	}

	// Ctrl+Letter (ASCII control)
	if (ctrl && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
		char letter = static_cast<char>('A' + (key - GLFW_KEY_A));
		return term->sendCtrl(letter);
	}

	// Special keys (arrows, Home/End, PgUp/PgDn, F-keys) with modifiers
	Terminal::SpecialKey sk;
	if (map_special_key(key, sk)) {
		return term->sendSpecialKey(sk, shift, alt, ctrl);
	}

	// Let printable characters be handled by on_char_event (GLFW will emit it)
	return false;
}

bool TerminalWidget::on_char_event(unsigned int keycode) {
	if (!is_visible) { return false; }
	
	if (App::activeLeafNode != this) {
		return false;
	}
	
	if (!term || settingup) return false;
	// keycode is Unicode codepoint (per GLFW docs)
	
	// Convert Unicode codepoint to UTF-8 and send
	char utf8[5] = {0};
	unsigned int cp = keycode;
	
	if (cp < 0x80) {
		utf8[0] = static_cast<char>(cp);
		utf8[1] = '\0';
	} else if (cp < 0x800) {
		utf8[0] = static_cast<char>(0xC0 | (cp >> 6));
		utf8[1] = static_cast<char>(0x80 | (cp & 0x3F));
		utf8[2] = '\0';
	} else if (cp < 0x10000) {
		utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
		utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
		utf8[3] = '\0';
	} else if (cp < 0x110000) {
		utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
		utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
		utf8[4] = '\0';
	}
	
	return term->sendText(utf8);
}

bool TerminalWidget::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible) { return false; }
	
	if (!term || settingup) return false;

	bool shift=false, alt=false, ctrl=false;
	mods_to_bools(mods, shift, alt, ctrl);

	// Map GLFW mouse button to 0/1/2
	int b = 0;
	if (button == GLFW_MOUSE_BUTTON_LEFT) b = 0;
	else if (button == GLFW_MOUSE_BUTTON_MIDDLE) b = 1;
	else if (button == GLFW_MOUSE_BUTTON_RIGHT) b = 2;
	else return false; // ignore additional buttons

	int row=0, col=0;
	cell_from_cursor(row, col);
	
	if (row < 0 || col < 0) {
		return false;
	}
	if (App::activeLeafNode != this) {
		App::setActiveLeafNode(this);
	}

	const bool pressed = (action == GLFW_PRESS);
	s_dragging = pressed ? true : s_dragging && pressed; // start on press, stop on release below

	bool ok = term->mousePress(row, col, b, pressed, shift, alt, ctrl);

	if (action == GLFW_RELEASE) s_dragging = false;
	return ok;
}

bool TerminalWidget::on_mouse_move_event() {
	if (!is_visible) { return false; }
	
	if (!term || settingup) return false;

	int row=0, col=0;
	cell_from_cursor(row, col);

	return term->mouseMove(row, col, /*buttonHeld*/ s_dragging);
}

bool TerminalWidget::on_scroll_event(double /*xchange*/, double ychange) {
	if (!is_visible) { return false; }
	
	if (!term || settingup) return false;
	
	int row=0, col=0;
	cell_from_cursor(row, col);
	
	ychange *= -1;
	
	int lines = static_cast<int>( (ychange > 0) ? std::floor(ychange + 0.5) : std::ceil (ychange - 0.5) );
	if (lines == 0) return true;
	
	return term->mouseScroll(row, col, lines);
}

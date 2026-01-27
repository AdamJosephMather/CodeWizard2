#include "terminalwidget.h"
#include "application.h"
#include "terminal.h"
#include "text_renderer.h"
#include <iostream>
#include "Verify.hpp"

TerminalWidget::TerminalWidget(Widget* parent)  : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Terminal");
	
	ajm_asv3_tm = new CheckBox(nullptr, [&](CheckBox* c, int,int,int,int){
		c->t_h = TextRenderer::get_text_height()*1.5;
		c->t_w = c->t_h;
		c->t_x = t_w + t_x - c->t_w - App::text_padding;
		c->t_y = t_y + App::text_padding;
	}, [&](CheckBox* cb){
		bool send_to_asv3 = cb->is_checked; // assistant v3
		
		ajm_set_asv3(send_to_asv3);
	});
	ajm_asv3_tm->rounded = true;
	ajm_asv3_tm->border = true;
	ajm_asv3_tm->bgcolor = App::theme.hover_background_color; // brighter for black terminal background
	
	if (App::settings->getValue("ajm_asv3_tm_url", std::string()) != std::string()) {
		App::MoveWidget(ajm_asv3_tm, this);
	}
	
	reset_client();
}

void TerminalWidget::executeAction(WidgetActionType typ) {
	Widget::executeAction(typ);
	
	if (typ != AJM_SETTINGS_CHANGE) {
		return;
	}
	
	std::cout << "Settings changed!\n";
	
	if (App::settings->getValue("ajm_asv3_tm_url", std::string()) != std::string()) {
		std::cout << "ON\n";
		if (ajm_asv3_tm->parent != this) {
			App::MoveWidget(ajm_asv3_tm, this);
		}
		ajm_set_asv3(ajm_asv3_tm->is_checked);
	}else{
		std::cout << "OFF\n";
		if (ajm_asv3_tm->parent != nullptr) {
			App::RemoveWidgetFromParent(ajm_asv3_tm);
		}
		ajm_set_asv3(false);
	}
}

void TerminalWidget::ajm_set_asv3(bool connect) {
	reset_client();
	
	if (connect) {
		std::string prefix = "http://";
		if (App::settings->getValue("ajm_asv3_tm_use_https", false)) {
			prefix = "https://";
		}
		
		std::map<std::string, std::string> config;
		config["reconnect"] = "true";
		config["reconnections"] = "9999999"; // Effectively infinite
		config["reconnectionDelay"] = "5000"; // 5 seconds between attempts
		ajm_asv3_client->connect(prefix+App::settings->getValue("ajm_asv3_tm_url", std::string())+":8059", config);
	}
}

void TerminalWidget::run() {
	int width_cells = std::max((t_w-App::text_padding*2) / TextRenderer::get_text_width(1), 1);
	int height_cells = std::max((t_h-App::text_padding*2) / TextRenderer::get_text_height(), 1);
	
	term = new Terminal(width_cells, height_cells, [&](int num) {
		sel_doc_r0 -= num;
		sel_doc_r1 -= num;
	});
	
	if (term->start()) {
//		term->enableMouseTracking(true);
	}else {
		std::cerr << "Failed to create terminal..." << std::endl;
		return;
	}
	
	settingup = false;
}

static sio::message::ptr jsonToSio(const nlohmann::json& j) {
	using json = nlohmann::json;

	if (j.is_null()) {
		return sio::null_message::create();
	}
	if (j.is_boolean()) {
		return sio::bool_message::create(j.get<bool>());
	}
	if (j.is_number_integer()) {
		// socket.io-client-cpp uses double for numbers internally
		return sio::double_message::create(static_cast<double>(j.get<long long>()));
	}
	if (j.is_number_unsigned()) {
		return sio::double_message::create(static_cast<double>(j.get<unsigned long long>()));
	}
	if (j.is_number_float()) {
		return sio::double_message::create(j.get<double>());
	}
	if (j.is_string()) {
		return sio::string_message::create(j.get<std::string>());
	}
	if (j.is_array()) {
		auto arr = sio::array_message::create();
		for (const auto& el : j) {
			arr->get_vector().push_back(jsonToSio(el));
		}
		return arr;
	}
	if (j.is_object()) {
		auto obj = sio::object_message::create();
		auto& m = obj->get_map();
		for (auto it = j.begin(); it != j.end(); ++it) {
			m[it.key()] = jsonToSio(it.value());
		}
		return obj;
	}

	// Fallback: stringify unknown types
	return sio::string_message::create(j.dump());
}

static nlohmann::json sioToJson(const sio::message::ptr& msg) {
	using json = nlohmann::json;

	if (!msg) return nullptr;

	switch (msg->get_flag()) {
		case sio::message::flag_null:
			return nullptr;

		case sio::message::flag_boolean:
			return msg->get_bool();

		case sio::message::flag_integer:
			return static_cast<long long>(msg->get_int());

		case sio::message::flag_double:
			return msg->get_double();

		case sio::message::flag_string:
			return msg->get_string();

		case sio::message::flag_array: {
			json out = json::array();
			const auto& vec = msg->get_vector();
			for (const auto& el : vec) {
				out.push_back(sioToJson(el));
			}
			return out;
		}

		case sio::message::flag_object: {
			json out = json::object();
			const auto& mp = msg->get_map();
			for (const auto& kv : mp) {
				out[kv.first] = sioToJson(kv.second);
			}
			return out;
		}

		case sio::message::flag_binary: {
			// Different socket.io-client-cpp builds differ:
			// - some: get_binary() returns std::shared_ptr<std::string const>
			// - others: get_binary() returns std::string
			//
			// We'll support both patterns by using auto and type inspection.

			json out = json::array();

			// Pattern A: shared_ptr<string const>
			// (this is what your compiler error suggests)
			try {
				const auto& binPtr = msg->get_binary(); // likely shared_ptr<const std::string>
				if (binPtr) {
					const std::string& s = *binPtr;      // <-- dereference fixes your error
					for (unsigned char b : s) {
						out.push_back(static_cast<int>(b));
					}
					return out;
				}
			} catch (...) {
				// fallthrough
			}

			// Pattern B: if get_binary() is a string in your build, you can adapt here.
			// If you hit this path and it still doesn't compile, tell me the exact signature
			// of get_binary() from your headers and I'll match it precisely.
			return out;
		}

		default:
			return nullptr;
	}
}

void TerminalWidget::reset_client() {
	ASSISTANT_V3_ID = "";
	
	if (ajm_asv3_client) {
		ajm_asv3_client->clear_socket_listeners();
		ajm_asv3_client->clear_con_listeners();

		std::thread([old_client = ajm_asv3_client]() {
			if (old_client->opened()) {
				old_client->sync_close();
			}
		}).detach();
	}

	ajm_asv3_client = std::make_shared<sio::client>();

	ajm_asv3_client->set_open_listener([&]() {
		App::displayText(icu::UnicodeString::fromUTF8("AssistantV3 Connection Made"));
		
		Verify::json payload = {
			{"CodeWizard", "Terminal"}
		};
		ajm_asv3_client->socket()->emit("register_terminal", jsonToSio(Verify::createPayload(payload)));
	});
	
	ajm_asv3_client->set_fail_listener([&]() {
		App::displayToast(icu::UnicodeString::fromUTF8("FAILED: AssistantV3 Connection"));
	});
	
	ajm_asv3_client->set_close_listener([&](sio::client::close_reason const& reason) {
		App::displayToast(icu::UnicodeString::fromUTF8("AssistantV3 Connection Closed."));
	});
	
	ajm_asv3_client->socket()->on("get_term_screen", [&](std::string const& name, sio::message::ptr const& data, bool isAck, sio::message::list &ack_resp) {
		auto res = Verify::verifyPayload(sioToJson(data));
		
		if (res == std::nullopt) {
			return;
		}
		
		std::string text = get_last_n_doc_lines(50);
		Verify::json payload = {
			{"content", text}
		};
		ajm_asv3_client->socket()->emit("terminal_functs_resp", jsonToSio(Verify::createPayload(payload)));
	});
	
	ajm_asv3_client->socket()->on("term_exec_command", [&](std::string const& name, sio::message::ptr const& data, bool isAck, sio::message::list &ack_resp) {
		auto res = Verify::verifyPayload(sioToJson(data));
		
		if (res == std::nullopt) {
			return;
		}
		
		std::string command_string = res->at("command").get<std::string>();
		
		bool is_raw = false;
		std::string latest = "";
		for (int i = 0; i < command_string.length(); i++) {
			char c = command_string.at(i);
			if (c == '\\') {
				if (is_raw) {
					latest += c; // this escape character was escaped. So, we add it as per regular
				}
				
				is_raw = !is_raw;
			}else{
				if (is_raw) {
					if (latest != "") {
						term->sendText(latest);
					}
					
					latest = "";
					
					if (c == 'C' || c == 'c') {
						term->sendCtrl('c');
					}else if (c == 'n') {
						term->sendEnter();
					}else if (c == 't') {
						term->sendText("\t");
					}else if (c == 'u') {
						term->sendSpecialKey(Terminal::SpecialKey::Up);
					}else if (c == 'd') {
						term->sendSpecialKey(Terminal::SpecialKey::Down);
					}else if (c == 'l') {
						term->sendSpecialKey(Terminal::SpecialKey::Left);
					}else if (c == 'r') {
						term->sendSpecialKey(Terminal::SpecialKey::Up);
					}else if (c == 'b') {
						term->sendBackspace();
					}
				}else{
					latest += c;
				}
				
				is_raw = false;
			}
		}
		
		if (latest != "") {
			term->sendText(latest);
		}
	});
}

void TerminalWidget::request_close(close_callback_type callback) {
	term->stop();
	
	std::cout << "Initiating instant UI close...";

	reset_client();

	std::cout << "UI is free!";
	
	// Now the widget can be deleted immediately
	Widget::request_close(callback);
}

Widget* TerminalWidget::findTerminal() {
	return this;
}

void TerminalWidget::runCommand(std::string command) {
	if (term == nullptr) {
		std::thread([&]() {
			run();
			
			term->sendCtrl('c');
			term->sendCtrl('c');
			term->sendText(command);
			term->sendEnter();
		}).detach();
		return;
	}
	
	term->sendCtrl('c');
	term->sendCtrl('c');
	term->sendText(command);
	term->sendEnter();
}

void TerminalWidget::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
	
	if (term == nullptr) {
		std::thread([&]() { run(); }).detach();
		return;
	}
	
	int width_cells = std::max((w-App::text_padding*2) / TextRenderer::get_text_width(1), 1);
	int height_cells = std::max((h-App::text_padding*2) / TextRenderer::get_text_height(), 1);
	
	if (!settingup && (prev_w_cells != width_cells || prev_h_cells != height_cells) && !glfwGetWindowAttrib(App::window, GLFW_ICONIFIED)) {
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
	
	int row_to_use = (prev_h_cells/2);
	int col_to_use = (prev_w_cells/2);
	
	OurCell bgcell = term->getCell(row_to_use, col_to_use);
	uint8_t bgr = bgcell.bg_red;
	uint8_t bgg = bgcell.bg_green;
	uint8_t bbg = bgcell.bg_blue;
	
	App::DrawRect(t_x, t_y, t_w, t_h, bgr, bgg, bbg);
//	App::DrawRect(t_x+App::text_padding, t_y+App::text_padding, TextRenderer::get_text_width(prev_w_cells), prev_h_cells*TextRenderer::get_text_height(), bgr, bgg, bbg);
	
	UChar32 empty = U' ';
	
	auto ci = term->getCursorInfo();
	int c_wid = TextRenderer::get_text_width(1)/4;
	
	for (int r = 0; r < prev_h_cells; r++) {
		for (int c = 0; c < prev_w_cells; c++) {
			OurCell cell = term->getCell(r, c);
			
			int x = t_x+App::text_padding+TextRenderer::get_text_width(c);
			int y = t_y+App::text_padding+TextRenderer::get_text_height()*r;
			
			// inside the (r,c) loop, after computing x,y and before drawing text
			bool show_local_sel = !term->appWantsMouse();
			if (show_local_sel && cell_in_selection(r, c)) {
				App::DrawRect(x, y, TextRenderer::get_text_width(1), TextRenderer::get_text_height(), App::theme.white);
				
				cell.fg_red = 255-cell.fg_red;
				cell.fg_green = 255-cell.fg_green;
				cell.fg_blue = 255-cell.fg_blue;
			}else if (cell.bg_red != bgr || cell.bg_green != bgg || cell.bg_blue != bbg) {
				App::DrawRect(x, y, TextRenderer::get_text_width(1), TextRenderer::get_text_height(), cell.bg_red, cell.bg_green, cell.bg_blue);
			}
			
			
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
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
	
	Widget::render();
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
	if (!is_visible || App::activeLeafNode != this) { return false; }
	
	if (!term || settingup) return false;
	if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

	bool shift=false, alt=false, ctrl=false;
	mods_to_bools(mods, shift, alt, ctrl);
	
	if (ctrl && key == GLFW_KEY_C) {
		if (!term->appWantsMouse()) {
			std::string txt = selection_text();
			if (!txt.empty()) {
				SetClipboardText(txt.c_str());
				clear_selection();
				return true;
			}
		}
	}
	
	Terminal::SpecialKey sk;
	if ((ctrl && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) || key == GLFW_KEY_ESCAPE || map_special_key(key, sk)) {
		clear_selection();
	}
	
	// Enter / Backspace / Tab / Escape
	if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
		return term->sendEnter();
	} else if (key == GLFW_KEY_BACKSPACE) {
		return term->sendBackspace();
	} else if (key == GLFW_KEY_TAB) {
		return term->sendText("\t");
	} else if (key == GLFW_KEY_ESCAPE) {
		return term->sendText("\x1b");
	} else if (key == GLFW_KEY_V && ctrl) {
		return term->sendText(GetClipboardText());
	}
	
	// Ctrl+Letter (ASCII control)
	if (ctrl && key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
		char letter = static_cast<char>('A' + (key - GLFW_KEY_A));
		return term->sendCtrl(letter);
	}

	// Special keys (arrows, Home/End, PgUp/PgDn, F-keys) with modifiers
	
	if (map_special_key(key, sk)) {
		return term->sendSpecialKey(sk, shift, alt, ctrl);
	}

	// Let printable characters be handled by on_char_event (GLFW will emit it)
	return false;
}

bool TerminalWidget::on_char_event(unsigned int keycode) {
	if (!is_visible || App::activeLeafNode != this) { return false; }
	
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
	
	std::string txt = selection_text();
	if (!txt.empty()) {
		clear_selection();
	}
	
	return term->sendText(utf8);
}

bool TerminalWidget::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible || !term || settingup) return false;
	
	if (ajm_asv3_tm->parent == this) {
		if (ajm_asv3_tm->on_mouse_button_event(button, action, mods)) {
			return true;
		}
	}

	bool shift=false, alt=false, ctrl=false;
	mods_to_bools(mods, shift, alt, ctrl);
	
	int row=0, col=0;
	cell_from_cursor(row, col);
	if (row < 0 || col < 0) return false;
	
	if (App::activeLeafNode != this && action == GLFW_PRESS) App::setActiveLeafNode(this);
	
	const bool left   = (button == GLFW_MOUSE_BUTTON_LEFT);
	const bool press  = (action == GLFW_PRESS);
	const bool release= (action == GLFW_RELEASE);
	
	// --- Terminal-side selection when the app does NOT want mouse ---
	if (left && !term->appWantsMouse()) {
		if (press) {
			selecting = true;
			int doc = term->docLineIdForScreenRow(row);
			sel_doc_r1 = doc;
			sel_c1 = col;
			if (!shift) {
				sel_doc_r0 = sel_doc_r1;
				sel_c0 = sel_c1;
			}
			return true;
		} else if (release && selecting) {
			sel_doc_r1 = term->docLineIdForScreenRow(row);
			sel_c1 = col;
			selecting = false;
			return true;
		}
	}
	
	// otherwise: forward to the app (nvim/tui, etc.)
	int b = 0;
	if      (button == GLFW_MOUSE_BUTTON_LEFT)   b = 0;
	else if (button == GLFW_MOUSE_BUTTON_MIDDLE) b = 1;
	else if (button == GLFW_MOUSE_BUTTON_RIGHT)  b = 2;
	else return false;
	
	s_dragging = press ? true : s_dragging && press;
	bool ok = term->mousePress(row, col, b, press, shift, alt, ctrl);
	if (release) s_dragging = false;
	return ok;
}

bool TerminalWidget::on_mouse_move_event() {
	if (!is_visible || !term || settingup) return false;
	
	ajm_asv3_tm->on_mouse_move_event();
	
	int row=0, col=0;
	cell_from_cursor(row, col);
	
	// Grow selection locally if app doesn't want mouse
	if (!term->appWantsMouse()) {
		
		if (selecting) {
			int state = glfwGetMouseButton(App::window, GLFW_MOUSE_BUTTON_LEFT);
			if (state != GLFW_PRESS) { selecting = false; return false; }
			
			sel_doc_r1 = term->docLineIdForScreenRow(row);
			sel_c1 = col;
			return true;
		}
		return false;
	}
	
	// otherwise forward motion
	return term->mouseMove(row, col, /*buttonHeld*/ s_dragging);
}

bool TerminalWidget::on_scroll_event(double /*xchange*/, double ychange) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x + t_w || my < t_y || my > t_y + t_h) return false;
	
	if (!is_visible) { return false; }
	
	if (!term || settingup) return false;
	
	int row=0, col=0;
	cell_from_cursor(row, col);
	
	ychange *= -6;
	
	int lines = static_cast<int>( (ychange > 0) ? std::floor(ychange + 0.5) : std::ceil (ychange - 0.5) );
	if (lines == 0) return true;
	
	return term->mouseScroll(row, col, lines);
}

bool TerminalWidget::cell_in_selection(int screen_r, int c) const {
	if (sel_doc_r0 == sel_doc_r1 && sel_c0 == sel_c1) { return false; }
	
	if (!selecting && (sel_doc_r0 < 0 || sel_doc_r1 < 0)) return false;

	// Map this screen row to its stable document id
	int doc = term->docLineIdForScreenRow(screen_r);
	if (doc < 0) return false;

	int r0 = sel_doc_r0, c0 = sel_c0, r1 = sel_doc_r1, c1 = sel_c1;
	normalize_sel(r0, c0, r1, c1);

	if (doc < r0 || doc > r1) return false;
	if (r0 == r1)  return (c >= c0 && c <= c1);
	if (doc == r0) return (c >= c0);
	if (doc == r1) return (c <= c1);
	return true;
}

void TerminalWidget::clear_selection() {
	selecting = false;
	sel_doc_r0 = sel_c0 = sel_doc_r1 = sel_c1 = -1;
}

std::string TerminalWidget::selection_text() const {
	if (sel_doc_r0 < 0 || sel_doc_r1 < 0) return {};

	int r0 = sel_doc_r0, c0 = sel_c0, r1 = sel_doc_r1, c1 = sel_c1;
	normalize_sel(r0, c0, r1, c1);

	// Use terminal's authoritative width to walk columns
	const int W = term->docCols();
	std::string out;
	out.reserve((r1 - r0 + 1) * (W + 1)); // rough prealloc

	auto append_utf8 = [&](char32_t cp, std::string& s) {
		char u[5] = {0};
		if (cp < 0x80) { u[0] = static_cast<char>(cp); s.append(u, u+1); }
		else if (cp < 0x800) {
			u[0] = static_cast<char>(0xC0 | (cp >> 6));
			u[1] = static_cast<char>(0x80 | (cp & 0x3F));
			s.append(u, u+2);
		} else if (cp < 0x10000) {
			u[0] = static_cast<char>(0xE0 | (cp >> 12));
			u[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			u[2] = static_cast<char>(0x80 | (cp & 0x3F));
			s.append(u, u+3);
		} else {
			u[0] = static_cast<char>(0xF0 | (cp >> 18));
			u[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			u[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			u[3] = static_cast<char>(0x80 | (cp & 0x3F));
			s.append(u, u+4);
		}
	};

	for (int doc = r0; doc <= r1; ++doc) {
		const int start_c = (doc == r0 ? c0 : 0);
		const int end_c   = (doc == r1 ? c1 : (W - 1));

		std::string line;
		line.reserve(end_c - start_c + 1);

		for (int c = start_c; c <= end_c; ++c) {
			OurCell cell{};
			if (!term->getDocCell(doc, c, cell)) {
				// Out of range of the document; stop this line gracefully
				break;
			}

			// Treat cell.c == 0 as space; otherwise encode UTF-8
			char32_t cp = static_cast<char32_t>(cell.c ? cell.c : U' ');
			append_utf8(cp, line);
		}

		// Trim right spaces (so we don't copy terminal padding)
		while (!line.empty() && line.back() == ' ') line.pop_back();

		out += line;
		
		if (!term->getDocWraps(doc+1) && doc != r1) {
			out += '\n';
		}
		
	}
	return out;
}

std::string TerminalWidget::get_last_n_doc_lines(int n) {
	if (!term) return {};
	if (n <= 0) return {};

	// Pick the most recent doc line by mapping the *bottom visible screen row*.
	// If your viewport can scroll, this returns "bottom of what you're viewing".
	const int bottom_screen_row = prev_h_cells - 1;
	int end_doc = term->docLineIdForScreenRow(bottom_screen_row);
	if (end_doc < 0) return {};

	int start_doc = end_doc - (n - 1);
	if (start_doc < 0) start_doc = 0;

	// Full-width selection (so selection_text() returns whole lines)
	const int W = term->docCols();
	if (W <= 0) return {};

	// Save selection state (so we don't mess with the user's selection)
	int saved_r0 = sel_doc_r0, saved_c0 = sel_c0, saved_r1 = sel_doc_r1, saved_c1 = sel_c1;
	bool saved_selecting = selecting;

	// Set selection to the last N doc lines
	selecting  = false;
	sel_doc_r0 = start_doc;
	sel_c0     = 0;
	sel_doc_r1 = end_doc;
	sel_c1     = W - 1;

	std::string out = selection_text();

	// Restore prior selection state
	selecting  = saved_selecting;
	sel_doc_r0 = saved_r0; sel_c0 = saved_c0; sel_doc_r1 = saved_r1; sel_c1 = saved_c1;

	return out;
}
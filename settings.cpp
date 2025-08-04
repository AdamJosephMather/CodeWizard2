#include "settings.h"
#include "text_renderer.h"
#include "application.h"

SettingsFloat* Settings::makeFloat(std::string name, std::string key, float default_value) {
	auto f = new SettingsFloat();
	f->name = name;
	f->key_name = key;
	f->default_value = default_value;
	f->value = App::settings->getValue(key, default_value);
	f->set = (f->value != f->default_value);
	return f;
}

SettingsInt* Settings::makeInt(std::string name, std::string key, int default_value) {
	auto f = new SettingsInt();
	f->name = name;
	f->key_name = key;
	f->default_value = default_value;
	f->value = App::settings->getValue(key, default_value);
	f->set = (f->value != f->default_value);
	return f;
}

SettingsBool* Settings::makeBool(std::string name, std::string key, bool default_value) {
	auto f = new SettingsBool();
	f->name = name;
	f->key_name = key;
	f->default_value = default_value;
	f->value = App::settings->getValue(key, default_value);
	f->set = (f->value != f->default_value);
	return f;
}

SettingsString* Settings::makeString(std::string name, std::string key, std::string default_value) {
	auto f = new SettingsString();
	f->name = name;
	f->key_name = key;
	f->default_value = default_value;
	f->value = App::settings->getValue(key, default_value);
	f->set = (f->value != f->default_value);
	return f;
}

SettingsLabel* Settings::makeLabel(std::string name) {
	auto f = new SettingsLabel();
	f->name = name;
	return f;
}

Settings::Settings(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Settings");
	
	tab_bar = new Tabs(this);
	tab_bar->has_close_button = false;
	tab_bar->can_add_new = false;
	
	tab_bar->tab_clicked_callback = [&](TabInfo info){
		if (info.id == 2) {
			// project specifics
			bool worked = App::settings->makeProjectSettings();
			if (auto el = dynamic_cast<SettingsLabel*>(settings_menus[2][0])) {
				if (worked) {
					el->default_value = App::settings->getProjectSettingsPath();
				}else{
					el->default_value = "Failed to initialize project settings.";
				}
			}
		}
		
		handleChildren();
	};
	
	
	
	
	settings_menus[0] = {
		makeFloat(
			"Font Size",
			"font_size",
			24.0f
		),
		makeString(
			"Font Path",
			"font_path",
			getExecutableDir()+"\\cascadia\\CascadiaCode-Regular.ttf"
		),
		makeString(
			"Tint Color",
			"c_tint_color",
			colorToString(App::theme.tint_color)
		),
		makeString(
			"Strings Colors",
			"c_strings_color",
			colorToString(App::theme.syntax_colors[1])
		),
		makeString(
			"Comments Color",
			"c_comments_color",
			colorToString(App::theme.syntax_colors[2])
		),
		makeString(
			"Variables Color",
			"c_vars_color",
			colorToString(App::theme.syntax_colors[3])
		),
		makeString(
			"Types Color",
			"c_types_color",
			colorToString(App::theme.syntax_colors[4])
		),
		makeString(
			"Functions Color",
			"c_functs_color",
			colorToString(App::theme.syntax_colors[5])
		),
		makeString(
			"Keywords Color",
			"c_keywords_color",
			colorToString(App::theme.syntax_colors[6])
		),
		makeString(
			"Punctuation Color",
			"c_punctuation_color",
			colorToString(App::theme.syntax_colors[7])
		),
		makeString(
			"Literals Color",
			"c_literals_color",
			colorToString(App::theme.syntax_colors[8])
		),
	};
	
	settings_menus[1] = {
		makeBool(
			"Use Modal Editor",
			"use_vim",
			false
		),
		makeBool(
			"Use Relative Line Numbers",
			"use_rel_line_num",
			false
		),
		makeBool(
			"Use File Tabs",
			"use_tabs",
			true
		),
		makeBool(
			"Invert Scroll Vertical",
			"invert_scroll_v",
			false
		),
		makeBool(
			"Invert Scroll Horizontal",
			"invert_scroll_h",
			false
		),
		makeInt(
			"Max Files To Index",
			"max_index_files",
			2000
		),
		makeString(
			"AI Model ID",
			"lm_studio_model_id",
			"qwen2.5-coder-1.5b-instruct@q4_k_m"
		),
		makeString(
			"AI Model Provider",
			"ai_model_url",
			"http://localhost:1234/api/v0"
		),
		makeString(
			"AI Model API Key",
			"ai_model_api_key",
			""
		),
		makeInt(
			"Context Lines",
			"lm_studio_context_lines",
			30
		),
		makeInt(
			"Max New Tokens (Only Non-Chat Completion)",
			"lm_studio_max_tokens",
			30
		),
		makeBool(
			"Load AI Model On Start",
			"lm_load_model_on_start",
			false
		),
		makeBool(
			"AI Provider Supports Non-Chat Completions",
			"use_non_chat_completions",
			true
		)
	};
	
	settings_menus[2] = {
		makeLabel("Project Settings Located At (not editable):")
	};
	
	std::vector<std::string> tabs = {
		"Appearance",
		"Editor",
		"Project Specific"
	};
	
	int tabid = 0;
	
	for (auto str : tabs) {
		auto ti = TabInfo();
		
		ti.title = icu::UnicodeString::fromUTF8(str);
		ti.id = tabid;
		
		tab_bar->addTab(ti);
		
		tabid ++;
	}
	
	handleChildren();
}

void Settings::handleChildren() {
	edits_to_element.clear();
	elements_to_edit.clear();
	
	App::activeLeafNode = nullptr;
	
	auto copy = children;
	
	for (auto c : copy) {
		if (c && c != tab_bar) {
			c->request_close([&](Widget* w){
				close_callback(w);
			});
		}
	}
	
	for (auto el : settings_menus[tab_bar->selected_id]) {
		TextEdit* e = new TextEdit(this, [&](Widget* t){
			auto el = edits_to_element[dynamic_cast<TextEdit*>(t)];
			SettingsElement* before = nullptr;
			
			for (auto te : settings_menus[tab_bar->selected_id]) {
				if (te == el) {
					break; // this loop finds the element directly prior to this one
				}
				before = te;
			}
			
			int y = 0; // we apply the scrolled to on the first element so that it will propegate down to the rest
			if (before != nullptr) {
				TextEdit* prior = elements_to_edit[before];
				y = prior->t_y+prior->t_h; // all that just to get the y location... maybe there's a problem with my system. Oh well, it's too late now y'all
			}else{
				y = t_y+TextRenderer::get_text_height()+20-scrolled_to;
			}
			
			t->t_y = y+TextRenderer::get_text_height()+5;
			t->t_h = TextRenderer::get_text_height()+10;
			t->t_x = t_x+10;
			t->t_w = TextRenderer::get_text_width(40);
			
			max_scroll = fmax(0, (t->t_y+t->t_h+scrolled_to)-(t_h-tab_bar->t_h)-(t->t_h)*2);
		});
		
		setWithValue(e, el);
		
		edits_to_element[e] = el;
		elements_to_edit[el] = e;
	}
}

void Settings::setWithValue(TextEdit* e, SettingsElement* el) {
	if (auto i = dynamic_cast<SettingsInt*>(el)) {
		e->setFullText(icu::UnicodeString::fromUTF8(std::to_string(i->value)));
	}else if (auto i = dynamic_cast<SettingsFloat*>(el)) {
		e->setFullText(icu::UnicodeString::fromUTF8(std::to_string(i->value)));
	}else if (auto i = dynamic_cast<SettingsString*>(el)) {
		e->setFullText(icu::UnicodeString::fromUTF8(i->value));
	}else if (auto i = dynamic_cast<SettingsBool*>(el)) {
		icu::UnicodeString vl = icu::UnicodeString::fromUTF8("false");
		if (i->value) {
			vl = icu::UnicodeString::fromUTF8("true");
		}
		e->setFullText(vl);
	}else if (auto i = dynamic_cast<SettingsLabel*>(el)) {
		icu::UnicodeString vl = icu::UnicodeString::fromUTF8(i->default_value);
		e->setFullText(vl);
	}
}

void Settings::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	App::runWithSKIZ(tab_bar->t_x, tab_bar->t_y, tab_bar->t_w, tab_bar->t_h, [&](){
		tab_bar->render();
	});
	
	App::runWithSKIZ(t_x, tab_bar->t_y+tab_bar->t_h, t_w, t_h-tab_bar->t_h, [&](){
		for (auto it : elements_to_edit) {
			int y = it.second->t_y-TextRenderer::get_text_height()-5;
			int x = t_x+10;
			icu::UnicodeString text = icu::UnicodeString::fromUTF8(it.first->name);
			
			TextRenderer::draw_text(x, y, text, App::theme.main_text_color);
			App::runWithSKIZ(it.second->t_x, it.second->t_y, it.second->t_w, it.second->t_h, [&](){
				it.second->render();
			});
		}
	});
}

void Settings::handleChange(TextEdit* te, SettingsElement* se) {
	icu::UnicodeString text = te->getFullText();
	
	std::string str;
	text.toUTF8String(str);
	
	if (auto i = dynamic_cast<SettingsString*>(se)) {
		if (str == "") {
			i->value = i->default_value;
		}else{
			i->value = str;
		}
		
		if (validate_input(i)) {
			App::settings->setValue(se->key_name, str);
		}
	}else if (auto i = dynamic_cast<SettingsBool*>(se)) {
		if (str == "true") {
			i->value = true;
		}else if (str == "false") {
			i->value = false;
		}
		
		if (validate_input(i)) {
			App::settings->setValue(se->key_name, i->value);
		}
	}else if (auto i = dynamic_cast<SettingsInt*>(se)) {
		int result = -1;
		try {
			size_t pos;
			result = std::stoi(str, &pos);
			if (pos != str.length() || str.length() == 0) {
				result = -1;
			}
		} catch (...) {
			
		}
		
		if (result >= 0 && i->value != result) {
			i->value = result;
			if (validate_input(i)) {
				App::settings->setValue(se->key_name, result);
			}
		}
	}else if (auto i = dynamic_cast<SettingsFloat*>(se)) {
		float result = -1;
		try {
			size_t pos;
			result = std::stof(str, &pos);
			if (pos != str.length() || result > 50 || str.length() == 0) {
				result = -1;
			}
		} catch (...) {
			
		}
		
		if (result >= 0 && i->value != result) {
			i->value = result;
			if (validate_input(i)) {
				App::settings->setValue(se->key_name, result);
			}
		}
	}
	
	setWithValue(te, se);
}

bool Settings::on_key_event(int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ENTER && (action == GLFW_PRESS || action == GLFW_RELEASE)){
		if (auto te = dynamic_cast<TextEdit*>(App::activeLeafNode)){
			auto it = edits_to_element.find(te);
			if (it != edits_to_element.end()) {
				handleChange(it->first, it->second);
				App::setActiveLeafNode(nullptr);
				return true;
			}
		}
	}
	
	return Widget::on_key_event(key, scancode, action, mods);
}

bool Settings::on_scroll_event(double xchange, double ychange) {
	if (Widget::on_scroll_event(xchange, ychange)) {
		return true;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x + t_w || my < t_y || my > t_y + t_h) {
		return false;
	}
	
	scrolled_to += ychange*40;
	
	if (scrolled_to < 0) {
		scrolled_to = 0;
	}else if (scrolled_to > max_scroll) {
		scrolled_to = max_scroll;
	}
	return true;
}

bool Settings::on_mouse_button_event(int button, int action, int mods) {
	if (!Widget::on_mouse_button_event(button, action, mods)) {
		if (action == GLFW_RELEASE) {
			return false;
		}
		
		if (auto te = dynamic_cast<TextEdit*>(App::activeLeafNode)){
			auto it = edits_to_element.find(te);
			if (it != edits_to_element.end()) {
				handleChange(it->first, it->second);
				App::setActiveLeafNode(nullptr);
				return true;
			}
		}
	}
	return false;
}

void Settings::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
}

bool Settings::validate_input(SettingsFloat* el) {
	if (el->key_name == "font_size") {
		if (el->value < 8 || el->value > 40) {
			el->value = el->default_value;
			return false;
		}
		
		TextRenderer::set_font_size(el->value);
		std::string default_font_path = getExecutableDir()+"\\cascadia\\CascadiaCode-Regular.ttf";
		std::string font_path = App::settings->getValue("font_path", default_font_path);
		bool success = TextRenderer::init_font(font_path.c_str());
		
		if (!success) {
			TextRenderer::init_font(default_font_path.c_str());
		}
	}
	
	return true;
}

bool Settings::validate_input(SettingsInt* el) {
	if (el->key_name == "max_index_files") {
		if (el->value < 1) {
			el->value = el->default_value;
			return false;
		}
	}
	
	return true;
}

bool Settings::validate_input(SettingsBool* el) {
	
	
	return true;
}

bool handleColor(bool syntax, SettingsString* el, int indx) {
	bool worked;
	Color c = stringToColor(el->value, worked);
	if (!worked) {
		el->value = el->default_value;
		return false;
	}
	
	if (syntax) {
		App::theme.syntax_colors[indx]->r = c.r;
		App::theme.syntax_colors[indx]->g = c.g;
		App::theme.syntax_colors[indx]->b = c.b;
	}else if (indx == -1){ // handle non syntax-colors here
		App::theme.tint_color->r = c.r;
		App::theme.tint_color->g = c.g;
		App::theme.tint_color->b = c.b;
	}
	return true;
}

bool Settings::validate_input(SettingsString* el) {
	if (el->key_name == "font_path") {
		if (!std::filesystem::exists(el->value)) {
			std::cout << "Path: " << el->value << "\ndoes not exist.\n";
			el->value = el->default_value;
			return false;
		}
		
		std::string default_font_path = el->default_value;
		std::string font_path = el->value;
		
		bool success = TextRenderer::init_font(font_path.c_str()); // Or whatever .ttf you have
		
		if (!success) { // even with no sucess let's leave it be for now (we load the default font anyways so...).
			TextRenderer::init_font(default_font_path.c_str());
		}
		
		return true;
	}else if (el->key_name == "c_strings_color") {
		return handleColor(true, el, 1);
	}else if (el->key_name == "c_comments_color") {
		return handleColor(true, el, 2);
	}else if (el->key_name == "c_vars_color") {
		return handleColor(true, el, 3);
	}else if (el->key_name == "c_types_color") {
		return handleColor(true, el, 4);
	}else if (el->key_name == "c_functs_color") {
		return handleColor(true, el, 5);
	}else if (el->key_name == "c_keywords_color") {
		return handleColor(true, el, 6);
	}else if (el->key_name == "c_punctuation_color") {
		return handleColor(true, el, 7);
	}else if (el->key_name == "c_literals_color") {
		return handleColor(true, el, 8);
	}else if (el->key_name == "c_tint_color") {
		bool worked = handleColor(false, el, -1);
		App::updateFromTintColor(&App::theme);
		return worked;
	}
	
	return true;
}

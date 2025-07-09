#pragma once

#include "tabs.h"
#include "textedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>

struct SettingsElement {
	std::string name;
	std::string key_name;
	bool set = false;
	virtual ~SettingsElement() = default; // make base polymorphic
};

struct SettingsInt : SettingsElement {
	int default_value;
	int value = 0;
};

struct SettingsFloat : SettingsElement {
	float default_value;
	float value = 0.0f;
};

struct SettingsBool : SettingsElement {
	bool default_value;
	bool value = false;
};

struct SettingsString : SettingsElement {
	std::string default_value;
	std::string value = "";
};

struct SettingsLabel : SettingsElement {
	std::string default_value;
};

class Settings : public Widget {
public:
	Settings(Widget* parent);
	Tabs* tab_bar;
	
	int scrolled_to = 0;
	int max_scroll = 0;
	std::unordered_map<int, std::vector<SettingsElement*>> settings_menus;
	std::unordered_map<TextEdit*, SettingsElement*> edits_to_element;
	std::unordered_map<SettingsElement*, TextEdit*> elements_to_edit;
	
	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	SettingsFloat* makeFloat(std::string name, std::string key, float default_value);
	SettingsInt* makeInt(std::string name, std::string key, int default_value);
	SettingsBool* makeBool(std::string name, std::string key, bool default_value);
	SettingsString* makeString(std::string name, std::string key, std::string default_value);
	SettingsLabel* makeLabel(std::string name);
	
	void handleChildren();
	void handleChange(TextEdit* te, SettingsElement* se);
	void setWithValue(TextEdit* e, SettingsElement* el);
	
	bool validate_input(SettingsFloat* el);
	bool validate_input(SettingsInt* el);
	bool validate_input(SettingsBool* el);
	bool validate_input(SettingsString* el);
	
	void position(int x, int y, int w, int h);
	void render();
private:
};
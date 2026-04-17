#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <vector>
#include "unicode/unistr.h"

enum WidgetActionType {
	RESTART_LSP,
	AJM_SETTINGS_CHANGE,
	SETTINGS_CHANGE
};

class Widget {
public:
	Widget() : parent(nullptr) {}
	Widget(Widget* parent);
	virtual ~Widget() = default;  // now deletes are safe
	
	using close_callback_type = std::function<void(Widget*)>;
	
	bool is_visible = true;
	bool clickthrough = false;
	
	Widget* parent;
	std::vector<Widget*> children;
	
	int t_x = 1;
	int t_y = 1;
	int t_w = 1;
	int t_h = 1;
	
	icu::UnicodeString id = icu::UnicodeString::fromUTF8("UNNAMED");
	
	bool closing = false;
	
	close_callback_type on_close = nullptr;
	close_callback_type before_self_close = nullptr;
	
	virtual bool on_key_event(int key, int scancode, int action, int mods);
	virtual bool on_char_event(unsigned int keycode);
	virtual bool on_mouse_button_event(int button, int action, int mods);
	virtual bool on_mouse_move_event();
	virtual bool on_scroll_event(double xchange, double ychange);
	
	virtual void position(int x, int y, int width, int height); // the individual widgets will store the info they need. This here provides the space in which it **can** be placed
	virtual void render();
	
	virtual void hide();
	virtual void show();
	
	virtual void request_close(close_callback_type callback);
	virtual void close_callback(Widget*);
	
	virtual bool widgetexists(Widget*);
	
	virtual void save();
	virtual Widget* fileOpen(std::string fname);
	virtual Widget* getFirstEditor();
	virtual Widget* findTerminal();
	virtual void lspmessage(std::string& from, std::string& message);
	
	virtual std::vector<std::vector<std::string>> getOpenFiles(bool includeText);
	virtual int openUnnamedFile(int count);
	
	virtual void executeAction(WidgetActionType typ);
};
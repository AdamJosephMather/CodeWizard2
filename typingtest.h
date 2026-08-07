#pragma once

#include "button.h"
#include "widget.h"
#include <chrono>
#include <random>

enum TestTypes {
	Time,
	Words
};

struct Results {
	std::chrono::steady_clock::duration time;
	float words;
	MST::MonoString showtime;
	MST::MonoString showacc;
};

struct CharEntry {
	MST::u32 correct;
	MST::u32 typed;
};

enum TestStatus {
	Idle,
	Testing,
	ShowingResults
};

class TypingTest : public Widget {
public:
	TypingTest(Widget* parent);
	
	Button* timeWords;
	Button* swapInputs;
	
	int oldWidth = -1;
	
	TestTypes curType = TestTypes::Time;
	
	std::vector<CharEntry> test;
	std::vector<int> lineStarts;
	
	std::random_device rd;
	std::mt19937 gen;
	std::uniform_int_distribution<size_t> distr;
	
	Results res;
	TestStatus status = Idle;
	std::chrono::steady_clock::time_point started_at;
	int location = 0;
	MST::MonoString displayDuringTest;
	
	void updateWords();
	void resetTest();
	void startTest();
	void endTest();
	void updatePositioning();
	
	bool on_key_event(int key, int scancode, int action, int mods) override;
	bool on_char_event(unsigned int keycode) override;
	bool on_mouse_button_event(int button, int action, int mods) override;
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h) override;
	void render() override;
private:
};

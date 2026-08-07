#include "typingtest.h"
#include "text_renderer.h"
#include "application.h"
#include <random>
#include "all_words.h"

TypingTest::TypingTest(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("TypingTest");
	
	gen = std::mt19937(rd());
	distr = std::uniform_int_distribution<size_t>(0, std::size(all_words) - 1);
	
	timeWords = new Button(this, MST::toMonoString("Time"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+TextRenderer::get_text_height();
		btn->t_y = t_y+TextRenderer::get_text_height();
	},  [&](Button* btn){
		// onclick
		if (curType == Words) {
			curType = Time;
			timeWords->BUTTON_LABEL = MST::toMonoString("Time");
		}else {
			curType = Words;
			timeWords->BUTTON_LABEL = MST::toMonoString("Words");
		}
		
		resetTest();
	});
	timeWords->rounded = true;
	
	swapInputs = new Button(this, MST::toMonoString("Switch"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = timeWords->t_x+timeWords->t_w+TextRenderer::get_text_height();
		btn->t_y = t_y+TextRenderer::get_text_height();
	},  [&](Button* btn){
		// onclick
		
		resetTest();
	});
	swapInputs->rounded = true;
	
	resetTest();
}

void TypingTest::updateWords() {
	int count = 50;
	
	if (curType == Time) {
		count = 400; // in 30 seconds how many do we think is possible to type... we'll add more if needed I guess
	}
	
	test.clear();
	
	int length = std::size(all_words);
	
	int fancy = length*0.04;
	float constant = std::log2(length+fancy);
	float coef     = length/(constant-std::log2(fancy));
	
	for (int i = 0; i < count; i++) {
		int random_num = distr(gen);
		random_num = (constant - std::log2(random_num+fancy)) * coef;
		
		if (random_num > length-1) {
			random_num = length-1;
		}
		
		std::string word = all_words[random_num];
		for (char c : word) {
			CharEntry e;
			e.correct = c;
			e.typed = 0x0;
			
			test.push_back(e);
		}
		
		CharEntry e;
		e.correct = ' ';
		e.typed = 0x0;
		
		test.push_back(e);
	}
	
	updatePositioning();
}

void TypingTest::updatePositioning() {
	int maxCharsPerLine = (t_w*0.8) / TextRenderer::get_text_width(1);
	if (maxCharsPerLine <= 0) {
		maxCharsPerLine = 1;
	}
	
	lineStarts   = { 0 };
	int curStart =   0;
	int lastEnd  =   0;
	
	for (int i = 0; i < test.size(); i++) {
		int curLen = i - curStart + 1;
		
		if (test[i].correct == ' ') {
			lastEnd = i;
		}
		
		if (curLen <= maxCharsPerLine) {
			continue;
		}
		
		if (lastEnd == curStart) { // in this case we can't fit the word into a single line
			curStart = i+1;
		}else {
			curStart = lastEnd+1;
		}
		
		lineStarts.push_back(curStart);
		lastEnd = curStart;
	}
	
	lineStarts.push_back(test.size()); // indicates the end of the draw for the last line
}

void TypingTest::startTest() {
	started_at = std::chrono::steady_clock::now();
	status = Testing;
	
	if (curType == Time) {
		displayDuringTest = MST::toMonoString("30.0");
	}else {
		displayDuringTest = MST::toMonoString("0.0");
	}
}

void TypingTest::endTest() {
	auto now = std::chrono::steady_clock::now();
	
	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at);
	res = {};
	res.time = time;
	
	float words = 0;
	int wordLen = 0;
	int completedLen = 0;
	
	int correct = 0;
	int totaldone = 0;
	
	for (int i = 0; i < test.size(); i++) {
		if (test[i].correct == ' ') { // the test always ends with a space
			words += (float)completedLen/wordLen;
			
			if (completedLen == 0) {
				break;
			}
			
			wordLen = 0;
			completedLen = 0;
			continue;
		}
		
		wordLen += 1;
		
		if (test[i].typed != 0x0) {
			completedLen += 1;
			
			if (test[i].correct == test[i].typed) {
				correct += 1;
			}
			totaldone += 1;
		}
	}
	
	float minutes = std::chrono::duration<float>(time).count()/60;
	
	res.words = words;
	res.showtime = doubleToMonoString_pretty(words/minutes);
	res.showacc = doubleToMonoString_pretty(((float)correct/totaldone)*100);
	
	status = ShowingResults;
}

void TypingTest::resetTest() {
	for (int i = 0; i < test.size(); i++) {
		test[i].typed = 0x0;
	}
	
	status = Idle;
	location = 0;
	
	updateWords();
}

void TypingTest::position(int x, int y, int w, int h) {
	Widget::position(x, y, w, h);
	
	if (oldWidth != w) {
		oldWidth = w;
		updatePositioning();
	}
	
	if (status == Testing) {
		App::time_till_regular = 2;
		
		auto now = std::chrono::steady_clock::now();
		auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at);
		float seconds = std::chrono::duration<float>(time).count();
		
		if (curType == Time) {
			if (seconds >= 30) {
				endTest();
			}
			
			displayDuringTest = doubleToMonoString_pretty(30-seconds);
		}else{
			displayDuringTest = doubleToMonoString_pretty(seconds);
		}
	}
}

void TypingTest::render() {
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.darker_background_color);
	if (App::activeLeafNode == this) {
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.active_color, 5, App::text_padding);
	}else {
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}
	
	if (status == ShowingResults) {
		MST::MonoString displaySpeed = MST::toMonoString("Speed:    ") + res.showtime + MST::toMonoString(" WPM");
		MST::MonoString displayAcc   = MST::toMonoString("Accuracy: ") + res.showacc + MST::toMonoString("%");
		
		int x = t_x + (t_w - TextRenderer::get_text_width(displaySpeed.length))/2;
		int y = swapInputs->t_y + swapInputs->t_h + TextRenderer::get_text_height();
		
		TextRenderer::draw_text(x, y, displaySpeed, App::theme.main_text_color);
		TextRenderer::draw_text(x, y+TextRenderer::get_text_height(), displayAcc, App::theme.main_text_color);
		
		Widget::render();
		return;
	}
	
	int y = timeWords->t_y+timeWords->t_h+TextRenderer::get_text_height();
	
	for (int line = 0; line < lineStarts.size()-1; line++) {
		int x = t_x + t_w * 0.1;
		
		for (int i = lineStarts[line]; i < lineStarts[line+1]; i++) {
			if (test[i].correct == ' ') {
				if (test[i].correct != test[i].typed && test[i].typed != 0x0) {
					TextRenderer::draw_text(x, y, MST::toMonoString(test[i].typed), 255, 100, 100);
				}
			}else {
				if (test[i].correct == test[i].typed) {
					TextRenderer::draw_text(x, y, MST::toMonoString(test[i].correct), 100, 255, 100);
				}else if (test[i].typed == 0x0) {
					TextRenderer::draw_text(x, y, MST::toMonoString(test[i].correct), App::theme.lesser_text_color);
				}else{
					TextRenderer::draw_text(x, y, MST::toMonoString(test[i].correct), 255, 100, 100);
				}
			}
			
			x += TextRenderer::get_text_width(1);
		}
		y += TextRenderer::get_text_height();
	}
	
	if (status == Testing) {
		TextRenderer::draw_text(swapInputs->t_x + swapInputs->t_w+TextRenderer::get_text_height(), swapInputs->t_y+App::text_padding, displayDuringTest, App::theme.main_text_color);
	}
	
	Widget::render();
}

bool TypingTest::on_mouse_button_event(int button, int action, int mods) {
	Widget::on_mouse_button_event(button, action, mods);
	
	if (cursor_in_this) {
		if (App::activeLeafNode != this) {
			App::setActiveLeafNode(this);
		}
		return true;
	}
	
	return false;
}

bool TypingTest::on_key_event(int key, int scancode, int action, int mods) {
	if (!is_visible || !parent || this != App::activeLeafNode || (action != GLFW_PRESS && action != GLFW_REPEAT)) {
		return false;
	}
	
	if (status == Testing) {
		if (key == GLFW_KEY_BACKSPACE) {
			if (location >= 1) {
				location -= 1;
				test[location].typed = 0x0;
			}
		}
		
		return true;
	}else if (status == ShowingResults) {
		if (key == GLFW_KEY_ENTER) {
			resetTest();
		}
	}
	
	return false;
}

bool TypingTest::on_char_event(unsigned int codepoint) {
	if (!is_visible || !parent || this != App::activeLeafNode) {
		return false;
	}
	
	if (status == ShowingResults) {
		return true;
	}
	
	MST::u32 ch = static_cast<MST::u32>(codepoint);
	
	if (ch == '\t' || ch == '\n') {
		return true;
	}
	
	App::time_till_regular = 2;
	
	if (status == Idle) {
		startTest();
	}
	
	if (status == Testing || status == Idle) {
		test[location].typed = ch;
	}
	
	if (location < test.size()-2) { // 2 because the last item is a space so we can't see it even if we wanted to
		location += 1;
	}else {
		endTest();
	}
	
	return true;
}
#include "widgetchooser.h"
#include "button.h"
#include "chat.h"
#include "compare.h"
#include "editor.h"
#include "filetree.h"
#include "settings.h"
#include "text_renderer.h"
#include "application.h"

WidgetChooser::WidgetChooser(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Widgetchooser");
	
	auto text = icu::UnicodeString::fromUTF8("Editor View");
	b1 = new Button(this, text, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = t_y+50;
	}, [&](Button* button) {
		std::cout << "Init editor in relacement\n";
		App::ReplaceWith(this, new Editor(nullptr));
		delete this;
	});
	
	auto text2 = icu::UnicodeString::fromUTF8("File Tree");
	b2 = new Button(this, text2, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b1->t_y+b1->t_h+20;
	}, [&](Button* button) {
		App::ReplaceWith(this, new FileTree(nullptr));
		std::cout << "Creating file tree\n";
		delete this;
	});
	
	auto text3 = icu::UnicodeString::fromUTF8("Settings Menu");
	b3 = new Button(this, text3, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b2->t_y+b2->t_h+20;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Settings(nullptr));
		std::cout << "Creating settings menu\n";
		delete this;
	});
	
	auto text4 = icu::UnicodeString::fromUTF8("Compare Two Files");
	b4 = new Button(this, text4, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b3->t_y+b3->t_h+20;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Compare(nullptr, [&](Widget* w){
			return;
		}));
		std::cout << "Creating compare menu\n";
		delete this;
	});
	
	auto text5 = icu::UnicodeString::fromUTF8("AI Chat");
	b5 = new Button(this, text5, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b4->t_y+b4->t_h+20;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Chat(nullptr));
		std::cout << "Creating chat menu\n";
		delete this;
	});
}

void WidgetChooser::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	auto txt = icu::UnicodeString::fromUTF8("Widget chooser");
	TextRenderer::draw_text(t_x+t_w/2-TextRenderer::get_text_width(txt.length())/2, t_y+10, txt, App::theme.main_text_color);
	
	Widget::render();
}

void WidgetChooser::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
}

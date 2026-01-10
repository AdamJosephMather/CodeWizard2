#include "widgetchooser.h"
#include "button.h"
#include "chat.h"
#include "compare.h"
#include "editor.h"
#include "filetree.h"
#include "lspdebug.h"
#include "settings.h"
#include "text_renderer.h"
#include "application.h"
#include "terminalwidgettabbed.h"

WidgetChooser::WidgetChooser(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Widgetchooser");
	
	auto text = icu::UnicodeString::fromUTF8("Editor View");
	b1 = new Button(this, text, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = t_y+App::text_padding*2+TextRenderer::get_text_height();
	}, [&](Button* button) {
		std::cout << "Init editor in relacement\n";
		App::ReplaceWith(this, new Editor(nullptr));
		delete this;
	});
	b1->rounded = true;
	b1->border = true;
	
	auto text2 = icu::UnicodeString::fromUTF8("File Tree");
	b2 = new Button(this, text2, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b1->t_y+b1->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new FileTree(nullptr));
		std::cout << "Creating file tree\n";
		delete this;
	});
	b2->rounded = true;
	b2->border = true;
	
	auto text3 = icu::UnicodeString::fromUTF8("Settings Menu");
	b3 = new Button(this, text3, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b2->t_y+b2->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Settings(nullptr));
		std::cout << "Creating settings menu\n";
		delete this;
	});
	b3->rounded = true;
	b3->border = true;
	
	auto text4 = icu::UnicodeString::fromUTF8("Compare Two Files");
	b4 = new Button(this, text4, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b3->t_y+b3->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Compare(nullptr, [&](Widget* w){
			return;
		}));
		std::cout << "Creating compare menu\n";
		delete this;
	});
	b4->rounded = true;
	b4->border = true;
	
	auto text5 = icu::UnicodeString::fromUTF8("AI Chat");
	b5 = new Button(this, text5, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b4->t_y+b4->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Chat(nullptr));
		std::cout << "Creating chat menu\n";
		delete this;
	});
	b5->rounded = true;
	b5->border = true;
	
	auto text6 = icu::UnicodeString::fromUTF8("LSP Debugger");
	b6 = new Button(this, text6, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b5->t_y+b5->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new LspDebug(nullptr));
		std::cout << "Creating lsp debug menu\n";
		delete this;
	});
	b6->rounded = true;
	b6->border = true;
	
	auto text7 = icu::UnicodeString::fromUTF8("Terminal");
	b7 = new Button(this, text7, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b6->t_y+b6->t_h+App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new TerminalWidgetTabbed(nullptr));
		std::cout << "Creating terminal\n";
		delete this;
	});
	b7->rounded = true;
	b7->border = true;
}

void WidgetChooser::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	auto txt = icu::UnicodeString::fromUTF8("Widget chooser");
	TextRenderer::draw_text(t_x+t_w/2-TextRenderer::get_text_width(txt.length())/2, t_y+App::text_padding, txt, App::theme.main_text_color);
	
	Widget::render();
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
}

void WidgetChooser::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
}

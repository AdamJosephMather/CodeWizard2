#include "widgetchooser.h"
#include "button.h"
#include "chat.h"
#include "compare.h"
#include "editor.h"
#include "filetree.h"
#include "lspdebug.h"
#include "settings.h"
#include "mathwindow.h"
#include "asteroids.h"
#include "graphwindow.h"
#include "text_renderer.h"
#include "application.h"
#include "terminalwidgettabbed.h"

WidgetChooser::WidgetChooser(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("Widgetchooser");
	
	auto text = MST::toMonoString("Editor View");
	b1 = new Button(this, text, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = t_y+App::text_padding*2+TextRenderer::get_text_height();
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		std::cout << "Init editor in relacement\n";
		App::ReplaceWith(this, new Editor(nullptr));
		App::deleteWidget(this);
	});
	b1->rounded = true;
	
	auto text2 = MST::toMonoString("File Tree");
	b2 = new Button(this, text2, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b1->t_y+b1->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new FileTree(nullptr));
		std::cout << "Creating file tree\n";
		App::deleteWidget(this);
	});
	b2->rounded = true;
	
	auto text3 = MST::toMonoString("Settings Menu");
	b3 = new Button(this, text3, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b2->t_y+b2->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Settings(nullptr));
		std::cout << "Creating settings menu\n";
		App::deleteWidget(this);
	});
	b3->rounded = true;
	
	auto text4 = MST::toMonoString("Compare Two Files");
	b4 = new Button(this, text4, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b3->t_y+b3->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Compare(nullptr, [&](Widget* w){
			return;
		}));
		std::cout << "Creating compare menu\n";
		App::deleteWidget(this);
	});
	b4->rounded = true;
	
	auto text5 = MST::toMonoString("AI Chat");
	b5 = new Button(this, text5, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b4->t_y+b4->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Chat(nullptr));
		std::cout << "Creating chat menu\n";
		App::deleteWidget(this);
	});
	b5->rounded = true;
	
	auto text6 = MST::toMonoString("LSP Debugger");
	b6 = new Button(this, text6, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b5->t_y+b5->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new LspDebug(nullptr));
		std::cout << "Creating lsp debug menu\n";
		App::deleteWidget(this);
	});
	b6->rounded = true;
	
	auto text7 = MST::toMonoString("Terminal");
	b7 = new Button(this, text7, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b6->t_y+b6->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new TerminalWidgetTabbed(nullptr));
		std::cout << "Creating terminal\n";
		App::deleteWidget(this);
	});
	b7->rounded = true;
	
	auto text8 = MST::toMonoString("Math Window");
	b8 = new Button(this, text8, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b7->t_y+b7->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new MathWindow(nullptr));
		std::cout << "Creating mathwindow\n";
		App::deleteWidget(this);
	});
	b8->rounded = true;
	
	auto text9 = MST::toMonoString("Asteroids");
	b9 = new Button(this, text9, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b8->t_y+b8->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new Asteroids(nullptr));
		std::cout << "Creating asteroids\n";
		App::deleteWidget(this);
	});
	b9->rounded = true;
	
	auto text10 = MST::toMonoString("Graph Window");
	b10 = new Button(this, text10, [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_x+t_w/2-tw/2;
		button->t_y = b9->t_y+b9->t_h+App::text_padding/2;
		button->t_h = th-App::text_padding;
	}, [&](Button* button) {
		App::ReplaceWith(this, new GraphWindow(nullptr));
		std::cout << "Creating graphwindow\n";
		App::deleteWidget(this);
	});
	b10->rounded = true;
}

void WidgetChooser::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	auto txt = MST::toMonoString("Widget Chooser");
	TextRenderer::draw_text(t_x+t_w/2-TextRenderer::get_text_width(txt.length)/2, t_y+App::text_padding, txt, App::theme.main_text_color);
	
	Widget::render();
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
}

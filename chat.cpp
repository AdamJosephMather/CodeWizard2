#include "chat.h"
#include "application.h"
#include "text_renderer.h"
#include "curler.h"

Chat::Chat(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Chat");
	
	querybox = new TextEdit(this, [](Widget*){});
	querybox->background_color = App::theme.extras_background_color;
}

void Chat::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	int the_y = t_y+t_h;
	
	int bxsz = 4*TextRenderer::get_text_height()+App::text_padding*2;
	the_y -= bxsz;
	the_y -= App::text_padding*2;
	
	querybox->position(t_x + App::text_padding, the_y, t_w - App::text_padding*2, bxsz);
	
	the_y -= scrolled_to;
	min_scroll = 0;
	
	for (int mi = message_te.size()-1; mi >= 0; mi--) {
		auto m = message_te[mi];
		m->position(t_x + App::text_padding, 1, t_w - App::text_padding*2, 1);
		
		int size = m->should_be_h;
		the_y -= size;
		the_y -= TextRenderer::get_text_height();
		m->t_y = the_y;
		m->t_h = size;
		
		min_scroll -= TextRenderer::get_text_height();
		min_scroll -= size;
	}
	
	min_scroll += TextRenderer::get_text_height();
	
	if (scrolled_to > 0) {
		scrolled_to = 0;
	}if (scrolled_to < min_scroll) {
		scrolled_to = min_scroll;
	}
}

void Chat::render() {
	if (!is_visible) {
		return;
	}
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.main_background_color);
	
	for (auto m : message_te) {
		App::runWithSKIZ(m->t_x, m->t_y, m->t_w, m->t_h, [&](){
			m->render();
		});
	}
	
	App::DrawRect(t_x, querybox->t_y, t_w, t_h+t_y-querybox->t_y, App::theme.main_background_color);
	App::runWithSKIZ(querybox->t_x, querybox->t_y, querybox->t_w, querybox->t_h, [&](){
		querybox->render();
	});
}

bool Chat::on_scroll_event(double xchange, double ychange) {
	scrolled_to += ychange * 6 * TextRenderer::get_text_height();
	if (scrolled_to > 0) {
		scrolled_to = 0;
	}if (scrolled_to < min_scroll) {
		scrolled_to = min_scroll;
	}
	
	Widget::on_scroll_event(xchange, 0);
	
	return false;
}

bool Chat::on_key_event(int key, int scancode, int action, int mods) {
	if (App::activeLeafNode == querybox) {
		bool shift_held = ((mods & GLFW_MOD_SHIFT) != 0);
		if (key == GLFW_KEY_ENTER && action == GLFW_PRESS && !shift_held) {
			Label* te = new Label(this);
			auto message = querybox->getFullText();
			te->setFullText(message);
			
			message_te.push_back(te);
			from_user.push_back(true);
			
			querybox->setFullText(icu::UnicodeString::fromUTF8(""));
			
			std::vector<std::pair<bool,std::string>> messages;
			
			for (int mi = 0; mi < message_te.size(); mi++) {
				std::string msgstr;
				message_te[mi]->getFullText().toUTF8String(msgstr);
				messages.push_back( {from_user[mi], msgstr} );
			}
			
			Label* te2 = new Label(this);
			te2->background_color = App::theme.main_background_color;
			
			message_te.push_back(te2);
			from_user.push_back(false);
			
			new std::thread([messages,te2](){
				std::string full;
				full = Curler::StreamChatResponse(messages, [&](const std::string& part){
					te2->setFullText(te2->getFullText()+icu::UnicodeString::fromUTF8(part));
					App::time_till_regular += 2;
				});
				
				te2->setFullText(icu::UnicodeString::fromUTF8(full));	
				App::time_till_regular += 2;
			});
			
			return true;
		}
	}
	
	return Widget::on_key_event(key, scancode, action, mods);
}
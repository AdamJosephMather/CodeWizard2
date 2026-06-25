#include "chat.h"
#include "application.h"
#include "label.h"
#include "text_renderer.h"
#include "curler.h"
#include "markdown_utils.h"
#include <regex>

Chat::Chat(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Chat");
	
	before_self_close = [&](){
		while (running) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
	};
	
	querybox = new TextEdit(this, [](Widget*){});
	querybox->id = icu::UnicodeString::fromUTF8("QueryBox");
	querybox->borderColor = nullptr;
	querybox->activeBorderColor = nullptr;
	
	filesButton = new Button(this, icu::UnicodeString::fromUTF8("Insert File"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		btn->t_x = t_x + 2;
		btn->t_y = querybox->t_y - 1 - h;
	}, [&](Button* btn){
		auto files = App::rootelement->getOpenFiles(true); // get's the file text(s)
		
		textstoadd.clear();
		std::vector<icu::UnicodeString> els;
		for (auto f : files) {
			if (f[2] == "TEXT") {
				els.push_back(icu::UnicodeString::fromUTF8(f[0]));
				textstoadd.push_back("File: " + f[0] + "\n\n```" + f[3] + "```"); // full text
			}
		}
		filesAddList->setElements(els);
		
		filesAddList->is_visible_layered = !filesAddList->is_visible_layered;
	});
	filesButton->rounded = true;
	
	newChat = new Button(this, icu::UnicodeString::fromUTF8("Reset Chat"), [&](Widget* btn, int x, int y, int _, int _2, int w, int h){
		btn->t_x = t_x+App::text_padding;
		btn->t_y = t_y+App::text_padding;
	}, [&](Widget*){
		if (running) {
			return;
		}
		
		for (int i = 0; i < message_te.size(); i++) {
			App::RemoveWidgetFromParent(message_te[i]);
			App::deleteWidget(message_te[i]);
		}
		message_te.clear();
		from_user.clear();
	});
	newChat->rounded = true;
	
	filesAddList = new ListBox(this, [&](Widget* w){
		w->t_x = t_x + 2;
		w->t_y = filesButton->t_y - w->t_h - 3;
		w->t_w = t_w/2.5;
	});
	filesAddList->rounded = true;
	filesAddList->is_visible_layered = false;
	filesAddList->ONCLICK = [&](Widget* w, int idx) {
		std::string message = textstoadd[idx];
		Label* te = new Label(this);
		te->background_color = App::theme.extras_background_color;
		te->setFullText(icu::UnicodeString::fromUTF8(message));
		message_te.push_back(te);
		from_user.push_back(true);
		filesAddList->is_visible_layered = false;
	};
}

void Chat::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	int bxsz = 4*TextRenderer::get_text_height();
	int the_y = t_y+t_h - bxsz;
	
	querybox->position(t_x, the_y, t_w, bxsz);
	filesButton->position(t_x, t_y, t_w, t_h);
	filesAddList->position(t_x, t_y, t_w, t_h);
	newChat->position(t_x, t_y, t_w, t_h);
	
	the_y -= (App::text_padding + scrolled_to + 4 + filesButton->t_h);
	min_scroll = 0;
	
	for (int mi = message_te.size()-1; mi >= 0; mi--) {
		auto m = message_te[mi];
		
		if (auto te = dynamic_cast<TextEdit*>(message_te[mi])) {
			int size = te->lines.size()*TextRenderer::get_text_height()+App::text_padding*2;
			
			the_y -= size;
			the_y -= TextRenderer::get_text_height();
			
			m->position(t_x + App::text_padding, the_y, t_w - App::text_padding*2, size);
			
			min_scroll -= TextRenderer::get_text_height();
			min_scroll -= size;
		}else if (auto te = dynamic_cast<Label*>(message_te[mi])){
			m->position(t_x + App::text_padding, 1, t_w - App::text_padding*2, 1);
			
			int size = te->should_be_h;
			
			the_y -= size;
			the_y -= TextRenderer::get_text_height();
			m->t_y = the_y;
			m->t_h = size;
			
			min_scroll -= TextRenderer::get_text_height();
			min_scroll -= size;
		}
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
	
	App::DrawRect(querybox->t_x, querybox->t_y-2-filesButton->t_h, querybox->t_w, 2+filesButton->t_h, App::theme.darker_background_color);
	App::runWithSKIZ(querybox->t_x, querybox->t_y, querybox->t_w, querybox->t_h, [&](){
		querybox->render();
	});
	App::runWithSKIZ(filesButton->t_x, filesButton->t_y, filesButton->t_w, filesButton->t_h, [&](){
		filesButton->render();
	});
	App::runWithSKIZ(filesAddList->t_x, filesAddList->t_y, filesAddList->t_w, filesAddList->t_h, [&](){
		filesAddList->render();
	});
	App::runWithSKIZ(newChat->t_x, newChat->t_y, newChat->t_w, newChat->t_h, [&](){
		newChat->render();
	});
	TextRenderer::draw_text(filesButton->t_x+filesButton->t_w+App::text_padding, filesButton->t_y+App::text_padding, icu::UnicodeString::fromUTF8(App::settings->getValue("lm_studio_model_id",std::string("qwen2.5-coder-1.5b-instruct@q4_k_m"))), App::theme.lesser_text_color);
	
	Color* bColor = App::theme.border;
	if (querybox == App::activeLeafNode) {
		bColor = App::theme.active_color;
	}
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, bColor, 5, App::text_padding);
	}else {
		App::DrawBorder(t_x, t_y, t_w, t_h, bColor);
	}
	App::DrawRect(t_x, filesButton->t_y-2, t_w, 1, bColor);
}

bool Chat::on_scroll_event(double xchange, double ychange) {
	if (!cursor_in_this || !is_visible) { return false; }
	
	scrolled_to += ychange * 6 * TextRenderer::get_text_height();
	if (scrolled_to > 0) {
		scrolled_to = 0;
	}if (scrolled_to < min_scroll) {
		scrolled_to = min_scroll;
	}
	
	Widget::on_scroll_event(xchange, 0);
	
	return false;
}

bool Chat::on_mouse_button_event(int button, int action, int mods) {
	if (!cursor_in_this || !is_visible) { return false; }
	
	return Widget::on_mouse_button_event(button, action, mods);
}

std::vector<Segment> Chat::splitMarkdown(const std::string& input) {
	// Captures:
	//   m[1] = optional code block name/language
	//   m[2] = code contents
	static const std::regex re(
		R"(```([^\n`]*)?\r?\n([\s\S]*?)```)"
	);

	std::vector<Segment> segments;
	std::sregex_iterator it(input.begin(), input.end(), re), end;
	size_t lastPos = 0;

	for (; it != end; ++it) {
		const std::smatch& m = *it;

		size_t matchPos = static_cast<size_t>(m.position(0));
		size_t matchLen = static_cast<size_t>(m.length(0));

		// 1) Plaintext before this code block
		if (matchPos > lastPos) {
			ProcessedMarkdown processed =
				MarkdownParser::Process(input.substr(lastPos, matchPos - lastPos));

			segments.push_back({
				/*isCode=*/false,
				processed.cleanText,
				processed.spans
			});
		}

		std::string codeName = m[1].matched ? m[1].str() : "";
		std::string codeText = m[2].str();

		// 2) This code block
		segments.push_back({
			/*isCode=*/true,
			codeText,
			/*spans=*/{},
			/*codeName=*/codeName
		});

		lastPos = matchPos + matchLen;
	}

	// 3) Any trailing plaintext after the last code block
	if (lastPos < input.size()) {
		ProcessedMarkdown processed = MarkdownParser::Process(input.substr(lastPos));

		segments.push_back({
			/*isCode=*/false,
			processed.cleanText,
			processed.spans
		});
	}

	return segments;
}

bool Chat::on_key_event(int key, int scancode, int action, int mods) {
	if (App::activeLeafNode == querybox) {
		bool shift_held = ((mods & GLFW_MOD_SHIFT) != 0);
		if (key == GLFW_KEY_ENTER && action == GLFW_PRESS && !shift_held && !running) {
			Label* te = new Label(this);
			auto message = querybox->getFullText();
			te->background_color = App::theme.extras_background_color;
			te->setFullText(message);
			
			message_te.push_back(te);
			from_user.push_back(true);
			
			querybox->setFullText(icu::UnicodeString::fromUTF8(""));
			
			
			
			std::vector<std::pair<bool,std::string>> messages;
			
			bool lastwasuser = false;
			for (int mi = 0; mi < message_te.size(); mi++) {
				std::string msgstr;
				if (auto te = dynamic_cast<TextEdit*>(message_te[mi])) {
					te->getFullText().toUTF8String(msgstr);
				}else if (auto te = dynamic_cast<Label*>(message_te[mi])){
					te->getFullText().toUTF8String(msgstr);
				}
				
				if (from_user[mi] && lastwasuser) {
					messages[messages.size()-1].second += "\n\n" + msgstr;
				}else{
					messages.push_back( {from_user[mi], msgstr} );
				}
				
				lastwasuser = from_user[mi];
			}
			
			Label* te2 = new Label(this);
			te2->background_color = App::theme.main_background_color;
			te2->border = false;
			
			message_te.push_back(te2);
			from_user.push_back(false);
			
			running = true;
			
			new std::thread([messages,te2, this](){
				std::string full;
				full = Curler::StreamChatResponse(messages, [&](const std::string& part){
					te2->setFullText(te2->getFullText()+icu::UnicodeString::fromUTF8(part));
					App::time_till_regular = 2;
				}, SYSTEM_PROMPT);
				
				App::RemoveWidgetFromParent(te2);
				message_te.erase(message_te.begin()+message_te.size()-1);
				from_user.erase(from_user.begin()+from_user.size()-1);
				App::deleteWidget(te2);
				
				for (auto segment : splitMarkdown(full)) {
					if (segment.isCode) {
						auto te = new TextEdit(this, [](Widget*){});
						te->setFullText(icu::UnicodeString::fromUTF8(trim(segment.content)));
						te->DONT_SCROLL_VERT_CURS = true;
						
						auto name = segment.name;
						if (!name.empty()) {
							auto it = App::highlighters.find(name);
							if (it == App::highlighters.end()) {
								te->highlighter = cw_syntect_setup(
									name.c_str(),
									nullptr,
									0
								);
								
								App::highlighters[name] = te->highlighter;
							}else{
								te->highlighter = it->second;
							}
							
							if (te->highlighter != nullptr) {
								te->highlighter_initial_state.reset(cw_syntect_initial_state(te->highlighter));
							}
						}
						
						message_te.push_back(te);
					}else{
						auto la = new Label(this);
						la->setFullText(icu::UnicodeString::fromUTF8(segment.content), segment.spans);
						la->background_color = App::theme.main_background_color;
						la->border = false;
						message_te.push_back(la);
					}
					from_user.push_back(false);
				}
				
				App::time_till_regular = 2;
				
				running = false;
			});
			
			return true;
		}
	}
	
	return Widget::on_key_event(key, scancode, action, mods);
}
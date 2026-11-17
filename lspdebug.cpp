#include "lspdebug.h"
#include "application.h"

LspDebug::LspDebug(Widget *parent) : Widget(parent) {
	id = MST::toMonoString("LspDebug");
	
	viewbox = new TextEdit(this, [](Widget*){});
	viewbox->surface = TextEdit::Surface::Document;
	viewbox->borderColor = nullptr;
	viewbox->activeBorderColor = nullptr;
}

void LspDebug::lspmessage(std::string& from, std::string& message) {
	new std::thread([=]() {
		std::lock_guard<std::mutex> lock(App::canMakeChanges);
		Cursor c = Cursor();
		
		c.head_line = viewbox->lines.size()-1;
		c.head_char = viewbox->lines[c.head_line].line_text.length;
		
		c.anchor_line = c.head_line;
		c.anchor_char = c.head_char;
		
		std::string newstring = "\n\n["+from+"] --> "+trim(message);
		MST::MonoString log = MST::toMonoString(newstring);
		viewbox->insertTextAtCursor(c, log);
	});
}

void LspDebug::render() {
	if (!is_visible) return;
	const float radius = App::themeCornerRadius(t_w, t_h);
	App::runWithRoundedClip(t_x, t_y, t_w, t_h, radius, [&]() {
		Widget::render();

		Color* borderC = App::theme.border;
		if (viewbox == App::activeLeafNode) {
			borderC = App::theme.active_color;
		}

		if (radius > 0.0f) {
			App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, radius);
		}else{
			App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
		}
	});
}
#include <GL/glew.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include "application.h"
#include "button.h"
#include "editor.h"
#include "panel_holder.h"

#include <Windows.h>

static std::string empty = "";

void setSynColor(Theme* t, std::string name, int id) {
	std::string cl = App::settings->getValue(name, empty);
	if (cl == empty) { return; }
	bool worked;
	Color c = stringToColor(cl, worked);
	if (!worked) { return; }
	t->syntax_colors[id]->r = c.r;
	t->syntax_colors[id]->g = c.g;
	t->syntax_colors[id]->b = c.b;
}

int main() {
	App::Init();
	
	Theme theme;
	
	theme.tint_color = MakeColor(0.435294,0.670588,0.819608);
	
	theme.main_background_color = MakeColor(0.0509803922, 0.0784313725, 0.0941176471);
	theme.extras_background_color = MakeColor(0.0862745098, 0.1294117647, 0.1607843137);
	theme.hover_background_color = MakeColor(0.2, 0.3, 0.35);
	theme.lesser_text_color = MakeColor(0.2039215686, 0.3137254902, 0.3843137255);
	theme.main_text_color = MakeColor(0.5137254902, 0.7960784314, 0.9725490196);
	theme.darker_background_color = MakeColor(0.0274509804, 0.0470588235, 0.0549019608);
	theme.overlay_background_color = MakeColor(0.1, 0.15, 0.2);
	theme.border = MakeColor(0, 0, 0);
	
	theme.error_color = MakeColor(1.0, 0.3, 0.3);
	theme.warning_color = MakeColor(1.0, 0.6431372549, 0.21);
	theme.suggestion_color = MakeColor(0.1137254902, 0.3254901961, 0.6);
	
	theme.white = MakeColor(1.0, 1.0, 1.0);
	theme.black = MakeColor(0.0, 0.0, 0.0);
	
	theme.syntax_colors[0] = theme.main_text_color;
	theme.syntax_colors[1] = MakeColor(0.4980392156862745, 0.6784313725490196, 0.3686274509803922);
	theme.syntax_colors[2] = MakeColor(0.4980392156862745, 0.5176470588235295, 0.5568627450980392);
	theme.syntax_colors[3] = MakeColor(0.9607843137254902, 0.3568627450980392, 0.4);
	theme.syntax_colors[4] = MakeColor(0.3333333333333333, 0.6627450980392157, 0.9294117647058824);
	theme.syntax_colors[5] = MakeColor(0.7803921568627451, 0.615686274509804, 0.3058823529411765);
	theme.syntax_colors[6] = MakeColor(0.6901960784313725, 0.37254901960784315, 0.7803921568627451);
	theme.syntax_colors[7] = MakeColor(0.4980392156862745, 0.5176470588235295, 0.5568627450980392);
	theme.syntax_colors[8] = MakeColor(0.7607843137254902, 0.4980392156862745, 0.25098039215686274);
	
	setSynColor(&theme, "c_strings_color", 1);
	setSynColor(&theme, "c_comments_color", 2);
	setSynColor(&theme, "c_vars_color", 3);
	setSynColor(&theme, "c_types_color", 4);
	setSynColor(&theme, "c_functs_color", 5);
	setSynColor(&theme, "c_keywords_color", 6);
	setSynColor(&theme, "c_punctuation_color", 7);
	setSynColor(&theme, "c_literals_color", 8);
	
	std::string cl = App::settings->getValue("c_tint_color", empty);
	if (cl != empty) {
		bool worked;
		Color c = stringToColor(cl, worked);
		if (worked) {
			theme.tint_color->r = c.r;
			theme.tint_color->g = c.g;
			theme.tint_color->b = c.b;
		}
	}
	
	App::updateFromTintColor(&theme);
	App::setTheme(theme);

	
	std::vector<Language> langs = App::settings->loadLanguages();
	
	for (auto l : langs) {
		App::languagemap[l.name] = l;
	}
	
	
	
	Widget* mainwidget = App::rootelement;
	
	nlohmann::json state = App::settings->getConfig();
	
	if (state.contains("children")) {
		auto ph = dynamic_cast<PanelHolder*>(mainwidget->children[0]);
		ph->setState(state);
	}else{
		Editor* first_widget = new Editor(mainwidget->children[0]);
	}
	
	Button* add_button = new Button(App::tb, icu::UnicodeString::fromUTF8("+"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = 0;
		button->t_y = 0;
	}, [&](Button* button) {
		App::adding_panel();
	});
	
	Button* remove_button = new Button(App::tb, icu::UnicodeString::fromUTF8("-"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = add_button->t_w+3;
		button->t_y = 0;
	}, [&](Button* button) {
		App::removing_panel();
	});
	
	TextEdit* commandPalette = new TextEdit(App::tb, [&](Widget* w){
		w->t_x = w->t_w/2 - w->t_w/6;
		w->t_w /= 3;
		w->t_y = 0;
		w->t_h = App::tb->children[0]->t_h;
	});
	commandPalette->background_color = App::theme.extras_background_color;
	
	ListBox* commandBox = new ListBox(nullptr, [&](Widget* w){
		w->t_x = commandPalette->t_x;
		w->t_w = commandPalette->t_w;
		w->t_y = commandPalette->t_y+5+commandPalette->t_h;
	});
	commandBox->is_visible_layered = true;
	commandBox->toshow = 12;
	commandBox->ONCLICK = [&](Widget* w, int sel_id) {
		commandBox->selected_id = sel_id;
		App::executeCommandPaletteAction();
	};
	
	commandPalette->ontextchange = [&](Widget* w) {
		if (App::activeLeafNode != commandPalette) {
			return;
		}
		App::fillCmdBox();
	};
	
	App::commandPalette = commandPalette;
	App::commandBox = commandBox;
	
	add_button->window_button = true;
	remove_button->window_button = true;
	add_button->transparent = true;
	remove_button->transparent = true;
	
	Widget* wdgt = App::rootelement->getFirstEditor();
	if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
		auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
		if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
			if (cdet->textedit) {
				App::setActiveLeafNode(cdet->textedit);
			}
		}
	}
	
	App::Run();
	
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
	return main();
}
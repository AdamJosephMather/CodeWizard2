#include <GL/glew.h>
#include <curl/curl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include "application.h"
#include "helpmenu.h"
#include "button.h"
#include "editor.h"
#include "listbox.h"
#include "textedit.h"
#include "codeedit.h"
#include "scrollnotify.h"
#include "panel_holder.h"
#include "text_renderer.h"

#ifdef _WIN32
#include <Windows.h>
#include <delayimp.h>
#endif


//static FARPROC WINAPI DelayHook(unsigned dliNotify, PDelayLoadInfo info) {
//	if (dliNotify == dliNotePreLoadLibrary && info && info->szDll) {
//		char buf[512];
//		std::snprintf(buf, sizeof(buf), "DelayLoad: about to load DLL: %s\n", info->szDll);
//		OutputDebugStringA(buf);
//	}
//	if (dliNotify == dliNotePreGetProcAddress && info && info->szDll) {
//		char buf[512];
//		const char* proc = (info->dlp.fImportByName && info->dlp.szProcName) ? info->dlp.szProcName : "(by ordinal)";
//		std::snprintf(buf, sizeof(buf), "DelayLoad: resolving %s!%s\n", info->szDll, proc);
//		OutputDebugStringA(buf);
//	}
//	return nullptr;
//}
//
//// Tell the delay-load helper to call our hook:
//extern "C" PfnDliHook __pfnDliNotifyHook2 = DelayHook;

int main(int argc, char* argv[]) {
	auto start = std::chrono::steady_clock::now();
	
	
	#ifdef _WIN32
		HRESULT comHr = CoInitializeEx(
			nullptr,
			COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
		);
	
		bool didInitCom = SUCCEEDED(comHr);
	
		if (FAILED(comHr)) {
			std::cerr << "CoInitializeEx failed: 0x"
					  << std::hex << static_cast<unsigned long>(comHr)
					  << std::dec << "\n";
		}
	#endif
	
		// your app code here
	
	#ifdef _WIN32
		if (didInitCom) {
			CoUninitialize();
		}
	#endif
	
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	Theme theme;
	
	theme.add_panel = MakeColor(0.2f, 1.0f, 0.2f, 0.25f);
	theme.remove_panel = MakeColor(1.0f, 0.2f, 0.2f, 0.25f);
	
	theme.tint_color = MakeColor(1, 1, 1, 0.7);
	
	theme.main_background_color = MakeColor(0.0509803922, 0.0784313725, 0.0941176471);
	theme.extras_background_color = MakeColor(0.0862745098, 0.1294117647, 0.1607843137);
	theme.hover_background_color = MakeColor(0.2, 0.3, 0.35);
	theme.lesser_text_color = MakeColor(0.2039215686, 0.3137254902, 0.3843137255);
	theme.main_text_color = MakeColor(0.5137254902, 0.7960784314, 0.9725490196);
	theme.active_color = MakeColor(0.356862745, 0.635294118, 0.811764706);
	theme.darker_background_color = MakeColor(0.0274509804, 0.0470588235, 0.0549019608);
	theme.overlay_background_color = MakeColor(0.1, 0.15, 0.2);
	theme.border = MakeColor(0.8, 0.8, 0.8);
	
	theme.error_color = MakeColor(1.0, 0.3, 0.3);
	theme.warning_color = MakeColor(1.0, 0.6431372549, 0.21);
	theme.suggestion_color = MakeColor(0.203922, 0.478431, 0.921569);
	
	theme.add_diff = MakeColor(0.321569, 0.960784, 0.250980);
	theme.del_diff = MakeColor(0.921569, 0.286275, 0.203922);
	theme.equal_diff = MakeColor(0.203922, 0.478431, 0.921569);
	
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
	
	App::theme = theme;
	
	App::Init();
	
	App::setSynColor(&theme, "c_strings_color", 1);
	App::setSynColor(&theme, "c_comments_color", 2);
	App::setSynColor(&theme, "c_vars_color", 3);
	App::setSynColor(&theme, "c_types_color", 4);
	App::setSynColor(&theme, "c_functs_color", 5);
	App::setSynColor(&theme, "c_keywords_color", 6);
	App::setSynColor(&theme, "c_punctuation_color", 7);
	App::setSynColor(&theme, "c_literals_color", 8);
	
	std::string cl = App::settings->getValue("c_tint_color", App::empty);
	if (cl != App::empty) {
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
		new Editor(mainwidget->children[0]);
	}
	
	TextEdit* commandPalette = new TextEdit(App::tb, [&](Widget* w){
		w->t_x = w->t_w/2 - w->t_w/6;
		w->t_w /= 3;
		w->t_y = 0;
		w->t_h = App::tb->children[0]->t_h;
	});
	commandPalette->background_color = App::theme.extras_background_color;
	commandPalette->rounded = true;
	commandPalette->id = icu::UnicodeString::fromUTF8("CommandPalette");
	
	ListBox* commandBox = new ListBox(nullptr, [&](Widget* w){
		w->t_x = commandPalette->t_x; 
		w->t_w = commandPalette->t_w;
		w->t_y = App::tb->t_y+App::tb->t_h+2;
	});
	commandBox->rounded = true;
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
	
	
	// Menubar (file/help)
	
	ContextMenu* menu = new ContextMenu(nullptr);
	menu->is_visible_2 = true;
	menu->is_visible_3 = true;
	
	Button* file_button = new Button(App::tb, icu::UnicodeString::fromUTF8("File"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = App::text_padding;
		button->t_y = App::text_padding/2;
		button->t_h -= App::text_padding;
		
		if (menu->parent != nullptr && button->cursor_in_this && App::currentMenu != 0) {
			button->ONCLICK(button);
		}
	}, [&](Button* button) {
		menu->clearMenu();
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Open File\t(Ctrl+O)"), [](Button*){
			App::closeMenu();
			
			Editor* e = dynamic_cast<Editor*>(App::activeEditor);
			if (e == nullptr) {
				e = dynamic_cast<Editor*>(App::rootelement->getFirstEditor());
				if (e == nullptr) {
					App::displayToast(icu::UnicodeString::fromUTF8("No open Editor widgets"));
				}
			}
			
			e->fileOpenRequested(nullptr); // triggers a file open
		});
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Open Folder\t(Ctrl+Shift+O)"), [](Button*){
			App::closeMenu();
			App::openFolderSelector();
		});
		
		menu->addSeparaterToMenu();
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Save All\t(Ctrl+S)"), [](Button*){
			App::closeMenu();
			App::displayText(icu::UnicodeString::fromUTF8("Saving..."));
			App::save();
		});
		
		if (menu->parent == nullptr || App::currentMenu != 0) {
			App::openMenu(button->t_x);
		}else { // only close the menu if it's open to this tab
			App::closeMenu();
		}
		
		App::currentMenu = 0;
	});
	file_button->border_color = nullptr;
	file_button->background_color = nullptr;
	file_button->rounded = true;
	
	Button* actions_button = new Button(App::tb, icu::UnicodeString::fromUTF8("Actions"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = file_button->t_x+file_button->t_w+App::text_padding;
		button->t_y = App::text_padding/2;
		button->t_h -= App::text_padding;
		
		if (menu->parent != nullptr && button->cursor_in_this && App::currentMenu != 1) {
			button->ONCLICK(button);
		}
	}, [&](Button* button) {
		menu->clearMenu();
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Git Push\t(Cmd Palette)"), [](Button*){
			App::closeMenu();
			App::gitPush();
		});
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Git Pull\t(Cmd Palette)"), [](Button*){
			App::closeMenu();
			App::gitPull();
		});
		
		menu->addToMenu(icu::UnicodeString::fromUTF8("Git Force Pull\t(Cmd Palette)"), [](Button*){
			App::closeMenu();
			App::gitForcePull();
		});
		
		if (menu->parent == nullptr || App::currentMenu != 1) {
			App::openMenu(button->t_x);
		}else { // only close the menu if it's open to this tab
			App::closeMenu();
		}
		
		App::currentMenu = 1;
	});
	actions_button->border_color = nullptr;
	actions_button->background_color = nullptr;
	actions_button->rounded = true;
	
	Button* help_button = new Button(App::tb, icu::UnicodeString::fromUTF8("Help"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = actions_button->t_x+actions_button->t_w+App::text_padding;
		button->t_y = App::text_padding/2;
		button->t_h -= App::text_padding;
	}, [&](Button* button) {
		if (App::helpMenu->parent == nullptr) {
			App::MoveWidget(App::helpMenu, App::rootelement);
		}else{
			App::RemoveWidgetFromParent(App::helpMenu);
		}
		App::closeMenu();
		App::reclear = 2;
		App::currentMenu = 2;
	});
	help_button->border_color = nullptr;
	help_button->background_color = nullptr;
	help_button->rounded = true;
	
	// Widget controls
	
	Button* remove_button = new Button(App::tb, icu::UnicodeString::fromUTF8("- "), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = commandPalette->t_x-tw-App::text_padding;
		button->t_y = 0;
	}, [&](Button* button) {
		App::removing_panel();
	});
	remove_button->rounded = true;
	remove_button->text_special = 3;
	remove_button->background_color = nullptr;
	remove_button->background_color_hover = App::theme.remove_panel;
	remove_button->border_color = nullptr; // transparent
	remove_button->border_color_hover = nullptr;
	remove_button->window_button = false;
	
	Button* add_button = new Button(App::tb, icu::UnicodeString::fromUTF8("+ "), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = remove_button->t_x-tw;
		button->t_y = 0;
	}, [&](Button* button) {
		App::adding_panel();
	});
	add_button->rounded = true;
	add_button->text_special = 2;
	add_button->background_color = nullptr;
	add_button->background_color_hover = App::theme.add_panel;
	add_button->border_color = nullptr; // transparent
	add_button->border_color_hover = nullptr;
	add_button->window_button = false;
	
	ListBox* filesList = new ListBox(nullptr, [&](Widget* w) {
		w->t_x = App::filesButton->t_x;
		w->t_y = App::filesButton->t_y + App::filesButton->t_h + App::text_padding;
		w->t_w = std::min(App::WINDOW_WIDTH-w->t_x, App::WINDOW_WIDTH/5);
	});
	filesList->rounded = true;
	filesList->is_visible_layered = true;
	filesList->ONCLICK = [&](Widget* w, int sel_id) {
		App::closeFilesList();
		
		if (sel_id >= App::files_in_box.size()) {
			return;
		}
		
		std::string filepath = App::files_in_box[sel_id][1];
		std::string filename = App::files_in_box[sel_id][0];
		if (filepath == "") {
			int count = 0;
			
			for (int i = 0; i < sel_id; i++) {
				if (App::files_in_box[i][1] == "") {
					count ++;
				}
			}
			
			App::rootelement->openUnnamedFile(count);
			return;
		}
		
		if (auto edtr = dynamic_cast<Editor*>(App::rootelement->fileOpen(filepath))) { // first check if *an* editor currently has it open
			FileInfo* fi = new FileInfo();
			fi->filepath = filepath;
			fi->filename = filename;
			fi->is_opening = false;
			edtr->fileOpenRequested(fi);
		}else{
			App::displayToast(icu::UnicodeString::fromUTF8("File No Longer Open"));
		}
	};
	
	Button* filesButton = new Button(App::tb, icu::UnicodeString::fromUTF8("0 Active"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = commandPalette->t_x+commandPalette->t_w+App::text_padding;
		button->t_y = App::text_padding/2;
		button->t_h -= App::text_padding;
		button->BUTTON_LABEL = icu::UnicodeString::fromUTF8(std::to_string(App::rootelement->getOpenFiles(false).size())+" Active");
	}, [&](Button* button) {
		if (filesList->parent) {
			App::closeFilesList();
		}else{
			App::openFilesList();
		}
	});
	filesButton->rounded = true;
	filesButton->background_color = nullptr; // transparent
	filesButton->border_color = nullptr; // transparent
	
	
	ScrollNotify* displayMessage = new ScrollNotify(App::tb, [&](ScrollNotify* sn, int x, int y, int w, int h){
		sn->t_w = TextRenderer::get_text_width(20);
		
		int x1 = help_button->t_x+help_button->t_w+TextRenderer::get_text_width(2);
		int x2 = add_button->t_x - TextRenderer::get_text_width(2);
		int allwidth = x2-x1;
		
		sn->t_w = std::min(sn->t_w, allwidth);
		sn->t_x = x1 + (allwidth - sn->t_w)/2;
		
		sn->t_y = commandPalette->t_y;
		sn->t_h = commandPalette->t_h;
	});
	
	
	App::commandPalette = commandPalette;
	App::commandBox = commandBox;
	App::filesButton = filesButton;
	App::filesList = filesList;
	App::scrollNotifyBox = displayMessage;
	App::menu = menu;
	
	Widget* wdgt = App::rootelement->getFirstEditor();
	if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
		auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
		if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
			if (cdet->textedit) {
				App::setActiveLeafNode(cdet->textedit);
			}
		}
		
		if (argc >= 2) {
			std::string candidate = argv[1];
			std::filesystem::path p(candidate);
	
			if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) {
				App::setFolder(p.parent_path().string());
				
				FileInfo* f = new FileInfo;
				
				f->filepath = p.string();
				f->filename = p.filename().string();
				
				edtr->fileOpenRequested(f);
			}
		}
	}
	
	HelpMenu* helpMenu = new HelpMenu(nullptr);
	App::helpMenu = helpMenu;
	
	auto end = std::chrono::steady_clock::now();
	
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	
	std::cout << "Init took: " << duration.count() << " milliseconds" << std::endl;
	
	App::Run();
	
	curl_global_cleanup();
	
	return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
	return main(__argc, __argv);
}
#endif

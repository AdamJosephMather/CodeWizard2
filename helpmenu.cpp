#include "helpmenu.h"
#include "text_renderer.h"

HelpMenu::HelpMenu(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("HelpMenu");
	closebutton = new Button(this, icu::UnicodeString::fromUTF8("X"), [&](Widget* b, int x, int y, int w, int h, int width, int height){
		b->t_x = t_x+t_w-width;
		b->t_y = t_y;
		b->t_w = width;
		b->t_h = height;
	}, [&](Widget*){
		App::RemoveWidgetFromParent(this);
	});
	closebutton->text_special = 1;
	closebutton->window_button = true;
	closebutton->rounded = true;
	closebutton->background_color = App::theme.del_diff;
	
	tb = new Tabs(this);
	tb->POSITIONER = [&](Widget* t) {
		tb->t_y = t_y+closebutton->t_h;
	};
	
	tb->tab_clicked_callback = [&](TabInfo info){
		setToIndex(info.id);
	};
	
	tb->can_add_new = false;
	tb->has_close_button = false;
	
	helpInformation = {
		{
			icu::UnicodeString::fromUTF8("General"),
			icu::UnicodeString::fromUTF8(R":(CodeWizard2 V):" + App::vnumstr + R":(

	CodeWizard2 is a performant code editor/ide written in C++. CodeWizard is designed to be extendable to a number of languages and configurations. Highlighting is provided using the same syntax highlighting system that VSCode uses (but written completely in C++ by yours truly,) and supports the Language Server Protocol. Note that the LSP support is not perfect, and doesn't support everything available. But, it's been tested with gopls, rust analyzer, clangd, and of course pypls (my multi-purpose LSP.)

CodeWizard was created by Adam Mather.):"),
		},{
			icu::UnicodeString::fromUTF8("Using CodeWizard"),
			icu::UnicodeString::fromUTF8(R":(    CodeWizard is an extremely opinionated editor. Namely, most actions are handled via keyboard key combos, mouse support is limited, and it's designed to work in the way I will use it.

Opening Files/Folders:
	
	● CodeWizard maintains an active folder. All operations will be executed from this folder, including command palette actions. To change the active folder, use Ctrl+Shift+O.
	
	● Once in the correct folder (or even when not), one can open specific files in the editor interface (see widgets tab to learn how CodeWizard handles widgets.) To open a file, you can use the Ctrl+O command, or use the command palette to open a file in the current directory. To open the command palette without clicking, use Ctrl+Shift+P.
	
	● Use Ctrl+W to close the current file (or click on the close button on the file tab.)

Running Programs:
	
	● Assuming the language you're using has been configured (see 'Languages' section) you can run any files in that language using the F5 key. It will use the command specified for that language.
	
	● Project specific settings can be created to run code with a specific command for a given folder. You can create the project specific settings by creating a settings menu and navigating to 'Project Specific'. Then, it will provide you with a JSON file that you can edit to control that project's build command and language server starting commands.

Quirks:
	
	● The biggest will be the use of tabs instead of four space increments. Deal with it :) it's better.
):"),
		},{
			icu::UnicodeString::fromUTF8("Widgets"),
			icu::UnicodeString::fromUTF8(R":(    Widgets are the gas on which CodeWizard runs. The most important widgets are the: editor, settings menu, file tree, and terminal widgets. The editor is where all code editing happens, including a tab bar (by default). The file tree is a tree constructed to match the files in your actively selected directory (see Using CodeWizard.) The terminal widget will take over the execution for your code if it's open, otherwise CodeWizard will open a Microsoft Command Prompt and run your code there. And of course the settings menu is where some of CodeWizard's settings are managed.

	To open a new widget, click the '+' button in the top left of the screen, hover over where you want the new widget, and click again. (This will not work while in the help menu.)
	
	Similarly, to close, use the '-' button in the top left, hovering over the pane you want removed.
):"),
		},{
			icu::UnicodeString::fromUTF8("Settings"),
			icu::UnicodeString::fromUTF8(R":(    The majority of settings are accessible via the settings widget (see 'Widgets'.) This includes editor appearance, toggling features (file tabs, etc,) and AI settings (see AI).

	However, CodeWizard also stores language settings in a json file located in C:\Users\<username>\AppData\Local\CodeWizard\languages.json. This file is where you can add syntax highlighting and language servers for specific languages. By default CodeWizard comes setup for some languages, including Python, C++, Go, R, Java, JavaScript, HTML, and Rust.
	
	Project specific settings are described in 'Using CodeWizard', under 'Running Programs'.
):"),
		},{
			icu::UnicodeString::fromUTF8("Languages"),
			icu::UnicodeString::fromUTF8(R":(    Languages in CodeWizard are extensible and all configured in the languages settings (see 'Settings')

CodeWizard requires a few things:
	
	● The name of the language, the line comment strings for the language (ex '//' for C++, '#' for Python)
	
	● The filetypes (ex '.py', '.cpp', etc)
	
	● The lsp command (if unsure use "%INSTALL_DIR%\\pypls.exe" for my pypls LSP. That maps to "C:\Users\<username>\AppData\Local\CodeWizard\pypls.exe")
	
	● And the build command for the language (ex: 'cd /d %FILE_LOCATION% && python %FILE_NAME%' to navigate to the current file directory, and run it with python)

In these settings, the following are available as variables in your build commands: %FILE_LOCATION%, %FILE_NAME%, %FILE_NAME_NO_EXT%, and %INSTALL_DIR%
):"),
		},{
			icu::UnicodeString::fromUTF8("KeyMaps"),
			icu::UnicodeString::fromUTF8(R":(General:
	● Ctrl+Shift+O ----- Open Folder
	● Ctrl+O ----------- Open File
	● Ctrl+S ----------- Save (CodeWizard saves automatically every 4 seconds)
	● Ctrl+Shift+P ----- Bring focus to the command palette
	● Ctrl+Shift+U ----- Bring focus to the command palette, and begins a project search (any command palette search starting with '&')
	● Ctrl+W ----------- Close the current file
	● Ctrl+N ----------- New empty file
	● Ctrl+< ----------- Jump to corresponding opening bracket
	● Ctrl+> ----------- Jump to corresponding closing bracket
	● Tab -------------- When selecting text, indent one tab
	● Shift+Tab -------- When selecting text, unindent one tab
	● Ctrl+] ----------- When selecting text, indent one tab
	● Ctrl+[ ----------- When selecting text, unindent one tab
	● Alt+3 ------------ Comment selected region
	● Alt+4 ------------ Uncomment selected region
	● Ctrl+/ ----------- Toggle comment on selected region
	● F5 --------------- Run code (see 'Using CodeWizard')
	● Ctrl+F ----------- Open the find/replace menu
	● Ctrl+Shift+Tab --- Next file tab

Macro Recording:
	● F12 -------------- To start a macro recording
	● F12 -------------- Again to finish recording
	● F11 -------------- To replay the macro

CodeWizard also has an optional modal experience which is designed to help you code faster. Enable it in the settings. Essentially every text edit will then have two modes, insert, and normal. While in insert you can type normally. When in normal mode you can execute commands to move around the text faster.

From Within Insert Mode:
	● All normal commands
	● Esc -------------- To exit insert mode, and enter normal mode

From Within Normal Mode:
	● All normal commands
	● 'i' -------------- To re-enter insert mode and exit normal mode
	● 'n' -------------- To re-enter insert mode and exit normal mode, but one character to the right
	● 'h' -------------- Move left one char
	● 'l' -------------- Move right one char
	● 'j' -------------- Move down one line
	● 'k' -------------- Move up one line
	● 'a' -------------- Move to the end of the line
	● 'o' -------------- Create a new line below the current line
	● 'w' -------------- Move back one word (same as ctrl+[left arrow])
	● 'e' -------------- Move forward one word (same as ctrl+[right arrow])
	● 'b' -------------- Also move back one word
	● 's' -------------- Select the word under the cursor.
	● Shift+'s' -------- Extend the selection with the word under the cursor
	● '>' -------------- Jump to corresponding closing bracket
	● '<' -------------- Jump to corresponding opening bracket
	● 'f' -------------- Open the find/replace menu
	● 'c' -------------- Copy text in selection
	● 'v' -------------- Paste at cursor position
	● 'x' -------------- Cut text in selection
	● 'r' -------------- Rename variable under cursor (only works with supported LSPs currently pypls does not support this)

Note that for most movement shortcuts ('h', 'j', 'k', 'l', '<', '>', 'a', 'w', 'e', 'b') you can also hold shift to extend the selected cursor text.

Hold alt while clicking or pressing an arrow (or one of hjkl keys) to add another cursor. When you're done with them press esc.):"),
		},{
			icu::UnicodeString::fromUTF8("AI"),
			icu::UnicodeString::fromUTF8(R":(    CodeWizard has support for AI. Specifically, after setting an AI provider, model, and key in the settings, press Alt+A to trigger an insertion at your current cursor position. Or you can then open an AI chat widget to chat with your chosen model in a window.

	For the settings you must get a provider (openrouter or lmstudio for example), a key for the provider (if required), the number of lines to send to the model for completions. If you choose 'Load AI Model On Start' it will send a request to your provider on startup (ONLY DO THIS IF YOU'RE RUNNING AN OFFLINE MODEL). The non-chat completions are not supported by most providers, but lmstudio supports them.):"),
		}
	};
	
	for (int i = 0; i < helpInformation.size(); i++) {
		auto ti = TabInfo();
		ti.id = i;
		ti.title = helpInformation[i][0];
		tb->addTab(ti);
		scrolled_to.push_back(0.0);
	}
	tb->selected_id = 0;
	
	label = new Label(this);
	label->setFullText(icu::UnicodeString::fromUTF8("Widgets"));
	label->POSITIONER = [&](Widget* l) {
		l->t_x = t_x;
		l->t_y = tb->t_h+tb->t_y - scrolled_to[tb->selected_id];
		l->t_w = t_w;
		l->t_h = label->should_be_h;
	};
	label->border = false;
	
	setToIndex(0);
}

void HelpMenu::setToIndex(int index) {
	label->setFullText(helpInformation[index][1]);
}

void HelpMenu::render() {
	App::DrawRect(t_x, t_y+closebutton->t_h-App::text_padding, t_w, App::text_padding, App::theme.main_background_color); // this fixes the corners on the bottom to be flat. In a kinda dumb way, but it will work. Probably
	App::DrawRoundedRect(t_x, t_y, t_w, closebutton->t_h, App::text_padding, App::theme.main_background_color);
	App::DrawRoundedRect(t_x, t_y+closebutton->t_h, t_w, t_h-closebutton->t_h, App::text_padding, App::theme.darker_background_color);
	
	int vert_padding = (closebutton->t_h-TextRenderer::get_text_height())/2;
	TextRenderer::draw_text(t_x+vert_padding, t_y+vert_padding, icu::UnicodeString::fromUTF8("CodeWizard2 Help Menu"), App::theme.main_text_color);
	
	closebutton->render();
	App::runWithSKIZ(tb->t_x, tb->t_y, tb->t_w, tb->t_h, [&](){
		tb->render();
	});
	App::runWithSKIZ(t_x, tb->t_y+tb->t_h, t_w, t_h-tb->t_h-closebutton->t_h, [&](){
		label->render();
	});
	
	App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	App::DrawRect(t_x, tb->t_y+tb->t_h, t_w, 1, App::theme.border);
}

void HelpMenu::position(int x, int y, int w, int h) {
	t_x = x+w*.1;
	t_y = y+h*.1;
	t_w = w*.8;
	t_h = h*.8;
	
	Widget::position(t_x, t_y, t_w, t_h);
	
	on_scroll_event(0, 0);
}

bool HelpMenu::on_key_event(int key, int scancode, int action, int mods) {
	Widget::on_key_event(key, scancode, action, mods);
	return true;
}

bool HelpMenu::on_char_event(unsigned int codepoint) {
	Widget::on_char_event(codepoint);
	return true;
}

bool HelpMenu::on_mouse_button_event(int button, int action, int mods) {
	int my = App::mouseY;
	
	if (my <= App::tb->t_h) {
		return false;
	}
	
	if (action == GLFW_PRESS && !cursor_in_this) {
		App::RemoveWidgetFromParent(this);
		return true;
	}
	
	Widget::on_mouse_button_event(button, action, mods);
	return true;
}

bool HelpMenu::on_mouse_move_event() {
	int my = App::mouseY;
	if (my <= App::tb->t_h) {
		return false;
	}
	
	Widget::on_mouse_move_event();
	return true;
}

bool HelpMenu::on_scroll_event(double xchange, double ychange) {
	if (tb->on_scroll_event(xchange, ychange) || !cursor_in_this) { return true; }
	
	scrolled_to[tb->selected_id] += ychange*6.0*(double)TextRenderer::get_text_height();
	
	int maxScroll = std::max(0, label->t_h-((t_h+t_y)-(tb->t_h+tb->t_y)));
	
	if (scrolled_to[tb->selected_id] < 0) {
		scrolled_to[tb->selected_id] = 0;
	}else if (scrolled_to[tb->selected_id] > maxScroll) {
		scrolled_to[tb->selected_id] = maxScroll;
	}
	
	return true;
}
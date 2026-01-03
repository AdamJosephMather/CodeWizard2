#pragma once

#include "brokenstatemenu.h"
#include "linenumbers.h"
#include "listbox.h"
#include "textedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"
#include "checkbox.h"

struct Position {
	int line;
	int character;
};

struct Range {
	Position start;
	Position end;
};

struct EditDoc {
	Range range;
	std::string newText;
};

struct FileEdit {
	std::string uri;
	std::vector<EditDoc> edits;
};

struct EditSection {
	int startLine, startChar;
	int   endLine,   endChar;
	std::string newText;
};

class CodeEdit : public Widget {
public:
	CodeEdit(Widget* parent, int tabid, App::PosFunction positioner, App::UpdateFInfoFunction fupdater);
	
	void request_close(close_callback_type callback);
	
	int TABID;
	
	std::atomic<bool> is_loading{false};
	
	App::PosFunction POS_FUNC = nullptr;
	App::UpdateFInfoFunction FUPDATER = nullptr;
	
	int timeuntil = -1;
	int timeuntilchauffeur = -1;
	
	TextEdit* textedit;
	LineNumbers* line_numbers;
	
	Highlighter* highlighter = nullptr;
	
	std::thread hoverthread;
	std::thread chauffeurthread;
	
	static int indentIdentifierAfterLine(icu::UnicodeString line, icu::UnicodeString nextline);
	
	int completion_id = -1;
	int goto_id = -1;
	int hover_id = -1;
	bool should_move_mouse_hover = false;
	int code_actions_id = -1;
	int rename_id = -1;
	
	bool rounded = true;
	
	TextEdit* renamebox = nullptr;
	Cursor renamecursor = Cursor();
	std::vector<std::string> completions = {};
	std::vector<int> errorlines = {};
	ListBox* completionbox;
	bool are_code_actions = false;
	bool is_chauffeur = false;
	std::vector<json> code_actions = {};
	ListBox* errorMenu;
	std::filesystem::file_time_type last_file_mod_time = {};
	
	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_char_event(unsigned int keycode);
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	void executeAction(WidgetActionType typ);
	
	bool hoveringHoverbox(int mx, int my, int padding = 0);
	bool hoveringCompletionBox(int mx, int my, int padding = 0);
	
	int last_mouse_x = -1;
	int last_mouse_y = -1;
	
	FileInfo* file = nullptr;
	std::string language = "";
	std::string lsp = "";
	void detectLanguage();
	
	TextEdit* hoverbox = nullptr;
	Cursor hoverCrsr = Cursor();
	
	LineResult highlightline(icu::UnicodeString line, TextMateInfo info);
	
	void position(int x, int y, int w, int h);
	void render();
	
	void triggerSaveAs();
	
	void openFile();
	
	void save();
	
	boolean was_in_a_file = false;
	
	bool FILE_BROKEN_STATE = false;
	BrokenStateMenu* broken_state_menu = nullptr;
	bool REQUESTING_FIXIT = false;
	BrokenStateMenu* fixit_request_menu = nullptr;
	
	void overwrite_file();
	void reload_file();
	
	bool find_menu_open = false;
	TextEdit* findTextEdit = nullptr;
	TextEdit* replaceTextEdit = nullptr;
	CheckBox* caseSensitivity = nullptr;
	Button* prevButton = nullptr;
	Button* nextButton = nullptr;
	Button* nextReplButton = nullptr;
	Button* allButton = nullptr;
	Button* showErrorsButton = nullptr;
	
	bool madeChangeBetweenSaves = false;
	
	std::mutex saving_lock;
	
	void activateCompletion();
	void activateFind(bool forwards, icu::UnicodeString tofind, bool case_sensitive);
	void activateReplace(bool forwards, icu::UnicodeString tofind, icu::UnicodeString replace, bool case_sensitive);
	void replaceAll(icu::UnicodeString tofind, icu::UnicodeString toreplace, bool case_sensitive);
	
	void run_fixit();
	int analyzeForFixit_on_lines(const std::vector<Line>&);
	
	void completionRecieved(std::vector<std::string> completions, int id);
	void publishDiagnostics(std::string file, std::vector<std::string> messages, std::vector<int> startC, std::vector<int> startL, std::vector<int> endC, std::vector<int> endL, std::vector<int> severities);
	
	void onTextChanged(Widget* w);
	void gotoerror(int errnum);
	void gotoDef(int id, int line1, int character1, int line, int character, std::string locURI);
	void hoverRecieved(std::string content, std::string type, int id);
	void showhideerrors();
	void actionsReceived(int id, json resp);
	void renameReceived(int id, json resp);
	
	std::vector<FileEdit> parseCommandArguments(const json& changesObj);
	std::vector<FileEdit> parseWorkspaceEdits(const json& docChangesArr);
	std::vector<FileEdit> parseCodeAction(const json& action);
	std::vector<std::string> splitLines(const std::string& s);
	void applyEditToLines(std::vector<std::string>& lines, const EditDoc& te);
	void applyOtherFileEdits(const std::vector<FileEdit>& edits, const std::string& currentFilePath);
	std::vector<EditSection> gatherCurrentFileSections(const std::vector<FileEdit>& edits, const std::string& currentFilePath);
	
	std::string augmentBuildCommand(std::string inital);
	
	void setComments();
	void removeComments();
private:
};
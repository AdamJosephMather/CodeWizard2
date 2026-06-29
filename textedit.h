#pragma once

#include "contextmenu.h"
#include "scrollbar.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

struct LineDiagnostic {
	MST::MonoString message;
	int sc;
	int ec;
	int type;
};

struct SyntectStateDeleter {
	void operator()(CW_SyntaxState* ptr) const noexcept {
		if (ptr) {
			cw_syntect_destroy_state(ptr);
		}
	}
};

using SyntectStatePtr = std::unique_ptr<CW_SyntaxState, SyntectStateDeleter>;

struct Line {
	MST::MonoString line_text;

	// Own tokens on the C++ side.
	// No Rust token pointer stored here.
	std::vector<CW_HighlightToken> tokens = {};
	
	int visual_length = 0;
	bool changed = true;
	bool highlightinguptodate = false;
	
	// Hash of the state that was used as input to highlight this line.
	uint64_t prev_hash = 0;

	// State after this line has been parsed/highlighted.
	SyntectStatePtr after_line_state = nullptr;
	uint64_t after_line_hash = 0;
	
	std::vector<LineDiagnostic> diagnostics = {};
	bool isMarked = false;

	Line() = default;
	~Line() = default;

	// Do not allow accidental shallow copies.
	Line(const Line&) = delete;
	Line& operator=(const Line&) = delete;

	// Allow vector erase/reallocation/move assignment.
	Line(Line&&) noexcept = default;
	Line& operator=(Line&&) noexcept = default;
	
	Line clone() const {
		Line l;
		l.line_text    = line_text;
		l.isMarked     = isMarked;
		l.diagnostics  = diagnostics;
		l.changed      = true;   // force rehighlight after restore
		return l;
	}
};

struct Cursor {
	int anchor_line = 0;
	int anchor_char = 0;
	int head_line = 0;
	int head_char = 0;
	
	int preffered_collumn = 0;
};

struct CursorScreen {
	int rel_line = 0;
	int rel_char = 0;
	MST::u32 charUnder = '\0';
	Color* color = App::theme.main_text_color;
};

struct CursorSelect {
	int rel_line = 0;
	int rel_char_start = 0;
	int rel_char_end = 0;
};

struct DiagnosticUnderline {
	int rel_line = 0;
	int rel_char_start = 0;
	int rel_char_end = 0;
	int type = 0;
};

enum class EditType {
	InsertLine,
	DeleteLine,
	ChangeLine
};

struct Edit {
	EditType type;
	Line line;
	int index;

	Edit() = default;
	Edit(Edit&&) noexcept = default;
	Edit& operator=(Edit&&) noexcept = default;
	Edit(const Edit&) = delete;
	Edit& operator=(const Edit&) = delete;
};

struct History {
	std::vector<Edit> edits;
	std::vector<Cursor> cursors_before;
	std::vector<Cursor> cursors_after;
	long long millis;

	History() = default;
	History(History&&) noexcept = default;
	History& operator=(History&&) noexcept = default;
	History(const History&) = delete;
	History& operator=(const History&) = delete;
};

class TextEdit : public Widget {
public:
	using LineChange = std::function<void(EditType,int)>;
	
	TextEdit(Widget* parent, App::PosFunction fnct);
	
	bool scrollbar_vertical = false;
	Scrollbar* scrollbar_v = nullptr;
	bool scrollbar_horizontal = false;
	Scrollbar* scrollbar_h = nullptr;
	
	ContextMenu* contextmenu = nullptr;
	
	Color* borderColor;
	Color* activeBorderColor;
	bool rounded = false;
	
	CW_SyntaxEngine* highlighter = nullptr;
	SyntectStatePtr highlighter_initial_state = nullptr;
	bool alreadyHighlighted = false;
	
	using IndentIdentifier = std::function<int(MST::MonoString line, MST::MonoString nextline)>;
	
	int getVisLen(const MST::MonoString& line);
	
	App::PosFunction POS_FUNC = nullptr;
	IndentIdentifier getIndentationLevelAfterLine = nullptr;
	
	History historyThisUpdate;
	std::vector<Line> lines;
	bool changed_during_update = true;
	
	std::vector<Cursor> cursors;
	std::vector<MST::MonoString> coppiedText;
	
	double scrolled_to_horz = 0.0;
	double scrolled_to_vert = 0.0;
	
	double scroll_horizontal_change = 0.0;
	double scroll_vertical_change = 0.0;
	
	double max_scroll_horz = 0.0;
	double max_scroll_vert = 0.0;
	
	// calculated each frame
	double start_y = 0.0; // lines
	double start_x = 0.0; // chars
	
	int max_line_len = 0;
	
	int vim_repeater = 0;
	char mode = 'i';
	char wasmode = 'i';
	char ignoringChar = '\0';
	
	std::vector<MST::MonoString>  draw_text;
	std::vector<std::vector<Color*>> draw_color;
	std::vector<DiagnosticUnderline> draw_diagnostics;
	std::vector<bool>                draw_mark;
	std::vector<CursorScreen>        draw_cursor;
	std::vector<CursorSelect>        draw_selection;
	
	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	bool on_char_event(unsigned int codepoint);
	
	bool handleUserKey(int key, int scancode, int action, int mods);
	bool handleNavKey(int key, int scancode, int action, int mods);
	bool handleDeleteKey(int key, int scancode, int action, int mods);
	bool handleInsertKey(int key, int scancode, int action, int mods);
	
	void applyMoveToAllCursors(int key, bool shift, bool control);
	Cursor applyMoveToCursor(Cursor c, int key, bool shift, bool control);
	void applyDeleteToAllCursors(int key, bool control);
	void applyIndentChangeToAllCursors(int change_by);
	void deleteTextAtCursor(Cursor c, int key, bool control);
	void applyIndentChangeToCursor(Cursor c, int change_by);
	void applyInsertToAllCursors(MST::MonoString);
	void insertTextAtCursor(Cursor c, MST::MonoString);
	bool tryingToEnsureCursorPos = false;
	void ensureCursorVisible(Cursor c);
	void toggleMark();
	void clearMarks();
	void gotoPrevMark();
	void gotoNextMark();
	
	void cut();
	void copy();
	void paste();
	
	void insertNewCursorDown();
	void insertNewCursorUp();
	void HandleOverlappingCursors();
	
	std::vector<History> undo_stack = {};
	std::vector<History> redo_stack = {};
	
	void updateUndoHistory();
	void activateUndo();
	void activateRedo();
	
	void insertPendingCharInput();
	void flushPendingCharInput();
	bool shouldHoldCharInput(const std::u32string& s);
	
	MST::MonoString getSelectedText(Cursor c);
	
	void position(int x, int y, int w, int h);
	void render();
	void executeAction(WidgetActionType typ);
	void Highlight(int first_line, int last_line);
	History createHistory();
	
	Color* getColorFromToken(const CW_HighlightToken& token);
	
	void setFullText(MST::MonoString text);
	MST::MonoString getFullText();
	
	Cursor getCursorForMousePosition(int mx, int my, bool* gottoit = nullptr);
	bool is_selecting_text_with_mouse = false;
	
	std::pair<int,int> findMatchingBracket(int type, int direction, int line, int col);
	std::pair<std::pair<int,int>,std::pair<int,int>> _getCursSelec(Cursor c);
	
	
	App::PosFunction ontextchange = nullptr;
	LineChange onlinechange = nullptr;
	bool largereditblock = false;
	
	int _mapFromVisualToReal(int line, int c);
	
	MST::MonoString getCurrentWord(const MST::MonoString& blockText, int blockPos);
	
	Color* background_color = App::theme.darker_background_color;
	
	bool DONT_SCROLL_VERT_CURS = false;
	bool DO_POSITION = false;
	bool WAS_ACTIVE = false;
	bool DID_POSITION = false;
	
	int tabWidth = 4;
private:
	std::pair<int,int> _handleSectionRemoved(int l, int c, int sl, int el, int sc, int ec);
	std::pair<int,int> _handleSectionAdded(int l, int c, int sl, int sc, int nl, int nc);
	int _findNextWord(Cursor c, int dir);
	int charType(MST::u32 c);
	
	std::u32string pendingCharInput;
	bool hasPendingCharInput = false;
	
};

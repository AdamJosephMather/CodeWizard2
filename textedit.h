#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>
#include <map>
#include "application.h"
#include "highlighter.h"

struct LineDiagnostic {
	icu::UnicodeString message;
	int sc;
	int ec;
	int type;
};

struct Line {
	icu::UnicodeString line_text;
	std::vector<ColoredTokens> tokens;
	
	int visual_length = 0;
	bool changed = true;
	bool highlightinguptodate = false;
	
	int64_t prev_hash;
	TextMateInfo after_line_colored;
	
	std::vector<LineDiagnostic> diagnostics = {};
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
};

struct History {
	std::vector<Edit> edits;
	std::vector<Cursor> cursors_before;
	std::vector<Cursor> cursors_after;
	long long millis;
};

class TextEdit : public Widget {
public:
	using HighlightFunct = std::function<LineResult(icu::UnicodeString line, TextMateInfo info)>;
	using HighlightBeginFunct = std::function<TextMateInfo()>;
	using CompareHighlightInfo = std::function<bool(TextMateInfo*, TextMateInfo*)>;
	
	TextEdit(Widget* parent, App::PosFunction fnct);
	
	HighlightFunct highlighter = nullptr;
	HighlightBeginFunct getblankhighlighting = nullptr;
	CompareHighlightInfo highlighterNotEqual = nullptr;
	
	using IndentIdentifier = std::function<int(icu::UnicodeString line, icu::UnicodeString nextline)>;
	
	App::PosFunction POS_FUNC = nullptr;
	IndentIdentifier getIndentationLevelAfterLine = nullptr;
	
	History historyThisUpdate;
	std::vector<Line> lines;
	bool changed_during_update = true;
	
	std::vector<Cursor> cursors;
	
	double scrolled_to_horz = 0.0;
	double scrolled_to_vert = 0.0;
	
	double max_scroll_horz = 0.0;
	double max_scroll_vert = 0.0;
	
	// calculated each frame
	double start_y = 0.0; // lines
	double start_x = 0.0; // chars
	
	int max_line_len = 0;
	
	int vim_repeater = 0;
	char mode = 'i';
	char wasmode = 'i';
	
	std::vector<icu::UnicodeString>  draw_text;
	std::vector<std::vector<Color*>> draw_color;
	std::vector<DiagnosticUnderline> draw_diagnostics;
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
	void applyInsertToAllCursors(icu::UnicodeString);
	void insertTextAtCursor(Cursor c, icu::UnicodeString);
	bool tryingToEnsureCursorPos = false;
	void ensureCursorVisible(Cursor c);
	
	void insertNewCursorDown();
	void insertNewCursorUp();
	void HandleOverlappingCursors();
	
	std::vector<History> undo_stack = {};
	std::vector<History> redo_stack = {};
	
	void updateUndoHistory();
	void activateUndo();
	void activateRedo();
	
	icu::UnicodeString getSelectedText(Cursor c);
	
	void position(int x, int y, int w, int h);
	void render();
	void Highlight(int first_line, int last_line);
	History createHistory();
	
	Color* getColorFromTokens(int indx, std::vector<ColoredTokens> tokens, bool* arange);
	
	void setFullText(icu::UnicodeString text);
	icu::UnicodeString getFullText();
	
	Cursor getCursorForMousePosition(int mx, int my, bool* gottoit = nullptr);
	bool is_selecting_text_with_mouse = false;
	
	std::pair<int,int> findMatchingBracket(int type, int direction, int line, int col);
	std::pair<std::pair<int,int>,std::pair<int,int>> _getCursSelec(Cursor c);
	
	
	App::PosFunction ontextchange = nullptr;
	bool largereditblock = false;
	
	int _mapFromVisualToReal(int line, int c);
	int _mapFromRealToVisual(int line, int c);
	
	icu::UnicodeString getCurrentWord(const icu::UnicodeString& blockText, int blockPos);
	
	Color* background_color = App::theme.darker_background_color;
private:
	std::pair<int,int> _handleSectionRemoved(int l, int c, int sl, int el, int sc, int ec);
	std::pair<int,int> _handleSectionAdded(int l, int c, int sl, int sc, int nl, int nc);
	int _findNextWord(Cursor c, int dir);
	int charType(UChar32 c);
	
};

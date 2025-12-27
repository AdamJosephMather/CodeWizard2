// ============================================================================
// terminal.h
// ============================================================================
#pragma once

#include <unicode/umachine.h>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <cstring>
#include <functional>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

// ---- libvterm ----
#define small vterm_small_avoid_winrpc
extern "C" {
#include <vterm.h>
}
#undef small

struct CursorInfo {
	int row = 0;
	int col = 0;
	bool visible = true;
	bool blink = false;
	int shape = 1;  // 1=BLOCK, 2=UNDERLINE, 3=BAR_LEFT
};

struct OurCell {
	UChar32 c;
	uint8_t bg_red = 0;
	uint8_t bg_green = 0;
	uint8_t bg_blue = 0;
	uint8_t fg_red = 0;
	uint8_t fg_green = 0;
	uint8_t fg_blue = 0;
};

struct OurLine {
	std::vector<OurCell> cells;
	bool continuation;
};

class Terminal {
public:
	using SCROLLDOWN = std::function<void(int)>;
	
	Terminal(int cols, int rows, SCROLLDOWN sd);
	~Terminal();
	
	bool start(const std::wstring& shell = L"");
	void stop();

	bool writeInput(const void* data, size_t bytes);
	bool writeInput(const std::string& utf8) { return writeInput(utf8.data(), utf8.size()); }
	bool writeInput(const char* utf8_cstr) { return writeInput(utf8_cstr, std::strlen(utf8_cstr)); }

	bool resize(int cols, int rows);
	
	OurCell getCell(int row, int col);
	CursorInfo getCursorInfo();

	// --- Keyboard ---
	bool sendText(const std::string& utf8);
	bool sendEnter();
	bool sendBackspace();
	bool sendCtrl(char letter);
	
	enum class SpecialKey {
		Up, Down, Left, Right, Home, End, InsertKey, DeleteKey, PageUp, PageDown,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
	};
	bool sendSpecialKey(SpecialKey key, bool shift = false, bool alt = false, bool ctrl = false);

	// --- Mouse (cell coords) ---
	bool enableMouseTracking(bool enable);
	bool mousePress(int row, int col, int button, bool pressed,
	                bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseMove(int row, int col, bool buttonHeld,
	               bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseDrag(int startRow, int startCol, int endRow, int endCol, int button,
	               bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseScroll(int row, int col, int lines,
	                 bool shift = false, bool alt = false, bool ctrl = false);

	// --- Scrollback & document access ---
	int docLineIdForScreenRow(int screenRow) const;
	int screenRowForDocLineId(int docId) const;
	bool getDocCell(int docId, int col, OurCell& out);
	
	size_t scrollbackSize() const { return m_scrollback.size(); }
	int docCols() const { return m_cols; }
	int docRows() const { return m_rows; }
	int docLineCount() const { return static_cast<int>(m_scrollback.size()) + m_rows; }
	
	bool appWantsMouse() const;
	bool getDocWraps(int docId);
	
private:
	// --- ConPTY ---
	bool initConPty();
	bool launchShell(const std::wstring& shell);
	void teardownConPty();
	void readerLoop();

	// --- vterm ---
	bool initVTerm();
	void teardownVTerm();
	void flushVTermDamage();
	void onDamage(const VTermRect& rect);
	void dumpRow(int r);

	// --- Scrollback helpers ---
	void clampView();
	void savePushLine(int cols, const VTermScreenCell* cells);
	bool savePopLine(int cols, VTermScreenCell* cells);
	void scrollbackLines(int delta);
	void scrollbackPageUp();
	void scrollbackPageDown();

	// --- vterm callback trampolines ---
	static int s_screen_damage(VTermRect rect, void* user);
	static int s_screen_moverect(VTermRect dest, VTermRect src, void* user);
	static int s_screen_movecursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
	static int s_screen_settermprop(VTermProp prop, VTermValue* val, void* user);
	static int s_screen_bell(void* user);
	static int s_screen_resize(int rows, int cols, void* user);
	static int s_screen_sb_pushline(int cols, const VTermScreenCell* cells, void* user);
	static int s_screen_sb_popline(int cols, VTermScreenCell* cells, void* user);

	// --- Members ---
	HPCON m_hPC{ nullptr };
	HANDLE m_hToPty{ INVALID_HANDLE_VALUE };
	HANDLE m_hFromPty{ INVALID_HANDLE_VALUE };
	PROCESS_INFORMATION m_pi{};

	std::thread m_reader;
	std::atomic<bool> m_running{ false };

	int m_cols{ 0 };
	int m_rows{ 0 };

	VTerm* m_vt{ nullptr };
	VTermScreen* m_screen{ nullptr };
	std::mutex m_vtermMutex;
	std::atomic<bool> m_screenReady{ false };

	std::vector<char> m_rowBuf;

	CursorInfo m_cursorInfo;
	std::deque<OurLine> m_scrollback;
	size_t m_sb_max = 10000;
	int m_view_off = 0;
	std::atomic<bool> m_altScreen{ false };
	std::atomic<bool> m_mouseReporting{ false };

	Terminal(const Terminal&) = delete;
	Terminal& operator=(const Terminal&) = delete;
	
	SCROLLDOWN scrollDown = nullptr;
};
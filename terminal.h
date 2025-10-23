#pragma once

#include <mutex>
#include <unicode/umachine.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define NOMINMAX
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

#include <deque>

// ---- libvterm ----
#define small vterm_small_avoid_winrpc   // avoid Windows 'small' collision
extern "C" {
#include <vterm.h>
}
#undef small

struct CursorInfo {
	int row = 0;
	int col = 0;
	bool visible = true;   // as reported by libvterm
	bool blink = false;    // DECSCUSR blink bit
	int  shape = 1;        // libvterm cursor shape enum value
						   // (typically 1=BLOCK, 2=UNDERLINE, 3=BAR_LEFT)
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

class Terminal {
public:
	Terminal(int cols, int rows);
	~Terminal();
	
	std::mutex m_vtermMutex; // <-- ADD THIS

	bool start(const std::wstring& shell = L"powershell.exe");
	void stop();

	bool writeInput(const void* data, size_t bytes);
	bool writeInput(const std::string& utf8) { return writeInput(utf8.data(), utf8.size()); }
	bool writeInput(const char* utf8_cstr)   { return writeInput(utf8_cstr, std::strlen(utf8_cstr)); }

	bool resize(int cols, int rows);
	
	OurCell getCell(int row, int col);
	CursorInfo m_cursorInfo;
	
	// --- Keyboard ---
	bool sendText(const std::string& utf8);
	bool sendEnter();            // \r
	bool sendBackspace();        // DEL (0x7F)
	bool sendCtrl(char letter);  // e.g. sendCtrl('C') -> 0x03
	
	// A few common special keys (xterm sequences)
	enum class SpecialKey { Up, Down, Left, Right, Home, End, InsertKey, DeleteKey, PageUp, PageDown,
	                        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12 };
	bool sendSpecialKey(SpecialKey key, bool shift=false, bool alt=false, bool ctrl=false);
	
	// --- Mouse (cell coords) ---
	bool enableMouseTracking(bool enable); // ?1002 + ?1006
	bool mousePress(int row, int col, int button /*0=L,1=M,2=R*/, bool pressed,
	                bool shift=false, bool alt=false, bool ctrl=false);
	bool mouseMove(int row, int col, bool buttonHeld,
	               bool shift=false, bool alt=false, bool ctrl=false);
	bool mouseDrag(int startRow, int startCol, int endRow, int endCol, int button /*0/1/2*/,
	               bool shift=false, bool alt=false, bool ctrl=false);
	bool mouseScroll(int row, int col, int lines /*+up/-down*/,
	                 bool shift=false, bool alt=false, bool ctrl=false);
	
	CursorInfo getCursorInfo();
	
	
	
	std::deque<std::vector<OurCell>> m_scrollback;
	size_t m_sb_max = 2000;             // configurable max lines
	int m_view_off = 0;                 // 0 = follow bottom; >0 = lines scrolled back
	std::atomic<bool> m_altScreen{false};
	std::atomic<bool> m_mouseReporting{false};
	
	// helpers:
	void clampView();
	bool appWantsMouse() const;         // alt-screen or explicit mouse mode
	void savePushLine(int cols, const VTermScreenCell* cells);
	bool savePopLine(int cols, VTermScreenCell* cells);
	

private:
	// --- ConPTY internals ---
	void readerLoop();
	bool initConPty();
	bool launchShell(const std::wstring& shell);
	void teardownConPty();

	// --- vterm internals ---
	bool initVTerm();
	void teardownVTerm();
	void flushVTermDamage();

	// vterm screen callback helpers
	void onDamage(const VTermRect& rect);
	void dumpRow(int r);

	// Pseudo console handle
	HPCON m_hPC{ nullptr };

	// App-side pipe handles
	HANDLE m_hToPty{ INVALID_HANDLE_VALUE };
	HANDLE m_hFromPty{ INVALID_HANDLE_VALUE };

	// Child process info
	PROCESS_INFORMATION m_pi{};

	// Reader thread
	std::thread m_reader;
	std::atomic<bool> m_running{ false };

	// Size tracking
	int m_cols{ 0 };
	int m_rows{ 0 };

	// ---- vterm state ----
	VTerm*        m_vt{ nullptr };
	VTermScreen*  m_screen{ nullptr };
	
	std::atomic<bool> m_screenReady{false};

	// Scratch buffer for row printing
	std::vector<char> m_rowBuf;

	// No copying
	Terminal(const Terminal&) = delete;
	Terminal& operator=(const Terminal&) = delete;

	// ---- vterm callback trampolines (correct signatures) ----
	static int s_screen_damage(VTermRect rect, void* user);
	static int s_screen_moverect(VTermRect dest, VTermRect src, void* user);
	static int s_screen_movecursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
	static int s_screen_settermprop(VTermProp prop, VTermValue* val, void* user);
	static int s_screen_bell(void* user);
	static int s_screen_resize(int rows, int cols, void* user);
	static int s_screen_sb_pushline(int cols, const VTermScreenCell* cells, void* user);
	static int s_screen_sb_popline(int cols, VTermScreenCell* cells, void* user);
	
	bool drainVTermOutputToPty_();
	
	
	
	
	void scrollbackLines(int delta);
	void scrollbackPageUp();
	void scrollbackPageDown();
};

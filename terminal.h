#pragma once

#include <unicode/umachine.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

extern "C" {
#include <ghostty/vt.h>
}

struct CursorInfo {
	int row = 0;
	int col = 0;
	bool visible = true;
	bool blink = false;
	int shape = 1; // 1=BLOCK, 2=UNDERLINE, 3=BAR_LEFT
};

struct OurCell {
	UChar32 c = U' ';
	uint8_t bg_red = 0;
	uint8_t bg_green = 0;
	uint8_t bg_blue = 0;
	uint8_t fg_red = 255;
	uint8_t fg_green = 255;
	uint8_t fg_blue = 255;
};

class Terminal {
public:
	using SCROLLDOWN = std::function<void(int)>;

	Terminal(int cols, int rows, SCROLLDOWN sd = nullptr);
	~Terminal();

	bool start(const std::wstring& shell = L"");
	void stop();

	bool writeInput(const void* data, size_t bytes);
	bool writeInput(const std::string& utf8) { return writeInput(utf8.data(), utf8.size()); }
	bool writeInput(const char* utf8_cstr) { return writeInput(utf8_cstr, std::strlen(utf8_cstr)); }

	bool resize(int cols, int rows);
	bool resize(int cols, int rows, int cellWidthPx, int cellHeightPx);

	OurCell getCell(int row, int col);
	CursorInfo getCursorInfo();
	bool getViewSnapshot(std::vector<OurCell>& cells, CursorInfo& cursor, int& cols, int& rows);
	std::string getLastTextLines(int maxLogicalLines);

	bool sendText(const std::string& utf8);
	bool sendEnter();
	bool sendBackspace();
	bool sendCtrl(char letter);

	enum class SpecialKey {
		Up, Down, Left, Right, Home, End, InsertKey, DeleteKey, PageUp, PageDown,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
	};
	bool sendSpecialKey(SpecialKey key, bool shift = false, bool alt = false, bool ctrl = false);

	// Debug/compatibility helper. This injects the DEC mouse-mode sequence into
	// libghostty's parser; it does not write the sequence to the child shell.
	bool enableMouseTracking(bool enable);
	bool mousePress(int row, int col, int button, bool pressed,
	                bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseMove(int row, int col, bool buttonHeld,
	               bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseDrag(int startRow, int startCol, int endRow, int endCol, int button,
	               bool shift = false, bool alt = false, bool ctrl = false);
	bool mouseScroll(int row, int col, int lines,
	                 bool shift = false, bool alt = false, bool ctrl = false);

	int docLineIdForScreenRow(int screenRow) const;
	int screenRowForDocLineId(int docId) const;
	bool getDocCell(int docId, int col, OurCell& out);
	bool getDocWraps(int docId);

	size_t scrollbackSize() const;
	int docCols() const { return m_cols.load(std::memory_order_acquire); }
	int docRows() const { return m_rows.load(std::memory_order_acquire); }
	int docLineCount() const;

	bool appWantsMouse() const;

	std::atomic<bool> RERENDER{true};

private:
	bool initConPty();
#ifdef _WIN32
	using CreatePseudoConsoleFn = HRESULT (WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
	using ResizePseudoConsoleFn = HRESULT (WINAPI*)(HPCON, COORD);
	using ClosePseudoConsoleFn = VOID (WINAPI*)(HPCON);

	void loadConPtyApi();
	void unloadConPtyApi();
	bool launchShell(const std::wstring& shell);
#else
	bool launchShell(const std::string& shell);
#endif
	void teardownConPty();
	void readerLoop();

	bool initGhostty();
	void teardownGhostty();
	bool refreshRenderStateLocked();
	void refreshTerminalMetadataLocked();
	void updateMouseGeometryLocked();
	bool readGridCellLocked(int docId, int col, OurCell& out);
	bool readGridWrapLocked(int docId, bool& out);

	bool encodeMouseEvent(GhosttyMouseAction action,
	                      GhosttyMouseButton button,
	                      bool hasButton,
	                      int row,
	                      int col,
	                      bool shift,
	                      bool alt,
	                      bool ctrl,
	                      std::string& out);
	static GhosttyMouseButton toGhosttyMouseButton(int button);
	static GhosttyColorRgb resolveColor(const GhosttyStyleColor& color,
	                                    const GhosttyRenderStateColors& colors,
	                                    GhosttyColorRgb fallback);
	static void sWritePty(GhosttyTerminal terminal, void* userdata,
	                      const uint8_t* data, size_t len);

#ifdef _WIN32
	HPCON m_hPC{nullptr};
	HANDLE m_hToPty{INVALID_HANDLE_VALUE};
	HANDLE m_hFromPty{INVALID_HANDLE_VALUE};
	PROCESS_INFORMATION m_pi{};
	HMODULE m_conPtyModule{nullptr};
	CreatePseudoConsoleFn m_createPseudoConsole{&::CreatePseudoConsole};
	ResizePseudoConsoleFn m_resizePseudoConsole{&::ResizePseudoConsole};
	ClosePseudoConsoleFn m_closePseudoConsole{&::ClosePseudoConsole};
	bool m_usingSideBySideConPty{false};
	std::mutex m_outputGateMutex;
	bool m_resizeInProgress{false};
	std::vector<uint8_t> m_pendingPtyOutput;
#else
	int m_masterFd{-1};
	pid_t m_childPid{-1};
#endif

	std::thread m_reader;
	std::atomic<bool> m_running{false};

	std::atomic<int> m_cols{0};
	std::atomic<int> m_rows{0};
	std::atomic<int> m_cellWidthPx{1};
	std::atomic<int> m_cellHeightPx{1};

	GhosttyTerminal m_terminal{nullptr};
	GhosttyRenderState m_renderState{nullptr};
	GhosttyRenderStateRowIterator m_rowIterator{nullptr};
	GhosttyRenderStateRowCells m_rowCells{nullptr};
	GhosttyMouseEncoder m_mouseEncoder{nullptr};
	GhosttyMouseEvent m_mouseEvent{nullptr};

	mutable std::mutex m_stateMutex;
	std::mutex m_resizeMutex;
	std::atomic<bool> m_terminalReady{false};

	std::vector<OurCell> m_viewCells;
	bool m_renderSnapshotDirty{true};
	GhosttyRenderStateColors m_colors{};
	GhosttyTerminalScrollbar m_scrollbar{};
	CursorInfo m_cursorInfo;
	std::atomic<bool> m_altScreen{false};
	std::atomic<bool> m_mouseReporting{false};
	std::atomic<uint32_t> m_buttonsDownMask{0};
	// libghostty currently applies max_scrollback as a byte limit. The
	// maximum value matches its internal unlimited mode and prevents a narrow
	// resize from pruning history merely because reflow creates more rows.
	const size_t m_scrollbackMax{std::numeric_limits<size_t>::max()};

	Terminal(const Terminal&) = delete;
	Terminal& operator=(const Terminal&) = delete;

	// Kept so existing construction sites do not need to change. libghostty
	// owns scrollback pruning/reflow, so this callback is no longer used.
	SCROLLDOWN scrollDown;
};

// ============================================================================
// terminal.cpp
// ============================================================================
#include "terminal.h"
#include "application.h"

#include <iostream>
#include <cassert>
#include <algorithm>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <processthreadsapi.h>
#include <consoleapi2.h>
#include <consoleapi3.h>

namespace {

inline void closeHandleIfValid(HANDLE& h) {
	if (h && h != INVALID_HANDLE_VALUE) {
		::CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
	}
}

inline std::wstring defaultShell() {
	return widen(App::settings->getValue("terminal_cmd", (std::string)"cmd.exe"));
}

} // namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

Terminal::Terminal(int cols, int rows, SCROLLDOWN sd) : m_cols(cols), m_rows(rows) {
	scrollDown = sd;
	ZeroMemory(&m_pi, sizeof(m_pi));
}

Terminal::~Terminal() {
	stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool Terminal::start(const std::wstring& shell) {
	if (m_running.load()) return true;
	
	if (!initConPty()) {
		teardownConPty();
		return false;
	}
	
	if (!launchShell(shell.empty() ? defaultShell() : shell)) {
		teardownConPty();
		return false;
	}
	
	if (!initVTerm()) {
		stop();
		return false;
	}

	m_running = true;
	m_reader = std::thread(&Terminal::readerLoop, this);
	
	return true;
}

void Terminal::stop() {
	m_running.exchange(false);

	// Unblock pending I/O first to let readerLoop exit quickly
	if (m_hFromPty != INVALID_HANDLE_VALUE) {
		::CancelIoEx(m_hFromPty, nullptr);
	}
	if (m_hToPty != INVALID_HANDLE_VALUE) {
		::CancelIoEx(m_hToPty, nullptr);
	}

	// Join the reader thread
	if (m_reader.joinable()) {
		m_reader.join();
	}

	// Close pipe handles
	closeHandleIfValid(m_hToPty);
	closeHandleIfValid(m_hFromPty);

	// Terminate child process
	if (m_pi.hProcess) {
		::WaitForSingleObject(m_pi.hProcess, 200);
		::TerminateProcess(m_pi.hProcess, 0);
		closeHandleIfValid(m_pi.hProcess);
	}

	// Close ConPTY
	if (m_hPC) {
		::ClosePseudoConsole(m_hPC);
		m_hPC = nullptr;
	}

	// Free libvterm
	teardownVTerm();
}

bool Terminal::resize(int cols, int rows) {
	{
		std::lock_guard<std::mutex> lock(m_vtermMutex);
		if (m_vt) {
			vterm_set_size(m_vt, rows, cols);
		} else {
			m_rows = rows;
			m_cols = cols;
			m_rowBuf.resize(static_cast<size_t>(m_cols) + 1, 0);
		}
	}
	
	if (m_hPC) {
		COORD size{};
		size.X = static_cast<SHORT>(cols);
		size.Y = static_cast<SHORT>(rows);
		if (FAILED(::ResizePseudoConsole(m_hPC, size))) {
			return false;
		}
	}

	return true;
}

// ============================================================================
// ConPTY
// ============================================================================

bool Terminal::initConPty() {
	HANDLE hPtyInRead = INVALID_HANDLE_VALUE;
	if (!::CreatePipe(&hPtyInRead, &m_hToPty, nullptr, 0)) {
		std::cerr << "CreatePipe (to PTY) failed: " << GetLastError() << "\n";
		return false;
	}

	HANDLE hPtyOutWrite = INVALID_HANDLE_VALUE;
	if (!::CreatePipe(&m_hFromPty, &hPtyOutWrite, nullptr, 0)) {
		std::cerr << "CreatePipe (from PTY) failed: " << GetLastError() << "\n";
		closeHandleIfValid(hPtyInRead);
		closeHandleIfValid(m_hToPty);
		return false;
	}

	COORD size{};
	size.X = static_cast<SHORT>(m_cols);
	size.Y = static_cast<SHORT>(m_rows);

	HRESULT hr = ::CreatePseudoConsole(size, hPtyInRead, hPtyOutWrite, 0, &m_hPC);
	closeHandleIfValid(hPtyInRead);
	closeHandleIfValid(hPtyOutWrite);

	if (FAILED(hr)) {
		std::cerr << "CreatePseudoConsole failed: hr=0x" << std::hex << hr << std::dec << "\n";
		closeHandleIfValid(m_hToPty);
		closeHandleIfValid(m_hFromPty);
		return false;
	}
	return true;
}

bool Terminal::launchShell(const std::wstring& shell) {
	SIZE_T attrListSize = 0;
	::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
	std::vector<char> attrStorage(attrListSize);
	auto attrList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrStorage.data());

	if (!::InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize)) {
		std::cerr << "InitializeProcThreadAttributeList failed: " << GetLastError() << "\n";
		return false;
	}

	if (!::UpdateProcThreadAttribute(
			attrList, 0,
			PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
			m_hPC, sizeof(m_hPC),
			nullptr, nullptr)) {
		std::cerr << "UpdateProcThreadAttribute failed: " << GetLastError() << "\n";
		::DeleteProcThreadAttributeList(attrList);
		return false;
	}

	STARTUPINFOEXW siex{};
	siex.StartupInfo.cb = sizeof(siex);
	siex.lpAttributeList = attrList;

	std::wstring cmdline = shell;
	DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

	BOOL ok = ::CreateProcessW(
		nullptr, cmdline.data(),
		nullptr, nullptr,
		FALSE, flags,
		nullptr, nullptr,
		&siex.StartupInfo, &m_pi
	);

	::DeleteProcThreadAttributeList(attrList);

	if (!ok) {
		return false;
	}

	closeHandleIfValid(m_pi.hThread);
	return true;
}

void Terminal::teardownConPty() {
	closeHandleIfValid(m_hToPty);
	closeHandleIfValid(m_hFromPty);

	if (m_pi.hProcess) {
		::TerminateProcess(m_pi.hProcess, 0);
		closeHandleIfValid(m_pi.hProcess);
	}
	if (m_hPC) {
		::ClosePseudoConsole(m_hPC);
		m_hPC = nullptr;
	}
}

void Terminal::readerLoop() {
	constexpr DWORD BUF = 4096;
	std::vector<char> buffer(BUF);

	for (;;) {
		if (!m_running.load()) break;
		HANDLE h = m_hFromPty;
		if (h == INVALID_HANDLE_VALUE) break;

		DWORD got = 0;
		BOOL ok = ::ReadFile(h, buffer.data(), BUF, &got, nullptr);
		if (!ok) {
			DWORD err = ::GetLastError();
			if (err == ERROR_OPERATION_ABORTED || err == ERROR_BROKEN_PIPE) break;
			break;
		}
		if (got == 0) break;

		std::lock_guard<std::mutex> lock(m_vtermMutex);
		if (!m_vt) continue;
		vterm_input_write(m_vt, buffer.data(), static_cast<size_t>(got));
		flushVTermDamage();
	}
}

// ============================================================================
// vterm
// ============================================================================

bool Terminal::initVTerm() {
	m_vt = vterm_new(m_rows, m_cols);
	if (!m_vt) return false;
	vterm_set_utf8(m_vt, 1);

	m_screen = vterm_obtain_screen(m_vt);
	if (!m_screen) {
		vterm_free(m_vt);
		m_vt = nullptr;
		return false;
	}

	vterm_screen_enable_altscreen(m_screen, 1);
	m_rowBuf.resize(static_cast<size_t>(m_cols) + 1, 0);

	static const VTermScreenCallbacks Cbs = {
		&Terminal::s_screen_damage,
		&Terminal::s_screen_moverect,
		&Terminal::s_screen_movecursor,
		&Terminal::s_screen_settermprop,
		&Terminal::s_screen_bell,
		&Terminal::s_screen_resize,
		&Terminal::s_screen_sb_pushline,
		&Terminal::s_screen_sb_popline
	};
	vterm_screen_set_callbacks(m_screen, &Cbs, this);

#ifdef VTERM_MOUSE_PROTO_SGR
	vterm_mouse_set_protocol(m_vt, VTERM_MOUSE_PROTO_SGR);
#elif defined(VTERM_MOUSE_PROTOCOL_SGR)
	vterm_mouse_set_protocol(m_vt, VTERM_MOUSE_PROTOCOL_SGR);
#endif

	m_screenReady.store(true, std::memory_order_release);
	vterm_screen_reset(m_screen, 1);
	return true;
}

void Terminal::teardownVTerm() {
	std::lock_guard<std::mutex> lock(m_vtermMutex);
	if (m_vt) {
		vterm_free(m_vt);
		m_vt = nullptr;
		m_screen = nullptr;
	}
}

void Terminal::flushVTermDamage() {
	if (!m_screen) return;
	vterm_screen_flush_damage(m_screen);
}

void Terminal::onDamage(const VTermRect& rect) {
	if (!m_screenReady.load(std::memory_order_acquire)) return;
	
	for (int r = rect.start_row; r < rect.end_row; ++r) {
		dumpRow(r);
	}
}

void Terminal::dumpRow(int r) {
	if (!m_screen) return;
	const int cols = m_cols;
	if (cols <= 0) return;

	int out = 0;
	for (int c = 0; c < cols; ++c) {
		VTermScreenCell cell{};
		vterm_screen_get_cell(m_screen, VTermPos{ r, c }, &cell);
		char32_t cp = cell.chars[0] ? static_cast<char32_t>(cell.chars[0]) : U' ';
		m_rowBuf[out++] = (cp < 128) ? static_cast<char>(cp) : '?';
	}
	while (out > 0 && m_rowBuf[out - 1] == ' ') --out;
	m_rowBuf[out] = '\0';
	
	std::fflush(stdout);
	App::time_till_regular = std::max(App::time_till_regular, 2);
}

// ============================================================================
// Cell Access
// ============================================================================

OurCell Terminal::getCell(int row, int col) {
	std::lock_guard<std::mutex> lock(m_vtermMutex);

	if (row < 0 || col < 0 || row >= m_rows || col >= m_cols) {
		return { (UChar32)U'?' };
	}

	// If scrolled back, draw from scrollback for the top part
	int from_sb = std::min(m_view_off, static_cast<int>(m_scrollback.size()));
	if (from_sb > 0 && row < from_sb) {
		const auto& line = m_scrollback[m_scrollback.size() - from_sb + row];
		if (col < static_cast<int>(line.cells.size())) {
			return line.cells[col];
		} else {
			return { (UChar32)U' ', 0, 0, 0, 255, 255, 255 };
		}
	}

	// Otherwise, draw from the live screen
	int live_row = row - from_sb;
	VTermScreenCell cell{};
	vterm_screen_get_cell(m_screen, VTermPos{ live_row, col }, &cell);
	char32_t cp = cell.chars[0] ? static_cast<char32_t>(cell.chars[0]) : U' ';

	VTermColor fg = cell.fg;
	vterm_screen_convert_color_to_rgb(m_screen, &fg);
	VTermColor bg = cell.bg;
	vterm_screen_convert_color_to_rgb(m_screen, &bg);

	return { (UChar32)cp, bg.rgb.red, bg.rgb.green, bg.rgb.blue,
	         fg.rgb.red, fg.rgb.green, fg.rgb.blue };
}

CursorInfo Terminal::getCursorInfo() {
	CursorInfo ci = m_cursorInfo;

	int from_sb = std::min(m_view_off, static_cast<int>(m_scrollback.size()));
	if (from_sb > 0) {
		int disp_row = ci.row + from_sb;
		if (disp_row < 0 || disp_row >= m_rows) {
			ci.visible = false;
		} else {
			ci.row = disp_row;
		}
	}

	if (!m_screenReady.load(std::memory_order_acquire)) {
		ci.visible = false;
	}

	return ci;
}

// ============================================================================
// Input
// ============================================================================

bool Terminal::writeInput(const void* data, size_t bytes) {
	if (!m_running.load()) return false;
	HANDLE h = m_hToPty;
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	BOOL ok = ::WriteFile(h, data, static_cast<DWORD>(bytes), &written, nullptr);
	return ok && written == bytes;
}

// ============================================================================
// Keyboard
// ============================================================================

bool Terminal::sendText(const std::string& utf8) {
	if (utf8.empty()) return true;
	return writeInput(utf8.data(), utf8.size());
}

bool Terminal::sendEnter() {
	const char cr = '\r';
	return writeInput(&cr, 1);
}

bool Terminal::sendBackspace() {
	const char del = 0x7F;
	return writeInput(&del, 1);
}

bool Terminal::sendCtrl(char letter) {
	unsigned char c = static_cast<unsigned char>(letter);
	if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 1);
	else if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 1);
	else return false;
	char b = static_cast<char>(c);
	return writeInput(&b, 1);
}

static inline std::string CSI(const std::string& tail) {
	return "\x1b[" + tail;
}

static inline int xtermMod(bool shift, bool alt, bool ctrl) {
	return 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
}

bool Terminal::sendSpecialKey(SpecialKey key, bool shift, bool alt, bool ctrl) {
	// Handle local scrollback for PgUp/PgDn when app doesn't want mouse
	if (!appWantsMouse()) {
		if (key == SpecialKey::PageUp) {
			scrollbackPageUp();
			VTermRect all{ 0, m_rows, 0, m_cols };
			onDamage(all);
			return true;
		}
		if (key == SpecialKey::PageDown) {
			scrollbackPageDown();
			VTermRect all{ 0, m_rows, 0, m_cols };
			onDamage(all);
			return true;
		}
	}

	std::string seq;
	int mod = xtermMod(shift, alt, ctrl);

	auto withMod = [&](const char* /*base*/, char code) {
		seq = CSI("1;" + std::to_string(mod) + std::string(1, code));
	};

	switch (key) {
		case SpecialKey::Up:        withMod("\x1b[", 'A'); break;
		case SpecialKey::Down:      withMod("\x1b[", 'B'); break;
		case SpecialKey::Right:     withMod("\x1b[", 'C'); break;
		case SpecialKey::Left:      withMod("\x1b[", 'D'); break;
		case SpecialKey::Home:      withMod("\x1b[", 'H'); break;
		case SpecialKey::End:       withMod("\x1b[", 'F'); break;
		case SpecialKey::InsertKey: seq = CSI("2~"); break;
		case SpecialKey::DeleteKey: seq = CSI("3~"); break;
		case SpecialKey::PageUp:    seq = CSI("5~"); break;
		case SpecialKey::PageDown:  seq = CSI("6~"); break;
		case SpecialKey::F1:        seq = "\x1bOP"; break;
		case SpecialKey::F2:        seq = "\x1bOQ"; break;
		case SpecialKey::F3:        seq = "\x1bOR"; break;
		case SpecialKey::F4:        seq = "\x1bOS"; break;
		case SpecialKey::F5:        seq = CSI("15~"); break;
		case SpecialKey::F6:        seq = CSI("17~"); break;
		case SpecialKey::F7:        seq = CSI("18~"); break;
		case SpecialKey::F8:        seq = CSI("19~"); break;
		case SpecialKey::F9:        seq = CSI("20~"); break;
		case SpecialKey::F10:       seq = CSI("21~"); break;
		case SpecialKey::F11:       seq = CSI("23~"); break;
		case SpecialKey::F12:       seq = CSI("24~"); break;
	}

	return writeInput(seq.data(), seq.size());
}

// ============================================================================
// Mouse
// ============================================================================

bool Terminal::enableMouseTracking(bool enable) {
	std::string seq;
	if (enable) {
		seq = CSI("?1002h") + CSI("?1006h");
	} else {
		seq = CSI("?1002l") + CSI("?1006l");
	}
	return writeInput(seq.data(), seq.size());
}

static inline int sgrMods(bool shift, bool alt, bool ctrl) {
	int m = 0;
	if (shift) m |= 4;
	if (alt) m |= 8;
	if (ctrl) m |= 16;
	return m;
}

static inline bool sgrSend(Terminal* t, int b, int row, int col, bool press) {
	int x = col + 1;
	int y = row + 1;
	char final = press ? 'M' : 'm';
	std::string seq = CSI("<" + std::to_string(b) + ";" + std::to_string(x) + ";" + std::to_string(y) + final);
	return t->writeInput(seq.data(), seq.size());
}

bool Terminal::mousePress(int row, int col, int button, bool pressed,
                          bool shift, bool alt, bool ctrl) {
	int base = (button == 0 ? 0 : button == 1 ? 1 : 2);
	int b = (pressed ? base : 3) + sgrMods(shift, alt, ctrl);
	return sgrSend(this, b, row, col, pressed);
}

bool Terminal::mouseMove(int row, int col, bool buttonHeld,
                         bool shift, bool alt, bool ctrl) {
	int b = 32 + sgrMods(shift, alt, ctrl);
	if (!buttonHeld) {
		b = 35 + sgrMods(shift, alt, ctrl);
	}
	return sgrSend(this, b, row, col, true);
}

bool Terminal::mouseDrag(int startRow, int startCol, int endRow, int endCol, int button,
                         bool shift, bool alt, bool ctrl) {
	if (!mousePress(startRow, startCol, button, true, shift, alt, ctrl)) return false;

	int steps = std::max(std::abs(endRow - startRow), std::abs(endCol - startCol));
	steps = std::max(steps, 1);
	for (int i = 1; i <= steps; ++i) {
		int r = startRow + (endRow - startRow) * i / steps;
		int c = startCol + (endCol - startCol) * i / steps;
		if (!mouseMove(r, c, true, shift, alt, ctrl)) return false;
	}

	return mousePress(endRow, endCol, button, false, shift, alt, ctrl);
}

bool Terminal::mouseScroll(int row, int col, int lines,
                           bool shift, bool alt, bool ctrl) {
	if (lines == 0) return true;

	const bool altScreen = m_altScreen.load(std::memory_order_acquire);
	const bool mouseOn = m_mouseReporting.load(std::memory_order_acquire);

	if (altScreen || mouseOn) {
		int n = std::abs(lines);
		bool up = (lines > 0);
		int wheelCode = up ? 64 : 65;
		for (int i = 0; i < n; ++i) {
			if (!sgrSend(this, wheelCode, row, col, true)) return false;
		}
		return true;
	}

	// Terminal scrollback
	scrollbackLines(lines);
	VTermRect all{ 0, m_rows, 0, m_cols };
	onDamage(all);
	return true;
}

// ============================================================================
// Scrollback
// ============================================================================

void Terminal::clampView() {
	if (m_view_off < 0) m_view_off = 0;
	int maxOff = static_cast<int>(m_scrollback.size());
	if (m_view_off > maxOff) m_view_off = maxOff;
}

bool Terminal::appWantsMouse() const {
	return m_mouseReporting.load(std::memory_order_acquire) ||
	       m_altScreen.load(std::memory_order_acquire);
}

void Terminal::savePushLine(int cols, const VTermScreenCell* cells) {
	std::vector<OurCell> line(cols);
	for (int i = 0; i < cols; i++) {
		const auto& c = cells[i];
		char32_t cp = c.chars[0] ? static_cast<char32_t>(c.chars[0]) : U' ';

		VTermColor fg = c.fg;
		VTermColor bg = c.bg;
		vterm_screen_convert_color_to_rgb(m_screen, &fg);
		vterm_screen_convert_color_to_rgb(m_screen, &bg);

		line[i] = {
			static_cast<UChar32>(cp),
			bg.rgb.red, bg.rgb.green, bg.rgb.blue,
			fg.rgb.red, fg.rgb.green, fg.rgb.blue
		};
	}
	
	int wrapped = vterm_state_get_lineinfo(vterm_obtain_state(m_vt), 0)->continuation;
	OurLine l = {std::move(line), static_cast<bool>(wrapped)};
	m_scrollback.push_back(l);
	if (m_scrollback.size() > m_sb_max) {
		m_scrollback.pop_front();
		scrollDown(1);
	}
	if (m_view_off > 0) {
		m_view_off++;
		clampView();
	}
}

bool Terminal::savePopLine(int cols, VTermScreenCell* cells) {
	if (m_scrollback.empty()) return false;

	const auto line = m_scrollback.back();
	m_scrollback.pop_back();

	const int n = std::min(cols, static_cast<int>(line.cells.size()));
	for (int i = 0; i < n; ++i) {
		const auto s = line.cells[i];
		VTermScreenCell& cell = cells[i];
		cell.chars[0] = static_cast<uint32_t>(s.c);
		cell.chars[1] = 0;
		cell.width = 1;
		cell.fg.type = VTERM_COLOR_RGB;
		cell.fg.rgb.red = s.fg_red;
		cell.fg.rgb.green = s.fg_green;
		cell.fg.rgb.blue = s.fg_blue;
		cell.bg.type = VTERM_COLOR_RGB;
		cell.bg.rgb.red = s.bg_red;
		cell.bg.rgb.green = s.bg_green;
		cell.bg.rgb.blue = s.bg_blue;
	}

	if (m_view_off > 0) {
		m_view_off--;
	}
	return true;
}

void Terminal::scrollbackLines(int delta) {
	if (delta == 0) return;
	m_view_off += delta;
	clampView();
}

void Terminal::scrollbackPageUp() {
	scrollbackLines(m_rows - 1);
}

void Terminal::scrollbackPageDown() {
	scrollbackLines(-(m_rows - 1));
}

// ============================================================================
// Document Access
// ============================================================================

int Terminal::docLineIdForScreenRow(int screenRow) const {
	int from_sb = std::min(m_view_off, static_cast<int>(m_scrollback.size()));
	if (screenRow < 0 || screenRow >= m_rows) return -1;
	if (screenRow < from_sb) {
		return static_cast<int>(m_scrollback.size()) - from_sb + screenRow;
	}
	int live_row = screenRow - from_sb;
	return static_cast<int>(m_scrollback.size()) + live_row;
}

int Terminal::screenRowForDocLineId(int docId) const {
	int from_sb = std::min(m_view_off, static_cast<int>(m_scrollback.size()));
	int sb_size = static_cast<int>(m_scrollback.size());
	
	if (docId >= sb_size - from_sb && docId < sb_size) {
		return docId - (sb_size - from_sb);
	}
	
	int live_start_doc = sb_size;
	int live_end_doc = sb_size + (m_rows - from_sb);
	if (docId >= live_start_doc && docId < live_end_doc) {
		return (docId - live_start_doc) + from_sb;
	}
	return -1;
}

bool Terminal::getDocCell(int docId, int col, OurCell& out) {
	std::lock_guard<std::mutex> lock(m_vtermMutex);
	
	if (col < 0 || col >= m_cols || docId < 0) return false;

	const int sb_size = static_cast<int>(m_scrollback.size());
	if (docId < sb_size) {
		// From scrollback
		const auto& line = m_scrollback[docId];
		if (col < static_cast<int>(line.cells.size())) {
			out = line.cells[col];
		} else {
			out = OurCell{};
			out.c = 0;
		}
		return true;
	}

	// From live screen
	int live_row = docId - sb_size;
	if (live_row < 0 || live_row >= m_rows) return false;
	
	if (!m_screen) return false;
	VTermScreenCell cell{};
	vterm_screen_get_cell(m_screen, VTermPos{ live_row, col }, &cell);
	char32_t cp = cell.chars[0] ? static_cast<char32_t>(cell.chars[0]) : U' ';

	VTermColor fg = cell.fg;
	vterm_screen_convert_color_to_rgb(m_screen, &fg);
	VTermColor bg = cell.bg;
	vterm_screen_convert_color_to_rgb(m_screen, &bg);

	out = { (UChar32)cp, bg.rgb.red, bg.rgb.green, bg.rgb.blue,
	                     fg.rgb.red, fg.rgb.green, fg.rgb.blue };
	return true;
}

bool Terminal::getDocWraps(int docId) {
	std::lock_guard<std::mutex> lock(m_vtermMutex);
	
	if (docId < 0) return false;

	const int sb_size = static_cast<int>(m_scrollback.size());
	if (docId < sb_size) {
		// From scrollback
		const auto& line = m_scrollback[docId];
		return line.continuation;
	}
	
	// From live screen
	int live_row = docId - sb_size;
	if (live_row < 0 || live_row >= m_rows) return false;
	
	
	int wrapped = vterm_state_get_lineinfo(vterm_obtain_state(m_vt), live_row)->continuation;
	return wrapped;
}

// ============================================================================
// vterm callbacks
// ============================================================================

int Terminal::s_screen_damage(VTermRect rect, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (self) self->onDamage(rect);
	return 1;
}

int Terminal::s_screen_moverect(VTermRect /*dest*/, VTermRect /*src*/, void* /*user*/) {
	return 1;
}

int Terminal::s_screen_movecursor(VTermPos pos, VTermPos /*oldpos*/, int visible, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (self) {
		self->m_cursorInfo.row = pos.row;
		self->m_cursorInfo.col = pos.col;
		self->m_cursorInfo.visible = (visible != 0);
	}
	return 1;
}

int Terminal::s_screen_settermprop(VTermProp prop, VTermValue* val, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (!self || !val) return 0;

	switch (prop) {
		case VTERM_PROP_CURSORVISIBLE:
			self->m_cursorInfo.visible = val->boolean;
			break;
		case VTERM_PROP_CURSORBLINK:
			self->m_cursorInfo.blink = val->boolean;
			break;
		case VTERM_PROP_CURSORSHAPE: {
			int v = val->number;
			if (v == 0) v = 1;
			if (v < 1 || v > 3) v = 1;
			self->m_cursorInfo.shape = v;
			break;
		}
		case VTERM_PROP_ALTSCREEN: {
			bool on = !!val->boolean;
			self->m_altScreen.store(on, std::memory_order_release);
			if (on) {
				self->m_view_off = 0;
			}
			break;
		}
		case VTERM_PROP_MOUSE:
			self->m_mouseReporting.store(val->number != 0, std::memory_order_release);
			break;
		default:
			break;
	}
	return 1;
}

int Terminal::s_screen_bell(void* /*user*/) {
	return 1;
}

int Terminal::s_screen_resize(int rows, int cols, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (!self) return 1;
	self->m_rows = rows;
	self->m_cols = cols;
	self->m_rowBuf.resize(static_cast<size_t>(cols) + 1, 0);
	return 1;
}

int Terminal::s_screen_sb_pushline(int cols, const VTermScreenCell* cells, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (!self) return 1;
	if (self->m_altScreen.load(std::memory_order_acquire)) {
		return 1;
	}
	self->savePushLine(cols, cells);
	return 1;
}

int Terminal::s_screen_sb_popline(int cols, VTermScreenCell* cells, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (!self) return 1;
	if (self->m_altScreen.load(std::memory_order_acquire)) {
		return 0;
	}
	
	for (int i = 0; i < cols; ++i) {
		VTermScreenCell& cell = cells[i];
		std::memset(&cell, 0, sizeof(cell));
		cell.width = 1;
		cell.chars[0] = U' ';
		cell.fg.type = VTERM_COLOR_DEFAULT_FG;
		cell.bg.type = VTERM_COLOR_DEFAULT_BG;
	}

	bool ok = self->savePopLine(cols, cells);
	return ok ? 1 : 0;
}
#include "terminal.h"

#include <iostream>
#include <cassert>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define NOMINMAX
#include <windows.h>
#include <processthreadsapi.h>
#include <consoleapi2.h> // ResizePseudoConsole
#include <consoleapi3.h> // CreatePseudoConsole, ClosePseudoConsole
#include "application.h"

namespace {

inline void closeHandleIfValid(HANDLE& h) {
	if (h && h != INVALID_HANDLE_VALUE) {
		::CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
	}
}

inline std::wstring defaultShell() {
	return L"cmd.exe";
}

} // namespace

Terminal::Terminal(int cols, int rows) : m_cols(cols), m_rows(rows) {
	ZeroMemory(&m_pi, sizeof(m_pi));
}

Terminal::~Terminal() {
	stop();
}

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
	// Flip the flag (cleanup still proceeds if it was already false)
	m_running.exchange(false);

	// Unblock any pending I/O *first* to let readerLoop exit quickly.
	if (m_hFromPty != INVALID_HANDLE_VALUE) {
		::CancelIoEx(m_hFromPty, nullptr);
	}
	if (m_hToPty != INVALID_HANDLE_VALUE) {
		::CancelIoEx(m_hToPty, nullptr);
	}

	// Now join the reader thread; at this point ReadFile should have unwound.
	if (m_reader.joinable()) {
		m_reader.join();
	}

	// Close our pipe handles after the thread is gone.
	closeHandleIfValid(m_hToPty);
	closeHandleIfValid(m_hFromPty);

	// Ask the child to die; if it’s already exited, these are no-ops.
	if (m_pi.hProcess) {
		::WaitForSingleObject(m_pi.hProcess, 200);
		::TerminateProcess(m_pi.hProcess, 0);
		closeHandleIfValid(m_pi.hProcess);
	}

	// Tear down the ConPTY after the reader is joined and pipes are closed.
	if (m_hPC) {
		::ClosePseudoConsole(m_hPC);
		m_hPC = nullptr;
	}

	// Finally, free libvterm under the mutex.
	teardownVTerm();
}

bool Terminal::writeInput(const void* data, size_t bytes) {
	if (!m_running.load()) return false;
	HANDLE h = m_hToPty;
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	BOOL ok = ::WriteFile(h, data, static_cast<DWORD>(bytes), &written, nullptr);
	return ok && written == bytes;
}


bool Terminal::resize(int cols, int rows) {
	{
		std::lock_guard<std::mutex> lock(m_vtermMutex);
	
		if (m_vt) {
			vterm_set_size(m_vt, rows, cols);
		} else {
			// vterm not up yet — keep our members coherent anyway
			m_rows = rows;
			m_cols = cols;
			m_rowBuf.resize(static_cast<size_t>(m_cols) + 1, 0);
		}
	}
	
	// 2) Now resize the ConPTY
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

// ----------------- ConPTY helpers -----------------

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

// ----------------- vterm helpers -----------------

bool Terminal::initVTerm() {
	m_vt = vterm_new(m_rows, m_cols);
	if (!m_vt) {
		return false;
	}
	vterm_set_utf8(m_vt, 1);

	m_screen = vterm_obtain_screen(m_vt);
	if (!m_screen) {
		vterm_free(m_vt); m_vt = nullptr;
		return false;
	}

	// IMPORTANT: allocate row buffer BEFORE callbacks/reset,
	// because reset will emit damage immediately.
	m_rowBuf.resize(static_cast<size_t>(m_cols) + 1, 0);

	static const VTermScreenCallbacks Cbs = {
		/* damage      */ &Terminal::s_screen_damage,
		/* moverect    */ &Terminal::s_screen_moverect,
		/* movecursor  */ &Terminal::s_screen_movecursor,
		/* settermprop */ &Terminal::s_screen_settermprop,
		/* bell        */ &Terminal::s_screen_bell,
		/* resize      */ &Terminal::s_screen_resize,
		/* sb_pushline */ &Terminal::s_screen_sb_pushline,
		/* sb_popline  */ &Terminal::s_screen_sb_popline
	};
	vterm_screen_set_callbacks(m_screen, &Cbs, this);
	
	// Optional but recommended: select SGR mouse protocol so TUIs see mouse events.
	#ifdef VTERM_MOUSE_PROTO_SGR
		vterm_mouse_set_protocol(m_vt, VTERM_MOUSE_PROTO_SGR);
	#elif defined(VTERM_MOUSE_PROTOCOL_SGR)
		vterm_mouse_set_protocol(m_vt, VTERM_MOUSE_PROTOCOL_SGR);
	#endif
	
	// Some apps want focus / extended modes; harmless if unsupported.
	#ifdef VTERM_PROP_MOUSE
		// nothing needed; just documenting that mouse goes via keyboard output
	#endif
	

	// Now safe to reset (may trigger damage callbacks)
	m_screenReady.store(true, std::memory_order_release);
	vterm_screen_reset(m_screen, 1 /* hard */);

	return true;
}


void Terminal::teardownVTerm() {
	std::lock_guard<std::mutex> lock(m_vtermMutex); // <-- ADD THIS LOCK
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
	// Avoid printing if someone calls before we're ready (belt & suspenders)
	if (!m_screenReady.load(std::memory_order_acquire)) return;
	
	for (int r = rect.start_row; r < rect.end_row; ++r) {
		dumpRow(r);
	}
}

OurCell Terminal::getCell(int row, int col) {
//	std::cout << "GetCell called: " << row << ", " << col << std::endl;
	std::lock_guard<std::mutex> lock(m_vtermMutex);
	
	if (row < 0 || col < 0 || row >= m_rows || col >= m_cols) {
		return {(UChar32)U'?'};
	}
	
	VTermScreenCell cell{};
	vterm_screen_get_cell(m_screen, VTermPos{ row, col }, &cell);
	char32_t cp = cell.chars[0] ? static_cast<char32_t>(cell.chars[0]) : U' ';
	
	VTermColor fg = cell.fg;
	vterm_screen_convert_color_to_rgb(m_screen, &fg);
	VTermColor bg = cell.bg;
	vterm_screen_convert_color_to_rgb(m_screen, &bg);
	
	return { (UChar32)cp, bg.rgb.red, bg.rgb.green, bg.rgb.blue, fg.rgb.red, fg.rgb.green, fg.rgb.blue };
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

// ----------------- Reader loop -----------------

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
			// If we cancelled I/O during shutdown, bail out quietly.
			DWORD err = ::GetLastError();
			if (err == ERROR_OPERATION_ABORTED || err == ERROR_BROKEN_PIPE) break;
			// Other errors: also exit; nothing sane to do on teardown.
			break;
		}
		if (got == 0) break;

		std::lock_guard<std::mutex> lock(m_vtermMutex);
		if (!m_vt) continue;
		vterm_input_write(m_vt, buffer.data(), static_cast<size_t>(got));
		flushVTermDamage();
	}
}

// ----------------- vterm callback trampolines (definitions) -----------------

int Terminal::s_screen_damage(VTermRect rect, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (self) self->onDamage(rect);
	return 1;
}

int Terminal::s_screen_moverect(VTermRect /*dest*/, VTermRect /*src*/, void* /*user*/) {
	// You could choose to redraw dest rows; returning 1 = handled
	return 1;
}

int Terminal::s_screen_movecursor(VTermPos pos, VTermPos /*oldpos*/, int visible, void* user) {
	auto* self = static_cast<Terminal*>(user);
	if (self) {
		self->m_cursorInfo.row = pos.row;
		self->m_cursorInfo.col = pos.col;
		self->m_cursorInfo.visible = (visible != 0); // <-- use libvterm's visibility bit
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
			if (v == 0) v = 1;               // normalize odd 0-based enums
			if (v < 1 || v > 3) v = 1;
			self->m_cursorInfo.shape = v;
			break;
		}
		default: break;
	}
	return 1;
}

int Terminal::s_screen_bell(void* /*user*/) {
	// Beep or flash if you want
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

int Terminal::s_screen_sb_pushline(int /*cols*/, const VTermScreenCell* /*cells*/, void* /*user*/) {
	// Scrollback received (line left the top); ignore or capture if you keep your own scrollback
	return 0;
}

int Terminal::s_screen_sb_popline(int /*cols*/, VTermScreenCell* /*cells*/, void* /*user*/) {
	// Provide a line to pop back from scrollback; we don't maintain one, so just say handled
	return 0;
}

// Pulls everything libvterm wants to send *to the child PTY* and writes it.
bool Terminal::drainVTermOutputToPty_() {
	if (!m_vt || m_hToPty == INVALID_HANDLE_VALUE) return false;

	// libvterm exposes an output buffer you can read from in chunks.
	// Typical signatures:
	//   size_t vterm_output_read(VTerm*, char* buffer, size_t len);
	//   size_t vterm_output_get_buffer_size(VTerm*);
	// We’ll use a loop until it returns 0.
	char outbuf[4096];
	bool any = false;
	for (;;) {
		size_t n = vterm_output_read(m_vt, outbuf, sizeof(outbuf));
		if (n == 0) break;
		any = true;
		if (!writeInput(outbuf, n)) return false;
	}
	return any || true;
}

// --- small helper: write an ESC sequence ---
static inline bool writeEscSeq(Terminal* t, const std::string& s) {
	return t->writeInput(s.data(), s.size());
}

// --- Keyboard ---

bool Terminal::sendText(const std::string& utf8) {
	if (utf8.empty()) return true;
	return writeInput(utf8.data(), utf8.size());
}
bool Terminal::sendEnter()     { const char cr = '\r'; return writeInput(&cr, 1); }
bool Terminal::sendBackspace() { const char del = 0x7F; return writeInput(&del, 1); }

bool Terminal::sendCtrl(char letter) {
	// Map A-Z or a-z to control (Ctrl+A = 0x01 ... Ctrl+Z = 0x1A)
	unsigned char c = static_cast<unsigned char>(letter);
	if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 1);
	else if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 1);
	else return false;
	char b = static_cast<char>(c);
	return writeInput(&b, 1);
}

// xterm CSI helper
static inline std::string CSI(const std::string& tail) { return "\x1b[" + tail; }

// xterm “modifyOtherKeys” style for arrows/Home/End etc: ESC [ 1 ; <mod> <code>
// mod = 1 + (shift?1:0) + (alt?2:0) + (ctrl?4:0)
static inline int xtermMod(bool shift, bool alt, bool ctrl) {
	return 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
}

bool Terminal::sendSpecialKey(SpecialKey key, bool shift, bool alt, bool ctrl) {
	std::string seq;
	int mod = xtermMod(shift, alt, ctrl);

	auto withMod = [&](const char* base, char code) {
		// ESC [ 1 ; <mod> <code>
		seq = CSI("1;" + std::to_string(mod) + std::string(1, code));
	};

	switch (key) {
		case SpecialKey::Up:       withMod("\x1b[", 'A'); break;
		case SpecialKey::Down:     withMod("\x1b[", 'B'); break;
		case SpecialKey::Right:    withMod("\x1b[", 'C'); break;
		case SpecialKey::Left:     withMod("\x1b[", 'D'); break;
		case SpecialKey::Home:     withMod("\x1b[", 'H'); break;
		case SpecialKey::End:      withMod("\x1b[", 'F'); break;
		case SpecialKey::InsertKey: seq = CSI("2~"); break;
		case SpecialKey::DeleteKey: seq = CSI("3~"); break;
		case SpecialKey::PageUp:    seq = CSI("5~"); break;
		case SpecialKey::PageDown:  seq = CSI("6~"); break;
		case SpecialKey::F1:        seq = "\x1bOP"; break; // F1..F4 old style
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

	// If modifiers requested for keys that don't include them by default,
	// many terminals use “;mod” forms; basic mapping above covers arrows/home/end.
	return writeEscSeq(this, seq);
}

// --- Mouse via SGR (1006) ---

// Enable (?1002h + ?1006h) or disable (?1002l + ?1006l) mouse reporting
bool Terminal::enableMouseTracking(bool enable) {
	std::string seq;
	if (enable) {
		seq = CSI("?1002h") + CSI("?1006h");  // button-event + SGR
	} else {
		seq = CSI("?1002l") + CSI("?1006l");
	}
	return writeEscSeq(this, seq);
}

// Build SGR report: ESC [ < b ; x ; y (M|m)
// b: base button code + modifiers (+32 if motion while button held)
// coords are 1-based (x=col+1, y=row+1)
static inline int sgrMods(bool shift, bool alt, bool ctrl) {
	int m = 0;
	if (shift) m |= 4;
	if (alt)   m |= 8;
	if (ctrl)  m |= 16;
	return m;
}

static inline bool sgrSend(Terminal* t, int b, int row, int col, bool press) {
	int x = col + 1;
	int y = row + 1;
	char final = press ? 'M' : 'm';
	std::string seq = CSI("<" + std::to_string(b) + ";" + std::to_string(x) + ";" + std::to_string(y) + final);
	return writeEscSeq(t, seq);
}

bool Terminal::mousePress(int row, int col, int button, bool pressed,
						  bool shift, bool alt, bool ctrl) {
	// base button: 0=L,1=M,2=R; release uses b=3 but SGR prefers final='m'
	int base = (button == 0 ? 0 : button == 1 ? 1 : 2);
	int b = (pressed ? base : 3) + sgrMods(shift, alt, ctrl);
	// In SGR, releases should use 'm' and b=3+mods (xterm-compatible).
	return sgrSend(this, b, row, col, /*press*/pressed);
}

bool Terminal::mouseMove(int row, int col, bool buttonHeld,
						 bool shift, bool alt, bool ctrl) {
	// Motion with any button held: base=0 + 32; without button held, many apps ignore.
	int b = 32 + sgrMods(shift, alt, ctrl);
	// Use 'M' (press form) for motion events.
	if (!buttonHeld) {
		// Some apps expect 35 for plain motion; but usually they only listen when held.
		b = 35 + sgrMods(shift, alt, ctrl);
	}
	return sgrSend(this, b, row, col, /*press*/true);
}

bool Terminal::mouseDrag(int startRow, int startCol, int endRow, int endCol, int button,
						 bool shift, bool alt, bool ctrl) {
	// Press
	if (!mousePress(startRow, startCol, button, true, shift, alt, ctrl)) return false;

	int steps = std::max(std::abs(endRow - startRow), std::abs(endCol - startCol));
	steps = std::max(steps, 1);
	for (int i = 1; i <= steps; ++i) {
		int r = startRow + (endRow - startRow) * i / steps;
		int c = startCol + (endCol - startCol) * i / steps;
		if (!mouseMove(r, c, /*buttonHeld*/true, shift, alt, ctrl)) return false;
	}

	// Release
	return mousePress(endRow, endCol, button, false, shift, alt, ctrl);
}

bool Terminal::mouseScroll(int row, int col, int lines,
						   bool shift, bool alt, bool ctrl) {
	if (lines == 0) return true;
	int n = std::abs(lines);
	bool up = (lines > 0);
	// SGR wheel: up=64, down=65 (no release event; always 'M')
	int wheelCode = (up ? 64 : 65) + sgrMods(shift, alt, ctrl);
	for (int i = 0; i < n; ++i) {
		if (!sgrSend(this, wheelCode, row, col, /*press*/true)) return false;
	}
	return true;
}

CursorInfo Terminal::getCursorInfo() {
	return m_cursorInfo;
}
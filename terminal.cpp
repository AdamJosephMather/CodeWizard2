#include "terminal.h"
#include "filebackend.h"
#include "sshfilebackend.h"
#include "application.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

namespace {
std::string quotePosixTerminalArg(const std::string& value) {
	std::string result = "'";
	for (const char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
	return result + "'";
}

std::vector<std::string> remoteTerminalArguments(const SSHFileBackend& backend) {
	const auto& options = backend.connectionOptions();
	const auto& info = backend.remoteInfo();
	const std::string cwd = App::settings
		? App::settings->getValue("current_folder", info.cwd)
		: info.cwd;
	std::vector<std::string> arguments{
		"ssh", "-tt", "-p", std::to_string(options.port)
	};
	if (!options.key_path.empty()) {
		arguments.push_back("-i");
		arguments.push_back(options.key_path);
	}
	arguments.push_back("--");
	arguments.push_back(options.username.empty() ? options.hostname : options.username + "@" + options.hostname);
	if (info.os != "windows" && !cwd.empty()) {
		const std::string shell = info.shell.empty() ? "/bin/sh" : info.shell;
		arguments.push_back("cd -- " + quotePosixTerminalArg(cwd) + " && exec " + quotePosixTerminalArg(shell) + " -l");
	}
	return arguments;
}

#ifdef _WIN32
std::wstring widenTerminalArg(const std::string& value) {
	if (value.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
	return result;
}

std::wstring quoteWindowsTerminalArg(const std::string& value) {
	const std::wstring source = widenTerminalArg(value);
	if (source.find_first_of(L" \t\n\v\"") == std::wstring::npos) return source;
	std::wstring result = L"\"";
	std::size_t slashes = 0;
	for (const wchar_t c : source) {
		if (c == L'\\') {
			++slashes;
		} else if (c == L'"') {
			result.append(slashes * 2 + 1, L'\\');
			result.push_back(c);
			slashes = 0;
		} else {
			result.append(slashes, L'\\');
			slashes = 0;
			result.push_back(c);
		}
	}
	result.append(slashes * 2, L'\\');
	result.push_back(L'"');
	return result;
}

std::wstring windowsTerminalCommand(const std::vector<std::string>& arguments) {
	std::wstring result;
	for (const auto& argument : arguments) {
		if (!result.empty()) result.push_back(L' ');
		result += quoteWindowsTerminalArg(argument);
	}
	return result;
}
#else
std::string posixTerminalCommand(const std::vector<std::string>& arguments) {
	std::string result;
	for (const auto& argument : arguments) {
		if (!result.empty()) result.push_back(' ');
		result += quotePosixTerminalArg(argument);
	}
	return result;
}
#endif
} // namespace
#include <consoleapi2.h>
#include <consoleapi3.h>
#include <processthreadsapi.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <stdio.h>
#endif

namespace {

constexpr OurCell kDefaultCell{U' ', 0, 0, 0, 255, 255, 255};

#ifdef _WIN32
inline void closeHandleIfValid(HANDLE& h) {
	if (h && h != INVALID_HANDLE_VALUE) {
		::CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
	}
}

inline std::wstring defaultShell() {
	return widen(App::settings->getValue("terminal_cmd", std::string("cmd.exe")));
}
#else
inline int fdCloseIfValid(int& fd) {
	if (fd >= 0) {
		const int ret = ::close(fd);
		fd = -1;
		return ret;
	}
	return 0;
}

inline std::string defaultShell() {
	std::string sh = App::settings->getValue("terminal_cmd", std::string("/bin/bash"));
	if (sh.empty()) {
		const char* env = std::getenv("SHELL");
		sh = env ? env : "/bin/bash";
	}
	return sh;
}

static std::string wstringToUtf8(const std::wstring& wstr) {
	MST::MonoString u = MST::toMonoString(
		reinterpret_cast<const UChar32*>(wstr.data()),
		static_cast<int32_t>(wstr.size()));
	std::string out;
	u.toUTF8String(out);
	return out;
}
#endif

static inline int xtermMod(bool shift, bool alt, bool ctrl) {
	return 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
}

static inline int clampDimension(int value) {
#ifdef _WIN32
	// COORD uses signed 16-bit SHORT values.
	constexpr int maxDimension = static_cast<int>(std::numeric_limits<SHORT>::max());
#else
	constexpr int maxDimension = static_cast<int>(std::numeric_limits<uint16_t>::max());
#endif
	return std::max(1, std::min(value, maxDimension));
}

} // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

Terminal::Terminal(int cols, int rows, SCROLLDOWN sd)
	: m_cols(clampDimension(cols)),
	  m_rows(clampDimension(rows)),
	  scrollDown(std::move(sd)) {
#ifdef _WIN32
	ZeroMemory(&m_pi, sizeof(m_pi));
#endif
}

Terminal::~Terminal() {
	stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

#ifdef WIN32
std::string wstring_to_utf8_windows(const std::wstring& wstr) {
	if (wstr.empty()) return std::string();

	// Calculate required buffer size first
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	
	std::string strTo(size_needed, 0);
	// Perform the actual conversion
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	
	return strTo;
}
#endif

bool Terminal::start(const std::wstring& shell) {
	if (m_running.load(std::memory_order_acquire)) return true;

	if (!initConPty()) {
		teardownConPty();
		return false;
	}

#ifdef _WIN32
	std::wstring command = shell.empty() ? defaultShell() : shell;
	if (auto remote = std::dynamic_pointer_cast<SSHFileBackend>(FileBackends::current())) {
		command = windowsTerminalCommand(remoteTerminalArguments(*remote));
		shellStr = "SSH: " + remote->displayName();
	} else {
		shellStr = wstring_to_utf8_windows(command);
	}
	if (!launchShell(command)) {
		teardownConPty();
		return false;
	}
#else
	m_remoteCommand.clear();
	if (auto remote = std::dynamic_pointer_cast<SSHFileBackend>(FileBackends::current())) {
		m_remoteCommand = posixTerminalCommand(remoteTerminalArguments(*remote));
		shellStr = "SSH: " + remote->displayName();
	} else {
		shellStr = shell.empty() ? defaultShell() : wstringToUtf8(shell);
	}
	if (!launchShell(shellStr)) {
		teardownConPty();
		return false;
	}
#endif

	if (!initGhostty()) {
		teardownConPty();
		return false;
	}

	m_running.store(true, std::memory_order_release);
	m_reader = std::thread(&Terminal::readerLoop, this);
	return true;
}

void Terminal::stop() {
	m_running.exchange(false, std::memory_order_acq_rel);

#ifdef _WIN32
	if (m_hFromPty != INVALID_HANDLE_VALUE) ::CancelIoEx(m_hFromPty, nullptr);
	if (m_hToPty != INVALID_HANDLE_VALUE) ::CancelIoEx(m_hToPty, nullptr);

	if (m_reader.joinable()) m_reader.join();

	closeHandleIfValid(m_hToPty);
	closeHandleIfValid(m_hFromPty);

	if (m_pi.hProcess) {
		if (::WaitForSingleObject(m_pi.hProcess, 200) == WAIT_TIMEOUT) {
			::TerminateProcess(m_pi.hProcess, 0);
		}
		closeHandleIfValid(m_pi.hProcess);
	}
	if (m_hPC) {
		m_closePseudoConsole(m_hPC);
		m_hPC = nullptr;
	}
	unloadConPtyApi();
#else
	if (m_childPid > 0) {
		::kill(m_childPid, SIGTERM);
		int status = 0;
		if (::waitpid(m_childPid, &status, WNOHANG) == 0) {
			::kill(m_childPid, SIGKILL);
			::waitpid(m_childPid, nullptr, 0);
		}
		m_childPid = -1;
	}

	fdCloseIfValid(m_masterFd);
	if (m_reader.joinable()) m_reader.join();
#endif

	teardownGhostty();
}

bool Terminal::resize(int cols, int rows) {
	return resize(cols, rows,
	              m_cellWidthPx.load(std::memory_order_acquire),
	              m_cellHeightPx.load(std::memory_order_acquire));
}

bool Terminal::resize(int cols, int rows, int cellWidthPx, int cellHeightPx) {
	cols = clampDimension(cols);
	rows = clampDimension(rows);
	cellWidthPx = std::max(cellWidthPx, 1);
	cellHeightPx = std::max(cellHeightPx, 1);

	RERENDER.store(true, std::memory_order_release);
	std::lock_guard<std::mutex> resizeLock(m_resizeMutex);

	auto resizeModel = [&](int targetCols,
	                       int targetRows,
	                       int targetCellWidth,
	                       int targetCellHeight) -> bool {
		std::lock_guard<std::mutex> stateLock(m_stateMutex);

		if (!m_terminal) {
			m_cols.store(targetCols, std::memory_order_release);
			m_rows.store(targetRows, std::memory_order_release);
			m_cellWidthPx.store(targetCellWidth, std::memory_order_release);
			m_cellHeightPx.store(targetCellHeight, std::memory_order_release);
			m_viewCells.assign(
				static_cast<size_t>(targetCols) * static_cast<size_t>(targetRows),
				kDefaultCell);
			return true;
		}

		refreshTerminalMetadataLocked();

		// If the user is looking at history, preserve the actual top-left
		// cell rather than an absolute row number. Reflow changes row numbers,
		// but a tracked grid ref follows the cell through that reflow.
		const bool wasAtBottom =
			m_scrollbar.offset + m_scrollbar.len >= m_scrollbar.total;
		GhosttyTrackedGridRef viewportAnchor = nullptr;
		if (!wasAtBottom) {
			GhosttyPoint topLeft{};
			topLeft.tag = GHOSTTY_POINT_TAG_VIEWPORT;
			topLeft.value.coordinate.x = 0;
			topLeft.value.coordinate.y = 0;
			if (ghostty_terminal_grid_ref_track(
					m_terminal, topLeft, &viewportAnchor) != GHOSTTY_SUCCESS) {
				viewportAnchor = nullptr;
			}
		}

		const GhosttyResult resizeResult = ghostty_terminal_resize(
			m_terminal,
			static_cast<uint16_t>(targetCols),
			static_cast<uint16_t>(targetRows),
			static_cast<uint32_t>(targetCellWidth),
			static_cast<uint32_t>(targetCellHeight));

		if (resizeResult != GHOSTTY_SUCCESS) {
			if (viewportAnchor) ghostty_tracked_grid_ref_free(viewportAnchor);
			return false;
		}

		m_cols.store(targetCols, std::memory_order_release);
		m_rows.store(targetRows, std::memory_order_release);
		m_cellWidthPx.store(targetCellWidth, std::memory_order_release);
		m_cellHeightPx.store(targetCellHeight, std::memory_order_release);

		if (viewportAnchor) {
			GhosttyPointCoordinate resolved{};
			if (ghostty_tracked_grid_ref_point(
					viewportAnchor,
					GHOSTTY_POINT_TAG_SCREEN,
					&resolved) == GHOSTTY_SUCCESS) {
				GhosttyTerminalScrollViewport scroll{};
				scroll.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
				scroll.value.row = static_cast<size_t>(resolved.y);
				ghostty_terminal_scroll_viewport(m_terminal, scroll);
			}
			ghostty_tracked_grid_ref_free(viewportAnchor);
		}

		updateMouseGeometryLocked();
		refreshTerminalMetadataLocked();
		m_renderSnapshotDirty = true;
		return true;
	};

	const int oldCols = m_cols.load(std::memory_order_acquire);
	const int oldRows = m_rows.load(std::memory_order_acquire);
	const int oldCellWidth = m_cellWidthPx.load(std::memory_order_acquire);
	const int oldCellHeight = m_cellHeightPx.load(std::memory_order_acquire);

#ifdef _WIN32
	// A pixel-size-only update does not require a ConPTY resize. In particular,
	// the post-start resize that changes 1x1 cell metrics must not trigger an
	// unnecessary ConPTY repaint at the same row/column geometry.
	const bool ptyGeometryChanged = oldCols != cols || oldRows != rows;
	if (!m_hPC || !ptyGeometryChanged) {
		return resizeModel(cols, rows, cellWidthPx, cellHeightPx);
	}

	// Keep reading the output pipe during ResizePseudoConsole, but temporarily
	// queue the bytes instead of parsing them. This avoids both interleaving the
	// ConPTY repaint with libghostty's reflow and blocking a large repaint while
	// holding m_stateMutex.
	{
		std::lock_guard<std::mutex> gateLock(m_outputGateMutex);
		m_resizeInProgress = true;
	}

	auto finishPtyResize = [&]() {
		bool parsedPendingOutput = false;
		std::unique_lock<std::mutex> gateLock(m_outputGateMutex);
		std::vector<uint8_t> pending;
		pending.swap(m_pendingPtyOutput);

		if (!pending.empty()) {
			std::lock_guard<std::mutex> stateLock(m_stateMutex);
			if (m_terminal) {
				ghostty_terminal_vt_write(
					m_terminal, pending.data(), pending.size());
				refreshTerminalMetadataLocked();
				m_renderSnapshotDirty = true;
				parsedPendingOutput = true;
			}
		}

		// The gate remains locked until queued bytes have been parsed. A reader
		// that already completed ReadFile cannot overtake them.
		m_resizeInProgress = false;
		gateLock.unlock();

		if (parsedPendingOutput) {
			App::time_till_regular = std::max(App::time_till_regular, 2);
			RERENDER.store(true, std::memory_order_release);
		}
	};

	COORD size{};
	size.X = static_cast<SHORT>(cols);
	size.Y = static_cast<SHORT>(rows);
	const HRESULT ptyResizeResult = m_resizePseudoConsole(m_hPC, size);
	if (FAILED(ptyResizeResult)) {
		std::cerr << "ResizePseudoConsole failed: hr=0x"
		          << std::hex << ptyResizeResult << std::dec << "\n";
		finishPtyResize();
		return false;
	}

	if (!resizeModel(cols, rows, cellWidthPx, cellHeightPx)) {
		// ConPTY accepted the new size but libghostty did not. Restore ConPTY so
		// the child and renderer do not remain permanently out of sync.
		COORD oldSize{};
		oldSize.X = static_cast<SHORT>(oldCols);
		oldSize.Y = static_cast<SHORT>(oldRows);
		const HRESULT rollbackResult = m_resizePseudoConsole(m_hPC, oldSize);
		if (FAILED(rollbackResult)) {
			std::cerr << "ResizePseudoConsole rollback failed: hr=0x"
			          << std::hex << rollbackResult << std::dec << "\n";
		}
		finishPtyResize();
		return false;
	}

	finishPtyResize();
	return true;
#else
	if (!resizeModel(cols, rows, cellWidthPx, cellHeightPx)) return false;

	if (m_masterFd >= 0) {
		struct winsize ws{};
		ws.ws_row = static_cast<unsigned short>(rows);
		ws.ws_col = static_cast<unsigned short>(cols);
		ws.ws_xpixel = static_cast<unsigned short>(std::min(cols * cellWidthPx, 65535));
		ws.ws_ypixel = static_cast<unsigned short>(std::min(rows * cellHeightPx, 65535));
		if (::ioctl(m_masterFd, TIOCSWINSZ, &ws) < 0) {
			resizeModel(oldCols, oldRows, oldCellWidth, oldCellHeight);
			return false;
		}
	}
	return true;
#endif
}

// ============================================================================
// ConPTY / PTY
// ============================================================================

#ifdef _WIN32

void Terminal::loadConPtyApi() {
	// Start with the inbox API. A current side-by-side conpty.dll plus its
	// matching OpenConsole.exe can be placed beside this executable to avoid
	// depending on the older ConPTY implementation bundled with the OS.
	unloadConPtyApi();

	wchar_t executablePath[32768]{};
	const DWORD pathLength = ::GetModuleFileNameW(
		nullptr, executablePath,
		static_cast<DWORD>(sizeof(executablePath) / sizeof(executablePath[0])));
	if (pathLength == 0 || pathLength >= sizeof(executablePath) / sizeof(executablePath[0])) {
		return;
	}

	std::wstring dllPath(executablePath, pathLength);
	const size_t slash = dllPath.find_last_of(L"\\/");
	if (slash == std::wstring::npos) return;
	dllPath.resize(slash + 1);
	dllPath += L"conpty.dll";

	HMODULE module = ::LoadLibraryExW(
		dllPath.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!module) return;

	FARPROC createProc = ::GetProcAddress(module, "CreatePseudoConsole");
	if (!createProc) createProc = ::GetProcAddress(module, "ConptyCreatePseudoConsole");
	FARPROC resizeProc = ::GetProcAddress(module, "ResizePseudoConsole");
	if (!resizeProc) resizeProc = ::GetProcAddress(module, "ConptyResizePseudoConsole");
	FARPROC closeProc = ::GetProcAddress(module, "ClosePseudoConsole");
	if (!closeProc) closeProc = ::GetProcAddress(module, "ConptyClosePseudoConsole");

	auto createFn = reinterpret_cast<CreatePseudoConsoleFn>(createProc);
	auto resizeFn = reinterpret_cast<ResizePseudoConsoleFn>(resizeProc);
	auto closeFn = reinterpret_cast<ClosePseudoConsoleFn>(closeProc);

	if (!createFn || !resizeFn || !closeFn) {
		::FreeLibrary(module);
		return;
	}

	m_conPtyModule = module;
	m_createPseudoConsole = createFn;
	m_resizePseudoConsole = resizeFn;
	m_closePseudoConsole = closeFn;
	m_usingSideBySideConPty = true;
	std::cerr << "Loaded side-by-side conpty.dll; use its matching OpenConsole.exe.\n";
}

void Terminal::unloadConPtyApi() {
	{
		std::lock_guard<std::mutex> gateLock(m_outputGateMutex);
		m_resizeInProgress = false;
		m_pendingPtyOutput.clear();
	}
	if (m_conPtyModule) {
		::FreeLibrary(m_conPtyModule);
		m_conPtyModule = nullptr;
	}
	m_createPseudoConsole = &::CreatePseudoConsole;
	m_resizePseudoConsole = &::ResizePseudoConsole;
	m_closePseudoConsole = &::ClosePseudoConsole;
	m_usingSideBySideConPty = false;
}

bool Terminal::initConPty() {
	loadConPtyApi();
	if (!m_usingSideBySideConPty) {
		static std::once_flag inboxWarningOnce;
		std::call_once(inboxWarningOnce, [] {
			std::cerr
				<< "Using the Windows inbox ConPTY. Older Windows builds can repaint "
				   "and truncate host-managed scrollback during resize; bundle a current "
				   "conpty.dll and matching OpenConsole.exe beside the application.\n";
		});
	}

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
	size.X = static_cast<SHORT>(m_cols.load());
	size.Y = static_cast<SHORT>(m_rows.load());

	// Only pass documented flags. The old internal value 0x2 is not a feature
	// probe: CreatePseudoConsole may succeed while silently ignoring that bit.
	HRESULT hr = m_createPseudoConsole(
		size, hPtyInRead, hPtyOutWrite, 0, &m_hPC);

	// A broken or mismatched side-by-side installation should not prevent the
	// terminal from starting. Retry with the inbox implementation.
	if (FAILED(hr) && m_usingSideBySideConPty) {
		if (m_hPC) {
			m_closePseudoConsole(m_hPC);
			m_hPC = nullptr;
		}
		std::cerr << "Side-by-side ConPTY failed; retrying with the Windows inbox API.\n";
		unloadConPtyApi();
		hr = m_createPseudoConsole(size, hPtyInRead, hPtyOutWrite, 0, &m_hPC);
	}

	closeHandleIfValid(hPtyInRead);
	closeHandleIfValid(hPtyOutWrite);

	if (FAILED(hr)) {
		std::cerr << "CreatePseudoConsole failed: hr=0x" << std::hex << hr << std::dec << "\n";
		closeHandleIfValid(m_hToPty);
		closeHandleIfValid(m_hFromPty);
		unloadConPtyApi();
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
			attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
			m_hPC, sizeof(m_hPC), nullptr, nullptr)) {
		std::cerr << "UpdateProcThreadAttribute failed: " << GetLastError() << "\n";
		::DeleteProcThreadAttributeList(attrList);
		return false;
	}

	STARTUPINFOEXW siex{};
	siex.StartupInfo.cb = sizeof(siex);
	siex.lpAttributeList = attrList;

	std::wstring cmdline = shell;
	const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
	const BOOL ok = ::CreateProcessW(
		nullptr, cmdline.data(), nullptr, nullptr, FALSE, flags,
		nullptr, nullptr, &siex.StartupInfo, &m_pi);

	::DeleteProcThreadAttributeList(attrList);
	if (!ok) return false;

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
		m_closePseudoConsole(m_hPC);
		m_hPC = nullptr;
	}
	unloadConPtyApi();
}

void Terminal::readerLoop() {
	constexpr DWORD kBufferSize = 16384;
	std::vector<uint8_t> buffer(kBufferSize);

	while (m_running.load(std::memory_order_acquire)) {
		const HANDLE h = m_hFromPty;
		if (h == INVALID_HANDLE_VALUE) break;

		DWORD got = 0;
		const BOOL ok = ::ReadFile(h, buffer.data(), kBufferSize, &got, nullptr);
		if (!ok) {
			const DWORD err = ::GetLastError();
			if (err == ERROR_OPERATION_ABORTED || err == ERROR_BROKEN_PIPE) break;
			break;
		}
		if (got == 0) break;

		bool queuedForResize = false;
		{
			// ResizePseudoConsole may emit a full-screen repaint. Continue draining
			// the pipe while a resize is active, but let resize() parse these bytes
			// only after libghostty has adopted the matching geometry.
			std::unique_lock<std::mutex> gateLock(m_outputGateMutex);
			if (m_resizeInProgress) {
				m_pendingPtyOutput.insert(
					m_pendingPtyOutput.end(), buffer.begin(), buffer.begin() + got);
				queuedForResize = true;
			} else {
				std::lock_guard<std::mutex> stateLock(m_stateMutex);
				if (!m_terminal) continue;
				ghostty_terminal_vt_write(
					m_terminal, buffer.data(), static_cast<size_t>(got));
				refreshTerminalMetadataLocked();
				m_renderSnapshotDirty = true;
			}
		}

		if (!queuedForResize) {
			App::time_till_regular = std::max(App::time_till_regular, 2);
			RERENDER.store(true, std::memory_order_release);
		}
	}
}

#else // Linux

bool Terminal::initConPty() {
	m_masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
	if (m_masterFd < 0) {
		std::cerr << "posix_openpt failed: " << strerror(errno) << "\n";
		return false;
	}
	if (::grantpt(m_masterFd) < 0) {
		std::cerr << "grantpt failed: " << strerror(errno) << "\n";
		fdCloseIfValid(m_masterFd);
		return false;
	}
	if (::unlockpt(m_masterFd) < 0) {
		std::cerr << "unlockpt failed: " << strerror(errno) << "\n";
		fdCloseIfValid(m_masterFd);
		return false;
	}

	struct winsize ws{};
	ws.ws_row = static_cast<unsigned short>(m_rows.load());
	ws.ws_col = static_cast<unsigned short>(m_cols.load());
	::ioctl(m_masterFd, TIOCSWINSZ, &ws);
	return true;
}

bool Terminal::launchShell(const std::string& shell) {
	m_childPid = ::fork();
	if (m_childPid < 0) {
		std::cerr << "fork failed: " << strerror(errno) << "\n";
		return false;
	}

	if (m_childPid == 0) {
		if (::setsid() < 0) ::_exit(1);

		const char* ptsName = ::ptsname(m_masterFd);
		if (!ptsName) ::_exit(1);

		const int slave = ::open(ptsName, O_RDWR);
		if (slave < 0) ::_exit(1);
		::ioctl(slave, TIOCSCTTY, 0);
		::close(m_masterFd);

		::dup2(slave, STDIN_FILENO);
		::dup2(slave, STDOUT_FILENO);
		::dup2(slave, STDERR_FILENO);
		if (slave > STDERR_FILENO) ::close(slave);

		::setenv("TERM", "xterm-256color", 1);
		::setenv("COLORTERM", "truecolor", 1);

		if (!m_remoteCommand.empty()) {
			::execl("/bin/sh", "sh", "-lc", m_remoteCommand.c_str(), nullptr);
			::_exit(1);
		}

		const char* sh = shell.c_str();
		const char* shBase = std::strrchr(sh, '/');
		shBase = shBase ? shBase + 1 : sh;
		::execlp(sh, shBase, nullptr);
		::_exit(1);
	}
	return true;
}

void Terminal::teardownConPty() {
	fdCloseIfValid(m_masterFd);
	if (m_childPid > 0) {
		::kill(m_childPid, SIGTERM);
		::waitpid(m_childPid, nullptr, WNOHANG);
		m_childPid = -1;
	}
}

void Terminal::readerLoop() {
	constexpr size_t kBufferSize = 16384;
	std::vector<uint8_t> buffer(kBufferSize);

	while (m_running.load(std::memory_order_acquire)) {
		const int fd = m_masterFd;
		if (fd < 0) break;

		const ssize_t got = ::read(fd, buffer.data(), buffer.size());
		if (got <= 0) {
			if (got < 0 && errno == EINTR) continue;
			break;
		}

		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			if (!m_terminal) continue;
			ghostty_terminal_vt_write(m_terminal, buffer.data(), static_cast<size_t>(got));
			refreshTerminalMetadataLocked();
			m_renderSnapshotDirty = true;
		}

		App::time_till_regular = std::max(App::time_till_regular, 2);
		RERENDER.store(true, std::memory_order_release);
	}
}

#endif

// ============================================================================
// libghostty state and rendering
// ============================================================================

bool Terminal::initGhostty() {
	std::lock_guard<std::mutex> lock(m_stateMutex);

	GhosttyTerminalOptions options{};
	options.cols = static_cast<uint16_t>(m_cols.load());
	options.rows = static_cast<uint16_t>(m_rows.load());
	options.max_scrollback = m_scrollbackMax;

	if (ghostty_terminal_new(nullptr, &m_terminal, options) != GHOSTTY_SUCCESS) return false;
	if (ghostty_render_state_new(nullptr, &m_renderState) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}
	if (ghostty_render_state_row_iterator_new(nullptr, &m_rowIterator) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}
	if (ghostty_render_state_row_cells_new(nullptr, &m_rowCells) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}
	if (ghostty_mouse_encoder_new(nullptr, &m_mouseEncoder) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}
	if (ghostty_mouse_event_new(nullptr, &m_mouseEvent) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}

	if (ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this) != GHOSTTY_SUCCESS ||
	    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
	                        reinterpret_cast<const void*>(&Terminal::sWritePty)) != GHOSTTY_SUCCESS) {
		teardownGhostty();
		return false;
	}

	ghostty_terminal_resize(
		m_terminal,
		static_cast<uint16_t>(m_cols.load()),
		static_cast<uint16_t>(m_rows.load()),
		static_cast<uint32_t>(m_cellWidthPx.load()),
		static_cast<uint32_t>(m_cellHeightPx.load()));

	updateMouseGeometryLocked();
	m_terminalReady.store(true, std::memory_order_release);
	return refreshRenderStateLocked();
}

void Terminal::teardownGhostty() {
	m_terminalReady.store(false, std::memory_order_release);

	// This function is also used by initGhostty while the mutex is already held.
	if (m_mouseEvent) {
		ghostty_mouse_event_free(m_mouseEvent);
		m_mouseEvent = nullptr;
	}
	if (m_mouseEncoder) {
		ghostty_mouse_encoder_free(m_mouseEncoder);
		m_mouseEncoder = nullptr;
	}
	if (m_rowCells) {
		ghostty_render_state_row_cells_free(m_rowCells);
		m_rowCells = nullptr;
	}
	if (m_rowIterator) {
		ghostty_render_state_row_iterator_free(m_rowIterator);
		m_rowIterator = nullptr;
	}
	if (m_renderState) {
		ghostty_render_state_free(m_renderState);
		m_renderState = nullptr;
	}
	if (m_terminal) {
		ghostty_terminal_free(m_terminal);
		m_terminal = nullptr;
	}
	m_viewCells.clear();
	m_renderSnapshotDirty = true;
}

GhosttyColorRgb Terminal::resolveColor(const GhosttyStyleColor& color,
									  const GhosttyRenderStateColors& colors,
									  GhosttyColorRgb fallback) {
	switch (color.tag) {
		case GHOSTTY_STYLE_COLOR_RGB:
			return color.value.rgb;
		case GHOSTTY_STYLE_COLOR_PALETTE:
			return colors.palette[color.value.palette];
		default:
			return fallback;
	}
}

void Terminal::refreshTerminalMetadataLocked() {
	GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
	bool mouse = false;
	GhosttyTerminalScrollbar scrollbar{};

	ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
	ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse);
	ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);

	m_altScreen.store(screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE, std::memory_order_release);
	m_mouseReporting.store(mouse, std::memory_order_release);
	m_scrollbar = scrollbar;

	if (m_mouseEncoder) ghostty_mouse_encoder_setopt_from_terminal(m_mouseEncoder, m_terminal);
}

bool Terminal::refreshRenderStateLocked() {
	if (!m_terminal || !m_renderState || !m_rowIterator || !m_rowCells) return false;
	if (ghostty_render_state_update(m_renderState, m_terminal) != GHOSTTY_SUCCESS) return false;

	GhosttyRenderStateColors colors{};
	colors.size = sizeof(colors);
	if (ghostty_render_state_colors_get(m_renderState, &colors) == GHOSTTY_SUCCESS) {
		m_colors = colors;
	}

	refreshTerminalMetadataLocked();

	const int cols = m_cols.load(std::memory_order_acquire);
	const int rows = m_rows.load(std::memory_order_acquire);
	m_viewCells.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), kDefaultCell);

	if (ghostty_render_state_get(
			m_renderState, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m_rowIterator) != GHOSTTY_SUCCESS) {
		return false;
	}

	int rowIndex = 0;
	while (rowIndex < rows && ghostty_render_state_row_iterator_next(m_rowIterator)) {
		if (ghostty_render_state_row_get(
				m_rowIterator, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m_rowCells) != GHOSTTY_SUCCESS) {
			++rowIndex;
			continue;
		}

		int colIndex = 0;
		while (colIndex < cols && ghostty_render_state_row_cells_next(m_rowCells)) {
			uint32_t graphemeLen = 0;
			ghostty_render_state_row_cells_get(
				m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &graphemeLen);

			uint32_t codepoint = U' ';
			if (graphemeLen > 0) {
				uint32_t inlineGraphemes[16]{};
				if (graphemeLen <= 16) {
					ghostty_render_state_row_cells_get(
						m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, inlineGraphemes);
					codepoint = inlineGraphemes[0];
				} else {
					std::vector<uint32_t> graphemes(graphemeLen);
					ghostty_render_state_row_cells_get(
						m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, graphemes.data());
					codepoint = graphemes[0];
				}
			}

			GhosttyStyle style{};
			style.size = sizeof(style);
			ghostty_render_state_row_cells_get(
				m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

			GhosttyColorRgb fg = m_colors.foreground;
			GhosttyColorRgb bg = m_colors.background;
			ghostty_render_state_row_cells_get(
				m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
			ghostty_render_state_row_cells_get(
				m_rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg);

			if (style.inverse) std::swap(fg, bg);
			if (style.invisible) fg = bg;

			OurCell cell{};
			cell.c = static_cast<UChar32>(codepoint);
			cell.bg_red = bg.r;
			cell.bg_green = bg.g;
			cell.bg_blue = bg.b;
			cell.fg_red = fg.r;
			cell.fg_green = fg.g;
			cell.fg_blue = fg.b;
			m_viewCells[static_cast<size_t>(rowIndex) * cols + colIndex] = cell;
			++colIndex;
		}

		const bool cleanRow = false;
		ghostty_render_state_row_set(
			m_rowIterator, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &cleanRow);
		++rowIndex;
	}

	bool cursorVisible = false;
	bool cursorBlinking = false;
	bool cursorInViewport = false;
	ghostty_render_state_get(
		m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursorVisible);
	ghostty_render_state_get(
		m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &cursorBlinking);
	ghostty_render_state_get(
		m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursorInViewport);

	CursorInfo cursor{};
	cursor.visible = cursorVisible && cursorInViewport;
	cursor.blink = cursorBlinking;
	if (cursor.visible) {
		uint16_t x = 0;
		uint16_t y = 0;
		GhosttyRenderStateCursorVisualStyle visual = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
		ghostty_render_state_get(
			m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &x);
		ghostty_render_state_get(
			m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &y);
		ghostty_render_state_get(
			m_renderState, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &visual);
		cursor.col = x;
		cursor.row = y;
		switch (visual) {
			case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE: cursor.shape = 2; break;
			case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR: cursor.shape = 3; break;
			default: cursor.shape = 1; break;
		}
	}
	m_cursorInfo = cursor;

	GhosttyRenderStateDirty clean = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
	ghostty_render_state_set(m_renderState, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean);
	m_renderSnapshotDirty = false;
	return true;
}

void Terminal::updateMouseGeometryLocked() {
	if (!m_mouseEncoder) return;
	GhosttyMouseEncoderSize size{};
	size.size = sizeof(size);
	size.cell_width = static_cast<uint32_t>(std::max(m_cellWidthPx.load(), 1));
	size.cell_height = static_cast<uint32_t>(std::max(m_cellHeightPx.load(), 1));
	size.screen_width = static_cast<uint32_t>(m_cols.load()) * size.cell_width;
	size.screen_height = static_cast<uint32_t>(m_rows.load()) * size.cell_height;
	size.padding_top = 0;
	size.padding_bottom = 0;
	size.padding_left = 0;
	size.padding_right = 0;
	ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
}

void Terminal::sWritePty(GhosttyTerminal, void* userdata, const uint8_t* data, size_t len) {
	auto* self = static_cast<Terminal*>(userdata);
	if (self && data && len > 0) self->writeInput(data, len);
}

// ============================================================================
// Cell/document access
// ============================================================================

OurCell Terminal::getCell(int row, int col) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_renderSnapshotDirty) refreshRenderStateLocked();
	const int rows = m_rows.load();
	const int cols = m_cols.load();
	if (row < 0 || col < 0 || row >= rows || col >= cols) return kDefaultCell;
	const size_t index = static_cast<size_t>(row) * cols + col;
	return index < m_viewCells.size() ? m_viewCells[index] : kDefaultCell;
}

CursorInfo Terminal::getCursorInfo() {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_renderSnapshotDirty) refreshRenderStateLocked();
	CursorInfo result = m_cursorInfo;
	if (!m_terminalReady.load(std::memory_order_acquire)) result.visible = false;
	return result;
}

bool Terminal::getViewSnapshot(std::vector<OurCell>& cells,
							   CursorInfo& cursor,
							   int& cols,
							   int& rows) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_renderSnapshotDirty && !refreshRenderStateLocked()) return false;
	cols = m_cols.load(std::memory_order_acquire);
	rows = m_rows.load(std::memory_order_acquire);
	cells = m_viewCells;
	cursor = m_cursorInfo;
	if (!m_terminalReady.load(std::memory_order_acquire)) cursor.visible = false;
	return cells.size() == static_cast<size_t>(cols) * static_cast<size_t>(rows);
}


std::string Terminal::getLastTextLines(int maxLogicalLines) {
	if (maxLogicalLines <= 0) return {};

	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (!m_terminal) return {};

	// Read the newest rows in the full screen, not the bottom of whichever
	// viewport the user happens to be looking at.
	refreshTerminalMetadataLocked();

	const int cols = m_cols.load(std::memory_order_acquire);
	if (cols <= 0 || m_scrollbar.total == 0) return {};

	const uint64_t maxInt = static_cast<uint64_t>(std::numeric_limits<int>::max());
	const int lastAvailable = static_cast<int>(
		std::min<uint64_t>(m_scrollbar.total - 1, maxInt));

	auto appendUtf8 = [](uint32_t cp, std::string& out) {
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
		if (cp < 0x80) {
			out.push_back(static_cast<char>(cp));
		} else if (cp < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else if (cp < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	};

	auto readRow = [&](int docId, std::string& row, bool& hasText) -> bool {
		row.clear();
		hasText = false;
		row.reserve(static_cast<size_t>(cols));

		for (int col = 0; col < cols; ++col) {
			OurCell cell{};
			if (!readGridCellLocked(docId, col, cell)) return false;

			uint32_t cp = static_cast<uint32_t>(cell.c);
			if (cp == 0) cp = U' ';
			if (cp != U' ' && cp != U'\t') hasText = true;
			appendUtf8(cp, row);
		}

		// Remove terminal padding. Wrapped rows are joined below without a
		// newline, so this also reconstructs the logical line.
		while (!row.empty() && row.back() == ' ') row.pop_back();
		return true;
	};

	// The active screen normally contains blank rows below the prompt. Find
	// the newest row containing actual text before choosing the requested
	// history range. This is why a fresh prompt now works without scrollback.
	int endDoc = lastAvailable;
	std::string scratch;
	while (endDoc >= 0) {
		bool hasText = false;
		if (!readRow(endDoc, scratch, hasText)) return {};
		if (hasText) break;
		--endDoc;
	}
	if (endDoc < 0) return {};

	// Include up to maxLogicalLines, while always including every physical
	// row belonging to a soft-wrapped logical line.
	int startDoc = endDoc;
	int logicalLines = 1;
	while (startDoc > 0) {
		bool previousWraps = false;
		if (!readGridWrapLocked(startDoc - 1, previousWraps)) break;

		if (!previousWraps && logicalLines >= maxLogicalLines) break;
		--startDoc;
		if (!previousWraps) ++logicalLines;
	}

	std::string out;
	for (int docId = startDoc; docId <= endDoc; ++docId) {
		bool hasText = false;
		if (!readRow(docId, scratch, hasText)) break;
		out += scratch;

		bool wraps = false;
		if (!readGridWrapLocked(docId, wraps)) wraps = false;
		if (!wraps && docId != endDoc) out.push_back('\n');
	}

	return out;
}

int Terminal::docLineIdForScreenRow(int screenRow) const {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (screenRow < 0 || static_cast<uint64_t>(screenRow) >= m_scrollbar.len) return -1;
	const uint64_t id = m_scrollbar.offset + static_cast<uint64_t>(screenRow);
	return id > static_cast<uint64_t>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(id);
}

int Terminal::screenRowForDocLineId(int docId) const {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (docId < 0) return -1;
	const uint64_t id = static_cast<uint64_t>(docId);
	if (id < m_scrollbar.offset || id >= m_scrollbar.offset + m_scrollbar.len) return -1;
	return static_cast<int>(id - m_scrollbar.offset);
}

bool Terminal::readGridCellLocked(int docId, int col, OurCell& out) {
	if (!m_terminal || docId < 0 || col < 0 || col >= m_cols.load()) return false;
	if (static_cast<uint64_t>(docId) >= m_scrollbar.total) return false;

	GhosttyPoint point{};
	point.tag = GHOSTTY_POINT_TAG_SCREEN;
	point.value.coordinate.x = static_cast<uint16_t>(col);
	point.value.coordinate.y = static_cast<uint32_t>(docId);

	GhosttyGridRef ref{};
	ref.size = sizeof(ref);
	if (ghostty_terminal_grid_ref(m_terminal, point, &ref) != GHOSTTY_SUCCESS) return false;

	uint32_t codepoint = U' ';
	uint32_t inlineGraphemes[8]{};
	size_t graphemeLen = 0;
	GhosttyResult graphemeResult = ghostty_grid_ref_graphemes(
		&ref, inlineGraphemes, sizeof(inlineGraphemes) / sizeof(inlineGraphemes[0]), &graphemeLen);
	if (graphemeResult == GHOSTTY_SUCCESS && graphemeLen > 0) {
		codepoint = inlineGraphemes[0];
	} else if (graphemeResult == GHOSTTY_OUT_OF_SPACE && graphemeLen > 0) {
		std::vector<uint32_t> graphemes(graphemeLen);
		if (ghostty_grid_ref_graphemes(
				&ref, graphemes.data(), graphemes.size(), &graphemeLen) != GHOSTTY_SUCCESS) {
			return false;
		}
		if (!graphemes.empty()) codepoint = graphemes[0];
	} else if (graphemeResult != GHOSTTY_SUCCESS) {
		return false;
	}

	GhosttyStyle style{};
	style.size = sizeof(style);
	if (ghostty_grid_ref_style(&ref, &style) != GHOSTTY_SUCCESS) return false;

	GhosttyColorRgb fg = resolveColor(style.fg_color, m_colors, m_colors.foreground);
	GhosttyColorRgb bg = resolveColor(style.bg_color, m_colors, m_colors.background);
	if (style.inverse) std::swap(fg, bg);
	if (style.invisible) fg = bg;
	out = {static_cast<UChar32>(codepoint), bg.r, bg.g, bg.b, fg.r, fg.g, fg.b};
	return true;
}

bool Terminal::readGridWrapLocked(int docId, bool& out) {
	out = false;
	if (!m_terminal || docId < 0 || static_cast<uint64_t>(docId) >= m_scrollbar.total) return false;

	GhosttyPoint point{};
	point.tag = GHOSTTY_POINT_TAG_SCREEN;
	point.value.coordinate.x = 0;
	point.value.coordinate.y = static_cast<uint32_t>(docId);

	GhosttyGridRef ref{};
	ref.size = sizeof(ref);
	if (ghostty_terminal_grid_ref(m_terminal, point, &ref) != GHOSTTY_SUCCESS) return false;
	GhosttyRow row = 0;
	if (ghostty_grid_ref_row(&ref, &row) != GHOSTTY_SUCCESS) return false;
	return ghostty_row_get(row, GHOSTTY_ROW_DATA_WRAP, &out) == GHOSTTY_SUCCESS;
}

bool Terminal::getDocCell(int docId, int col, OurCell& out) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return readGridCellLocked(docId, col, out);
}

bool Terminal::getDocWraps(int docId) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	bool result = false;
	readGridWrapLocked(docId, result);
	return result;
}

size_t Terminal::scrollbackSize() const {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_scrollbar.total > m_scrollbar.len
		? static_cast<size_t>(m_scrollbar.total - m_scrollbar.len)
		: 0;
}

int Terminal::docLineCount() const {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_scrollbar.total > static_cast<uint64_t>(std::numeric_limits<int>::max())
		? std::numeric_limits<int>::max()
		: static_cast<int>(m_scrollbar.total);
}

bool Terminal::appWantsMouse() const {
	return m_mouseReporting.load(std::memory_order_acquire);
}

// ============================================================================
// PTY input
// ============================================================================

bool Terminal::writeInput(const void* data, size_t bytes) {
	if (!m_running.load(std::memory_order_acquire)) return false;
	if (!data && bytes != 0) return false;
	const auto* ptr = static_cast<const uint8_t*>(data);
	size_t remaining = bytes;

#ifdef _WIN32
	const HANDLE h = m_hToPty;
	if (h == INVALID_HANDLE_VALUE) return false;
	while (remaining > 0) {
		DWORD written = 0;
		const DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, std::numeric_limits<DWORD>::max()));
		if (!::WriteFile(h, ptr, chunk, &written, nullptr) || written == 0) return false;
		ptr += written;
		remaining -= written;
	}
#else
	const int fd = m_masterFd;
	if (fd < 0) return false;
	while (remaining > 0) {
		const ssize_t written = ::write(fd, ptr, remaining);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) return false;
		ptr += written;
		remaining -= static_cast<size_t>(written);
	}
#endif
	return true;
}

bool Terminal::sendText(const std::string& utf8) {
	return utf8.empty() || writeInput(utf8.data(), utf8.size());
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
	const char b = static_cast<char>(c);
	return writeInput(&b, 1);
}

bool Terminal::sendSpecialKey(SpecialKey key, bool shift, bool alt, bool ctrl) {
	std::string seq;
	const int mod = xtermMod(shift, alt, ctrl);

	auto cursorKey = [&](char code) {
		if (mod == 1) seq = "\x1b[" + std::string(1, code);
		else seq = "\x1b[1;" + std::to_string(mod) + std::string(1, code);
	};

	switch (key) {
		case SpecialKey::Up: cursorKey('A'); break;
		case SpecialKey::Down: cursorKey('B'); break;
		case SpecialKey::Right: cursorKey('C'); break;
		case SpecialKey::Left: cursorKey('D'); break;
		case SpecialKey::Home: cursorKey('H'); break;
		case SpecialKey::End: cursorKey('F'); break;
		case SpecialKey::InsertKey: seq = "\x1b[2~"; break;
		case SpecialKey::DeleteKey: seq = "\x1b[3~"; break;
		case SpecialKey::PageUp: seq = "\x1b[5~"; break;
		case SpecialKey::PageDown: seq = "\x1b[6~"; break;
		case SpecialKey::F1: seq = "\x1bOP"; break;
		case SpecialKey::F2: seq = "\x1bOQ"; break;
		case SpecialKey::F3: seq = "\x1bOR"; break;
		case SpecialKey::F4: seq = "\x1bOS"; break;
		case SpecialKey::F5: seq = "\x1b[15~"; break;
		case SpecialKey::F6: seq = "\x1b[17~"; break;
		case SpecialKey::F7: seq = "\x1b[18~"; break;
		case SpecialKey::F8: seq = "\x1b[19~"; break;
		case SpecialKey::F9: seq = "\x1b[20~"; break;
		case SpecialKey::F10: seq = "\x1b[21~"; break;
		case SpecialKey::F11: seq = "\x1b[23~"; break;
		case SpecialKey::F12: seq = "\x1b[24~"; break;
	}
	return writeInput(seq.data(), seq.size());
}

// ============================================================================
// Mouse and scrollback
// ============================================================================

bool Terminal::enableMouseTracking(bool enable) {
	const std::string seq = enable ? "\x1b[?1002h\x1b[?1006h" : "\x1b[?1002l\x1b[?1006l";
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (!m_terminal) return false;
	ghostty_terminal_vt_write(
		m_terminal, reinterpret_cast<const uint8_t*>(seq.data()), seq.size());
	refreshTerminalMetadataLocked();
	m_renderSnapshotDirty = true;
	RERENDER.store(true, std::memory_order_release);
	return true;
}

GhosttyMouseButton Terminal::toGhosttyMouseButton(int button) {
	switch (button) {
		case 0: return GHOSTTY_MOUSE_BUTTON_LEFT;
		case 1: return GHOSTTY_MOUSE_BUTTON_MIDDLE;
		case 2: return GHOSTTY_MOUSE_BUTTON_RIGHT;
		default: return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
	}
}

bool Terminal::encodeMouseEvent(GhosttyMouseAction action,
								GhosttyMouseButton button,
								bool hasButton,
								int row,
								int col,
								bool shift,
								bool alt,
								bool ctrl,
								std::string& out) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (!m_terminal || !m_mouseEncoder || !m_mouseEvent) return false;

	ghostty_mouse_encoder_setopt_from_terminal(m_mouseEncoder, m_terminal);
	ghostty_mouse_event_set_action(m_mouseEvent, action);
	if (hasButton) ghostty_mouse_event_set_button(m_mouseEvent, button);
	else ghostty_mouse_event_clear_button(m_mouseEvent);

	uint8_t mods = 0;
	if (shift) mods |= static_cast<uint8_t>(GHOSTTY_MODS_SHIFT);
	if (ctrl) mods |= static_cast<uint8_t>(GHOSTTY_MODS_CTRL);
	if (alt) mods |= static_cast<uint8_t>(GHOSTTY_MODS_ALT);
	ghostty_mouse_event_set_mods(m_mouseEvent, static_cast<GhosttyMods>(mods));

	const int cellWidth = std::max(m_cellWidthPx.load(), 1);
	const int cellHeight = std::max(m_cellHeightPx.load(), 1);
	GhosttyMousePosition pos{};
	pos.x = static_cast<float>(std::max(col, 0) * cellWidth + cellWidth / 2);
	pos.y = static_cast<float>(std::max(row, 0) * cellHeight + cellHeight / 2);
	ghostty_mouse_event_set_position(m_mouseEvent, pos);

	const bool anyPressed = m_buttonsDownMask.load(std::memory_order_acquire) != 0;
	ghostty_mouse_encoder_setopt(
		m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyPressed);

	char stackBuffer[128];
	size_t outLen = 0;
	GhosttyResult result = ghostty_mouse_encoder_encode(
		m_mouseEncoder, m_mouseEvent, stackBuffer, sizeof(stackBuffer), &outLen);
	if (result == GHOSTTY_SUCCESS) {
		out.assign(stackBuffer, stackBuffer + outLen);
		return true;
	}
	if (result != GHOSTTY_OUT_OF_SPACE || outLen == 0) return false;

	std::vector<char> dynamicBuffer(outLen);
	result = ghostty_mouse_encoder_encode(
		m_mouseEncoder, m_mouseEvent, dynamicBuffer.data(), dynamicBuffer.size(), &outLen);
	if (result != GHOSTTY_SUCCESS) return false;
	out.assign(dynamicBuffer.data(), dynamicBuffer.data() + outLen);
	return true;
}

bool Terminal::mousePress(int row, int col, int button, bool pressed,
						  bool shift, bool alt, bool ctrl) {
	const GhosttyMouseButton ghostButton = toGhosttyMouseButton(button);
	if (ghostButton == GHOSTTY_MOUSE_BUTTON_UNKNOWN) return false;

	const uint32_t bit = 1u << static_cast<unsigned>(button);
	if (pressed) m_buttonsDownMask.fetch_or(bit, std::memory_order_acq_rel);
	else m_buttonsDownMask.fetch_and(~bit, std::memory_order_acq_rel);

	std::string encoded;
	if (!encodeMouseEvent(
			pressed ? GHOSTTY_MOUSE_ACTION_PRESS : GHOSTTY_MOUSE_ACTION_RELEASE,
			ghostButton, true, row, col, shift, alt, ctrl, encoded)) {
		return false;
	}
	return encoded.empty() || writeInput(encoded);
}

bool Terminal::mouseMove(int row, int col, bool buttonHeld,
						 bool shift, bool alt, bool ctrl) {
	std::string encoded;
	if (!encodeMouseEvent(
			GHOSTTY_MOUSE_ACTION_MOTION,
			GHOSTTY_MOUSE_BUTTON_LEFT,
			buttonHeld,
			row, col, shift, alt, ctrl, encoded)) {
		return false;
	}
	return encoded.empty() || writeInput(encoded);
}

bool Terminal::mouseDrag(int startRow, int startCol, int endRow, int endCol, int button,
						 bool shift, bool alt, bool ctrl) {
	if (!mousePress(startRow, startCol, button, true, shift, alt, ctrl)) return false;
	const int steps = std::max(1, std::max(std::abs(endRow - startRow), std::abs(endCol - startCol)));
	for (int i = 1; i <= steps; ++i) {
		const int row = startRow + (endRow - startRow) * i / steps;
		const int col = startCol + (endCol - startCol) * i / steps;
		if (!mouseMove(row, col, true, shift, alt, ctrl)) return false;
	}
	return mousePress(endRow, endCol, button, false, shift, alt, ctrl);
}

bool Terminal::mouseScroll(int row, int col, int lines,
						   bool shift, bool alt, bool ctrl) {
	if (lines == 0) return true;

	if (m_mouseReporting.load(std::memory_order_acquire)) {
		const GhosttyMouseButton wheel = lines < 0
			? GHOSTTY_MOUSE_BUTTON_FOUR
			: GHOSTTY_MOUSE_BUTTON_FIVE;
		for (int i = 0; i < std::abs(lines); ++i) {
			std::string encoded;
			if (!encodeMouseEvent(
					GHOSTTY_MOUSE_ACTION_PRESS, wheel, true,
					row, col, shift, alt, ctrl, encoded)) {
				return false;
			}
			if (!encoded.empty() && !writeInput(encoded)) return false;
		}
		return true;
	}

	if (m_altScreen.load(std::memory_order_acquire)) {
		const SpecialKey key = lines < 0 ? SpecialKey::Up : SpecialKey::Down;
		for (int i = 0; i < std::abs(lines); ++i) {
			if (!sendSpecialKey(key, shift, alt, ctrl)) return false;
		}
		return true;
	}

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		if (!m_terminal) return false;
		GhosttyTerminalScrollViewport scroll{};
		scroll.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
		scroll.value.delta = static_cast<intptr_t>(lines);
		ghostty_terminal_scroll_viewport(m_terminal, scroll);
		refreshTerminalMetadataLocked();
		m_renderSnapshotDirty = true;
	}
	RERENDER.store(true, std::memory_order_release);
	return true;
}

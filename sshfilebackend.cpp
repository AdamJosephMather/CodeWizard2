#include "sshfilebackend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
constexpr std::uint32_t maximumFrameSize = 64u * 1024u * 1024u;
constexpr unsigned char rpcChannel = 0;

std::array<unsigned char, 4> bigEndian(std::uint32_t value) {
	return {
		static_cast<unsigned char>((value >> 24) & 0xff),
		static_cast<unsigned char>((value >> 16) & 0xff),
		static_cast<unsigned char>((value >> 8) & 0xff),
		static_cast<unsigned char>(value & 0xff)
	};
}

std::uint32_t fromBigEndian(const std::array<unsigned char, 4>& value) {
	return (static_cast<std::uint32_t>(value[0]) << 24) |
		   (static_cast<std::uint32_t>(value[1]) << 16) |
		   (static_cast<std::uint32_t>(value[2]) << 8) |
		   static_cast<std::uint32_t>(value[3]);
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	result.reserve(((bytes.size() + 2) / 3) * 4);
	for (std::size_t i = 0; i < bytes.size(); i += 3) {
		const std::uint32_t a = bytes[i];
		const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
		const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
		const std::uint32_t value = (a << 16) | (b << 8) | c;
		result.push_back(alphabet[(value >> 18) & 63]);
		result.push_back(alphabet[(value >> 12) & 63]);
		result.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
		result.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
	}
	return result;
}

bool base64Decode(const std::string& input, std::vector<std::uint8_t>& output) {
	std::array<int, 256> values{};
	values.fill(-1);
	const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (std::size_t i = 0; i < alphabet.size(); ++i) values[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
	output.clear();
	std::uint32_t accumulator = 0;
	int bits = 0;
	for (const unsigned char c : input) {
		if (c == '=') break;
		const int value = values[c];
		if (value < 0) return false;
		accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			output.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
		}
	}
	return true;
}

#ifdef _WIN32
std::wstring widen(const std::string& value) {
	if (value.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
	return result;
}

std::wstring quoteWindowsArgument(const std::string& value) {
	const std::wstring source = widen(value);
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

std::wstring currentExecutablePath() {
	std::wstring path(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size()) return {};
	path.resize(length);
	return path;
}

class ScopedWindowsEnvironment {
public:
	ScopedWindowsEnvironment(std::wstring name, const std::wstring& value)
		: name_(std::move(name)) {
		SetLastError(ERROR_SUCCESS);
		const DWORD needed = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
		existed_ = needed != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
		if (needed > 0) {
			old_value_.resize(needed);
			const DWORD copied = GetEnvironmentVariableW(name_.c_str(), old_value_.data(), needed);
			old_value_.resize(copied);
		}
		SetEnvironmentVariableW(name_.c_str(), value.c_str());
	}

	~ScopedWindowsEnvironment() { restore(); }

	void restore() {
		if (restored_) return;
		SetEnvironmentVariableW(name_.c_str(), existed_ ? old_value_.c_str() : nullptr);
		restored_ = true;
	}

private:
	std::wstring name_;
	std::wstring old_value_;
	bool existed_ = false;
	bool restored_ = false;
};
#else
std::string currentExecutablePath() {
	std::array<char, 4096> path{};
	const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
	return length > 0 ? std::string(path.data(), static_cast<std::size_t>(length)) : std::string{};
}
#endif

std::vector<std::string> sshArguments(const SSHConnectionOptions& options) {
	std::vector<std::string> arguments{
		"ssh", "-T",
		"-o", options.password.empty() ? "BatchMode=yes" : "BatchMode=no",
		"-o", "ConnectTimeout=10",
		"-o", "ServerAliveInterval=10",
		"-o", "ServerAliveCountMax=3",
		"-p", std::to_string(options.port)
	};
	if (!options.password.empty()) {
		arguments.insert(arguments.end(), {
			"-o", "NumberOfPasswordPrompts=1",
			"-o", "PreferredAuthentications=password,keyboard-interactive"
		});
	}
	if (!options.key_path.empty()) {
		arguments.push_back("-i");
		arguments.push_back(options.key_path);
	}
	arguments.push_back("--");
	arguments.push_back(options.username.empty() ? options.hostname : options.username + "@" + options.hostname);
	arguments.push_back(options.helper_path);
	return arguments;
}
} // namespace

SSHTransport::SSHTransport() = default;

SSHTransport::~SSHTransport() {
	close();
}

void SSHTransport::setDisconnectCallback(std::function<void()> callback) {
	std::lock_guard<std::mutex> lock(monitor_mutex_);
	disconnect_callback_ = std::move(callback);
}

void SSHTransport::fireDisconnect() {
	std::function<void()> cb;
	{
		std::lock_guard<std::mutex> lock(monitor_mutex_);
		cb = disconnect_callback_;
	}
	if (cb) cb();
}

void SSHTransport::monitorProcess() {
	for (;;) {
		{
			std::lock_guard<std::mutex> lock(monitor_mutex_);
			if (monitor_stop_) return;
		}

#ifdef _WIN32
		DWORD exit_code = 0;
		const HANDLE proc = static_cast<HANDLE>(process_);
		if (!proc) return;
		if (!GetExitCodeProcess(proc, &exit_code)) return;
		if (exit_code != STILL_ACTIVE) {
			fireDisconnect();
			return;
		}
#else
		if (process_id_ <= 0) return;
		int status = 0;
		const pid_t result = ::waitpid(process_id_, &status, WNOHANG);
		if (result < 0) return;
		if (result > 0) {
			fireDisconnect();
			return;
		}
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

bool SSHTransport::start(const SSHConnectionOptions& options, SSHRemoteInfo& info, std::string& error) {
	if (options.hostname.empty()) {
		error = "SSH hostname is required";
		return false;
	}
	close();
	const auto arguments = sshArguments(options);

#ifdef _WIN32
	std::vector<std::unique_ptr<ScopedWindowsEnvironment>> askpass_environment;
	if (!options.password.empty()) {
		const std::wstring executable = currentExecutablePath();
		if (executable.empty()) {
			error = "could not locate CodeWizard for SSH password prompt";
			return false;
		}
		askpass_environment.emplace_back(std::make_unique<ScopedWindowsEnvironment>(L"SSH_ASKPASS", executable));
		askpass_environment.emplace_back(std::make_unique<ScopedWindowsEnvironment>(L"SSH_ASKPASS_REQUIRE", L"force"));
		askpass_environment.emplace_back(std::make_unique<ScopedWindowsEnvironment>(L"DISPLAY", L"codewizard:0"));
		askpass_environment.emplace_back(std::make_unique<ScopedWindowsEnvironment>(L"CODEWIZARD_SSH_ASKPASS", L"1"));
		askpass_environment.emplace_back(std::make_unique<ScopedWindowsEnvironment>(
			L"CODEWIZARD_SSH_PASSWORD", widen(options.password)));
	}
	SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	HANDLE child_input_read = nullptr;
	HANDLE child_output_write = nullptr;
	HANDLE parent_input_write = nullptr;
	HANDLE parent_output_read = nullptr;
	if (!CreatePipe(&child_input_read, &parent_input_write, &security, 0) ||
		!CreatePipe(&parent_output_read, &child_output_write, &security, 0)) {
		error = "could not create SSH pipes";
		if (child_input_read) CloseHandle(child_input_read);
		if (parent_input_write) CloseHandle(parent_input_write);
		if (parent_output_read) CloseHandle(parent_output_read);
		if (child_output_write) CloseHandle(child_output_write);
		return false;
	}
	SetHandleInformation(parent_input_write, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(parent_output_read, HANDLE_FLAG_INHERIT, 0);

	std::wstring command;
	for (const auto& argument : arguments) {
		if (!command.empty()) command.push_back(L' ');
		command += quoteWindowsArgument(argument);
	}
	std::vector<wchar_t> mutable_command(command.begin(), command.end());
	mutable_command.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = child_input_read;
	startup.hStdOutput = child_output_write;
	startup.hStdError = child_output_write;
	PROCESS_INFORMATION process{};
	const BOOL created = CreateProcessW(
		nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process);
	for (auto& variable : askpass_environment) variable->restore();
	CloseHandle(child_input_read);
	CloseHandle(child_output_write);
	if (!created) {
		CloseHandle(parent_input_write);
		CloseHandle(parent_output_read);
		error = "could not start ssh (is OpenSSH installed?)";
		return false;
	}
	CloseHandle(process.hThread);
	process_ = process.hProcess;
	input_write_ = parent_input_write;
	output_read_ = parent_output_read;
#else
	int input_pipe[2];
	int output_pipe[2];
	if (::pipe(input_pipe) != 0 || ::pipe(output_pipe) != 0) {
		error = "could not create SSH pipes";
		return false;
	}
	const pid_t child = ::fork();
	if (child < 0) {
		error = "could not fork ssh";
		::close(input_pipe[0]); ::close(input_pipe[1]);
		::close(output_pipe[0]); ::close(output_pipe[1]);
		return false;
	}
	if (child == 0) {
		::dup2(input_pipe[0], STDIN_FILENO);
		::dup2(output_pipe[1], STDOUT_FILENO);
		::dup2(output_pipe[1], STDERR_FILENO);
		::close(input_pipe[0]); ::close(input_pipe[1]);
		::close(output_pipe[0]); ::close(output_pipe[1]);
		std::vector<char*> argv;
		argv.reserve(arguments.size() + 1);
		for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
		argv.push_back(nullptr);
		if (!options.password.empty()) {
			const std::string executable = currentExecutablePath();
			if (executable.empty()) ::_exit(126);
			::setenv("SSH_ASKPASS", executable.c_str(), 1);
			::setenv("SSH_ASKPASS_REQUIRE", "force", 1);
			::setenv("DISPLAY", "codewizard:0", 1);
			::setenv("CODEWIZARD_SSH_ASKPASS", "1", 1);
			::setenv("CODEWIZARD_SSH_PASSWORD", options.password.c_str(), 1);
		}
		::execvp(argv[0], argv.data());
		::_exit(127);
	}
	::close(input_pipe[0]);
	::close(output_pipe[1]);
	input_write_ = input_pipe[1];
	output_read_ = output_pipe[0];
	process_id_ = static_cast<int>(child);
#endif

	if (!readHandshake(info, error)) {
		close();
		return false;
	}
	if (info.protocol != 1) {
		error = "unsupported cwremote protocol " + std::to_string(info.protocol);
		close();
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(monitor_mutex_);
		monitor_stop_ = false;
	}
	if (monitor_thread_.joinable()) monitor_thread_.join();
	monitor_thread_ = std::thread(&SSHTransport::monitorProcess, this);

	return true;
}

bool SSHTransport::readHandshake(SSHRemoteInfo& info, std::string& error) {
	std::string line;
	line.reserve(512);
	std::string last_non_json_line;
	for (;;) {
		char c = 0;
		std::string read_error;
		if (!readAll(&c, 1, read_error)) {
			if (!line.empty() && line.front() != '{') {
				error = line;
				return false;
			}
			if (!last_non_json_line.empty()) {
				error = last_non_json_line;
				return false;
			}
			error = read_error;
			return false;
		}
		if (c == '\n') {
			if (!line.empty() && line.front() == '{') {
				break;
			}
			if (!line.empty()) {
				last_non_json_line = line;
			}
			line.clear();
			continue;
		}
		if (line.size() >= 64 * 1024) {
			error = "cwremote handshake is too large";
			return false;
		}
		line.push_back(c);
	}
	try {
		const auto value = nlohmann::json::parse(line);
		info.protocol = value.value("protocol", 0);
		info.os = value.value("os", "");
		info.arch = value.value("arch", "");
		info.home = value.value("home", "");
		info.cwd = value.value("cwd", "");
		info.shell = value.value("shell", "");
		info.hostname = value.value("hostname", "");
	} catch (const std::exception& exception) {
		error = std::string("invalid cwremote handshake: ") + exception.what();
		return false;
	}
	return true;
}

bool SSHTransport::request(const std::string& method, const nlohmann::json& params, nlohmann::json& data, std::string& error) {
	std::lock_guard<std::mutex> lock(request_mutex_);
	if (!connected()) {
		error = "SSH connection is closed";
		return false;
	}
	const auto id = next_request_id_++;
	const std::string payload = nlohmann::json{{"id", id}, {"method", method}, {"params", params}}.dump();
	if (payload.size() + 1 > maximumFrameSize) {
		error = "request is too large";
		return false;
	}
	const auto length = bigEndian(static_cast<std::uint32_t>(payload.size() + 1));
	if (!writeAll(length.data(), length.size(), error) ||
		!writeAll(&rpcChannel, 1, error) ||
		!writeAll(payload.data(), payload.size(), error)) {
		return false;
	}

	std::array<unsigned char, 4> response_length{};
	if (!readAll(response_length.data(), response_length.size(), error)) return false;
	const auto size = fromBigEndian(response_length);
	if (size < 1 || size > maximumFrameSize) {
		error = "invalid response frame size";
		return false;
	}
	unsigned char channel = 0;
	if (!readAll(&channel, 1, error)) return false;
	if (channel != rpcChannel) {
		error = "unexpected response channel";
		return false;
	}
	std::string response_payload(size - 1, '\0');
	if (!response_payload.empty() && !readAll(response_payload.data(), response_payload.size(), error)) return false;
	try {
		const auto response = nlohmann::json::parse(response_payload);
		if (response.value("id", std::uint64_t{0}) != id) {
			error = "mismatched cwremote response id";
			return false;
		}
		if (!response.value("ok", false)) {
			error = response.value("error", "remote operation failed");
			return false;
		}
		data = response.value("data", nlohmann::json::object());
		return true;
	} catch (const std::exception& exception) {
		error = std::string("invalid cwremote response: ") + exception.what();
		return false;
	}
}

bool SSHTransport::writeAll(const void* data, std::size_t size, std::string& error) {
	const auto* bytes = static_cast<const unsigned char*>(data);
	while (size > 0) {
#ifdef _WIN32
		DWORD written = 0;
		const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, std::numeric_limits<DWORD>::max()));
		if (!WriteFile(static_cast<HANDLE>(input_write_), bytes, chunk, &written, nullptr) || written == 0) {
			error = "SSH pipe write failed";
			return false;
		}
#else
		const ssize_t written = ::write(input_write_, bytes, size);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) {
			error = "SSH pipe write failed";
			return false;
		}
#endif
		bytes += written;
		size -= written;
	}
	return true;
}

bool SSHTransport::readAll(void* data, std::size_t size, std::string& error) {
	auto* bytes = static_cast<unsigned char*>(data);
	while (size > 0) {
#ifdef _WIN32
		DWORD read = 0;
		const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, std::numeric_limits<DWORD>::max()));
		if (!ReadFile(static_cast<HANDLE>(output_read_), bytes, chunk, &read, nullptr) || read == 0) {
			error = "SSH connection closed while reading";
			return false;
		}
#else
		const ssize_t read = ::read(output_read_, bytes, size);
		if (read < 0 && errno == EINTR) continue;
		if (read <= 0) {
			error = "SSH connection closed while reading";
			return false;
		}
#endif
		bytes += read;
		size -= read;
	}
	return true;
}

void SSHTransport::close() {
	{
		std::lock_guard<std::mutex> lock(monitor_mutex_);
		monitor_stop_ = true;
	}
	if (monitor_thread_.joinable()) monitor_thread_.join();

	std::lock_guard<std::mutex> lock(request_mutex_);
#ifdef _WIN32
	if (input_write_) {
		CloseHandle(static_cast<HANDLE>(input_write_));
		input_write_ = nullptr;
	}
	if (output_read_) {
		CloseHandle(static_cast<HANDLE>(output_read_));
		output_read_ = nullptr;
	}
	if (process_) {
		const DWORD result = WaitForSingleObject(static_cast<HANDLE>(process_), 250);
		if (result == WAIT_TIMEOUT) TerminateProcess(static_cast<HANDLE>(process_), 0);
		CloseHandle(static_cast<HANDLE>(process_));
		process_ = nullptr;
	}
#else
	if (input_write_ >= 0) {
		::close(input_write_);
		input_write_ = -1;
	}
	if (output_read_ >= 0) {
		::close(output_read_);
		output_read_ = -1;
	}
	if (process_id_ > 0) {
		int status = 0;
		const pid_t result = ::waitpid(process_id_, &status, WNOHANG);
		if (result == 0) {
			::kill(process_id_, SIGTERM);
			::waitpid(process_id_, &status, 0);
		}
		process_id_ = -1;
	}
#endif
}

bool SSHTransport::connected() const {
#ifdef _WIN32
	return process_ && input_write_ && output_read_;
#else
	return process_id_ > 0 && input_write_ >= 0 && output_read_ >= 0;
#endif
}

SSHFileBackend::SSHFileBackend(std::shared_ptr<SSHTransport> transport, SSHRemoteInfo info, SSHConnectionOptions options)
	: transport_(std::move(transport)), info_(std::move(info)), options_(std::move(options)) {
	std::ostringstream id;
	id << std::hex << std::hash<std::string>{}(options_.username + "@" + options_.hostname + ":" + std::to_string(options_.port));
	lsp_virtual_root_ = (std::filesystem::temp_directory_path() / "codewizard-ssh" / id.str()).string();
	std::error_code ignored;
	std::filesystem::create_directories(lsp_virtual_root_, ignored);
}

std::shared_ptr<SSHFileBackend> SSHFileBackend::connect(const SSHConnectionOptions& options, std::string& error) {
	auto transport = std::make_shared<SSHTransport>();
	SSHRemoteInfo info;
	if (!transport->start(options, info, error)) return nullptr;
	SSHConnectionOptions stored_options = options;
	stored_options.password.clear();
	return std::shared_ptr<SSHFileBackend>(
		new SSHFileBackend(std::move(transport), std::move(info), std::move(stored_options)));
}

std::string SSHFileBackend::displayName() const {
	const std::string target = options_.username.empty() ? options_.hostname : options_.username + "@" + options_.hostname;
	return target + (info_.hostname.empty() ? "" : " (" + info_.hostname + ")");
}

char SSHFileBackend::pathSeparator() const {
	return info_.os == "windows" ? '\\' : '/';
}

std::string SSHFileBackend::homeDirectory() const {
	return info_.home;
}

std::string SSHFileBackend::toLspPath(const std::string& path) const {
	std::string normalized = path;
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	std::filesystem::path virtual_path(lsp_virtual_root_);
	if (info_.os == "windows") {
		if (normalized.size() >= 2 && normalized[1] == ':') {
			virtual_path /= "drive-" + std::string(1, normalized[0]);
			normalized.erase(0, 2);
		} else {
			virtual_path /= "windows";
		}
	} else {
		virtual_path /= "posix";
	}
	while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
	virtual_path /= std::filesystem::path(normalized);
	return virtual_path.string();
}

std::string SSHFileBackend::fromLspPath(const std::string& path) const {
	std::string normalized_path = path;
	std::string normalized_root = lsp_virtual_root_;
	std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
	std::replace(normalized_root.begin(), normalized_root.end(), '\\', '/');
	if (normalized_path.rfind(normalized_root + "/", 0) != 0) return path;
	std::string relative = normalized_path.substr(normalized_root.size() + 1);
	if (relative.rfind("posix/", 0) == 0) return "/" + relative.substr(6);
	if (relative.rfind("drive-", 0) == 0 && relative.size() >= 8 && relative[7] == '/') {
		const char drive = relative[6];
		std::string result = std::string(1, drive) + ":\\" + relative.substr(8);
		std::replace(result.begin(), result.end(), '/', '\\');
		return result;
	}
	if (relative.rfind("windows/", 0) == 0) {
		std::string result = relative.substr(8);
		std::replace(result.begin(), result.end(), '/', '\\');
		return result;
	}
	return path;
}

bool SSHFileBackend::call(const std::string& method, const nlohmann::json& params, nlohmann::json& data, std::string& error) {
	return transport_->request(method, params, data, error);
}

void SSHFileBackend::setDisconnectCallback(std::function<void()> callback) {
	transport_->setDisconnectCallback(std::move(callback));
}

bool SSHFileBackend::readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
	nlohmann::json data;
	if (!call("file/read", {{"path", path}}, data, error)) return false;
	if (!base64Decode(data.value("content", ""), bytes)) {
		error = "cwremote returned invalid base64 file content";
		return false;
	}
	return true;
}

bool SSHFileBackend::writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
	nlohmann::json data;
	const bool success = call("file/write", {{"path", path}, {"content", base64Encode(bytes)}}, data, error);
	if (success) {
		std::lock_guard<std::mutex> lock(cache_mutex_);
		stat_cache_.erase(path);
	}
	return success;
}

bool SSHFileBackend::stat(const std::string& path, BackendFileStat& result, std::string& error) {
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		const auto found = stat_cache_.find(path);
		if (found != stat_cache_.end() &&
			std::chrono::steady_clock::now() - found->second.stored < std::chrono::seconds(2)) {
			result = found->second.value;
			return true;
		}
	}
	nlohmann::json data;
	if (!call("file/stat", {{"path", path}}, data, error)) return false;
	result.exists = data.value("exists", false);
	result.is_directory = data.value("isDir", false);
	result.size = data.value("size", std::uintmax_t{0});
	result.mtime = data.value("mtime", std::int64_t{0});
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		stat_cache_[path] = {result, std::chrono::steady_clock::now()};
	}
	return true;
}

bool SSHFileBackend::listDirectory(const std::string& path, std::vector<BackendDirectoryEntry>& entries, std::string& error) {
	nlohmann::json data;
	if (!call("dir/list", {{"path", path}}, data, error)) return false;
	entries.clear();
	for (const auto& item : data.value("entries", nlohmann::json::array())) {
		entries.push_back({
			item.value("name", ""),
			item.value("isDir", false),
			item.value("size", std::uintmax_t{0}),
			item.value("mtime", std::int64_t{0})
		});
	}
	return true;
}

bool SSHFileBackend::isBinary(const std::string& path, bool& result, std::string& error) {
	nlohmann::json data;
	if (!call("file/isBinary", {{"path", path}}, data, error)) return false;
	result = data.value("isBinary", true);
	return true;
}

bool SSHFileBackend::modificationTime(const std::string& path, std::int64_t& result, std::string& error) {
	nlohmann::json data;
	if (!call("file/mtime", {{"path", path}}, data, error)) return false;
	result = data.value("mtime", std::int64_t{0});
	return true;
}

bool SSHFileBackend::remove(const std::string& path, std::string& error) {
	nlohmann::json data;
	const bool success = call("file/delete", {{"path", path}}, data, error);
	if (success) {
		std::lock_guard<std::mutex> lock(cache_mutex_);
		stat_cache_.erase(path);
	}
	return success;
}

bool SSHFileBackend::rename(const std::string& old_path, const std::string& new_path, std::string& error) {
	nlohmann::json data;
	const bool success = call("file/rename", {{"oldPath", old_path}, {"newPath", new_path}}, data, error);
	if (success) {
		std::lock_guard<std::mutex> lock(cache_mutex_);
		stat_cache_.erase(old_path);
		stat_cache_.erase(new_path);
	}
	return success;
}

bool SSHFileBackend::createDirectories(const std::string& path, std::string& error) {
	nlohmann::json data;
	const bool success = call("file/mkdir", {{"path", path}}, data, error);
	if (success) {
		std::lock_guard<std::mutex> lock(cache_mutex_);
		stat_cache_.erase(path);
	}
	return success;
}

bool SSHFileBackend::scanFiles(const std::string& rootPath, std::size_t maxFiles,
							   std::vector<ScannedFile>& files, std::string& error) {
	nlohmann::json data;
	if (!call("file/scan", {{"path", rootPath}, {"maxFiles", maxFiles}}, data, error)) return false;
	files.clear();
	for (const auto& item : data.value("files", nlohmann::json::array())) {
		files.push_back({
			item.value("name", ""),
			item.value("path", "")
		});
	}
	return true;
}

bool SSHFileBackend::searchFiles(const std::vector<std::string>& filePaths,
								 const std::string& searchTerm,
								 std::vector<SearchedFile>& results, std::string& error) {
	nlohmann::json data;
	if (!call("file/search", {{"files", filePaths}, {"searchTerm", searchTerm}}, data, error)) return false;
	results.clear();
	for (const auto& item : data.value("results", nlohmann::json::array())) {
		SearchedFile sf;
		sf.path = item.value("path", "");
		for (const auto& match : item.value("matches", nlohmann::json::array())) {
			sf.matches.push_back({
				match.value("line", 0),
				match.value("content", "")
			});
		}
		results.push_back(std::move(sf));
	}
	return true;
}

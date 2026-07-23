#include "filebackend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

std::mutex FileBackends::mutex_;
std::shared_ptr<FileBackend> FileBackends::current_ = std::make_shared<LocalFileBackend>();

namespace {
std::int64_t unixSeconds(std::filesystem::file_time_type value) {
	const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
	return std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
}

std::string errorMessage(const std::error_code& ec) {
	return ec ? ec.message() : std::string{};
}

#ifdef _WIN32
std::wstring widenUtf8(const std::string& input) {
	if (input.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), length);
	return result;
}
#endif

bool atomicWriteLocal(const std::filesystem::path& target, const std::vector<std::uint8_t>& bytes, std::string& error) {
	std::error_code ec;
	if (!target.parent_path().empty()) {
		std::filesystem::create_directories(target.parent_path(), ec);
		if (ec) {
			error = "could not create parent directory: " + ec.message();
			return false;
		}
	}

#ifdef _WIN32
	const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
	const auto pid = static_cast<unsigned long>(::getpid());
#endif
	const auto temp = target.parent_path() / (target.filename().string() + ".cwtmp-" + std::to_string(pid));
	std::FILE* file = std::fopen(temp.string().c_str(), "wb");
	if (!file) {
		error = "could not open temporary file";
		return false;
	}
	if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
		error = "could not write temporary file";
		std::fclose(file);
		std::filesystem::remove(temp, ec);
		return false;
	}
	std::fflush(file);
#ifdef _WIN32
	_commit(_fileno(file));
#else
	fsync(fileno(file));
#endif
	std::fclose(file);

#ifdef _WIN32
	const auto source = widenUtf8(temp.string());
	const auto destination = widenUtf8(target.string());
	if (!ReplaceFileW(destination.c_str(), source.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) &&
		!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		error = "could not replace destination file";
		std::filesystem::remove(temp, ec);
		return false;
	}
#else
	std::filesystem::rename(temp, target, ec);
	if (ec) {
		error = "could not replace destination file: " + ec.message();
		std::filesystem::remove(temp, ec);
		return false;
	}
#endif
	return true;
}
} // namespace

bool FileBackend::exists(const std::string& path, bool& result, std::string& error) {
	BackendFileStat info;
	if (!stat(path, info, error)) return false;
	result = info.exists;
	return true;
}

bool FileBackend::isBinary(const std::string& path, bool& result, std::string& error) {
	std::vector<std::uint8_t> bytes;
	if (!readFile(path, bytes, error)) return false;
	const auto count = std::min<std::size_t>(bytes.size(), 4096);
	if (count == 0) {
		result = false;
		return true;
	}
	std::size_t controls = 0;
	for (std::size_t i = 0; i < count; ++i) {
		const auto c = bytes[i];
		if (c == 0) {
			result = true;
			return true;
		}
		if (c < 9 || (c > 13 && c < 32)) {
			++controls;
			if (controls > count / 100) {
				result = true;
				return true;
			}
		}
	}
	result = false;
	return true;
}

bool FileBackend::modificationTime(const std::string& path, std::int64_t& result, std::string& error) {
	BackendFileStat info;
	if (!stat(path, info, error) || !info.exists) {
		if (error.empty()) error = "file does not exist";
		return false;
	}
	result = info.mtime;
	return true;
}

bool FileBackend::fileSize(const std::string& path, std::uintmax_t& result, std::string& error) {
	BackendFileStat info;
	if (!stat(path, info, error) || !info.exists) {
		if (error.empty()) error = "file does not exist";
		return false;
	}
	result = info.size;
	return true;
}

std::string FileBackend::join(const std::string& parent, const std::string& child) const {
	if (parent.empty()) return child;
	if (child.empty()) return parent;
	const char separator = pathSeparator();
	if (parent.back() == '/' || parent.back() == '\\') return parent + child;
	return parent + separator + child;
}

std::string FileBackend::filename(const std::string& path) const {
	const auto position = path.find_last_of("/\\");
	return position == std::string::npos ? path : path.substr(position + 1);
}

char LocalFileBackend::pathSeparator() const {
	return std::filesystem::path::preferred_separator;
}

std::string LocalFileBackend::homeDirectory() const {
#ifdef _WIN32
	const char* home = std::getenv("USERPROFILE");
#else
	const char* home = std::getenv("HOME");
#endif
	return home ? home : std::string{};
}

bool LocalFileBackend::readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		error = "could not open file";
		return false;
	}
	const auto size = file.tellg();
	if (size < 0) {
		error = "could not determine file size";
		return false;
	}
	bytes.resize(static_cast<std::size_t>(size));
	file.seekg(0, std::ios::beg);
	if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
		error = "could not read file";
		bytes.clear();
		return false;
	}
	return true;
}

bool LocalFileBackend::writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
	return atomicWriteLocal(std::filesystem::path(path), bytes, error);
}

bool LocalFileBackend::stat(const std::string& path, BackendFileStat& result, std::string& error) {
	std::error_code ec;
	const auto status = std::filesystem::status(path, ec);
	if (ec == std::errc::no_such_file_or_directory) {
		result = {};
		return true;
	}
	if (ec) {
		error = errorMessage(ec);
		return false;
	}
	result.exists = std::filesystem::exists(status);
	result.is_directory = std::filesystem::is_directory(status);
	if (!result.exists) return true;
	if (!result.is_directory) {
		result.size = std::filesystem::file_size(path, ec);
		if (ec) ec.clear();
	}
	const auto modified = std::filesystem::last_write_time(path, ec);
	if (!ec) result.mtime = unixSeconds(modified);
	return true;
}

bool LocalFileBackend::listDirectory(const std::string& path, std::vector<BackendDirectoryEntry>& entries, std::string& error) {
	entries.clear();
	std::error_code ec;
	for (std::filesystem::directory_iterator iterator(path, ec), end; !ec && iterator != end; iterator.increment(ec)) {
		const auto& item = *iterator;
		BackendDirectoryEntry entry;
		entry.name = item.path().filename().string();
		entry.is_directory = item.is_directory(ec);
		if (ec) {
			ec.clear();
			continue;
		}
		if (!entry.is_directory) {
			entry.size = item.file_size(ec);
			if (ec) ec.clear();
		}
		const auto modified = item.last_write_time(ec);
		if (!ec) entry.mtime = unixSeconds(modified);
		else ec.clear();
		entries.push_back(std::move(entry));
	}
	if (ec) {
		error = errorMessage(ec);
		return false;
	}
	std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
		if (left.is_directory != right.is_directory) return left.is_directory > right.is_directory;
		return left.name < right.name;
	});
	return true;
}

bool LocalFileBackend::remove(const std::string& path, std::string& error) {
	std::error_code ec;
	const bool removed = std::filesystem::remove(path, ec);
	if (ec) error = errorMessage(ec);
	return removed && !ec;
}

bool LocalFileBackend::rename(const std::string& old_path, const std::string& new_path, std::string& error) {
	std::error_code ec;
	std::filesystem::rename(old_path, new_path, ec);
	if (ec) error = errorMessage(ec);
	return !ec;
}

bool LocalFileBackend::createDirectories(const std::string& path, std::string& error) {
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	if (ec) error = errorMessage(ec);
	return !ec;
}

std::shared_ptr<FileBackend> FileBackends::current() {
	std::lock_guard<std::mutex> lock(mutex_);
	return current_;
}

void FileBackends::use(std::shared_ptr<FileBackend> backend) {
	std::lock_guard<std::mutex> lock(mutex_);
	current_ = backend ? std::move(backend) : std::make_shared<LocalFileBackend>();
}

void FileBackends::useLocal() {
	use(std::make_shared<LocalFileBackend>());
}

bool FileBackends::isRemote() {
	return current()->isRemote();
}

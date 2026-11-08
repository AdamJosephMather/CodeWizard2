#include "filebackend.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
	if (!condition) throw std::runtime_error(message);
}

int main() {
	const auto folder = fs::temp_directory_path() /
		("cw-save-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(folder);
	try {
		LocalFileBackend backend;
		const auto target = folder / "document.txt";
		std::string error;
		auto save = [&](const std::vector<std::uint8_t>& bytes) {
			require(backend.writeFile(target.string(), bytes, error), "save failed: " + error);
			std::vector<std::uint8_t> actual;
			require(backend.readFile(target.string(), actual, error), "read failed: " + error);
			require(actual == bytes, "saved content differs");
			for (const auto& entry : fs::directory_iterator(folder)) {
				require(entry.path() == target, "save left temporary file: " + entry.path().string());
			}
		};
		save({'o', 'l', 'd'});
#ifdef _WIN32
		// Indexers and language servers can keep the previous version open.
		const HANDLE reader = CreateFileW(target.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
		require(reader != INVALID_HANDLE_VALUE, "could not open shared reader");
		const HANDLE mapping = CreateFileMappingW(reader, nullptr, PAGE_READONLY, 0, 0, nullptr);
		require(mapping != nullptr, "could not map shared reader");
		const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
		require(view != nullptr, "could not view shared reader");
		try {
			save({'n', 'e', 'w'});
		} catch (...) {
			UnmapViewOfFile(view);
			CloseHandle(mapping);
			CloseHandle(reader);
			throw;
		}
		UnmapViewOfFile(view);
		CloseHandle(mapping);
		CloseHandle(reader);

		// A reader denying deletion must leave the original intact on failure.
		const HANDLE blocker = CreateFileW(target.c_str(), GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		require(blocker != INVALID_HANDLE_VALUE, "could not open blocking reader");
		const bool saved = backend.writeFile(target.string(), {'b', 'a', 'd'}, error);
		bool clean = true;
		for (const auto& entry : fs::directory_iterator(folder)) clean &= entry.path() == target;
		CloseHandle(blocker);
		require(!saved, "save unexpectedly replaced a locked file");
		require(clean, "locked save left temporary file while reader was open");
		std::vector<std::uint8_t> actual;
		require(backend.readFile(target.string(), actual, error), "could not read after failed save");
		require(actual == std::vector<std::uint8_t>({'n', 'e', 'w'}), "failed save changed original");
		for (const auto& entry : fs::directory_iterator(folder)) {
			require(entry.path() == target, "failed save left temporary file");
		}
#endif
		for (int i = 0; i < 20; ++i) save({'x', 0, static_cast<std::uint8_t>(i)});
		save({});
		fs::remove_all(folder);
		std::cout << "Local file save tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		fs::remove_all(folder);
		return 1;
	}
}

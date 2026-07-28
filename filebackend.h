#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct BackendFileStat {
	std::uintmax_t size = 0;
	std::int64_t mtime = 0;
	bool is_directory = false;
	bool exists = false;
};

struct BackendDirectoryEntry {
	std::string name;
	bool is_directory = false;
	std::uintmax_t size = 0;
	std::int64_t mtime = 0;
};

struct ScannedFile {
	std::string name;
	std::string fullPath;
};

struct SearchMatchResult {
	int line;
	std::string content;
};

struct SearchedFile {
	std::string path;
	std::vector<SearchMatchResult> matches;
};

class FileBackend {
public:
	virtual ~FileBackend() = default;

	virtual bool isRemote() const { return false; }
	virtual std::string displayName() const { return "Local"; }
	virtual char pathSeparator() const = 0;
	virtual std::string homeDirectory() const = 0;
	virtual std::string toLspPath(const std::string& path) const { return path; }
	virtual std::string fromLspPath(const std::string& path) const { return path; }

	virtual bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) = 0;
	virtual bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) = 0;
	virtual bool stat(const std::string& path, BackendFileStat& result, std::string& error) = 0;
	virtual bool listDirectory(const std::string& path, std::vector<BackendDirectoryEntry>& entries, std::string& error) = 0;

	virtual bool remove(const std::string& path, std::string& error) = 0;
	virtual bool rename(const std::string& old_path, const std::string& new_path, std::string& error) = 0;
	virtual bool createDirectories(const std::string& path, std::string& error) = 0;

	virtual bool scanFiles(const std::string& rootPath, std::size_t maxFiles,
						   std::vector<ScannedFile>& files, std::string& error) { return false; }
	virtual bool searchFiles(const std::vector<std::string>& filePaths,
							 const std::string& searchTerm,
							 std::vector<SearchedFile>& results, std::string& error) { return false; }

	bool exists(const std::string& path, bool& result, std::string& error);
	virtual bool isBinary(const std::string& path, bool& result, std::string& error);
	virtual bool modificationTime(const std::string& path, std::int64_t& result, std::string& error);
	bool fileSize(const std::string& path, std::uintmax_t& result, std::string& error);

	std::string join(const std::string& parent, const std::string& child) const;
	std::string filename(const std::string& path) const;
};

class LocalFileBackend final : public FileBackend {
public:
	char pathSeparator() const override;
	std::string homeDirectory() const override;

	bool isBinary(const std::string& path, bool& result, std::string& error);
	bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) override;
	bool readFilePartial(const std::string& path, std::vector<std::uint8_t>& bytes, std::size_t maxBytes, std::string& error);
	bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) override;
	bool stat(const std::string& path, BackendFileStat& result, std::string& error) override;
	bool listDirectory(const std::string& path, std::vector<BackendDirectoryEntry>& entries, std::string& error) override;
	bool remove(const std::string& path, std::string& error) override;
	bool rename(const std::string& old_path, const std::string& new_path, std::string& error) override;
	bool createDirectories(const std::string& path, std::string& error) override;
};

// Owns the active backend. Local mode is always available and is the default.
class FileBackends {
public:
	static std::shared_ptr<FileBackend> current();
	static void use(std::shared_ptr<FileBackend> backend);
	static void useLocal();
	static bool isRemote();

private:
	static std::mutex mutex_;
	static std::shared_ptr<FileBackend> current_;
};

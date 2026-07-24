#pragma once

#include "filebackend.h"
#include "json.hpp"

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct SSHConnectionOptions {
	std::string hostname;
	std::string username;
	unsigned short port = 22;
	std::string key_path;
	std::string helper_path = "cwremote";
	std::string password;
};

struct SSHRemoteInfo {
	int protocol = 0;
	std::string os;
	std::string arch;
	std::string home;
	std::string cwd;
	std::string shell;
	std::string hostname;
};

class SSHTransport {
public:
	SSHTransport();
	~SSHTransport();
	SSHTransport(const SSHTransport&) = delete;
	SSHTransport& operator=(const SSHTransport&) = delete;

	bool start(const SSHConnectionOptions& options, SSHRemoteInfo& info, std::string& error);
	bool request(const std::string& method, const nlohmann::json& params, nlohmann::json& data, std::string& error);
	void close();
	bool connected() const;
	void setDisconnectCallback(std::function<void()> callback);

private:
	bool writeAll(const void* data, std::size_t size, std::string& error);
	bool readAll(void* data, std::size_t size, std::string& error);
	bool readHandshake(SSHRemoteInfo& info, std::string& error);
	void monitorProcess();
	void fireDisconnect();

	mutable std::mutex request_mutex_;
	std::uint64_t next_request_id_ = 1;
	std::function<void()> disconnect_callback_;
	std::thread monitor_thread_;
	bool monitor_stop_ = false;
	std::mutex monitor_mutex_;

#ifdef _WIN32
	void* process_ = nullptr;
	void* input_write_ = nullptr;
	void* output_read_ = nullptr;
#else
	int input_write_ = -1;
	int output_read_ = -1;
	int process_id_ = -1;
#endif
};

class SSHFileBackend final : public FileBackend {
public:
	static std::shared_ptr<SSHFileBackend> connect(const SSHConnectionOptions& options, std::string& error);
	~SSHFileBackend() override = default;

	bool isRemote() const override { return true; }
	std::string displayName() const override;
	char pathSeparator() const override;
	std::string homeDirectory() const override;
	std::string toLspPath(const std::string& path) const override;
	std::string fromLspPath(const std::string& path) const override;
	const SSHRemoteInfo& remoteInfo() const { return info_; }
	const SSHConnectionOptions& connectionOptions() const { return options_; }
	void setDisconnectCallback(std::function<void()> callback);

	bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) override;
	bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) override;
	bool stat(const std::string& path, BackendFileStat& result, std::string& error) override;
	bool listDirectory(const std::string& path, std::vector<BackendDirectoryEntry>& entries, std::string& error) override;
	bool isBinary(const std::string& path, bool& result, std::string& error) override;
	bool modificationTime(const std::string& path, std::int64_t& result, std::string& error) override;
	bool remove(const std::string& path, std::string& error) override;
	bool rename(const std::string& old_path, const std::string& new_path, std::string& error) override;
	bool createDirectories(const std::string& path, std::string& error) override;

	bool scanFiles(const std::string& rootPath, std::size_t maxFiles,
				   std::vector<ScannedFile>& files, std::string& error) override;
	bool searchFiles(const std::vector<std::string>& filePaths,
					 const std::string& searchTerm,
					 std::vector<SearchedFile>& results, std::string& error) override;

private:
	SSHFileBackend(std::shared_ptr<SSHTransport> transport, SSHRemoteInfo info, SSHConnectionOptions options);
	bool call(const std::string& method, const nlohmann::json& params, nlohmann::json& data, std::string& error);

	std::shared_ptr<SSHTransport> transport_;
	SSHRemoteInfo info_;
	SSHConnectionOptions options_;
	std::string lsp_virtual_root_;
	struct CachedStat {
		BackendFileStat value;
		std::chrono::steady_clock::time_point stored;
	};
	std::mutex cache_mutex_;
	std::unordered_map<std::string, CachedStat> stat_cache_;
};

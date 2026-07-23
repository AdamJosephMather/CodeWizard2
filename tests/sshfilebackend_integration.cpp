#include "../sshfilebackend.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
	std::string error;
	SSHConnectionOptions options;
	options.hostname = "integration-test";
	options.username = "codewizard";
	options.helper_path = "ignored-by-fake-ssh";
	options.password = "integration-secret";

	auto backend = SSHFileBackend::connect(options, error);
	if (!backend) {
		std::cerr << "connect: " << error << '\n';
		return 1;
	}
	if (std::getenv("CODEWIZARD_SSH_PASSWORD") != nullptr) {
		std::cerr << "SSH password leaked into the parent environment\n";
		return 2;
	}

	const auto root = std::filesystem::temp_directory_path() / "cwremote-client-integration";
	const auto path = root / "roundtrip.txt";
	const std::string expected = "CodeWizard SSH backend round trip\n";
	const std::vector<std::uint8_t> outgoing(expected.begin(), expected.end());
	if (!backend->writeFile(path.string(), outgoing, error)) {
		std::cerr << "write: " << error << '\n';
		return 3;
	}

	std::vector<std::uint8_t> incoming;
	if (!backend->readFile(path.string(), incoming, error) || incoming != outgoing) {
		std::cerr << "read: " << error << '\n';
		return 4;
	}

	BackendFileStat info;
	if (!backend->stat(path.string(), info, error) || !info.exists || info.size != outgoing.size()) {
		std::cerr << "stat: " << error << '\n';
		return 5;
	}
	const std::string lsp_path = backend->toLspPath(path.string());
	if (lsp_path == path.string() || backend->fromLspPath(lsp_path) != path.string()) {
		std::cerr << "LSP path mapping did not round trip\n";
		return 6;
	}

	std::vector<BackendDirectoryEntry> entries;
	if (!backend->listDirectory(root.string(), entries, error) || entries.size() != 1 ||
		entries.front().name != "roundtrip.txt") {
		std::cerr << "list: " << error << '\n';
		return 7;
	}

	if (!backend->remove(path.string(), error)) {
		std::cerr << "remove: " << error << '\n';
		return 8;
	}
	std::error_code ignored;
	std::filesystem::remove(root, ignored);
	std::cout << "SSHFileBackend integration test passed\n";
	return 0;
}

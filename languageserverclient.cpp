#include "codeedit.h"
#ifdef _WIN32
#include "languageserverclient.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include "languageserverclient.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <fstream>

// Global variables (keeping same structure as original)
std::string langID;
std::string missingNext;
std::string currentExecution;
std::string fileRootURI;
std::map<int, std::string> requestsMap;
json diagnostics;
bool quitting = false;
int shutdownId = -999;
bool alreadyDoneShutdownLoop = false;
bool failedToStart = false;
int initializeRequestId = -999;

// Process implementation
class Process::ProcessImpl {
public:
#ifdef _WIN32
	HANDLE hChildStdInRd = nullptr;
	HANDLE hChildStdInWr = nullptr;
	HANDLE hChildStdOutRd = nullptr;
	HANDLE hChildStdOutWr = nullptr;
	PROCESS_INFORMATION piProcInfo{};
	STARTUPINFO siStartInfo{};
#else
	pid_t pid = -1;
	int stdin_pipe[2] = {-1, -1};
	int stdout_pipe[2] = {-1, -1};
#endif
	std::string errorStr;
	std::atomic<bool> running{false};
	std::thread readerThread;
	std::string buffer;
	std::mutex bufferMutex;
};

Process::Process() : impl(std::make_unique<ProcessImpl>()) {}

Process::~Process() {
	terminate();
	if (impl->readerThread.joinable()) {
		impl->readerThread.join();
	}
}

bool Process::start(const std::string& program, const std::vector<std::string>& arguments) {
#ifdef _WIN32
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = nullptr;

	if (!CreatePipe(&impl->hChildStdOutRd, &impl->hChildStdOutWr, &saAttr, 0)) {
		impl->errorStr = "Failed to create stdout pipe";
		return false;
	}
	if (!CreatePipe(&impl->hChildStdInRd, &impl->hChildStdInWr, &saAttr, 0)) {
		impl->errorStr = "Failed to create stdin pipe";
		return false;
	}

	SetHandleInformation(impl->hChildStdOutRd, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(impl->hChildStdInWr, HANDLE_FLAG_INHERIT, 0);

	ZeroMemory(&impl->piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&impl->siStartInfo, sizeof(STARTUPINFO));
	impl->siStartInfo.cb = sizeof(STARTUPINFO);
	impl->siStartInfo.hStdError = impl->hChildStdOutWr;
	impl->siStartInfo.hStdOutput = impl->hChildStdOutWr;
	impl->siStartInfo.hStdInput = impl->hChildStdInRd;
	impl->siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	std::string cmdLine = program;
	for (const auto& arg : arguments) {
		cmdLine += " " + arg;
	}

	// Before CreateProcess, add:
	impl->siStartInfo.dwFlags |= STARTF_USESTDHANDLES  // you already have this
						   |  STARTF_USESHOWWINDOW;    // add this
	impl->siStartInfo.wShowWindow = SW_HIDE;           // hide any window
	
	DWORD creationFlags = CREATE_NO_WINDOW;             // suppress console window
	
	if (!CreateProcess(
			nullptr,
			const_cast<char*>(cmdLine.c_str()),
			nullptr, nullptr,
			TRUE,
			creationFlags,            // use CREATE_NO_WINDOW here
			nullptr, nullptr,
			&impl->siStartInfo,
			&impl->piProcInfo))
	{
		impl->errorStr = "Failed to create process";
		return false;
	}

	CloseHandle(impl->hChildStdOutWr);
	CloseHandle(impl->hChildStdInRd);
	impl->running = true;

	// Start reader thread
	impl->readerThread = std::thread([this]() {
		char buffer[4096];
		DWORD bytesRead;
		while (impl->running && ReadFile(impl->hChildStdOutRd, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
			{
				std::lock_guard<std::mutex> lock(impl->bufferMutex);
				impl->buffer.append(buffer, bytesRead);
			}
			if (readyReadCallback) {
				readyReadCallback();
			}
		}
	});

	return true;
#else
	if (pipe(impl->stdin_pipe) == -1 || pipe(impl->stdout_pipe) == -1) {
		impl->errorStr = "Failed to create pipes";
		return false;
	}
	
	impl->pid = fork();
	if (impl->pid == -1) {
		impl->errorStr = "Failed to fork process";
		return false;
	}
	
	if (impl->pid == 0) {
		// Child process
		close(impl->stdin_pipe[1]);
		close(impl->stdout_pipe[0]);
		dup2(impl->stdin_pipe[0], STDIN_FILENO);
		dup2(impl->stdout_pipe[1], STDOUT_FILENO);
		dup2(impl->stdout_pipe[1], STDERR_FILENO);
		close(impl->stdin_pipe[0]);
		close(impl->stdout_pipe[1]);

		std::vector<char*> args;
		args.push_back(const_cast<char*>(program.c_str()));
		for (const auto& arg : arguments) {
			args.push_back(const_cast<char*>(arg.c_str()));
		}
		args.push_back(nullptr);

		execvp(program.c_str(), args.data());
		exit(1);
	} else {
		// Parent process
		close(impl->stdin_pipe[0]);
		close(impl->stdout_pipe[1]);
		
		// Make stdout non-blocking
		int flags = fcntl(impl->stdout_pipe[0], F_GETFL, 0);
		fcntl(impl->stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);
		
		impl->running = true;

		// Start reader thread
		impl->readerThread = std::thread([this]() {
			char buffer[4096];
			while (impl->running) {
				pollfd pfd = {impl->stdout_pipe[0], POLLIN, 0};
				int result = poll(&pfd, 1, 100); // 100ms timeout
				
				if (result > 0 && (pfd.revents & POLLIN)) {
					ssize_t bytesRead = read(impl->stdout_pipe[0], buffer, sizeof(buffer));
					if (bytesRead > 0) {
						{
							std::lock_guard<std::mutex> lock(impl->bufferMutex);
							impl->buffer.append(buffer, bytesRead);
						}
						if (readyReadCallback) {
							readyReadCallback();
						}
					} else if (bytesRead == 0) {
						break; // EOF
					}
				}
			}
		});
	}
	return true;
#endif
}

void Process::write(const std::string& data) {
#ifdef _WIN32
	DWORD bytesWritten;
	WriteFile(impl->hChildStdInWr, data.c_str(), data.length(), &bytesWritten, nullptr);
#else
	::write(impl->stdin_pipe[1], data.c_str(), data.length());
#endif
}

std::string Process::readAll() {
	std::lock_guard<std::mutex> lock(impl->bufferMutex);
	std::string result = impl->buffer;
	impl->buffer.clear();
	return result;
}

int Process::bytesAvailable() {
	std::lock_guard<std::mutex> lock(impl->bufferMutex);
	return impl->buffer.length();
}

void Process::terminate() {
	impl->running = false;
#ifdef _WIN32
	if (impl->piProcInfo.hProcess) {
		TerminateProcess(impl->piProcInfo.hProcess, 0);
	}
#else
	if (impl->pid > 0) {
		kill(impl->pid, SIGTERM);
	}
#endif
}

void Process::kill() {
#ifdef _WIN32
	if (impl->piProcInfo.hProcess) {
		TerminateProcess(impl->piProcInfo.hProcess, 9);
	}
#else
	if (impl->pid > 0) {
		::kill(impl->pid, SIGKILL);
	}
#endif
}

bool Process::waitForStarted(int timeout) {
	// Simple implementation - just check if process started
	return impl->running;
}

bool Process::waitForFinished(int timeout) {
#ifdef _WIN32
	if (impl->piProcInfo.hProcess) {
		DWORD result = WaitForSingleObject(impl->piProcInfo.hProcess, timeout);
		return result == WAIT_OBJECT_0;
	}
#else
	if (impl->pid > 0) {
		int status;
		pid_t result = waitpid(impl->pid, &status, WNOHANG);
		return result == impl->pid;
	}
#endif
	return false;
}

void Process::close() {
#ifdef _WIN32
	if (impl->hChildStdInWr) CloseHandle(impl->hChildStdInWr);
	if (impl->hChildStdOutRd) CloseHandle(impl->hChildStdOutRd);
	if (impl->piProcInfo.hProcess) CloseHandle(impl->piProcInfo.hProcess);
	if (impl->piProcInfo.hThread) CloseHandle(impl->piProcInfo.hThread);
#else
	if (impl->stdin_pipe[1] != -1) ::close(impl->stdin_pipe[1]);
	if (impl->stdout_pipe[0] != -1) ::close(impl->stdout_pipe[0]);
#endif
}

std::string Process::errorString() const {
	return impl->errorStr;
}

// URL conversion functions
std::string LanguageServerClient::fromLocalFile(const std::string& path) {
#ifdef _WIN32
	std::string result = "file:///" + path;
	std::replace(result.begin(), result.end(), '\\', '/');
	return result;
#else
	return "file://" + path;
#endif
}

// LanguageServerClient implementation
LanguageServerClient::LanguageServerClient(const std::string &serverPath, std::function<void(const std::string&)> lg)
	: requestId(0), documentVersion(1)
{
	logCallback = lg;
	
	lspPath = serverPath;

	langID = "";
	missingNext = "";
	currentExecution = "";
	fileRootURI = "";
	requestsMap.clear();
	diagnostics = json::array();
	quitting = false;
	shutdownId = -999;
	alreadyDoneShutdownLoop = false;
	initializeRequestId = -999;
	failedToStart = false;

	serverProcess.readyReadCallback = [this]() { onServerReadyRead(); };
	serverProcess.errorCallback = [this](Process::ProcessError error) { onServerErrorOccurred(error); };
	serverProcess.finishedCallback = [this](int exitCode, Process::ExitStatus exitStatus) { onServerFinished(exitCode, exitStatus); };
	
#ifdef _WIN32
	if (!serverProcess.start("cmd", {"/c", serverPath})) {
#else
	if (!serverProcess.start("/bin/sh", {"-c", serverPath})) {
#endif
		failedToStart = true;
		if (logCallback) {
			logCallback("Failed to start language server at: " + serverPath + "\nError: " + serverProcess.errorString());
		}
	}
}

void LanguageServerClient::onServerErrorOccurred(Process::ProcessError error)
{
	if (logCallback) {
		logCallback("Server error occurred: " + std::to_string(static_cast<int>(error)) + "\nError String: " + serverProcess.errorString());
	}
	initializeComplete = true;
	initializeCondition.notify_all();
	failedToStart = true;
}

void LanguageServerClient::onServerFinished(int exitCode, Process::ExitStatus exitStatus)
{
	if (logCallback) {
		logCallback("Server process finished with exit code: " + std::to_string(exitCode) + " and status: " + std::to_string(static_cast<int>(exitStatus)));
	}
	initializeComplete = true;
	initializeCondition.notify_all();
	failedToStart = true;
}

LanguageServerClient::~LanguageServerClient()
{
	shutdown();
}

void LanguageServerClient::initialize(const std::string &rootUri)
{
	fileRootURI = fromLocalFile(rootUri);

	json capabilities = {
		{"textDocument", {
			{"completion", {
				{"completionItem", {
					{"snippetSupport", true},
					{"resolveSupport", {
						{"properties", {"documentation", "detail", "additionalTextEdits"}}
					}}
				}}
			}},
			{"synchronization", {
				{"dynamicRegistration", true},
				{"didSave", true},
				{"didChange", true},
				{"willSave", false}
			}},
			{"publishDiagnostics", {
				{"enabled", true}
			}}
		}},
		{"completionProvider", {
			{"resolveProvider", true},
			{"triggerCharacters", {".", ":", ">", "<", "/", "@", "*", "(", "[", "{", "'", "\"", "#"}}
		}},
		{"workspace", {
			{"workspaceFolders", true},
			{"configuration", true}
		}}
	};

	json params = {
		{"rootUri", fileRootURI},
		{"capabilities", capabilities},
		{"clientInfo", {
			{"name", "CodeWizardLSP"},
			{"version", "1.0.0"}
		}},
		{"workspaceFolders", {{
			{"uri", fileRootURI},
			{"name", "workspace"}
		}}}
	};

	initializeRequestId = requestId++;
	json message = {
		{"jsonrpc", "2.0"},
		{"id", initializeRequestId},
		{"method", "initialize"},
		{"params", params}
	};

	sendMessage(message);

	if (failedToStart) {
		if (logCallback) {
			logCallback("Failed to start LSP - Ensure it's accessible via the command given.");
		}
		return;
	}

	// Wait for initialization
	std::unique_lock<std::mutex> lock(initializeMutex);
	initializeCondition.wait(lock, [this] { return initializeComplete.load(); });

	if (failedToStart) {
		if (logCallback) {
			logCallback("Failed to start LSP - Ensure it's accessible via the command given.");
		}
		return;
	}

	json initializedMessage = {
		{"jsonrpc", "2.0"},
		{"method", "initialized"},
		{"params", json::object()}
	};

	sendMessage(initializedMessage);

	json params2 = {
		{"settings", {
			{"python", json::object()}
		}}
	};

	json workspaceChanged = {
		{"jsonrpc", "2.0"},
		{"method", "workspace/didChangeConfiguration"},
		{"params", params2}
	};

	sendMessage(workspaceChanged);

	isInitialized = true;
}

void LanguageServerClient::shutdown()
{
	if (failedToStart) {
		return;
	}

	serverProcess.close();
	serverProcess.terminate();

	if (!serverProcess.waitForFinished(500)) {
		serverProcess.kill();
		serverProcess.waitForFinished();
	}
}

void LanguageServerClient::openDocument(const std::string &uri, const std::string &languageId, const std::string &content)
{
	langID = languageId;
	std::string fileURI = fromLocalFile(uri);

	json textDocument = {
		{"uri", fileURI},
		{"languageId", languageId},
		{"version", documentVersion},
		{"text", content}
	};

	json params = {
		{"textDocument", textDocument}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"method", "textDocument/didOpen"},
		{"params", params}
	};

	sendMessage(message);
}

void LanguageServerClient::closeDocument(const std::string &uri)
{
	std::string fileURI = fromLocalFile(uri);

	json textDocument = {
		{"uri", fileURI}
	};

	json params = {
		{"textDocument", textDocument}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"method", "textDocument/didClose"},
		{"params", params}
	};

	sendMessage(message);
}

void LanguageServerClient::updateDocument(const std::string &uri, const std::string &content)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI},
		{"version", ++documentVersion}
	};

	json contentChanges = json::array();
	contentChanges.push_back({{"text", content}});

	json params = {
		{"textDocument", textDocument},
		{"contentChanges", contentChanges}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"method", "textDocument/didChange"},
		{"params", params}
	};

	sendMessage(message);
}

void LanguageServerClient::documentSaved(const std::string& uri, const std::string& text)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI},
		{"languageId", langID},
		{"version", ++documentVersion}
	};

	json params = {
		{"textDocument", textDocument},
		{"text", text}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"method", "textDocument/didSave"},
		{"params", params}
	};

	sendMessage(message);
}

int LanguageServerClient::requestCompletion(const std::string& uri, int line, int character)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI}
	};

	json position = {
		{"line", line},
		{"character", character}
	};

	json context = {
		{"triggerKind", 1}
	};

	json params = {
		{"textDocument", textDocument},
		{"position", position},
		{"context", context}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"id", requestId++},
		{"method", "textDocument/completion"},
		{"params", params}
	};

	requestsMap[requestId-1] = "textDocument/completion";
	sendMessage(message);
	return requestId-1;
}

int LanguageServerClient::requestHover(const std::string& uri, int line, int character)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI}
	};

	json position = {
		{"line", line},
		{"character", character}
	};

	json params = {
		{"textDocument", textDocument},
		{"position", position}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"id", requestId++},
		{"method", "textDocument/hover"},
		{"params", params}
	};

	requestsMap[requestId-1] = "textDocument/hover";
	sendMessage(message);
	return requestId-1;
}

int LanguageServerClient::requestActions(const std::string& uri, int line, int character, int line2, int character2)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI}
	};

	json range = {
		{"start", {{"line", line}, {"character", character}}},
		{"end", {{"line", line2}, {"character", character2}}}
	};

	json context = {
		{"diagnostics", filterDiagnostics(diagnostics, line, character, line2, character2)}
	};

	json params = {
		{"textDocument", textDocument},
		{"range", range},
		{"context", context}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"id", requestId++},
		{"method", "textDocument/codeAction"},
		{"params", params}
	};

	requestsMap[requestId-1] = "textDocument/codeAction";
	sendMessage(message);
	
	return requestId-1;
}

int LanguageServerClient::requestRename(const std::string& uri, int line, int character, const std::string& newName)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI}
	};

	json position = {
		{"line", line},
		{"character", character}
	};

	json params = {
		{"textDocument", textDocument},
		{"position", position},
		{"newName", newName}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"id", requestId++},
		{"method", "textDocument/rename"},
		{"params", params}
	};

	requestsMap[requestId-1] = "textDocument/rename";
	sendMessage(message);
	return requestId-1;
}

json LanguageServerClient::filterDiagnostics(const json &diagnostics, int lineStart, int columnStart, int lineEnd, int columnEnd)
{
	json filteredDiagnostics = json::array();

	for (const auto &diagnostic : diagnostics) {
		if (diagnostic.contains("range")) {
			auto range = diagnostic["range"];
			auto start = range["start"];
			auto end = range["end"];

			int diagStartLine = start["line"];
			int diagStartColumn = start["character"];
			int diagEndLine = end["line"];
			int diagEndColumn = end["character"];

			if ((diagEndLine >= lineStart && diagStartLine <= lineEnd) &&
				(diagEndColumn >= columnStart && diagStartColumn <= columnEnd)) {
				filteredDiagnostics.push_back(diagnostic);
			}
		}
	}

	return filteredDiagnostics;
}

int LanguageServerClient::requestGotoDefinition(const std::string& uri, int line, int character)
{
	std::string fileURI = fromLocalFile(uri);
	
	json textDocument = {
		{"uri", fileURI}
	};

	json position = {
		{"line", line},
		{"character", character}
	};

	json params = {
		{"textDocument", textDocument},
		{"position", position}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"id", requestId++},
		{"method", "textDocument/definition"},
		{"params", params}
	};

	requestsMap[requestId-1] = "textDocument/definition";
	sendMessage(message);
	return requestId-1;
}

void LanguageServerClient::onServerReadyRead()
{
	while (serverProcess.bytesAvailable() > 0 || currentExecution.find("\r\n") != std::string::npos || currentExecution.find("Content-Length") != std::string::npos) {
		json response = readMessage();
		if (response.is_null() || response.empty()) {
			continue;
		}

		if (response.contains("method") && response["method"] == "window/logMessage") {
			continue;
		}

		int id = response.contains("id") ? response["id"].get<int>() : -1;

		if (id == shutdownId && (!response.contains("method") || response["method"].is_null())) {
			alreadyDoneShutdownLoop = true;
			shutdownComplete = true;
			shutdownCondition.notify_all();
			continue;
		}

		// Handle initialize response
		if (!isInitialized && response.contains("result") && response["result"].contains("capabilities")) {
			auto serverCapabilities = response["result"]["capabilities"];

			triggerChars.clear();
			if (serverCapabilities.contains("completionProvider") && 
				serverCapabilities["completionProvider"].contains("triggerCharacters")) {
				auto triggerCharsArr = serverCapabilities["completionProvider"]["triggerCharacters"];
				for (const auto& value : triggerCharsArr) {
					if (value.is_string()) {
						triggerChars.push_back(value.get<std::string>());
					}
				}
			}
			triggerChars.push_back("_");

			if (logCallback) {
				std::string triggerCharsStr = "TRIGGER CHARACTERS: ";
				for (const auto& ch : triggerChars) {
					triggerCharsStr += ch + " ";
				}
				logCallback(triggerCharsStr);
			}

			initializeComplete = true;
			initializeCondition.notify_all();
			if (initializeResponseReceivedCallback) {
				initializeResponseReceivedCallback();
			}
			continue;
		}

		if (!isInitialized && id == initializeRequestId && (!response.contains("method") || response["method"].is_null())) {
			initializeComplete = true;
			initializeCondition.notify_all();
			if (initializeResponseReceivedCallback) {
				initializeResponseReceivedCallback();
			}
			continue;
		}

		if (response.contains("error")) {
			if (logCallback) {
				logCallback("Error in response: " + response.dump());
			}
			continue;
		}

		if (response.contains("method") && response["method"] == "workspace/configuration") {
			json configResponse = {
				{"jsonrpc", "2.0"},
				{"id", id},
				{"result", json::array({json::object()})}
			};
			sendMessage(configResponse);
			continue;
		}

		if (response.contains("method") && response["method"] == "textDocument/publishDiagnostics") {
			std::string responseUri = response["params"]["uri"].get<std::string>();
			std::transform(responseUri.begin(), responseUri.end(), responseUri.begin(), ::tolower);
			
			auto items = response["params"]["diagnostics"];
			std::string file = response["params"]["uri"];
			
			diagnostics = items;
			std::vector<std::string> messages;
			std::vector<int> startC, startL, endC, endL, severity;

			for (const auto &item : items) {
				messages.push_back(item["message"].get<std::string>());
				startC.push_back(item["range"]["start"]["character"].get<int>());
				startL.push_back(item["range"]["start"]["line"].get<int>());
				endC.push_back(item["range"]["end"]["character"].get<int>());
				endL.push_back(item["range"]["end"]["line"].get<int>());
				severity.push_back(item["severity"].get<int>());
			}

			// Create a list of indices to sort the severity list
			std::vector<int> indices(severity.size());
			std::iota(indices.begin(), indices.end(), 0);

			// Sort indices based on severity values
			std::sort(indices.begin(), indices.end(), [&](int a, int b) {
				return severity[a] > severity[b];
			});

			// Create sorted versions of the other lists based on the sorted indices
			std::vector<std::string> sortedMessages;
			std::vector<int> sortedStartC, sortedStartL, sortedEndC, sortedEndL, sortedSeverity;

			for (int index : indices) {
				sortedMessages.push_back(messages[index]);
				sortedStartC.push_back(startC[index]);
				sortedStartL.push_back(startL[index]);
				sortedEndC.push_back(endC[index]);
				sortedEndL.push_back(endL[index]);
				sortedSeverity.push_back(severity[index]);
			}
			
			for (auto w : connected_edits) {
				if (auto ce = dynamic_cast<CodeEdit*>(w)) {
					ce->publishDiagnostics(file, sortedMessages, sortedStartC, sortedStartL, sortedEndC, sortedEndL, sortedSeverity);
				}
			}

			if (receivedDiagnosticsCallback) {
				receivedDiagnosticsCallback(sortedMessages, sortedStartC, sortedStartL, sortedEndC, sortedEndL, sortedSeverity);
			}
		} else if (response.contains("method") && (response["method"] == "window/showMessage" || response["method"] == "client/registerCapability")) {
			continue;
		}

		std::string responseType = "";
		if (requestsMap.find(id) != requestsMap.end()) {
			responseType = requestsMap[id];
		}

		json result = response.contains("result") ? response["result"] : json::object();

		if (responseType == "textDocument/hover") {
			std::string contents = "";
			std::string type = "";
			
			if (result.contains("contents")) {
				if (result["contents"].contains("value")) {
					contents = result["contents"]["value"].get<std::string>();
				}
				if (result["contents"].contains("kind")) {
					type = result["contents"]["kind"].get<std::string>();
				}
			}

			if (!contents.empty()) {
				// Replace triple newlines with double newlines
				size_t pos = 0;
				while ((pos = contents.find("\n\n\n", pos)) != std::string::npos) {
					contents.replace(pos, 3, "\n\n");
					pos += 2;
				}
				
				if (hoverInformationReceivedCallback) {
					hoverInformationReceivedCallback(contents, type, id);
				}
				
				for (auto w : connected_edits) {
					if (auto ce = dynamic_cast<CodeEdit*>(w)) {
						ce->hoverRecieved(contents, type, id);
					}
				}
			}
			continue;
		} else if (responseType == "textDocument/codeAction") {
			if (response.contains("result")) {
				for (auto w : connected_edits) {
					if (auto ce = dynamic_cast<CodeEdit*>(w)) {
						ce->actionsReceived(id, response["result"]);
					}
				}
				
				if (codeActionsReceivedCallback) {
					codeActionsReceivedCallback(response["result"]);
				}
			}
			continue;
		} else if (responseType == "textDocument/definition") {
			if (response.contains("result") && response["result"].is_array() && !response["result"].empty()) {
				auto array = response["result"];
				auto location = array[0]["range"]["end"];
				int line = location["line"].get<int>();
				int character = location["character"].get<int>();

				location = array[0]["range"]["start"];
				int line1 = location["line"].get<int>();
				int character1 = location["character"].get<int>();

				std::string locFile = array[0]["uri"].get<std::string>();

				if (gotoDefinitionsReceivedCallback) {
					gotoDefinitionsReceivedCallback(line1, character1, line, character, locFile);
				}
				
				for (auto w : connected_edits) {
					if (auto ce = dynamic_cast<CodeEdit*>(w)) {
						ce->gotoDef(id, line1, character1, line, character, locFile);
					}
				}
			}
			continue;
		} else if (responseType == "textDocument/rename") {
			for (auto w : connected_edits) {
				if (auto ce = dynamic_cast<CodeEdit*>(w)){
					ce->renameReceived(id, result);
				}
			}
			if (renameReceivedCallback) {
				renameReceivedCallback(result);
			}
		} else if (responseType == "textDocument/completion") {
			auto items = result.contains("items") ? result["items"] : json::array();

			std::vector<std::string> completions;
			std::vector<std::string> sortTexts;

			for (const auto &item : items) {
				std::string newStr = "";
				
				if (item.contains("insertText")) {
					newStr = item["insertText"].get<std::string>();
				} else if (item.contains("textEdit")) {
					newStr = item["textEdit"]["newText"].get<std::string>();
				} else if (item.contains("label")) {
					newStr = item["label"].get<std::string>();
				}

				if (!newStr.empty() && std::find(completions.begin(), completions.end(), newStr) == completions.end()) {
					completions.push_back(newStr);
					if (item.contains("sortText")) {
						sortTexts.push_back(item["sortText"].get<std::string>());
					}
				}

			}

			if (sortTexts.size() == completions.size()) {
				std::vector<std::pair<std::string, std::string>> combined;
				for (size_t i = 0; i < completions.size(); ++i) {
					combined.push_back(std::make_pair(sortTexts[i], completions[i]));
				}

				std::sort(combined.begin(), combined.end(), [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
					return a.first < b.first;
				});

				std::vector<std::string> outLst;
				for (const auto& pair : combined) {
					outLst.push_back(pair.second);
				}

				if (completionReceivedCallback) {
					completionReceivedCallback(outLst, id);
				}
				
				for (auto w : connected_edits) {
					if (auto ce = dynamic_cast<CodeEdit*>(w)) {
						ce->completionRecieved(outLst, id);
					}
				}
			} else {
				if (completionReceivedCallback) {
					completionReceivedCallback(completions, id);
				}
			}
			continue;
		}
	}
}

void LanguageServerClient::sendMessage(const json &message)
{
	std::thread writer([this, message]() {
		std::lock_guard<std::mutex> lk(writeMutex);
		
		std::string data   = message.dump();
		std::string header = "Content-Length: " + std::to_string(data.size()) + "\r\n\r\n";
		serverProcess.write(header + data);
	});

	// Detach so we don’t have to join later
	writer.detach();
}

void LanguageServerClient::changeFolder(const std::string& oldUri, const std::string& newUri)
{
	std::string newName = "project";
	
	json params = {
		{"event", {
			{"added", {{
				{"uri", fromLocalFile(newUri)},
				{"name", newName}
			}}},
			{"removed", {{
				{"uri", fromLocalFile(oldUri)},
				{"name", "previous-folder"}
			}}}
		}}
	};

	json message = {
		{"jsonrpc", "2.0"},
		{"method", "workspace/didChangeWorkspaceFolders"},
		{"params", params}
	};

	sendMessage(message);
}

json LanguageServerClient::readMessage()
{
	std::string newData = serverProcess.readAll();
	
//	if (logCallback) {
//		logCallback("Okay got data back...\n" + newData + "\n");
//	}
	
	if (!newData.empty()) {
		currentExecution += newData;
	}

	size_t index = currentExecution.find("Content-Length");

	if (index != std::string::npos) {
		std::string part1 = currentExecution.substr(0, index);
		currentExecution = currentExecution.substr(index + 14);

		try {
			json doc = json::parse(part1);
			return doc;
		} catch (const json::parse_error&) {
			// Continue processing
		}
	}

	index = currentExecution.find("\r\n");

	if (index != std::string::npos) {
		std::string part1 = currentExecution.substr(0, index);
		currentExecution = currentExecution.substr(index + 2);

		try {
			json doc = json::parse(part1);
			return doc;
		} catch (const json::parse_error&) {
			// Continue processing
		}
	}

	try {
		json doc = json::parse(currentExecution);
		currentExecution = "";
		return doc;
	} catch (const json::parse_error&) {
		// Return empty json
	}

	return json();
}
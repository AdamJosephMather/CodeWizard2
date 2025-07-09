#ifndef LANGUAGESERVERCLIENT_H
#define LANGUAGESERVERCLIENT_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Forward declarations for JSON library (assuming nlohmann/json or similar)
#include "json.hpp"
#include "widget.h"
using json = nlohmann::json;

// Process wrapper class
class Process {
public:
	Process();
	~Process();
	
	bool start(const std::string& program, const std::vector<std::string>& arguments);
	void write(const std::string& data);
	std::string readAll();
	int bytesAvailable();
	void terminate();
	void kill();
	bool waitForStarted(int timeout = 30000);
	bool waitForFinished(int timeout = 30000);
	void close();
	std::string errorString() const;
	
	enum ProcessError {
		FailedToStart,
		Crashed,
		Timedout,
		WriteError,
		ReadError,
		UnknownError
	};
	
	enum ExitStatus {
		NormalExit,
		CrashExit
	};
	
	std::function<void()> readyReadCallback;
	std::function<void(ProcessError)> errorCallback;
	std::function<void(int, ExitStatus)> finishedCallback;
	
private:
	class ProcessImpl;
	std::unique_ptr<ProcessImpl> impl;
};

class LanguageServerClient
{
public:
	explicit LanguageServerClient(const std::string &serverPath, std::function<void(const std::string&)> logCallback = nullptr);
	~LanguageServerClient();

	void initialize(const std::string &rootUri);
	void openDocument(const std::string &uri, const std::string &languageId, const std::string &content);
	void closeDocument(const std::string &uri);
	void updateDocument(const std::string &uri, const std::string &content);
	int requestCompletion(const std::string &uri, int line, int character);
	int requestHover(const std::string &uri, int line, int character);
	int requestGotoDefinition(const std::string &uri, int line, int character);
	int requestActions(const std::string &uri, int line, int character, int line2, int character2);
	int requestRename(const std::string &uri, int line, int character, const std::string& newName);
	void documentSaved(const std::string &uri, const std::string& text);
	void shutdown();
	void changeFolder(const std::string& oldUri, const std::string& newUri);
	
	std::vector<Widget*> connected_edits = {};

	bool isInitialized = false;
	std::string lspPath;
	std::vector<std::string> triggerChars;

	// Callback functions to replace Qt signals
	std::function<void(const std::vector<std::string>&, int)> completionReceivedCallback;
	std::function<void(int, int, int, int, const std::string&)> gotoDefinitionsReceivedCallback;
	std::function<void(const std::vector<std::string>&, const std::vector<int>&, const std::vector<int>&, 
					  const std::vector<int>&, const std::vector<int>&, const std::vector<int>&)> receivedDiagnosticsCallback;
	std::function<void(const std::string&)> serverErrorOccurredCallback;
	std::function<void(int, Process::ExitStatus)> serverFinishedCallback;
	std::function<void()> initializeResponseReceivedCallback;
	std::function<void(const std::string&, const std::string&, int)> hoverInformationReceivedCallback;
	std::function<void(const json&)> codeActionsReceivedCallback;
	std::function<void(const json&)> renameReceivedCallback;
	
	std::string fromLocalFile(const std::string& path);

private:
	void onServerReadyRead();
	void onServerErrorOccurred(Process::ProcessError error);
	void onServerFinished(int exitCode, Process::ExitStatus exitStatus);
	
	json filterDiagnostics(const json &diagnostics, int lineStart, int columnStart, int lineEnd, int columnEnd);
	void sendMessage(const json &message);
	json readMessage();

	Process serverProcess;
	int requestId;
	int documentVersion;
	
	// Replace QEventLoop with condition variables
	std::mutex initializeMutex;
	std::condition_variable initializeCondition;
	std::atomic<bool> initializeComplete{false};
	
	std::mutex shutdownMutex;
	std::condition_variable shutdownCondition;
	std::atomic<bool> shutdownComplete{false};
	
	std::function<void(const std::string&)> logCallback;
};

#endif // LANGUAGESERVERCLIENT_H
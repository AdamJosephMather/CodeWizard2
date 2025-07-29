#ifndef CURLER_H
#define CURLER_H

#include <string>
#include <vector>
#include <functional>

class Curler {
public:
	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

	static std::string run_curl(std::string url, std::string json, bool& worked);

	static std::string getInsertion(const std::string& before, const std::string& after);

	static std::string StreamInsertion(
		const std::string& before,
		const std::string& after,
		std::function<void(const std::string&)> onChunk
	);
	
	static void loadModel();
	static std::string StreamChatResponse(
		const std::vector<std::pair<bool, std::string>>& messages,
		std::function<void(const std::string&)> stream_callback
	);
};

#endif // CURLER_H
#include <string>
#include <vector>
#include <functional>

class Curler {
	public:
	
	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
	static std::string run_curl(std::string url, std::string data, bool& worked);
	static std::string getInsertion(std::string before, std::string after);
	static void loadModel();
	static std::string StreamChatResponse(const std::vector<std::pair<bool, std::string>>& messages, std::function<void(const std::string&)> stream_callback);
};
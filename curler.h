#include <string>

class Curler {
	public:
	
	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
	static std::string run_curl(std::string url, std::string data, bool& worked);
	static std::string getInsertion(std::string before, std::string after);
	static void loadModel();
};
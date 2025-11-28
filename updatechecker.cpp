#include "updatechecker.h"

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "json.hpp"
#include <sstream>

// Using declarations for convenience
using json = nlohmann::json;

// libcurl needs a callback function to write the received data into a string
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

bool UpdateChecker::parseVersionString(const std::string& version_str, int& major, int& minor, int& patch) {
	std::stringstream ss(version_str);
	char v_char, dot1, dot2;

	if (ss >> v_char && v_char == 'v' &&
		ss >> major &&
		ss >> dot1 && dot1 == '.' &&
		ss >> minor &&
		ss >> dot2 && dot2 == '.' &&
		ss >> patch &&
		ss.eof()) // This ensures the string ends *exactly* after the patch number
	{
		return true; // All parts parsed successfully
	}
	
	// If any part of the chain fails (wrong char, not a number, 
	// or extra text at the end), we return false.
	return false;
}

std::vector<int> UpdateChecker::getLatestVersion() {
	CURL *curl;
	CURLcode res;
	std::string readBuffer;

	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	
	std::vector<int> toReturn = {};

	if(curl) {
		const std::string url = "https://api.github.com/repos/AdamJosephMather/CodeWizard2/releases/latest";
		
		// Set the URL
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		
		// Set the User-Agent header (GitHub requires this)
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "User-Agent: CodeWizard2");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		// Set the callback function to write data
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

		// Follow redirects
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

		// Perform the request
		res = curl_easy_perform(curl);

		if(res == CURLE_OK) {
			long response_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

			if (response_code == 200) {
				try {
					// Parse the JSON
					json release_data = json::parse(readBuffer);
					std::string latest_version = release_data["tag_name"];
					
					int major;
					int minor;
					int patch;
					
					bool worked = parseVersionString(latest_version, major, minor, patch);
					
					if (worked) {
						toReturn = {major, minor, patch};
					}
					
					std::cout << "Latest version:  " << latest_version << std::endl;
				} catch (json::parse_error& e) {
					std::cerr << "Failed to parse JSON response: " << e.what() << std::endl;
				}
			} else {
				 std::cerr << "GitHub API returned status code: " << response_code << std::endl;
			}
		} else {
			std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
		}

		// Cleanup
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}

	curl_global_cleanup();
	return toReturn;
}
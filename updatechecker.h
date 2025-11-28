#pragma once
#include <string>
#include <vector>

class UpdateChecker {
public:
	static bool parseVersionString(const std::string& version_str, int& major, int& minor, int& patch);
	static std::vector<int> getLatestVersion();
	
private:
};
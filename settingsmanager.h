#pragma once

#include "json.hpp"
#include "helper_types.h"

class SettingsManager {
public:
	void loadSettings();
	void saveSettings();
	
	void saveConfig(nlohmann::json config);
	nlohmann::json getConfig();
	
	nlohmann::json& settings;

	SettingsManager();
	
	int getValue(std::string key, int default_val);
	float getValue(std::string key, float default_val);
	bool getValue(std::string key, bool default_val);
	std::string getValue(std::string key, std::string default_val);
	
	void setValue(std::string key, int val);
	void setValue(std::string key, float val);
	void setValue(std::string key, bool val);
	void setValue(std::string key, std::string val);
	
	std::vector<Language> loadLanguages();
	
	std::string getProjectLSP(std::string langname);
	std::string makeUUID();
	bool makeProjectSettings();
	nlohmann::json getProjSettings();
	std::string getProjectSettingsPath();
	std::string getProjectBuild();
private:
	nlohmann::json internalSettings;
	std::string getLocalAppDataPath();
};

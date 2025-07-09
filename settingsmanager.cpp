#include "SettingsManager.h"
#include "helper_types.h"
#include <windows.h>
#include <shlobj.h> // SHGetFolderPathA
#include <fstream>
#include <filesystem>
#include <iostream>
#include <array>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>

SettingsManager::SettingsManager() : settings(internalSettings) {}

void SettingsManager::loadSettings() {
	std::string path = getLocalAppDataPath() + "\\CodeWizard\\settings.json";
	std::ifstream file(path);
	bool gotsettings = false;
	
	if (file.is_open()) {
		try {
			file >> settings;
			gotsettings = true;
		} catch (const std::exception& e) {
			std::cerr << "Failed to parse settings: " << e.what() << std::endl;
		}
	}
	if (!gotsettings) {
		settings = nlohmann::json::object();
	}
	
	if (!settings["simple"].contains("uuid")) {
		auto uuid = makeUUID();
		std::cout << "Setting uuid: " << uuid << std::endl;
		settings["simple"]["uuid"] = uuid;
		saveSettings();
	}
}

std::string SettingsManager::makeUUID() {
	// Generate 16 random bytes
	std::array<uint8_t, 16> uuid{};
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dist(0, 255);
	
	for (auto &byte : uuid) {
		byte = static_cast<uint8_t>(dist(gen));
	}

	// Set the version to 4 --- RFC 4122 §4.1.3
	uuid[6] = (uuid[6] & 0x0F) | 0x40;
	// Set the variant to RFC 4122 --- §4.1.1
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

	// Format as 8-4-4-4-12 hex digits
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (size_t i = 0; i < uuid.size(); ++i) {
		oss << std::setw(2) << static_cast<int>(uuid[i]);
		if (i == 3 || i == 5 || i == 7 || i == 9) {
			oss << '-';
		}
	}

	return oss.str();
}

void SettingsManager::saveSettings() {
	std::string path = getLocalAppDataPath()+"\\CodeWizard\\settings.json";
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	std::ofstream file(path);
	if (file.is_open()) {
		file << settings.dump(4);
	}
}

void SettingsManager::saveConfig(nlohmann::json config) {
	settings["state"] = config;
	saveSettings();
}

nlohmann::json SettingsManager::getConfig() {
	return settings["state"];
}

std::string SettingsManager::getLocalAppDataPath() {
	char path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
		return std::string(path);
	}
	return "";
}


// interaction methods


int SettingsManager::getValue(std::string key, int default_val) {
	if (!settings.contains("simple")) {
		return default_val;
	}
	
	if (!settings["simple"].contains(key)) {
		return default_val;
	}
	
	return settings["simple"][key];
}

float SettingsManager::getValue(std::string key, float default_val) {
	if (!settings.contains("simple")) {
		return default_val;
	}
	
	if (!settings["simple"].contains(key)) {
		return default_val;
	}
	
	return settings["simple"][key];
}

bool SettingsManager::getValue(std::string key, bool default_val) {
	if (!settings.contains("simple")) {
		return default_val;
	}
	
	if (!settings["simple"].contains(key)) {
		return default_val;
	}
	
	return settings["simple"][key];
}

std::string SettingsManager::getValue(std::string key, std::string default_val) {
	if (!settings.contains("simple")) {
		return default_val;
	}
	
	if (!settings["simple"].contains(key)) {
		return default_val;
	}
	
	return settings["simple"][key];
}

void SettingsManager::setValue(std::string key, int val) {
	settings["simple"][key] = val;
	saveSettings();
}

void SettingsManager::setValue(std::string key, float val) {
	settings["simple"][key] = val;
	saveSettings();
}

void SettingsManager::setValue(std::string key, bool val) {
	settings["simple"][key] = val;
	saveSettings();
}

void SettingsManager::setValue(std::string key, std::string val) {
	settings["simple"][key] = val;
	saveSettings();
}

std::vector<Language> SettingsManager::loadLanguages() {
	nlohmann::json languages;
	
	std::string path = getLocalAppDataPath() + "\\CodeWizard\\languages.json";
	std::ifstream file(path);
	
	languages = nlohmann::json::object();
	
	if (file.is_open()) {
		try {
			file >> languages;
		} catch (const std::exception& e) {
			std::cerr << "Failed to parse settings: " << e.what() << std::endl;
		}
	}
	
	std::vector<Language> out;
	
	for (auto lang : languages["languages"]) {
		Language language;
		
		language.name = lang["name"];
		
		std::vector<std::string> filetypes;
		
		for (auto ft : lang["filetypes"]) {
			filetypes.push_back(ft);
		}
		language.filetypes = filetypes;
		
		std::string com = lang["line_comment"];
		
		language.line_comment = icu::UnicodeString::fromUTF8(com);
		
		std::string tmf = lang["textmatefile"];
		std::string replace_dir = "%INSTALL_DIR%";
		int indx = tmf.find(replace_dir);
		if (indx != std::string::npos) {
			tmf = tmf.replace(indx, replace_dir.size(), getLocalAppDataPath()+"\\CodeWizard");
		}
		language.textmatefile = tmf;
		
		std::string lsp = lang["lsp_command"];
		int indx2 = lsp.find(replace_dir);
		if (indx2 != std::string::npos) {
			lsp = lsp.replace(indx2, replace_dir.size(), getLocalAppDataPath()+"\\CodeWizard");
		}
		language.lsp = lsp;
		
		std::string build_command = lang["build_command"];
		language.build_command = build_command;
		
		out.push_back(language);
	}
	
	return out;
}

std::string SettingsManager::getProjectSettingsPath() {
	std::string empty = "";
	std::string uuid = getValue("uuid", empty);
	std::string folder = getValue("current_folder", empty);
	
	std::cout << "uuid: " << uuid << " folder: " << folder << "\n";
	
	if (uuid == empty || folder == empty) {
		return "";
	}
	
	std::string path = folder + "\\"+uuid+".json";
	return path;
}

nlohmann::json SettingsManager::getProjSettings() {
	nlohmann::json proj_settings;
	
	auto path = getProjectSettingsPath();
	if (path == "") {
		return nlohmann::json::object();
	}
	
	std::cout << "About to try and open file: " << path << "\n";
	std::ifstream file(path);
	if (file.is_open()) {
		try {
			file >> proj_settings;
			return proj_settings;
		} catch (const std::exception& e) {
			std::cerr << "Failed to parse project settings: " << e.what() << std::endl;
		}
	}
	return nlohmann::json::object();
}

std::string SettingsManager::getProjectLSP(std::string langname) {
	auto proj_settings = getProjSettings();
	
	if (!proj_settings.contains("LSPs")) {
		return "";
	}else if (!proj_settings["LSPs"].contains(langname)) {
		return "";
	}
	
	return proj_settings["LSPs"][langname];
}

std::string SettingsManager::getProjectBuild() {
	auto proj_settings = getProjSettings();
	
	if (!proj_settings.contains("build_command")) {
		return "";
	}
	
	return proj_settings["build_command"];
}

bool SettingsManager::makeProjectSettings() {
	std::string path = getProjectSettingsPath();
	if (path == "") {
		return false;
	}
	
	if (getProjSettings() != nlohmann::json::object()) {
		return true; // let's not overwrite it
	}
	
	nlohmann::json blank;
	blank["LSPs"]["language_name_here"] = "lsp_command_here";
	blank["build_command"] = "";
	
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	std::ofstream file(path);
	if (file.is_open()) {
		file << blank.dump(4);
	}else{
		return false;
	}
	
	return true;
}
#include "curler.h"
#include <iostream>
#include <string>
#include <curl/curl.h>
#include "json.hpp"
#include "application.h"

// Callback to write response data to a std::string
size_t Curler::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

std::string Curler::getInsertion(std::string before, std::string after) {
	int maxtokens = App::settings->getValue("lm_studio_max_tokens", 30);
	
	std::string request = "<|fim_prefix|>"+before+"<|fim_suffix|>"+after+"<|fim_middle|>";
	
	std::string url = "http://localhost:1234/api/v0/completions";
	
	std::string defmdl = "qwen2.5-coder-1.5b-instruct@q4_k_m";
	
	nlohmann::json itm;
	itm["model"] = App::settings->getValue("lm_studio_model_id", defmdl);
	itm["prompt"] = request;
	itm["temperature"] = 0.7;
	itm["max_tokens"] = maxtokens;
	itm["stream"] = false;
	
	std::string json = itm.dump();
	
	auto dta = run_curl(url, json);
	
	nlohmann::json resp = nlohmann::json::parse(dta);
	
	std::string final = resp["choices"][0]["text"];
	
	return final;
}


std::string Curler::run_curl(std::string url, std::string json) {
	CURL* curl;
	CURLcode res;
	std::string readBuffer = "";
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if(curl) {
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
		
		res = curl_easy_perform(curl);
		
		if(res != CURLE_OK){
			std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
		}else{
			std::cout << "Response: " << readBuffer << std::endl;
		}
		
		// Cleanup
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}
	
	curl_global_cleanup();
	
	return readBuffer;
}

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

void Curler::loadModel() {
	int maxtokens = 1;
	
	std::string request = "a";
	
	std::string url = "http://localhost:1234/api/v0/completions";
	
	std::string defmdl = "qwen2.5-coder-1.5b-instruct@q4_k_m";
	
	nlohmann::json itm;
	itm["model"] = App::settings->getValue("lm_studio_model_id", defmdl);
	itm["prompt"] = request;
	itm["temperature"] = 0.7;
	itm["max_tokens"] = maxtokens;
	itm["stream"] = false;
	
	std::string json = itm.dump();
	
	new std::thread([url, json](){
		bool wrkd;
		run_curl(url, json, wrkd);
	});
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
	itm["stop"] = {
		"<|endoftext|>",
		"<|fim_prefix|>",
		"<|fim_suffix|>",
		"<|fim_middle|>"
	};
	
	std::string json = itm.dump();
	
	bool wrkd;
	auto dta = run_curl(url, json, wrkd);
	
	if (!wrkd){
		App::displayToast(icu::UnicodeString::fromUTF8("Invalid response "+dta));
		return "";
	}
	
	std::cout << dta << std::endl;
	
	nlohmann::json resp = nlohmann::json::parse(dta);
	
	if (!resp.contains("choices")) {
		App::displayToast(icu::UnicodeString::fromUTF8("Invalid response: "+dta));
		return "";
	}
	
	std::string final = resp["choices"][0]["text"];
	
	return final;
}


std::string Curler::run_curl(std::string url, std::string json, bool& worked) {
	CURL* curl;
	CURLcode res;
	std::string readBuffer = "";
	worked = false;
	
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
			worked = false;
			readBuffer = "Could not access server.";
		}else{
			std::cout << "Response: " << readBuffer << std::endl;
			worked = true;
		}
		
		// Cleanup
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}
	
	curl_global_cleanup();
	
	return readBuffer;
}

struct StreamState {
	std::string buffer;                              // raw chunk buffer
	std::string accumulated;                         // full response
	std::function<void(const std::string&)> onChunk; // user callback
	bool done = false;
};

// This callback is called by libcurl as soon as there is data to be written
static size_t WriteStreamCallback(
	char* ptr,
	size_t size,
	size_t nmemb,
	void* userdata
) {
	auto* st = static_cast<StreamState*>(userdata);
	st->buffer.append(ptr, size * nmemb);

	// split on newline; SSE events end in '\n'
	size_t pos;
	while ((pos = st->buffer.find("\n")) != std::string::npos) {
		std::string line = st->buffer.substr(0, pos);
		st->buffer.erase(0, pos + 1);

		// only lines prefixed with "data: "
		if (line.rfind("data: ", 0) == 0) {
			std::string payload = line.substr(6);
			if (payload == "[DONE]") {
				st->done = true;
				break;
			}
			// parse JSON, extract the delta
			auto j = nlohmann::json::parse(payload);
			auto delta = j["choices"][0]["delta"];
			if (delta.contains("content")) {
				std::string text = delta["content"].get<std::string>();
				st->accumulated += text;
				st->onChunk(text);
			}
		}
	}
	return size * nmemb;
}

std::string Curler::StreamChatResponse(const std::vector<std::pair<bool, std::string>>& messages, std::function<void(const std::string&)> stream_callback) {
	// 1) Build the JSON payload
	nlohmann::json payload;
	payload["model"] = App::settings->getValue(
		"lm_studio_model_id",
		std::string("qwen2.5-coder-1.5b-instruct@q4_k_m")
	);
	payload["stream"] = true;

	// assemble the messages array
	nlohmann::json msgarr = nlohmann::json::array();
	for (auto& m : messages) {
		nlohmann::json mm;
		mm["role"]    = m.first ? "user" : "assistant";
		mm["content"] = m.second;
		msgarr.push_back(mm);
	}
	payload["messages"] = std::move(msgarr);

	std::string body = payload.dump();
	std::string url  = "http://localhost:1234/api/v0/chat/completions";

	// 2) Initialize cURL
	CURL* curl = curl_easy_init();
	if (!curl) {
		throw std::runtime_error("Failed to init curl");
	}

	// 3) Prepare our streaming state
	StreamState state;
	state.onChunk = std::move(stream_callback);

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: text/event-stream");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);

	// 4) Perform the request (this will block until the server closes the stream)
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		throw std::runtime_error(
			std::string("curl_easy_perform() failed: ") +
			curl_easy_strerror(res)
		);
	}

	// 5) Cleanup
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	// 6) Return the full accumulated text
	return state.accumulated;
}
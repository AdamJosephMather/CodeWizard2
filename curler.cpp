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

// Shared state for streaming
struct StreamState {
	std::string buffer;                                // raw chunk buffer
	std::string accumulated;                           // full response
	std::function<void(const std::string&)> onChunk;   // user callback
	bool done = false;
};

// SSE callback for streaming
static size_t WriteStreamCallback(
	char* ptr,
	size_t size,
	size_t nmemb,
	void* userdata
) {
	auto* st = static_cast<StreamState*>(userdata);
	st->buffer.append(ptr, size * nmemb);

	size_t pos;
	while ((pos = st->buffer.find("\n")) != std::string::npos) {
		std::string line = st->buffer.substr(0, pos);
		st->buffer.erase(0, pos + 1);

		if (line.rfind("data: ", 0) == 0) {
			std::string payload = line.substr(6);
			if (payload == "[DONE]") {
				st->done = true;
				break;
			}
			auto j = nlohmann::json::parse(payload);
			auto object = j["choices"][0];
			
			if (object.contains("delta")) {
				auto delta = object["delta"];
				if (delta.contains("content")) {
					std::string text = delta["content"].get<std::string>();
					st->accumulated += text;
					st->onChunk(text);
				}
			}else if (object.contains("text")){
				std::string text = object["text"].get<std::string>();
				st->accumulated += text;
				st->onChunk(text);
			}
			
		}
	}
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

// Streams a fill-in-the-middle completion, invoking onChunk for each text delta
std::string Curler::StreamInsertion(
	const std::string& before,
	const std::string& after,
	std::function<void(const std::string&)> onChunk
) {
	int maxtokens = App::settings->getValue("lm_studio_max_tokens", 30);
	std::string request = "<|fim_prefix|>" + before + "<|fim_suffix|>" + after + "<|fim_middle|>";
	std::string url = "http://localhost:1234/api/v0/completions";

	nlohmann::json payload;
	payload["model"] = App::settings->getValue(
		"lm_studio_model_id",
		std::string("qwen2.5-coder-1.5b-instruct@q4_k_m")
	);
	payload["prompt"] = request;
	payload["temperature"] = 0.7;
	payload["max_tokens"] = maxtokens;
	payload["stream"] = true;
	payload["stop"] = {
		"<|endoftext|>",
		"<|fim_prefix|>",
		"<|fim_suffix|>",
		"<|fim_middle|>"
	};

	std::string body = payload.dump();

	StreamState state;
	state.onChunk = std::move(onChunk);

	CURL* curl = curl_easy_init();
	if (!curl) {
		throw std::runtime_error("Failed to init curl");
	}

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: text/event-stream");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		throw std::runtime_error(
			std::string("curl_easy_perform() failed: ") +
			curl_easy_strerror(res)
		);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return state.accumulated;
}

// Updated to call the streaming insertion
std::string Curler::getInsertion(
	const std::string& before,
	const std::string& after
) {
	// print chunks as they arrive
	std::string result = Curler::StreamInsertion(
		before,
		after,
		[](const std::string& chunk) {
			std::cout << chunk;
		}
	);
	std::cout << std::endl;
	return result;
}

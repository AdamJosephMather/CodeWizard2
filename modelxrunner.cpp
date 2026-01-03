#include "modelxrunner.h"
#include <fstream>
#include <iostream>
#include <ATen/Parallel.h>
#include "application.h"
#include <chrono>
#include <torch/torch.h>

std::mutex ModelXRunner::genMutex;

bool ModelXRunner::loaded = false;
bool ModelXRunner::loading = false;
bool ModelXRunner::load_success = false;

static std::string load_file_text(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return "";
	
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool ModelXRunner::load() {
	if (loading || loaded) { return false; }
	
	App::displayText(icu::UnicodeString::fromUTF8("Loading Chauffeur"));
	
	loading = true;
	
	std::string model_path = App::settings->getValue("chauffeur_model_path", std::string());
	std::string tokenizer_path = App::settings->getValue("chauffeur_tokenizer_path", std::string());
	
	at::set_num_threads(4);
//	at::set_num_threads(std::thread::hardware_concurrency());
	at::set_num_interop_threads(4);
	
	std::cout << "threads " << at::get_num_threads() << " interop " << at::get_num_interop_threads() << "\n";
	std::cout << "mkldnn enabled " << at::globalContext().userEnabledMkldnn() << "\n";
	std::cout << torch::show_config() << "\n";
	
	
	try {
		// Load TorchScript model
//		torch::jit::load_library("modelx_ops.dll");
		
		s_model = torch::jit::load(model_path, torch::kCPU);
		s_model.eval();

		// Load tokenizer
		auto blob = load_file_text(tokenizer_path);
		
		if (blob == "") {
			load_success = false;
			loaded = true;
			loading = false;
			App::displayToast(icu::UnicodeString::fromUTF8("Failed to read Chauffeur tokenizer"));
			
			return false;
		}
		
		s_tokenizer = tokenizers::Tokenizer::FromBlobJSON(blob);
		
		if (!s_tokenizer) {
			load_success = false;
			loaded = true;
			loading = false;
			App::displayToast(icu::UnicodeString::fromUTF8("Failed to load Chauffeur tokenizer"));
			
			return false;
		}
		
		s_eos_id = s_tokenizer->TokenToId(kEosTokenString);
		
		load_success = true;
		loaded = true;
		loading = false;
		
		App::displayText(icu::UnicodeString::fromUTF8("Chauffeur Loaded!"));
		
		return true;
	} catch (const std::exception& e) {
		std::cout << "Failed to load: " << e.what() << "\n";
		
		App::displayToast(icu::UnicodeString::fromUTF8("Failed to load Chauffeur model"));
		
		load_success = false;
		loaded = true;
		loading = false;
		
		return false;
	}
}

int64_t ModelXRunner::greedy_next_token(const torch::Tensor& logits_1vocab) {
	return logits_1vocab.argmax(0).item<int64_t>();
}

std::string ModelXRunner::generate(const std::string& input, int max_tokens) {
	App::chauffeur_call_id += 1;
	int call_id = App::chauffeur_call_id;

	if (loading) return "";
	if (!loaded) load();
	if (!load_success) return "";

	App::displayText(icu::UnicodeString::fromUTF8("Generating With Chauffeur"));
	if (max_tokens <= 0) return "";

	std::lock_guard<std::mutex> lock(genMutex);
	if (App::chauffeur_call_id > call_id) return "";

	torch::NoGradGuard guard;

	// Encode
	std::vector<int32_t> prompt_ids_i32 = s_tokenizer->Encode(input);
	if (prompt_ids_i32.empty()) return "";

	if ((int)prompt_ids_i32.size() > kMaxPromptTokens) {
		prompt_ids_i32.resize(kMaxPromptTokens);
	}

	std::vector<int32_t> all_ids = prompt_ids_i32;
	std::vector<int64_t> prompt_ids_i64(all_ids.begin(), all_ids.end());

	// tokens: [1, T]
	torch::Tensor tokens = torch::from_blob(
		prompt_ids_i64.data(),
		{1, (int64_t)prompt_ids_i64.size()},
		torch::kInt64
	).clone();

	const int64_t T = tokens.size(1);
	if (T <= 0) return "";

	// Model outputs:
	//   logits: (B, V)  (last-token only)
	//   states: List[Tensor]
	//   token_cache: Tensor (B, C, K-1)
	torch::jit::IValue states_list = torch::jit::IValue(); // None
	torch::jit::IValue token_cache = torch::jit::IValue(); // None

	// Prefill: run prompt excluding last token to build (states, cache)
	if (T > 1) {
		torch::Tensor prefill = tokens.narrow(1, 0, T - 1); // [1, T-1]

		std::vector<torch::jit::IValue> pre_inputs;
		pre_inputs.push_back(prefill);
		pre_inputs.push_back(torch::jit::IValue()); // None states
		pre_inputs.push_back(torch::jit::IValue()); // None token_cache

		if (App::chauffeur_call_id > call_id) return "";

		auto pre_out = s_model.forward(pre_inputs).toTuple();
		// logits unused for prefill (it corresponds to last token of prefill)
		states_list = pre_out->elements()[1];
		token_cache = pre_out->elements()[2];
	}

	// Now feed the last prompt token to get the first next-token logits
	torch::Tensor tok_input = tokens.narrow(1, T - 1, 1).clone(); // [1,1]

	std::vector<torch::jit::IValue> first_inputs;
	first_inputs.push_back(tok_input);
	first_inputs.push_back(states_list);  // may be None if T==1
	first_inputs.push_back(token_cache);  // may be None if T==1

	if (App::chauffeur_call_id > call_id) return "";

	auto first_out = s_model.forward(first_inputs).toTuple();

	torch::Tensor logits = first_out->elements()[0].toTensor(); // (1, V)
	states_list = first_out->elements()[1];
	token_cache = first_out->elements()[2];

	int64_t next_id = greedy_next_token(logits.select(0, 0)); // (V)

	// Autoregressive loop
	for (int step = 0; step < max_tokens; ++step) {
		if (App::chauffeur_call_id > call_id) return "";

		if (s_eos_id >= 0 && next_id == (int64_t)s_eos_id) break;

		all_ids.push_back((int32_t)next_id);
		tok_input[0][0] = next_id; // keep [1,1] token tensor

		std::vector<torch::jit::IValue> step_inputs;
		step_inputs.push_back(tok_input);
		step_inputs.push_back(states_list);
		step_inputs.push_back(token_cache);

		auto step_out = s_model.forward(step_inputs).toTuple();

		torch::Tensor step_logits = step_out->elements()[0].toTensor(); // (1, V)
		states_list = step_out->elements()[1];
		token_cache = step_out->elements()[2];

		next_id = greedy_next_token(step_logits.select(0, 0));
	}

	if (App::chauffeur_call_id > call_id) return "";

	// Decode
	if (kReturnOnlyNewText) {
		std::vector<int32_t> gen_ids(all_ids.begin() + prompt_ids_i32.size(), all_ids.end());
		return s_tokenizer->Decode(gen_ids);
	} else {
		return s_tokenizer->Decode(all_ids);
	}
}
#include "modelxrunner.h"
#include <fstream>
#include <iostream>
#include <ATen/Parallel.h>
#include "application.h"

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
	
	at::set_num_threads(std::thread::hardware_concurrency());
	at::set_num_interop_threads(1);
	
	try {
		// Load TorchScript model
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
		
		std::cout << "CL";
		App::displayText(icu::UnicodeString::fromUTF8("Chauffeur Loaded!"));
		
		return true;
	} catch (const std::exception& e) {
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
	
	if (loading) {
		return "";
	}
	
	if (!loaded) {
		load();
	}
	
	if (!load_success) {
		return "";
	}
	
	App::displayText(icu::UnicodeString::fromUTF8("Generating With Chauffeur"));
	
	if (max_tokens <= 0) return "";
	
	std::lock_guard<std::mutex> lock(genMutex);
	
	if (App::chauffeur_call_id > call_id) { return ""; }
	
	torch::NoGradGuard no_grad;
	
	// Encode
	std::vector<int32_t> prompt_ids_i32 = s_tokenizer->Encode(input);
	if (prompt_ids_i32.empty()) return "";
	
	std::cout << "Token count: " << prompt_ids_i32.size() << "\n";
	
	if ((int)prompt_ids_i32.size() > kMaxPromptTokens) {
		prompt_ids_i32.resize(kMaxPromptTokens);
	}
	
	std::vector<int32_t> all_ids = prompt_ids_i32;
	std::vector<int64_t> prompt_ids_i64(all_ids.begin(), all_ids.end());
	
	// Create input tensor [1, T]
	auto tokens = torch::from_blob(
			prompt_ids_i64.data(),
			{1, (int64_t)prompt_ids_i64.size()},
			torch::kInt64
	).clone();
	
	// --- Initial Forward Pass ---
	// Python signature: forward(inpt_tkns, states: Optional[List[Tensor]])
	std::vector<torch::jit::IValue> inputs;
	inputs.push_back(tokens);
	inputs.push_back(c10::nullopt); // Passing None for initial states
	
	if (App::chauffeur_call_id > call_id) { return ""; }
	
	
	auto output_tuple = s_model.forward(inputs).toTuple();
	
	if (App::chauffeur_call_id > call_id) { return ""; }
	
	torch::Tensor logits = output_tuple->elements()[0].toTensor();
	
	// The model returns List[Tensor] for states
	torch::jit::IValue states_list = output_tuple->elements()[1];
	
	// Get next token from the very last position of the prompt
	torch::Tensor last_logits = logits.index({0, -1}); 
	int64_t next_id = greedy_next_token(last_logits);
	
	// --- Autoregressive Loop ---
	auto tok_input = torch::empty({1, 1}, torch::kInt64);

	for (int step = 0; step < max_tokens; ++step) {
		if (App::chauffeur_call_id > call_id) { return ""; }
		
		if (s_eos_id >= 0 && next_id == (int64_t)s_eos_id) break;
		
		all_ids.push_back((int32_t)next_id);
		tok_input[0][0] = next_id;

		std::vector<torch::jit::IValue> step_inputs;
		step_inputs.push_back(tok_input);
		step_inputs.push_back(states_list); // Pass the list back in
		
		auto step_out = s_model.forward(step_inputs).toTuple();
		
		// Update logits and states
		torch::Tensor step_logits = step_out->elements()[0].toTensor();
		states_list = step_out->elements()[1]; 
		
		next_id = greedy_next_token(step_logits.index({0, 0}));
	}
	
	if (App::chauffeur_call_id > call_id) { return ""; }
	
	// Decode
	if (kReturnOnlyNewText) {
		std::vector<int32_t> gen_ids(all_ids.begin() + prompt_ids_i32.size(), all_ids.end());
		return s_tokenizer->Decode(gen_ids);
	} else {
		return s_tokenizer->Decode(all_ids);
	}
}
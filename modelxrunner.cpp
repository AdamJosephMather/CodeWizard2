#include "modelxrunner.h"
#include <fstream>
#include <iostream>
#include <ATen/Parallel.h>
#include "application.h"
//#include <chrono>
#include <torch/torch.h>

std::mutex ModelXRunner::genMutex;

bool ModelXRunner::loaded = false;
bool ModelXRunner::loading = false;
bool ModelXRunner::load_success = false;

int32_t ModelXRunner::suffixTokenId = 0;
int32_t ModelXRunner::prefixTokenId = 0;
int32_t ModelXRunner::middleTokenId = 0;	
	

std::unique_ptr<torch::jit::Module> ModelXRunner::s_model = nullptr;

static std::string load_file_text(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return "";
	
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool ModelXRunner::load() {
	if (loading || loaded) { return false; }
	
	App::displayText(MST::toMonoString("Loading Chauffeur"));
	
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
		
		s_model = std::make_unique<torch::jit::Module>(torch::jit::load(model_path, torch::kCPU));
		s_model->eval();

		// Load tokenizer
		auto blob = load_file_text(tokenizer_path);
		
		if (blob == "") {
			load_success = false;
			loaded = true;
			loading = false;
			App::displayToast(MST::toMonoString("Failed to read Chauffeur tokenizer"));
			
			return false;
		}
		
		s_tokenizer = tokenizers::Tokenizer::FromBlobJSON(blob);
		
		if (!s_tokenizer) {
			load_success = false;
			loaded = true;
			loading = false;
			App::displayToast(MST::toMonoString("Failed to load Chauffeur tokenizer"));
			
			return false;
		}
		
		s_eos_id = s_tokenizer->TokenToId(kEosTokenString);
		
		suffixTokenId = s_tokenizer->TokenToId("<|suffix|>");
		prefixTokenId = s_tokenizer->TokenToId("<|prefix|>");
		middleTokenId = s_tokenizer->TokenToId("<|middle|>");
		
		if (suffixTokenId == -1 || prefixTokenId == -1 || middleTokenId == -1) {
			suffixTokenId = 0;
			prefixTokenId = 0;
			middleTokenId = 0;
		}
		
		load_success = true;
		loaded = true;
		loading = false;
		
		App::displayText(MST::toMonoString("Chauffeur Loaded!"));
		
		return true;
	} catch (const std::exception& e) {
		std::cout << "Failed to load: " << e.what() << "\n";
		
		App::displayToast(MST::toMonoString("Failed to load Chauffeur model"));
		
		load_success = false;
		loaded = true;
		loading = false;
		
		return false;
	}
}

//int64_t ModelXRunner::greedy_next_token(const torch::Tensor& logits_1vocab, std::string must_start_with) {
//	return logits_1vocab.argmax(0).item<int64_t>();
//}

int64_t ModelXRunner::next_token_temperature(
	const torch::Tensor& logits_1vocab,
	float temperature,
	const std::string& must_start_with
) {
	TORCH_CHECK(logits_1vocab.dim() == 1, "logits_1vocab must be a 1D tensor [vocab]");
	
	// Work on CPU float32 for simplicity + easy prefix filtering
	torch::Tensor logits = logits_1vocab.to(torch::kCPU, torch::kFloat32).contiguous();
	const int64_t vocab_size = logits.size(0);
	
	// Build allowed token id list if we have a prefix constraint
	std::vector<int64_t> allowed_ids;
	if (!must_start_with.empty()) {
		allowed_ids.reserve(1024);
		
		for (int64_t id = 0; id < vocab_size; ++id) {
			// Raw vocab token string (e.g. "Ġdef")
			std::string tok = s_tokenizer->IdToToken(static_cast<int32_t>(id));
			if (tok.rfind(must_start_with, 0) == 0) { // starts_with
				allowed_ids.push_back(id);
			}
		}

		// If nothing matches the constraint, fall back to greedy over full vocab
		if (allowed_ids.empty()) {
			return logits.argmax(0).item<int64_t>();
		}
	}

	// Candidate logits + candidate ids tensor
	torch::Tensor candidate_ids;
	torch::Tensor candidate_logits;

	if (allowed_ids.empty()) {
		candidate_ids = torch::arange(vocab_size, torch::TensorOptions().dtype(torch::kInt64));
		candidate_logits = logits;
	} else {
		candidate_ids = torch::from_blob(
			allowed_ids.data(),
			{static_cast<int64_t>(allowed_ids.size())},
			torch::TensorOptions().dtype(torch::kInt64)
		).clone(); // clone because from_blob points at vector memory
		candidate_logits = logits.index_select(0, candidate_ids);
	}

	// Greedy mode (temperature <= 0 or non-finite)
	if (!(temperature > 0.0f) || !std::isfinite(temperature)) {
		int64_t best_idx = candidate_logits.argmax(0).item<int64_t>();
		return candidate_ids[best_idx].item<int64_t>();
	}

	// Temperature scaling + numerically stable softmax
	candidate_logits = candidate_logits / temperature;
	candidate_logits = candidate_logits - std::get<0>(candidate_logits.max(0)); // subtract max
	torch::Tensor probs = torch::softmax(candidate_logits, 0);

	// Sample 1 token
	int64_t sampled_idx = torch::multinomial(probs, /*num_samples=*/1).item<int64_t>();
	return candidate_ids[sampled_idx].item<int64_t>();
}

std::string ModelXRunner::generate(const std::string& input, int max_tokens) {
	App::chauffeur_call_id += 1;
	int call_id = App::chauffeur_call_id;

	if (loading) return "";
	if (!loaded) load();
	if (!load_success) return "";

	App::displayText(MST::toMonoString("Generating With Chauffeur"));
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
	
	std::string must_start_with = "";
	std::string must_start_with_text = "";
	if ((int)prompt_ids_i32.size() > 1) {
		int32_t last_id = prompt_ids_i32.back();
		must_start_with = s_tokenizer->IdToToken(last_id);
		must_start_with_text = s_tokenizer->Decode({ last_id });
		prompt_ids_i32.pop_back();
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

		auto pre_out = s_model->forward(pre_inputs).toTuple();
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
	
	auto first_out = s_model->forward(first_inputs).toTuple();
	
	torch::Tensor logits = first_out->elements()[0].toTensor(); // (1, V)
	states_list = first_out->elements()[1];
	token_cache = first_out->elements()[2];
	
	int64_t next_id = next_token_temperature(logits.select(0, 0), App::settings->getValue("chauffeur_temp", 0.6f), must_start_with); // (V)
	
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

		auto step_out = s_model->forward(step_inputs).toTuple();

		torch::Tensor step_logits = step_out->elements()[0].toTensor(); // (1, V)
		states_list = step_out->elements()[1];
		token_cache = step_out->elements()[2];

		next_id = next_token_temperature(step_logits.select(0, 0), App::settings->getValue("chauffeur_temp", 0.6f));
	}

	if (App::chauffeur_call_id > call_id) return "";
	
	// Decode Ġ
	if (kReturnOnlyNewText) {
		std::vector<int32_t> gen_ids(all_ids.begin() + prompt_ids_i32.size(), all_ids.end());
		std::string text_out = s_tokenizer->Decode(gen_ids);
		
		if (!must_start_with_text.empty()) {
			if (text_out.rfind(must_start_with_text, 0) == 0) {
				text_out.erase(0, must_start_with_text.size());
			}
		}
		
		return text_out;
	} else {
		return s_tokenizer->Decode(all_ids);
	}
}

std::string ModelXRunner::generate_fim(const std::string& prefix, const std::string& suffix, int max_tokens) {
	App::chauffeur_call_id += 1;
	int call_id = App::chauffeur_call_id;
	
	if (loading) return "";
	if (!loaded) load();
	if (!load_success) return "";
	
	App::displayText(MST::toMonoString("Generating With Chauffeur"));
	if (max_tokens <= 0) return "";
	
	std::lock_guard<std::mutex> lock(genMutex);
	if (App::chauffeur_call_id > call_id) return "";
	
	torch::NoGradGuard guard;
	
	// Encode
	std::vector<int32_t> prefix_ids_i32 = s_tokenizer->Encode(prefix);
	if (prefix_ids_i32.empty()) return "";

	if ((int)prefix_ids_i32.size() > kMaxPrefixTokens) {
		prefix_ids_i32.resize(kMaxPrefixTokens);
	}
	
	std::vector<int32_t> suffix_ids_i32 = s_tokenizer->Encode(suffix);
	
	if ((int)suffix_ids_i32.size() > kMaxSuffixTokens) {
		suffix_ids_i32.resize(kMaxSuffixTokens);
	}
	
	std::string must_start_with = "";
	std::string must_start_with_text = "";
	if ((int)prefix_ids_i32.size() > 1) {
		int32_t last_id = prefix_ids_i32.back();
		must_start_with = s_tokenizer->IdToToken(last_id);
		must_start_with_text = s_tokenizer->Decode({ last_id });
		prefix_ids_i32.pop_back();
	}
	
	std::vector<int32_t> all_ids = {};
	all_ids.push_back(suffixTokenId);
	all_ids.insert(all_ids.end(), suffix_ids_i32.begin(), suffix_ids_i32.end());
	all_ids.push_back(prefixTokenId);
	all_ids.insert(all_ids.end(), prefix_ids_i32.begin(), prefix_ids_i32.end());
	all_ids.push_back(middleTokenId);
	
	size_t start_size = all_ids.size();
	
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

		auto pre_out = s_model->forward(pre_inputs).toTuple();
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
	
	auto first_out = s_model->forward(first_inputs).toTuple();
	
	torch::Tensor logits = first_out->elements()[0].toTensor(); // (1, V)
	states_list = first_out->elements()[1];
	token_cache = first_out->elements()[2];
	
	int64_t next_id = next_token_temperature(logits.select(0, 0), App::settings->getValue("chauffeur_temp", 0.6f), must_start_with); // (V)
	
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

		auto step_out = s_model->forward(step_inputs).toTuple();

		torch::Tensor step_logits = step_out->elements()[0].toTensor(); // (1, V)
		states_list = step_out->elements()[1];
		token_cache = step_out->elements()[2];

		next_id = next_token_temperature(step_logits.select(0, 0), App::settings->getValue("chauffeur_temp", 0.6f));
	}

	if (App::chauffeur_call_id > call_id) return "";
	
	// Decode Ġ
	if (kReturnOnlyNewText) {
		std::vector<int32_t> gen_ids(all_ids.begin() + start_size, all_ids.end());
		std::string text_out = s_tokenizer->Decode(gen_ids);
		
		if (!must_start_with_text.empty()) {
			if (text_out.rfind(must_start_with_text, 0) == 0) {
				text_out.erase(0, must_start_with_text.size());
			}
		}
		
		return text_out;
	} else {
		return s_tokenizer->Decode(all_ids);
	}
}
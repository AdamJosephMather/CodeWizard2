#include "modelxrunner.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

static std::string load_file_text(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) throw std::runtime_error("Failed to open file: " + path);
	std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return s;
}

bool ModelXRunner::load(const std::string& model_path, const std::string& tokenizer_path) {
	try {
		// --- Load TorchScript model ---
		s_model = torch::jit::load(model_path, torch::kCPU);
		s_model.eval();

		// --- Load tokenizer.json into tokenizers-cpp ---
		auto blob = load_file_text(tokenizer_path);
		s_tokenizer = tokenizers::Tokenizer::FromBlobJSON(blob); // HF tokenizer from JSON :contentReference[oaicite:5]{index=5}

		// EOS id if present
		s_eos_id = s_tokenizer->TokenToId(kEosTokenString); // -1 if unknown :contentReference[oaicite:6]{index=6}

		s_loaded = true;
		std::cerr << "Loaded model: " << model_path << "\n";
		std::cerr << "Loaded tokenizer: " << tokenizer_path << "\n";
		std::cerr << "EOS token '" << kEosTokenString << "' id = " << s_eos_id << "\n";
		return true;
	} catch (const std::exception& e) {
		std::cerr << "ModelXRunner::load failed: " << e.what() << "\n";
		s_loaded = false;
		s_tokenizer.reset();
		return false;
	}
}

int64_t ModelXRunner::greedy_next_token(const torch::Tensor& logits_1vocab) {
	// logits_1vocab: [vocab]
	// argmax on CPU
	auto max_idx = std::get<1>(logits_1vocab.max(/*dim=*/0, /*keepdim=*/false));
	return max_idx.item<int64_t>();
}

std::string ModelXRunner::generate(const std::string& input, int max_tokens) {
	if (!s_loaded || !s_tokenizer) {
		throw std::runtime_error("ModelXRunner not loaded. Call load(modelpath, tokenizerpath) first.");
	}
	if (max_tokens <= 0) return "";

	torch::NoGradGuard no_grad;

	// Encode prompt
	std::vector<int32_t> prompt_ids_i32 = s_tokenizer->Encode(input); // :contentReference[oaicite:7]{index=7}
	if (prompt_ids_i32.empty()) return "";

	if ((int)prompt_ids_i32.size() > kMaxPromptTokens) {
		prompt_ids_i32.resize(kMaxPromptTokens);
	}

	// Keep full token history for decode
	std::vector<int32_t> all_ids = prompt_ids_i32;

	// Build prompt tensor [1, T] int64
	std::vector<int64_t> prompt_ids_i64;
	prompt_ids_i64.reserve(all_ids.size());
	for (auto id : all_ids) prompt_ids_i64.push_back((int64_t)id);

	auto tokens = torch::from_blob(
			prompt_ids_i64.data(),
			{(int64_t)1, (int64_t)prompt_ids_i64.size()},
			torch::TensorOptions().dtype(torch::kInt64)
	).clone(); // clone so it owns memory

	// Forward(tokens, states=None)
	std::vector<torch::jit::IValue> iv;
	iv.push_back(tokens);
	iv.push_back(torch::jit::IValue()); // None

	auto out = s_model.forward(iv).toTuple();
	torch::Tensor logits = out->elements()[0].toTensor();          // [1, T, vocab]
	auto newstates = out->elements()[1].toTensorVector();          // list[Tensor]

	// Next token from last position
	torch::Tensor last_logits = logits.index({0, (int64_t)logits.size(1) - 1}); // [vocab]
	int64_t next_id = greedy_next_token(last_logits);

	// Autoregressive loop: feed one token at a time, reuse states
	for (int step = 0; step < max_tokens; ++step) {
		all_ids.push_back((int32_t)next_id);

		if (s_eos_id >= 0 && next_id == (int64_t)s_eos_id) {
			break;
		}

		// token tensor [1,1]
		auto tok1 = torch::tensor({next_id}, torch::TensorOptions().dtype(torch::kInt64)).reshape({1, 1});

		std::vector<torch::jit::IValue> iv2;
		iv2.push_back(tok1);
		iv2.push_back(newstates); // pass states list back in

		auto out2 = s_model.forward(iv2).toTuple();
		torch::Tensor logits2 = out2->elements()[0].toTensor();      // [1,1,vocab]
		newstates = out2->elements()[1].toTensorVector();

		torch::Tensor last_logits2 = logits2.index({0, 0}); // [vocab]
		next_id = greedy_next_token(last_logits2);
	}

	// Decode result
	if (kReturnOnlyNewText) {
		// decode only generated portion (may not perfectly align with tokenizer boundaries)
		std::vector<int32_t> gen_ids;
		gen_ids.reserve(all_ids.size() - prompt_ids_i32.size());
		for (size_t i = prompt_ids_i32.size(); i < all_ids.size(); ++i) gen_ids.push_back(all_ids[i]);
		return s_tokenizer->Decode(gen_ids); // :contentReference[oaicite:8]{index=8}
	} else {
		return s_tokenizer->Decode(all_ids); // :contentReference[oaicite:9]{index=9}
	}
}
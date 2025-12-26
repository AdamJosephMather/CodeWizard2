#pragma once
#include <torch/script.h>

#include <memory>
#include <string>
#include <vector>

// tokenizers-cpp header
#include <tokenizers_cpp.h>  // provides tokenizers::Tokenizer

class ModelXRunner {
public:
	// We only need one model instance, so use static storage.
	static bool load(const std::string& model_path, const std::string& tokenizer_path);
	static std::string generate(const std::string& input, int max_tokens);

private:
	// --- Configuration knobs (edit these) ---
	static constexpr const char* kEosTokenString = "<|end|>"; // change if your tokenizer uses a different EOS token
	static constexpr bool kReturnOnlyNewText = true;          // if false, returns prompt + completion
	static constexpr int  kMaxPromptTokens = 4096;            // safety cap

	// --- Loaded assets ---
	static inline torch::jit::script::Module s_model;
	static inline bool s_loaded = false;

	static inline std::unique_ptr<tokenizers::Tokenizer> s_tokenizer;
	static inline int32_t s_eos_id = -1;

	// Helpers
	static std::vector<uint8_t> load_file_bytes(const std::string& path);
	static int64_t greedy_next_token(const torch::Tensor& logits_1vocab);
};
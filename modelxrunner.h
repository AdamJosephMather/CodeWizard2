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
	static bool load();
	static std::string generate(const std::string& input, int max_tokens);
	static std::string generate_fim(const std::string& prefix, const std::string& suffix, int max_tokens);

private:
	// Mutex
	static bool loaded;
	static bool loading;
	static bool load_success;
	
	static std::mutex genMutex;
	
	// --- Configuration knobs (edit these) ---
	static constexpr const char* kEosTokenString = "<|end|>"; // change if your tokenizer uses a different EOS token
	static constexpr bool kReturnOnlyNewText = true;          // if false, returns prompt + completion
	static constexpr int  kMaxPromptTokens = 4096;            // safety cap
	static constexpr int  kMaxPrefixTokens = 4096;            // safety cap
	static constexpr int  kMaxSuffixTokens = 4096;            // safety cap
	
	static int32_t suffixTokenId;
	static int32_t prefixTokenId;
	static int32_t middleTokenId;
	
	
	
	// --- Loaded assets ---
	static std::unique_ptr<torch::jit::Module> s_model;
	
	static inline std::unique_ptr<tokenizers::Tokenizer> s_tokenizer;
	static inline int32_t s_eos_id = -1;

	// Helpers
	static std::vector<uint8_t> load_file_bytes(const std::string& path);
//	static int64_t greedy_next_token(const torch::Tensor& logits_1vocab, std::string must_start_with = "");
	static int64_t next_token_temperature(
		const torch::Tensor& logits_1vocab,
		float temperature,
		const std::string& must_start_with = ""
	);
};
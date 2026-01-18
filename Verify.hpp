#pragma once

#include "json.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <array>
#include <vector>
#include <string>
#include <optional>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <iostream>
#include <sstream>

class Verify {
public:
	using json = nlohmann::json;
	
	static std::string bytes_as_python_b(const unsigned char* p, size_t n) {
		std::ostringstream os;
		os << "b'";
		os << std::hex << std::setfill('0');
		for (size_t i = 0; i < n; ++i) {
			unsigned v = p[i];
			os << "\\x" << std::setw(2) << v;
		}
		os << "'";
		return os.str();
	}
	
	// Call once at startup (PBKDF2 with 1,000,000 iterations is expensive).
	static void setup(const std::string& password) {
		if (password.empty()) {
			std::cerr << "Verify::setup: password is empty\n";
			return;
		}
		
		std::array<unsigned char, KEY_LEN_BYTES> key{};
		
		// Match PyCryptodome PBKDF2 default digest (HMAC-SHA1)
		// int PKCS5_PBKDF2_HMAC(const char *pass, int passlen, const unsigned char *salt, int saltlen,
		//                       int iter, const EVP_MD *digest, int keylen, unsigned char *out);
		const int ok = PKCS5_PBKDF2_HMAC(
			password.data(),
			static_cast<int>(password.size()),
			SALT.data(),
			static_cast<int>(SALT.size()),
			PBKDF2_ITERS,
			EVP_sha1(),
			KEY_LEN_BYTES,
			key.data()
		);

		if (ok != 1) {
			return;
		}

		{
			std::lock_guard<std::mutex> lock(s_keyMutex);
			// overwrite any existing key
			OPENSSL_cleanse(s_key.data(), s_key.size());
			s_key = key;
//			std::cout << bytes_as_python_b(key.data(), key.size()) << "\n";
			s_isReady = true;
		}

		OPENSSL_cleanse(key.data(), key.size());
	}

	static bool isSetup() {
		std::lock_guard<std::mutex> lock(s_keyMutex);
		return s_isReady;
	}

	static void clearReplayCache() {
		std::lock_guard<std::mutex> lock(s_handledMutex);
		s_handled.clear();
	}

	// Creates an outer payload {nonce, tag, payload} (all hex strings).
	// The encrypted inner object is: { "timestamp": <double seconds>, "message": <json> }.
	static json createPayload(const json& messageObj) {
		requireSetup();
		
		json inner;
		inner["timestamp"] = nowSeconds();
		inner["message"] = messageObj;

		const std::string plaintext = inner.dump(); // UTF-8 JSON text

		std::vector<unsigned char> ciphertext;
		std::array<unsigned char, NONCE_LEN_BYTES> nonce{};
		std::array<unsigned char, TAG_LEN_BYTES> tag{};

		if (!aesGcmEncrypt(
				reinterpret_cast<const unsigned char*>(plaintext.data()),
				plaintext.size(),
				ciphertext,
				nonce,
				tag
			)) {
			std::cerr << "Verify::createPayload: encryption failed";
			json out;
			out["content"] = "Verify failed to encrypt";
			return out;
		}

		json out;
		out["nonce"]   = bytesToHex(nonce.data(), nonce.size());
		out["tag"]     = bytesToHex(tag.data(), tag.size());
		out["payload"] = bytesToHex(ciphertext.data(), ciphertext.size());
		return out;
	}

	// Verifies + decrypts. Returns inner["message"] if valid; otherwise nullopt.
	static std::optional<json> verifyPayload(const json& incoming) {
		requireSetup();

		try {
			if (!incoming.is_object()) return std::nullopt;
			if (!incoming.contains("nonce") || !incoming.contains("tag") || !incoming.contains("payload")) {
				return std::nullopt;
			}

			const std::string nonceHex   = incoming.at("nonce").get<std::string>();
			const std::string tagHex     = incoming.at("tag").get<std::string>();
			const std::string payloadHex = incoming.at("payload").get<std::string>();

			std::vector<unsigned char> nonce = hexToBytes(nonceHex);
			std::vector<unsigned char> tag   = hexToBytes(tagHex);
			std::vector<unsigned char> ct    = hexToBytes(payloadHex);

			if (nonce.size() != NONCE_LEN_BYTES) return std::nullopt;
			if (tag.size()   != TAG_LEN_BYTES)   return std::nullopt;

			std::vector<unsigned char> pt;
			if (!aesGcmDecrypt(ct, nonce, tag, pt)) {
				return std::nullopt; // auth failed
			}

			const std::string decrypted(reinterpret_cast<const char*>(pt.data()), pt.size());
			const json inner = json::parse(decrypted, nullptr, false);
			if (inner.is_discarded() || !inner.is_object()) return std::nullopt;

			if (!inner.contains("timestamp") || !inner.contains("message")) return std::nullopt;

			const double stamp = inner.at("timestamp").get<double>();
			const double nowS  = nowSeconds();
			if (std::abs(nowS - stamp) > MAX_SKEW_SECONDS) return std::nullopt;

			const uint64_t stampBits = doubleToBits(stamp);
			const uint64_t nowMs = nowMillis();

			// replay cache: store stampBits for 15 seconds
			{
				std::lock_guard<std::mutex> lock(s_handledMutex);

				// prune expired
				for (int i = static_cast<int>(s_handled.size()) - 1; i >= 0; --i) {
					if (nowMs > s_handled[i].expireAtMs) {
						s_handled.erase(s_handled.begin() + i);
					}
				}

				// replay check
				for (const auto& e : s_handled) {
					if (e.stampBits == stampBits) {
						return std::nullopt;
					}
				}

				s_handled.push_back(HandledEntry{nowMs + REPLAY_CACHE_MS, stampBits});
			}

			return inner.at("message");
		} catch (...) {
			return std::nullopt;
		}
	}

	// Convenience for Socket.IO: you can send createPayload(...).dump() as a string.
	static std::string createPayloadString(const json& messageObj) {
		return createPayload(messageObj).dump();
	}

private:
	struct HandledEntry {
		uint64_t expireAtMs;
		uint64_t stampBits;
	};

	inline static const std::vector<unsigned char> SALT = {
		's','t','a','t','i','c','_','s','a','l','t','_','f','o','r','_','r','e','p','e','a','t','a','b','i','l','i','t','y'
	};

	static constexpr int PBKDF2_ITERS = 1'000'000;
	static constexpr size_t KEY_LEN_BYTES = 32;

	static constexpr size_t NONCE_LEN_BYTES = 16; // PyCryptodome default nonce size for GCM
	static constexpr size_t TAG_LEN_BYTES   = 16; // 128-bit tag
	static constexpr int TAG_LEN_BITS       = 128;

	static constexpr double MAX_SKEW_SECONDS = 7.0;
	static constexpr uint64_t REPLAY_CACHE_MS = 15'000;

	inline static std::mutex s_keyMutex;
	inline static bool s_isReady = false;
	inline static std::array<unsigned char, KEY_LEN_BYTES> s_key{};

	inline static std::mutex s_handledMutex;
	inline static std::vector<HandledEntry> s_handled;

	static void requireSetup() {
		std::lock_guard<std::mutex> lock(s_keyMutex);
		if (!s_isReady) {
			std::cerr << "Verify: call setup(password) before use\n";
			return;
		}
	}

	static double nowSeconds() {
		using namespace std::chrono;
		return duration<double>(system_clock::now().time_since_epoch()).count();
	}

	static uint64_t nowMillis() {
		using namespace std::chrono;
		return static_cast<uint64_t>(
			duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
		);
	}

	static uint64_t doubleToBits(double x) {
		uint64_t u = 0;
		static_assert(sizeof(double) == sizeof(uint64_t), "double is not 64-bit");
		std::memcpy(&u, &x, sizeof(u));
		return u;
	}

	static bool aesGcmEncrypt(
		const unsigned char* plaintext,
		size_t plaintextLen,
		std::vector<unsigned char>& ciphertextOut,
		std::array<unsigned char, NONCE_LEN_BYTES>& nonceOut,
		std::array<unsigned char, TAG_LEN_BYTES>& tagOut
	) {
		// Random nonce
		if (RAND_bytes(nonceOut.data(), static_cast<int>(nonceOut.size())) != 1) {
			return false;
		}

		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx) return false;

		bool ok = false;

		do {
			if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;

			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
									static_cast<int>(nonceOut.size()), nullptr) != 1) break;

			std::array<unsigned char, KEY_LEN_BYTES> keyCopy{};
			{
				std::lock_guard<std::mutex> lock(s_keyMutex);
				keyCopy = s_key;
			}

			if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, keyCopy.data(), nonceOut.data()) != 1) break;

			ciphertextOut.resize(plaintextLen);
			int outLen1 = 0;
			if (plaintextLen > 0) {
				if (EVP_EncryptUpdate(ctx,
									 ciphertextOut.data(), &outLen1,
									 plaintext, static_cast<int>(plaintextLen)) != 1) break;
			}

			int outLen2 = 0;
			if (EVP_EncryptFinal_ex(ctx, ciphertextOut.data() + outLen1, &outLen2) != 1) break;

			ciphertextOut.resize(static_cast<size_t>(outLen1 + outLen2));

			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
									static_cast<int>(tagOut.size()), tagOut.data()) != 1) break;

			ok = true;
		} while (false);

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	static bool aesGcmDecrypt(
		const std::vector<unsigned char>& ciphertext,
		const std::vector<unsigned char>& nonce,
		const std::vector<unsigned char>& tag,
		std::vector<unsigned char>& plaintextOut
	) {
		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx) return false;

		bool ok = false;

		do {
			if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;

			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
									static_cast<int>(nonce.size()), nullptr) != 1) break;

			std::array<unsigned char, KEY_LEN_BYTES> keyCopy{};
			{
				std::lock_guard<std::mutex> lock(s_keyMutex);
				keyCopy = s_key;
			}

			if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, keyCopy.data(), nonce.data()) != 1) break;

			plaintextOut.resize(ciphertext.size());
			int outLen1 = 0;
			if (!ciphertext.empty()) {
				if (EVP_DecryptUpdate(ctx,
									 plaintextOut.data(), &outLen1,
									 ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) break;
			}

			// Set expected tag *before* finalizing
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
									static_cast<int>(tag.size()),
									const_cast<unsigned char*>(tag.data())) != 1) break;

			int outLen2 = 0;
			const int ret = EVP_DecryptFinal_ex(ctx, plaintextOut.data() + outLen1, &outLen2);
			if (ret != 1) break; // auth failed

			plaintextOut.resize(static_cast<size_t>(outLen1 + outLen2));
			ok = true;
		} while (false);

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	static std::string bytesToHex(const unsigned char* data, size_t len) {
		static const char* DIGITS = "0123456789abcdef";
		std::string out;
		out.resize(len * 2);
		for (size_t i = 0; i < len; ++i) {
			const unsigned v = data[i];
			out[i * 2 + 0] = DIGITS[(v >> 4) & 0xF];
			out[i * 2 + 1] = DIGITS[(v >> 0) & 0xF];
		}
		return out;
	}

	static int fromHexNibble(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
		if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
		return -1;
	}

	static std::vector<unsigned char> hexToBytes(const std::string& hex) {
		if ((hex.size() % 2) != 0) {
			std::cerr << "hex string must have even length\n";
			return {};
		}
		std::vector<unsigned char> out(hex.size() / 2);
		for (size_t i = 0; i < out.size(); ++i) {
			const int hi = fromHexNibble(hex[i * 2 + 0]);
			const int lo = fromHexNibble(hex[i * 2 + 1]);
			if (hi < 0 || lo < 0) {
				std::cerr << "invalid hex character";
				return {};
			}
			out[i] = static_cast<unsigned char>((hi << 4) | lo);
		}
		return out;
	}
};
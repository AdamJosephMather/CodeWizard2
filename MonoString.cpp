#pragma once

/*
A string implementation that keeps memory alligned with visual characters.

ie 4 bytes -> 1 visual char
*/

//#include <iostream>
//#define DEBUG_MONOSTRING
//#define DEBUG_MONOSTRING_FINICK

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" {
	#include "grapheme.h"
}

class MonoStringTools {
public:
	using u32 = uint32_t;

private:
	// Helper to determine if a specific Unicode codepoint falls within common Emoji blocks
	static bool is_emoji_codepoint(uint_least32_t cp) {
		return
			// Emoji-related BMP symbols.
			cp == 0x00A9 || // ©
			cp == 0x00AE || // ®
			cp == 0x203C || // ‼
			cp == 0x2049 || // ⁉
			cp == 0x2122 || // ™
			cp == 0x2139 || // ℹ
			(cp >= 0x2194 && cp <= 0x21AA) ||
			(cp >= 0x231A && cp <= 0x231B) ||
			cp == 0x2328 ||
			cp == 0x23CF ||
			(cp >= 0x23E9 && cp <= 0x23F3) ||
			(cp >= 0x23F8 && cp <= 0x23FA) ||
			cp == 0x24C2 ||
			(cp >= 0x25AA && cp <= 0x25AB) ||
			cp == 0x25B6 ||
			cp == 0x25C0 ||
			(cp >= 0x25FB && cp <= 0x25FE) ||
			(cp >= 0x2600 && cp <= 0x27BF) ||
			(cp >= 0x2934 && cp <= 0x2935) ||
			(cp >= 0x2B05 && cp <= 0x2B55) ||
			cp == 0x3030 ||
			cp == 0x303D ||
			cp == 0x3297 ||
			cp == 0x3299 ||
	
			// Regional indicator symbols: flags like 🇨🇦.
			(cp >= 0x1F1E6 && cp <= 0x1F1FF) ||
	
			// Enclosed / squared symbols used as emoji.
			(cp >= 0x1F170 && cp <= 0x1F251) ||
	
			// Main emoji planes.
			(cp >= 0x1F300 && cp <= 0x1F5FF) || // Misc Symbols and Pictographs
			(cp >= 0x1F600 && cp <= 0x1F64F) || // Emoticons
			(cp >= 0x1F680 && cp <= 0x1F6FF) || // Transport and Map
			(cp >= 0x1F700 && cp <= 0x1F77F) || // Alchemical symbols; some pictographic coverage
			(cp >= 0x1F780 && cp <= 0x1F7FF) || // Geometric Shapes Extended
			(cp >= 0x1F800 && cp <= 0x1F8FF) || // Supplemental Arrows-C
			(cp >= 0x1F900 && cp <= 0x1F9FF) || // Supplemental Symbols and Pictographs
			(cp >= 0x1FA70 && cp <= 0x1FAFF);   // Symbols and Pictographs Extended-A
	}
	
	static bool is_keycap_sequence(const std::vector<u32>& cps) {
		if (cps.empty()) {
			return false;
		}
	
		auto is_keycap_base = [](u32 cp) {
			return (cp >= U'0' && cp <= U'9') || cp == U'#' || cp == U'*';
		};
	
		// Modern/common form: 3 + FE0F + 20E3
		if (cps.size() == 3 &&
			is_keycap_base(cps[0]) &&
			cps[1] == 0xFE0F &&
			cps[2] == 0x20E3) {
			return true;
		}
	
		// Valid shorter form: 3 + 20E3
		if (cps.size() == 2 &&
			is_keycap_base(cps[0]) &&
			cps[1] == 0x20E3) {
			return true;
		}
	
		return false;
	}

public:
	static constexpr size_t NOT_FOUND = (size_t)-1; // max value
	
	static constexpr u32 MAX_UNICODE = 0x10FFFF;
	static constexpr u32 CONT        = 0x110000;
	static constexpr u32 HANDLE_BASE = 0x110001;
	
	inline static int TAB_WIDTH = 4;
	
	struct MonoString {
		u32* data = nullptr;
		size_t length = 0;
	
		// --- Rule of Three Requirements ---
		~MonoString() { 
			delete[] data; 
		}
	
		MonoString() = default;
	
		MonoString(MonoString&& other) noexcept
			: data(other.data), length(other.length) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "MonoString(MonoString&& other) noexcept\n";
#endif
			
			
			other.data = nullptr;
			other.length = 0;
		}
		
		MonoString& operator=(MonoString&& other) noexcept {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "MonoString& operator=(MonoString&& other) noexcept\n";
#endif
			
			if (this != &other) {
				delete[] data;
				data = other.data;
				length = other.length;
				other.data = nullptr;
				other.length = 0;
			}
			return *this;
		}
		
		MonoString(const MonoString& other) : length(other.length) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "MonoString(const MonoString& other) : length(other.length)\n";
#endif
			
			if (other.data) {
				data = new u32[length];
				std::memcpy(data, other.data, length * sizeof(u32));
			}
		}
		
		MonoString& operator=(const MonoString& other) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "MonoString& operator=(const MonoString& other)\n";
#endif

			if (this != &other) {
				u32* newdata = other.data ? new u32[other.length] : nullptr;
				if (newdata) {
					std::memcpy(newdata, other.data, other.length * sizeof(u32));
				}
				delete[] data;
				data = newdata;
				length = other.length;
			}
			return *this;
		}
		// ----------------------------------
		
		friend bool operator==(const MonoString& lhs, const MonoString& rhs) noexcept {
#ifdef DEBUG_MONOSTRING
			std::cout << "friend bool operator==(const MonoString& lhs, const MonoString& rhs) noexcept\n";
#endif
			
			if (lhs.data == rhs.data) {
				return lhs.length == rhs.length; 
			}
			
			if (lhs.length != rhs.length) {
				return false;
			}
			
			if (lhs.data && rhs.data) {
				return std::memcmp(lhs.data, rhs.data, lhs.length * sizeof(u32)) == 0;
			}
			
			return false;
		}
		
		friend bool operator!=(const MonoString& lhs, const MonoString& rhs) noexcept {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "friend bool operator!=(const MonoString& lhs, const MonoString& rhs) noexcept\n";
#endif

			return !(lhs == rhs);
		}
		// ----------------------------------
	
		MonoString& operator+=(const MonoString& rhs) {
#ifdef DEBUG_MONOSTRING
			std::cout << "MonoString& operator+=(const MonoString& rhs)\n";
#endif
			
			size_t new_length = this->length + rhs.length;
			if (new_length == 0) {
				return *this;
			}
			
			u32* newdata = new u32[new_length];
			
			if (this->data) {
				std::memcpy(newdata, this->data, this->length * sizeof(u32));
			}
			if (rhs.data) {
				std::memcpy(newdata + this->length, rhs.data, rhs.length * sizeof(u32));
			}
			
			delete[] this->data;
			
			this->data = newdata;
			this->length = new_length;
			return *this; 
		}
		
		MonoString operator+(const MonoString& rhs) const {
#ifdef DEBUG_MONOSTRING
			std::cout << "MonoString operator+(const MonoString& rhs) const\n";
#endif
			
			MonoString out;
			out.length = this->length + rhs.length;
			if (out.length == 0) {
				return out;
			}
			
			out.data = new u32[out.length];
			
			if (this->data) {
				std::memcpy(out.data, this->data, this->length * sizeof(u32));
			}
			if (rhs.data) {
				std::memcpy(out.data + this->length, rhs.data, rhs.length * sizeof(u32));
			}
			
			return out; 
		}
	};
private:
	inline static std::vector<std::string> idx_to_stored;
	inline static std::unordered_map<std::string, u32> stored_to_idx;
	inline static std::mutex table_mutex;
	
	inline static std::mutex tab_width_mutex;
	
public:
	static int getTableSize() {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static int getTableSize()\n";
#endif

		std::lock_guard<std::mutex> lock(table_mutex);
		return idx_to_stored.size();
	}
	
	static MonoString fromVector(const std::vector<u32>& v) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString fromVector(const std::vector<u32>& v)\n";
#endif
		
		MonoString out;
		out.length = v.size();
		
		if (v.empty()) {
			out.data = nullptr;
			return out;
		}
		
		out.data = new u32[v.size()];
		std::memcpy(out.data, v.data(), v.size() * sizeof(u32));
		return out;
	}
	
	static u32 idxToChar(u32 idx) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static u32 idxToChar(u32 idx)\n";
#endif
		
		return idx + HANDLE_BASE;
	}
	
	static u32 charToIdx(u32 chr) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static u32 charToIdx(u32 chr)\n";
#endif

		return chr - HANDLE_BASE;
	}
	
	static MonoString toMonoString(const std::string& text) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString toMonoString(const std::string& text)\n";
#endif

		std::vector<u32> vctr;
		
		size_t offset = 0;
		size_t total_bytes = text.length();
		
		while (offset < total_bytes) {
			size_t byte_count = grapheme_next_character_break_utf8(text.data()+offset, total_bytes-offset);
			
			if (byte_count == 0) { // ignore probable decoding errors
				offset += 1;
				continue;
			}
			
			std::string_view visual_char(text.data() + offset, byte_count);
			
			bool has_emoji = false;
			bool is_keycap = false;
			size_t cp_offset = 0;
			std::vector<u32> utf32_output;
			bool skip = false;
			
			while (cp_offset < byte_count) {
				uint_least32_t cp = 0;
				size_t cp_len = grapheme_decode_utf8(text.data() + offset + cp_offset, byte_count - cp_offset, &cp);
				
				if (cp_len == 0) {
					// failed to decode - invalid
					skip = true;
					break;
				}
				
				utf32_output.push_back(static_cast<u32>(cp));
				
				if (is_emoji_codepoint(cp)) {
					has_emoji = true;
				}
				
				cp_offset += cp_len;
			}
			
			is_keycap = is_keycap_sequence(utf32_output);
			
			offset += byte_count;
			
			if (skip) {
				continue;
			}
			
			if (has_emoji || utf32_output.size() > 1 || is_keycap) {
				// we now need to take the string we have (visual_char) and get index in our table
				std::string value{visual_char}; // we now need a copy
				
				std::lock_guard<std::mutex> lock(table_mutex); // lock the tables while we're using them
				
				auto it = stored_to_idx.find(value);
				if (it != stored_to_idx.end()) {
					vctr.push_back(idxToChar(it->second));
				}else{
					u32 idx = idx_to_stored.size();
					
					idx_to_stored.push_back(value); // store the std::string in our table
					stored_to_idx[value] = idx;
					
					vctr.push_back(idxToChar(idx));
				}
				
				if (has_emoji || is_keycap) {
					vctr.push_back(CONT); // all emojis are 2 wide, so add 1 CONT on top of the IDX
				}
				
				continue; // unlocks the table
			}
			
			vctr.push_back(utf32_output[0]);
			
			// we have 1 u32, and it's not an emoji
			if (utf32_output[0] == U'\t') {
				// tabs are ['\t', CONT, CONT, CONT] (or really TAB_WIDTH)
				std::lock_guard<std::mutex> lock(tab_width_mutex);
				for (size_t i = 1; i < TAB_WIDTH; i++) {
					vctr.push_back(CONT);
				}
			}
		}
		
		return fromVector(vctr);
	}
	
	static MonoString toMonoString(const u32 chr) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static MonoString toMonoString(const u32 chr)\n";
#endif
		
		MonoString a;
		
		if (chr == U'\t') {
			a.length = TAB_WIDTH;
		}else{
			a.length = 1;
		}
		
		a.data = new u32[a.length];
		a.data[0] = chr;
		
		for (int i = 1; i < a.length; i++) {
			a.data[i] = CONT;
		}
		
		return a;
	}
	
	static std::string toString(const MonoString& text) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static std::string toString(const MonoString& text)\n";
#endif
		
		std::string out = "";
		
		for (size_t i = 0; i < text.length; i++) {
			u32 chr = text.data[i];
			
			if (chr == CONT) {
				continue;
			}else if (chr > CONT) {
				std::lock_guard<std::mutex> lock(table_mutex); // lock the tables while we're using them
				
				size_t idx = charToIdx(chr);
				if (idx < idx_to_stored.size()) {
					out += idx_to_stored[idx];
				} else {
					out += "\xEF\xBF\xBD"; // U+FFFD replacement char
				}
			}else{
				char utf8_seq[4]; // A UTF-8 codepoint takes a maximum of 4 bytes
				size_t len = grapheme_encode_utf8(chr, utf8_seq, sizeof(utf8_seq));
				out += std::string(utf8_seq, len);
			}
		}
		
		return out;
	}
	
	static std::string toBastardizedStringUtf16Aligned(const MonoString& text) {
#ifdef DEBUG_MONOSTRING
		std::cout << "static std::string toBastardizedStringUtf16Aligned(const MonoString& text)\n";
#endif
	
		// Use for LSP text. LSP positions are normally UTF-16 code-unit offsets.
		// Every MonoString slot must decode to exactly one UTF-16 code unit.
	
		std::string out;
		out.reserve(text.length); // Minimum size. Grows only if non-ASCII BMP chars appear.
	
		for (size_t i = 0; i < text.length; i++) {
			u32 chr = text.data[i];
	
			if (chr == U'\t' || chr == CONT) {
				out.push_back(' ');
			}
			else if (chr > CONT) {
				out.push_back('?');
			}
			else if (chr > 0xFFFF || (chr >= 0xD800 && chr <= 0xDFFF)) {
				out.push_back('?');
			}
			else if (chr < 0x80) {
				out.push_back(static_cast<char>(chr));
			}
			else if (chr < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (chr >> 6)));
				out.push_back(static_cast<char>(0x80 | (chr & 0x3F)));
			}
			else {
				out.push_back(static_cast<char>(0xE0 | (chr >> 12)));
				out.push_back(static_cast<char>(0x80 | ((chr >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (chr & 0x3F)));
			}
		}
	
		return out;
	}
	
	static std::string toBastardizedStringUtf8Aligned(const MonoString& text) {
#ifdef DEBUG_MONOSTRING
		std::cout << "static std::string toBastardizedStringUtf8Aligned(const MonoString& text)\n";
#endif
	
		// Use for byte-offset parsers/highlighters.
		// Every MonoString slot must become exactly one UTF-8 byte.
	
		std::string out;
		out.resize(text.length);
	
		for (size_t i = 0; i < text.length; i++) {
			u32 chr = text.data[i];
	
			if (chr == U'\t' || chr == CONT) {
				out[i] = ' ';
			}
			else if (chr < 0x80 && chr <= CONT) {
				out[i] = static_cast<char>(chr);
			}
			else {
				out[i] = '?';
			}
		}
	
		return out;
	}
	
	static MonoString removeRange(const MonoString& inpt, size_t start, size_t end) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString removeRange(const MonoString& inpt, size_t start, size_t end)\n";
#endif
		
		// deletes from start (inclusive) to end (not inclusive)
		
		if (!inpt.data) {
			return {};
		}
		
		MonoString out;
		out.length = inpt.length-(end-start);
		if (out.length == 0) {
			return out;
		}
		
		out.data = new u32[out.length];
		
		std::memcpy(out.data, inpt.data, start * sizeof(u32));
		std::memcpy(out.data+start, inpt.data+end, (inpt.length-end) * sizeof(u32));
		
		return out;
	}
	
	static MonoString insertAt(const MonoString& inpt, size_t start, const MonoString& add) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString insertAt(const MonoString& inpt, size_t start, const MonoString& add)\n";
#endif
		// inserts add at start location in inpt
		
		if (!add.data || !inpt.data) {
			if (add.data) {
				return add;
			}else if (inpt.data) {
				return inpt;
			}
			
			return {};
		}
		
		MonoString out;
		out.length = inpt.length+add.length;
		if (out.length == 0) {
			return out;
		}
		out.data = new u32[out.length];
		
		std::memcpy(out.data, inpt.data, start * sizeof(u32));
		std::memcpy(out.data+start, add.data, add.length * sizeof(u32));
		std::memcpy(out.data+start+add.length, inpt.data+start, (inpt.length-start) * sizeof(u32));
		
		return out;
	}
	
	static std::vector<MonoString> split(const MonoString& inpt, u32 splitOn) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static std::vector<MonoString> split(const MonoString& inpt, u32 splitOn)\n";
#endif
		
		// split into list of MonoString by character
		if (!inpt.data) {
			return std::vector<MonoString>{ MonoString{} };
		}
		
		std::vector<MonoString> out;
		
		MonoString current;
		size_t lastHandled = 0;
		
		for (size_t i = 0; i < inpt.length; i++) {
			if (inpt.data[i] == splitOn) {
				current.length = i-lastHandled;
				if (current.length != 0) {
					current.data = new u32[current.length];
					std::memcpy(current.data, inpt.data+lastHandled, current.length * sizeof(u32));
				}
				
				out.push_back(current);
				lastHandled = i+1; // skip the character we're splitting on
				current = MonoString{};
			}
		}
		
		
		current.length = inpt.length-lastHandled;
		
		if (current.length != 0) {
			current.data = new u32[current.length];
			std::memcpy(current.data, inpt.data+lastHandled, current.length * sizeof(u32));
		}
		
		out.push_back(current);
		
		return out;
	}
	
	static MonoString join(const std::vector<MonoString>& items, const MonoString& joiner) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString join(const std::vector<MonoString>& items, const MonoString& joiner)\n";
#endif
		
		if (items.empty()) {
			return MonoString{};
		}
	
		size_t totalLength = 0;
		for (const auto& item : items) {
			totalLength += item.length;
		}
		if (items.size() > 1) {
			totalLength += joiner.length * (items.size() - 1);
		}
	
		if (totalLength == 0) {
			return MonoString{};
		}
	
		MonoString out;
		out.length = totalLength;
		out.data = new u32[totalLength];
	
		size_t offset = 0;
	
		if (items[0].data && items[0].length > 0) {
			std::memcpy(out.data + offset, items[0].data, items[0].length * sizeof(u32));
			offset += items[0].length;
		}
	
		for (size_t l = 1; l < items.size(); l++) {
			if (joiner.data && joiner.length > 0) {
				std::memcpy(out.data + offset, joiner.data, joiner.length * sizeof(u32));
				offset += joiner.length;
			}
			if (items[l].data && items[l].length > 0) {
				std::memcpy(out.data + offset, items[l].data, items[l].length * sizeof(u32));
				offset += items[l].length;
			}
		}
	
		return out;
	}
	
	static MonoString stripOfChar(const MonoString& string, const u32 to_strip) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString stripOfChar(const MonoString& string, const u32 to_strip)\n";
#endif
		if (!string.data || string.length == 0) {
			return MonoString{};
		}
	
		std::vector<u32> vctr;
		vctr.reserve(string.length);
	
		size_t i = 0;
		while (i < string.length) {
			if (string.data[i] == to_strip) {
				i++;
				while (i < string.length && string.data[i] == CONT) {
					i++;
				}
			} else {
				vctr.push_back(string.data[i]);
				i++;
			}
		}
	
		return fromVector(vctr);
	}
	
	static MonoString substring(const MonoString& inpt, size_t start, size_t end) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString substring(const MonoString& inpt, size_t start, size_t end)\n";
#endif
		
		if (!inpt.data) {
			return MonoString{};
		}
		
		if (start >= inpt.length) {
			return MonoString{};
		}
		
		if (end > inpt.length) {
			end = inpt.length;
		}
		
		MonoString out;
		out.length = end-start;
		if (out.length == 0) {
			return out;
		}
		out.data = new u32[out.length];
		std::memcpy(out.data, inpt.data+start, out.length * sizeof(u32));
		
		return out;
	}
	
	static size_t index(
		const MonoString& inpt,
		size_t start,
		const MonoString& searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static size_t index(const MonoString& inpt, size_t start, const MonoString& searchfor, bool ignoreCase = false)\n";
#endif
		if (!inpt.data || !searchfor.data || searchfor.length > inpt.length || searchfor.length == 0) {
			return NOT_FOUND;
		}
	
		size_t end = inpt.length - searchfor.length + 1;
	
		for (size_t i = start; i < end; i++) {
			bool foundit = true;
	
			for (size_t j = 0; j < searchfor.length; j++) {
				if (!charsEqual(inpt.data[i + j], searchfor.data[j], ignoreCase)) {
					foundit = false;
					break;
				}
			}
	
			if (foundit) {
				return i;
			}
		}
	
		return NOT_FOUND;
	}
	
	static size_t index(
		const MonoString& inpt,
		size_t start,
		const u32 searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static size_t index(const MonoString& inpt, size_t start, const u32 searchfor, bool ignoreCase = false)\n";
#endif

		
		// index, starting at start and working forward
		if (!inpt.data || inpt.length == 0) {
			return NOT_FOUND;
		}
	
		u32 needle = searchfor;
	
		if (ignoreCase && needle >= U'A' && needle <= U'Z') {
			needle += (U'a' - U'A');
		}
	
		for (size_t i = start; i < inpt.length; i++) {
			u32 ch = inpt.data[i];
	
			if (ignoreCase && ch >= U'A' && ch <= U'Z') {
				ch += (U'a' - U'A');
			}
	
			if (ch == needle) {
				return i;
			}
		}
	
		return NOT_FOUND;
	}
	
	static size_t indexBackwards(
		const MonoString& inpt,
		size_t start,
		const MonoString& searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static size_t indexBackwards(const MonoString& inpt, size_t start, const MonoString& searchfor, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data || !searchfor.data || searchfor.length > inpt.length || searchfor.length == 0) {
			return NOT_FOUND;
		}
	
		size_t end = inpt.length - searchfor.length;
		if (start < end) {
			end = start;
		}
	
		for (size_t i = end + 1; i-- > 0; ) {
			bool foundit = true;
	
			for (size_t j = 0; j < searchfor.length; j++) {
				if (!charsEqual(inpt.data[i + j], searchfor.data[j], ignoreCase)) {
					foundit = false;
					break;
				}
			}
	
			if (foundit) {
				return i;
			}
		}
	
		return NOT_FOUND;
	}
	
	static size_t indexBackwards(
		const MonoString& inpt,
		size_t start,
		const u32 searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static size_t indexBackwards(const MonoString& inpt, size_t start, const u32 searchfor, bool ignoreCase = false)\n";
#endif
		
		// index, starting at start and working backward
		if (!inpt.data || inpt.length == 0) {
			return NOT_FOUND;
		}
	
		u32 needle = searchfor;
	
		if (ignoreCase && needle >= U'A' && needle <= U'Z') {
			needle += (U'a' - U'A');
		}
	
		size_t end = inpt.length - 1;
		if (start < end) {
			end = start;
		}
	
		for (size_t i = end + 1; i-- > 0; ) {
			u32 ch = inpt.data[i];
	
			if (ignoreCase && ch >= U'A' && ch <= U'Z') {
				ch += (U'a' - U'A');
			}
	
			if (ch == needle) {
				return i;
			}
		}
	
		return NOT_FOUND;
	}
	
	static u32 lowerCharAscii(u32 ch) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static u32 lowerCharAscii(u32 ch)\n";
#endif
		
		if (ch >= U'A' && ch <= U'Z') {
			return ch + (U'a' - U'A');
		}
	
		return ch;
	}
	
	static bool charsEqual(u32 a, u32 b, bool ignoreCase) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool charsEqual(u32 a, u32 b, bool ignoreCase)\n";
#endif
		
		if (!ignoreCase) {
			return a == b;
		}
	
		// Do not case-fold CONT cells or stored grapheme/emoji handles.
		if (a == CONT || b == CONT || a >= HANDLE_BASE || b >= HANDLE_BASE) {
			return a == b;
		}
	
		return lowerCharAscii(a) == lowerCharAscii(b);
	}
	
	static bool startsWith(
		const MonoString& inpt,
		const MonoString& searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool startsWith(const MonoString& inpt, const MonoString& searchfor, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data) {
			return false;
		}
		
		if (!searchfor.data) {
			return true;
		}
	
		if (searchfor.length > inpt.length) {
			return false;
		}
	
		for (size_t i = 0; i < searchfor.length; i++) {
			if (!charsEqual(inpt.data[i], searchfor.data[i], ignoreCase)) {
				return false;
			}
		}
	
		return true;
	}
	
	static bool startsWith(
		const MonoString& inpt,
		const u32 searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool startsWith(const MonoString& inpt, const u32 searchfor, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data || inpt.length == 0) {
			return false;
		}
	
		return charsEqual(inpt.data[0], searchfor, ignoreCase);
	}
	
	static bool endsWith(
		const MonoString& inpt,
		const MonoString& searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool endsWith(const MonoString& inpt, const MonoString& searchfor, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data) {
			return false;
		}
	
		if (!searchfor.data) {
			return true;
		}
	
		if (searchfor.length > inpt.length) {
			return false;
		}
	
		size_t offset = inpt.length - searchfor.length;
	
		for (size_t i = 0; i < searchfor.length; i++) {
			if (!charsEqual(inpt.data[offset + i], searchfor.data[i], ignoreCase)) {
				return false;
			}
		}
	
		return true;
	}
	
	static bool endsWith(
		const MonoString& inpt,
		const u32 searchfor,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool endsWith(const MonoString& inpt, const u32 searchfor, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data || inpt.length == 0) {
			return false;
		}
	
		return charsEqual(inpt.data[inpt.length - 1], searchfor, ignoreCase);
	}
	
	static MonoString replaceAll(
		const MonoString& inpt,
		const MonoString& search,
		const MonoString& replace,
		bool ignoreCase = false
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString replaceAll(const MonoString& inpt, const MonoString& search, const MonoString& replace, bool ignoreCase = false)\n";
#endif
		
		if (!inpt.data || !search.data || search.length == 0 || search.length > inpt.length) {
			return inpt;
		}
	
		// Pass 1: find all match positions.
		std::vector<size_t> positions;
		size_t start = 0;
		while (true) {
			size_t idx = index(inpt, start, search, ignoreCase);
			if (idx == NOT_FOUND) break;
			positions.push_back(idx);
			start = idx + search.length;
		}
	
		if (positions.empty()) {
			return inpt;
		}
	
		size_t replaceLen = (replace.data ? replace.length : 0);
		size_t totalLength = inpt.length
			- positions.size() * search.length
			+ positions.size() * replaceLen;
	
		if (totalLength == 0) {
			return MonoString{};
		}
	
		// Pass 2: build output in one allocation.
		MonoString out;
		out.length = totalLength;
		out.data = new u32[totalLength];
	
		size_t src = 0;
		size_t dst = 0;
	
		for (size_t matchPos : positions) {
			size_t segLen = matchPos - src;
			if (segLen > 0) {
				std::memcpy(out.data + dst, inpt.data + src, segLen * sizeof(u32));
				dst += segLen;
			}
			if (replaceLen > 0) {
				std::memcpy(out.data + dst, replace.data, replaceLen * sizeof(u32));
				dst += replaceLen;
			}
			src = matchPos + search.length;
		}
	
		size_t tailLen = inpt.length - src;
		if (tailLen > 0) {
			std::memcpy(out.data + dst, inpt.data + src, tailLen * sizeof(u32));
		}
	
		return out;
	}
	
	static bool isAsciiDigit(u32 ch) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static bool isAsciiDigit(u32 ch)\n";
#endif
		
		return ch >= U'0' && ch <= U'9';
	}
	
	static bool snippetPlaceholderAt(
		const MonoString& text,
		size_t start,
		size_t& end
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static bool snippetPlaceholderAt(const MonoString& text, size_t start, size_t& end)\n";
#endif
		
		// Matches:
		// $1
		// $123
		// ${1:text}
		// ${123:text}
	
		end = start;
	
		if (!text.data || start >= text.length) {
			return false;
		}
	
		if (text.data[start] != U'$') {
			return false;
		}
	
		size_t i = start + 1;
	
		if (i >= text.length) {
			return false;
		}
	
		// $1 / $123
		if (isAsciiDigit(text.data[i])) {
			i++;
	
			while (i < text.length && isAsciiDigit(text.data[i])) {
				i++;
			}
	
			end = i;
			return true;
		}
	
		// ${1:text}
		if (text.data[i] == U'{') {
			i++;
	
			if (i >= text.length || !isAsciiDigit(text.data[i])) {
				return false;
			}
	
			while (i < text.length && isAsciiDigit(text.data[i])) {
				i++;
			}
	
			if (i >= text.length || text.data[i] != U':') {
				return false;
			}
	
			i++;
	
			while (i < text.length && text.data[i] != U'}') {
				i++;
			}
	
			if (i >= text.length || text.data[i] != U'}') {
				return false;
			}
	
			end = i + 1;
			return true;
		}
	
		return false;
	}
	
	static MonoString removeSnippetPlaceholders(
		const MonoString& input,
		int& firstIndex
	) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString removeSnippetPlaceholders(const MonoString& input, int& firstIndex )\n";
#endif
		
		firstIndex = -1;
	
		if (!input.data || input.length == 0) {
			return MonoString{};
		}
	
		std::vector<u32> vctr;
		vctr.reserve(input.length);
	
		for (size_t i = 0; i < input.length; ) {
			size_t placeholderEnd = i;
	
			if (snippetPlaceholderAt(input, i, placeholderEnd)) {
				if (firstIndex == -1) {
					firstIndex = static_cast<int>(vctr.size());
				}
				i = placeholderEnd;
				continue;
			}
	
			vctr.push_back(input.data[i]);
			i++;
		}
	
		return fromVector(vctr);
	}
	
	static MonoString toLower(const MonoString& inpt) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString toLower(const MonoString& inpt)\n";
#endif
		
		if (!inpt.data || inpt.length == 0) {
			return MonoString{};
		}
	
		MonoString out;
		out.length = inpt.length;
		out.data = new u32[out.length];
	
		for (size_t i = 0; i < inpt.length; i++) {
			u32 ch = inpt.data[i];
	
			// Preserve CONT cells and stored grapheme/emoji handles.
			if (ch == CONT || ch >= HANDLE_BASE) {
				out.data[i] = ch;
				continue;
			}
	
			// ASCII lowercase only.
			if (ch >= U'A' && ch <= U'Z') {
				out.data[i] = ch + (U'a' - U'A');
			} else {
				out.data[i] = ch;
			}
		}
	
		return out;
	}
	
	static std::pair<size_t, size_t> getCharInfo(const MonoString& inpt, size_t idx) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static std::pair<size_t, size_t> getCharInfo(const MonoString& inpt, size_t idx)\n";
#endif
		
		// returns {first_cell(inclusive), last_cell(exclusive)}
		
		if (!inpt.data || idx >= inpt.length) {
			return {NOT_FOUND, NOT_FOUND};
		}
		
		size_t left = idx;
		
		for (size_t i = left+1; i-- > 0;) {
			left = i;
			
			if (inpt.data[i] != CONT) {
				break;
			}
		}
		
		size_t right = idx;
		for (size_t i = right+1; i < inpt.length; i++) {
			if (inpt.data[i] != CONT) {
				break;
			}
			
			right = i;
		}
		
		return {left, right+1};
	}
	
	static void setTabWidth(int wdth) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static void setTabWidth(int wdth)\n";
#endif
		std::lock_guard<std::mutex> lock(tab_width_mutex);
		TAB_WIDTH = wdth;
	}
	
	static MonoString updateTabWidth(const MonoString& inpt) {
#ifdef DEBUG_MONOSTRING
			std::cout << "static MonoString updateTabWidth(const MonoString& inpt)\n";
#endif
		
		if (!inpt.data) {
			return inpt;
		}
		
		bool in_tab = false;
		std::vector<u32> vctr{};
		
		for (size_t i = 0; i < inpt.length; i++) {
			if (in_tab && inpt.data[i] == CONT) {
				continue;
			}else if (in_tab) {
				in_tab = false; // if we were in a tab, but no longer on a CONT, then we're no longer in a tab
			}
			
			vctr.push_back(inpt.data[i]);
			
			if (inpt.data[i] == U'\t') {
				in_tab = true;
				
				std::lock_guard<std::mutex> lock(tab_width_mutex);
				for (int j = 1; j < TAB_WIDTH; j++) {
					vctr.push_back(CONT);
				}
			}
		}
		
		return fromVector(vctr);
	}
	
	static bool isEmoji(const MonoString& inpt, size_t idx) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static bool isEmoji(const MonoString& inpt, size_t idx)\n";
#endif
		
		return inpt.data[idx] > CONT;
	}
	
	static bool skipIdx(const MonoString& inpt, size_t idx) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static bool skipIdx(const MonoString& inpt, size_t idx)\n";
#endif
		
		return inpt.data[idx] == CONT;
	}
	
	static std::string getEmoji(const MonoString& inpt, size_t idx) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static std::string getEmoji(const MonoString& inpt, size_t idx)\n";
#endif
		
		std::lock_guard<std::mutex> lock(table_mutex);
		u32 handle_index = charToIdx(inpt.data[idx]);
		if (handle_index < idx_to_stored.size()) {
			return idx_to_stored[handle_index];
		}
		return {};
	}
	
	static u32 char32At(const MonoString& inpt, size_t idx) {
#ifdef DEBUG_MONOSTRING_FINICK
			std::cout << "static u32 char32At(const MonoString& inpt, size_t idx)\n";
#endif
		
		if (idx >= inpt.length) {
			return U'\0';
		}
		
		return inpt.data[idx];
	}
};

using MST = MonoStringTools;
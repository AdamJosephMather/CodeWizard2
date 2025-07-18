#pragma once

#include <functional>
#include <iostream>
#include <vector>
#include "unicode/unistr.h"
#include <unicode/unum.h>
#include <windows.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <map>

using SearchFileKey  = std::pair<std::string, std::string>;
using SearchMatch    = std::pair<int, std::string>;
using SearchMatchVec = std::vector<SearchMatch>;
using SearchResult   = std::map<SearchFileKey, SearchMatchVec>;

struct FileIndexResult {
	std::vector<std::string> indexedNames;       // just the file names
	std::vector<icu::UnicodeString> displayPaths;       // cropped, relative display paths
	std::vector<std::string> fullPaths;          // full absolute paths (including “:Command” entries)
	std::vector<int> currentlyshowing;
	std::vector<int> currentlyshowingtype;
};

struct Language {
	std::string name = "";
	icu::UnicodeString line_comment = "";
	std::vector<std::string> filetypes = {};
	std::string textmatefile = "";
	std::string lsp = "";
	std::string build_command = "";
};

struct FileInfo {
	std::string filepath;
	std::string filename;
	bool ondisk;
};

struct Color {
	float r;
	float g;
	float b;
	float a;
};

inline Color* MakeColor(float r, float g, float b, float a = 1.0f){
	auto c = new Color();
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
	return c;
}

inline std::vector<Color*> AllOneColor(Color* c, int count){
	std::vector<Color*> cs;
	cs.reserve(count);
	
	for (int i = 0; i < count; i++) {
		cs.push_back(c);
	}
	
	return cs;
}

struct Theme {
	Color* main_text_color;
	Color* lesser_text_color;
	Color* main_background_color;
	Color* extras_background_color;
	Color* hover_background_color;
	Color* darker_background_color;
	Color* overlay_background_color;
	Color* border;
	
	Color* add_diff;
	Color* del_diff;
	Color* equal_diff;
	
	Color* tint_color;
	
	Color* white;
	Color* black;
	
	Color* error_color;
	Color* warning_color;
	Color* suggestion_color;
	
	Color* syntax_colors[9];
};

static std::vector<icu::UnicodeString> splitByChar(const icu::UnicodeString& input, UChar delimiter) {
	std::vector<icu::UnicodeString> result;
	int32_t start = 0;
	int32_t pos;
	
	while ((pos = input.indexOf(delimiter, start)) != -1) {
		result.push_back(input.tempSubStringBetween(start, pos));
		start = pos + 1;
	}
	
	// Add the last part
	result.push_back(input.tempSubString(start));
	return result;
}

static icu::UnicodeString joinByString(const std::vector<icu::UnicodeString> items, const icu::UnicodeString joiner) {
	icu::UnicodeString out = items[0];
	
	for (int l = 1; l < items.size(); l ++) {
		out += joiner;
		out += items[l];
	}
	
	return out;
}

static icu::UnicodeString stripOfChar(const icu::UnicodeString string, const UChar32 to_strip) {
	icu::UnicodeString result = string;
	int32_t pos = result.indexOf(to_strip);
	while (pos != -1) {
		result.remove(pos, 1);
		pos = result.indexOf(to_strip);
	}
	return result;
}

static icu::UnicodeString replaceWith(const icu::UnicodeString base, const icu::UnicodeString before, const icu::UnicodeString replacewith) {
	icu::UnicodeString result = base;
	int32_t pos = result.indexOf(before);
	while (pos != -1) {
		// replace 'before' at pos with 'replacewith'
		result.replace(pos, before.length(), replacewith);
		// advance past the newly-inserted text to avoid re-matching
		pos = result.indexOf(before, pos + replacewith.length());
	}
	return result;
}

static std::string GetClipboardText() {
	if (!OpenClipboard(nullptr)) return "";

	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (!hData) {
		CloseClipboard();
		return "";
	}

	wchar_t* wszText = static_cast<wchar_t*>(GlobalLock(hData));
	if (!wszText) {
		CloseClipboard();
		return "";
	}

	std::wstring wstr(wszText);
	GlobalUnlock(hData);
	CloseClipboard();

	// Convert UTF-16 (wstring) to UTF-8 (string)
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string utf8(len - 1, '\0'); // exclude null terminator
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8.data(), len, nullptr, nullptr);

	return utf8;
}

static void SetClipboardText(const std::string& text) {
	// Convert UTF-8 to UTF-16
	int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wlen == 0) return;

	HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
	if (!hGlob) return;

	wchar_t* wtext = static_cast<wchar_t*>(GlobalLock(hGlob));
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext, wlen);
	GlobalUnlock(hGlob);

	if (OpenClipboard(nullptr)) {
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hGlob);
		CloseClipboard();
		// Do not free hGlob; system owns it now
	} else {
		GlobalFree(hGlob); // clean up if clipboard open fails
	}
}

static std::string getExecutablePath() {
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	return std::string(path);
}

static std::string getExecutableDir() {
	std::string exePath = getExecutablePath();
	return std::filesystem::path(exePath).parent_path().string();
}

static int analyzeForFixit(const std::vector<icu::UnicodeString>& lines) {
	int two = 0;
	int four = 0;
	
	int prev = 0;
	
	for (auto l : lines) {
		if (l.length() == 0) {
			continue;
		} 
		
		int this_l = 0;
		for (int c = 0; c < l.length(); c++) {
			UChar32 uc = l.char32At(c);
			if (uc != U' ') {
				break;
			}
			this_l ++;
		}
		
		int diff = std::abs(this_l-prev);
		
		if (diff == 2) {
			two++;
		}else if (diff == 4){
			four++;
		}
		
		prev = this_l;
	}
	
	if (two == 0 && four == 0) {
		return 0;
	}else if (two >= four){
		return 2;
	}else {
		return 4;
	}
}

static icu::UnicodeString run_fixit_on_lines(std::vector<icu::UnicodeString> lines) {
	int indent = analyzeForFixit(lines);
	if (indent == 0) return joinByString(lines, "\n");
	
	icu::UnicodeString tabUnit("\t");
	UChar32 newline = U'\n';
	
	icu::UnicodeString newText;
	for (int i = 0; i < lines.size(); i++) {
		const auto& origLine = lines[i];
		
		int32_t origSpaces = 0;
		for (int32_t c = 0; c < origLine.length(); ++c) {
			if (origLine.char32At(c) == U' ') ++origSpaces;
			else break;
		}
		
		int levels = origSpaces / indent;
		
		icu::UnicodeString fixedPrefix;
		fixedPrefix.remove();  // ensure empty
		for (int i = 0; i < levels; ++i) {
			fixedPrefix += tabUnit;
		}
		
		icu::UnicodeString rest = origLine.tempSubString(levels*indent);
		
		newText += fixedPrefix;
		newText += rest;
		if (i != lines.size()-1) {
			newText += newline;
		}
	}
	
	return newText;
}

static icu::UnicodeString run_fixit_on_text(icu::UnicodeString text) {
	return run_fixit_on_lines( splitByChar(text, U'\n') );
}

static bool areSameFile(const std::string& path1, const std::string& path2) {
	try {
		return std::filesystem::equivalent(std::filesystem::path(path1), std::filesystem::path(path2));
	} catch (const std::filesystem::filesystem_error& e) {
		// Handle errors (e.g., file doesn't exist)
		return false;
	}
}

// Decode %XX sequences in URI
static std::string uriDecode(const std::string& uri) {
	std::ostringstream out;
	for (size_t i = 0; i < uri.size(); ++i) {
		if (uri[i] == '%' && i + 2 < uri.size()) {
			std::string hex = uri.substr(i + 1, 2);
			char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
			out << decoded;
			i += 2;
		} else if (uri[i] == '+') {
			out << ' ';
		} else {
			out << uri[i];
		}
	}
	return out.str();
}

// Strip "file://" prefix and decode
static std::string fileUriToPath(const std::string& uri) {
	std::string path = uri;
	const std::string prefix = "file://";
	if (path.rfind(prefix, 0) == 0) {
#ifdef _WIN32
		// Remove 'file:///' and keep drive letter
		path = path.substr(8);  // skip file:///
#else
		path = path.substr(7);  // skip file://
#endif
	}
	return uriDecode(path);
}

static icu::UnicodeString doubleToUnicodeString(double value) {
	std::string ascii = std::to_string(value);
	return icu::UnicodeString::fromUTF8(ascii);
}

static double unicodeStringToDouble_quick(const icu::UnicodeString& str, bool& worked) {
	std::string ascii;
	str.toUTF8String(ascii);        // UTF-8 → ASCII (for digits & . it's a no-op)
	try {
		worked = true;
		return std::stod(ascii);
	} catch (const std::exception&) {
		worked = false;
		return 0.0;
	}
}

static std::pair<bool, icu::UnicodeString> checkForAndEliminate(icu::UnicodeString expr, UChar32 symbol, std::function<double(double,double)> run){
	icu::UnicodeString val1;
	icu::UnicodeString val2;

	icu::UnicodeString afterItterationExpression = "";

	icu::UnicodeString partsOfDigit = "0987654321";

	while (expr.indexOf(symbol) != -1) {
		auto parts = splitByChar(expr, symbol);
		auto p1 = parts[0];
		auto p2 = parts[1];

		icu::UnicodeString trueDig1 = "";
		icu::UnicodeString trueDig2 = "";
		
		bool seenDot = false;
		for (int i = p1.length()-1; i >= 0; i--) {
			auto c = p1.char32At(i);
			if (c == U'.') {
				if (seenDot) return {false, icu::UnicodeString()};
				seenDot = true;
			}else if (c == U'-') {
				trueDig1 = c + trueDig1;
				break;
			}else if (partsOfDigit.indexOf(c) == -1) break;

			trueDig1 = c + trueDig1;
		}

		seenDot = false;
		bool added = false;
		for (int i = 0; i < p2.length(); i++) {
			auto c = p2.char32At(i);
			if (c == U'.') {
				if (seenDot) return {false, icu::UnicodeString()};
				seenDot = true;
			}else if (c == U'-' && !added) {
			}else if (partsOfDigit.indexOf(c) == -1) break;

			trueDig2 += c;
			if (c != U'-') {
				added = true;
			}
		}

		bool ok;
		double v1 = unicodeStringToDouble_quick(trueDig1, ok);
		if (!ok) return {false, icu::UnicodeString()};
		double v2 = unicodeStringToDouble_quick(trueDig2, ok);
		if (!ok) return {false, icu::UnicodeString()};

		double res = run(v1, v2);

		icu::UnicodeString startedWith = trueDig1+symbol+trueDig2;

		int index = expr.indexOf(startedWith);
		if (index != -1) {
			expr = expr.tempSubStringBetween(0, index) + doubleToUnicodeString(res) + expr.tempSubString(index + startedWith.length());
		}
	}
	return {true, expr};
}

static std::pair<bool, double> calcExpression(icu::UnicodeString expression) { // we are going to recurse on brackets...
	if (expression == "") return {false, 0.0};
	
	expression = stripOfChar(expression, UChar32(' '));
	expression = stripOfChar(expression, UChar32(','));
	expression = replaceWith(expression, icu::UnicodeString::fromUTF8(")("), icu::UnicodeString::fromUTF8(")*("));
	
	icu::UnicodeString allowed = icu::UnicodeString::fromUTF8("0987654321+-/*()%^.");

	icu::UnicodeString newExpression = "";

	int openedBrackets = 0;
	icu::UnicodeString subExpression = "";

	for (int char_indx = 0; char_indx < expression.length(); char_indx++) {
		auto c = expression.char32At(char_indx);
		if (allowed.indexOf(c) == -1) return {false, 0.0};
		
		if (c == U'(') {
			openedBrackets += 1;
			if (openedBrackets == 1) {
				subExpression = icu::UnicodeString();
			}
		}else if (c == U')') {
			openedBrackets -= 1;
			if (openedBrackets < 0) {
				return {false, 0.0};
			}else if (openedBrackets == 0) {
				auto [isValid, result] = calcExpression(subExpression);
				if (!isValid) return {false, 0.0};

				newExpression += doubleToUnicodeString(result);
			}
		}else if (openedBrackets != 0) {
			subExpression.append(c);
		}else {
			newExpression.append(c);
		}
	}
	
	if (openedBrackets != 0) return {false, 0.0};
	
	// our resulting expression will no longer contain brackets. They have been recursively removed. And replaced with numbers. (note they are decimal numbers)
	// thus we just need to do a work from left to right where we find the ops in "b e dm as" order.
	
	// the plan is to itterate over the expression 3 times, we already did b. So we'll do exponents, dm, and as.
	
	auto afterExp = checkForAndEliminate(newExpression, U'^', [&](double frst, double scnd){
		return pow(frst, scnd);
	});
	if (!afterExp.first) { return {false, 0.0}; }
	afterExp = checkForAndEliminate(afterExp.second, U'*', [&](double frst, double scnd){
		return frst * scnd;
	});
	if (!afterExp.first) { return {false, 0.0}; }
	afterExp = checkForAndEliminate(afterExp.second, U'/', [&](double frst, double scnd){
		return frst / scnd;
	});
	if (!afterExp.first) { return {false, 0.0}; }
	afterExp = checkForAndEliminate(afterExp.second, U'%', [&](double frst, double scnd){
		return std::fmod(frst, scnd);
	});
	if (!afterExp.first) { return {false, 0.0}; }
	
	newExpression = afterExp.second;
	
	icu::UnicodeString mm = icu::UnicodeString::fromUTF8("--");
	icu::UnicodeString pp = icu::UnicodeString::fromUTF8("++");
	icu::UnicodeString pm = icu::UnicodeString::fromUTF8("+-");
	icu::UnicodeString mp = icu::UnicodeString::fromUTF8("-+");
	icu::UnicodeString p = icu::UnicodeString::fromUTF8("+");
	icu::UnicodeString m = icu::UnicodeString::fromUTF8("-");
	
	
	while (newExpression.indexOf(mm) != -1 || newExpression.indexOf(pp) != -1 || newExpression.indexOf(pm) != -1 || newExpression.indexOf(mp) != -1) {
		if (newExpression.indexOf(mm) != -1) newExpression = replaceWith(newExpression, mm, p);
		if (newExpression.indexOf(pp) != -1) newExpression = replaceWith(newExpression, pp, p);
		if (newExpression.indexOf(pm) != -1) newExpression = replaceWith(newExpression, pm, m);
		if (newExpression.indexOf(mp) != -1) newExpression = replaceWith(newExpression, mp, m);
	}

	double runningTotal = 0.0;
	icu::UnicodeString next;
	icu::UnicodeString op;
	
	icu::UnicodeString partsOfDigit = "0987654321";

	for (int i = 0; i < newExpression.length(); i++) {
		auto c = newExpression.char32At(i);

		if (partsOfDigit.indexOf(c) != -1 || c == U'.') {
			next += c;
		}
		if (c == U'+' || c == U'-' || i == newExpression.length()-1) {
			bool ok;
			double nextDub = unicodeStringToDouble_quick(next, ok);
			if (next.length() == 0) {
				nextDub = 0;
			}else if (!ok) return {false, 0.0};
			
			if (op.length() == 0) {
				runningTotal = nextDub;
				next = "";
			}else {
				if (op == icu::UnicodeString::fromUTF8("+")) {
					runningTotal += nextDub;
				}else {
					runningTotal -= nextDub;
				}

				next = icu::UnicodeString();
			}

			op = c;
		}
	}

	return {true, runningTotal};
}

static std::string toLower(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) out.push_back(std::tolower(c));
	return out;
}

static bool caseInsensitiveFind(const std::string& hay, const std::string& needle) {
	auto h = toLower(hay);
	auto n = toLower(needle);
	return h.find(n) != std::string::npos;
}

static bool caseInsensitiveFindAlreadyLowered(const std::string& hay, const std::string& needle) {
	auto h = toLower(hay);
	return h.find(needle) != std::string::npos;
}

static std::string trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
	return s.substr(b, e - b);
}


static bool isBinaryFile(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		// Couldn’t open → treat as “binary”/ignore
		return true;
	}

	// Read up to the first 4 KB
	const std::size_t MAX_CHECK = 4096;
	std::vector<unsigned char> buf(MAX_CHECK);
	file.read(reinterpret_cast<char*>(buf.data()),
	 buf.size());
	std::streamsize n = file.gcount();

	if (n == 0) {
		// Empty file is text
		return false;
	}

	int controlCount = 0;
	for (std::streamsize i = 0; i < n; ++i) {
		unsigned char c = buf[i];
		if (c == 0) {
			// A single NUL byte almost certainly means binary
			return true;
		}
		// Count C0 controls except: TAB (9), LF (10), VT (11), FF (12), CR (13)
		if ( (c < 9) ||
			 (c > 13 && c < 32) ) 
		{
			++controlCount;
			// If more than 1% of bytes are “odd” controls, call it binary
			if (controlCount > static_cast<int>(n / 100)) {
				return true;
			}
		}
		// NOTE: we do *not* reject c >= 128 here
	}

	// Passed all binary tests → text
	return false;
}

static std::string colorToString(Color* c) {
	int r = (int)(c->r*255.0);
	int g = (int)(c->g*255.0);
	int b = (int)(c->b*255.0);
	return std::to_string(r)+","+std::to_string(g)+","+std::to_string(b);
}

static Color stringToColor(std::string s, bool& worked) {
	worked = true;
	Color c;
	
	std::vector<int> vals = { 0 };
	
	static std::string numbers = "0123456789";
	
	for (auto chr : s) {
		if (chr == ' ') { continue; }
		
		if (chr == ',') {
			vals.push_back(0);
			continue;
		}
		
		auto nm = numbers.find(chr);
		if (nm == std::string::npos){
			worked = false;
			return c;
		}
		
		vals[vals.size()-1] *= 10;
		vals[vals.size()-1] += nm;
	}
	
	if (vals.size() != 3) {
		worked = false;
		return c;
	}
	
	for (auto v : vals) {
		if (v < 0 || v > 255) {
			worked = false;
			return c;
		}
	}
	
	c.r = (float)(vals[0])/255.0;
	c.g = (float)(vals[1])/255.0;
	c.b = (float)(vals[2])/255.0;
	
	return c;
}
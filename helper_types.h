#pragma once

#ifdef _WIN32
#include <corecrt_io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#include <string>
#include <GLFW/glfw3.h>

extern GLFWwindow* g_main_window;

static std::string GetClipboardText() {
	const char* text = glfwGetClipboardString(g_main_window);
	return text ? std::string(text) : "";
}

static void SetClipboardText(const std::string& text) {
	glfwSetClipboardString(g_main_window, text.c_str());
}
#endif

#include "MonoString.cpp"
#include <cstdlib>
#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <map>
#include <array>
#include <string>
#include <cstdio>
#include <system_error>
#include <algorithm>

using SearchFileKey  = std::pair<std::string, std::string>;
using SearchMatch    = std::pair<int, std::string>;
using SearchMatchVec = std::vector<SearchMatch>;
using SearchResult   = std::map<SearchFileKey, SearchMatchVec>;

struct FileIndexResult {
	std::vector<std::string> indexedNames;       // just the file names
	std::vector<MST::MonoString> displayPaths;       // cropped, relative display paths
	std::vector<std::string> fullPaths;          // full absolute paths (including “:Command” entries)
	std::vector<int> currentlyshowing;
	std::vector<int> currentlyshowingtype;
};

struct Language {
	std::string name = "";
	MST::MonoString line_comment = {};
	std::vector<std::string> filetypes = {};
	std::string lsp = "";
	std::string build_command = "";
};

struct FileInfo {
	std::string filepath;
	std::string filename;
	bool is_opening = true;
};

struct Color {
	float r;
	float g;
	float b;
	float a;
};

static constexpr std::array<unsigned char,256> ToLower = []{
	std::array<unsigned char,256> m{};
	for(int i=0;i<256;i++){
		m[i] = (i >= 'A' && i <= 'Z') ? (i + 32) : i;
	}
	return m;
}();

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
	Color* active_color;
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
	
	Color* syntax_colors[12];
	
	Color* add_panel;
	Color* remove_panel;
};

#ifdef _WIN32
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
#endif

static std::string getExecutablePath() {
#ifdef _WIN32
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	return std::string(path);
#else
	std::string path(4096, '\0');
	ssize_t len = readlink("/proc/self/exe", path.data(), path.size());
	if (len == -1) return "";
	path.resize(len);
	return path;
#endif
}

static std::string getExecutableDir() {
	std::string exePath = getExecutablePath();
	return std::filesystem::path(exePath).parent_path().string();
}

static int analyzeForFixit(const std::vector<MST::MonoString>& lines) {
	int two = 0;
	int four = 0;
	
	int prev = 0;
	
	bool anyspaces = false;
	
	for (auto l : lines) {
		if (l.length == 0) {
			continue;
		}
		
		int this_l = 0;
		for (size_t c = 0; c < l.length; c++) {
			MST::u32 uc = MST::char32At(l, c);
			if (uc != U' ') {
				break;
			}
			this_l ++;
		}
		
		if (this_l >= 4) {
			anyspaces = true;
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
		if (anyspaces) {
			return 4;
		}else{
			return 0;
		}
	}else if (two >= four){
		return 2;
	}else {
		return 4;
	}
}

static MST::MonoString run_fixit_on_lines(const std::vector<MST::MonoString>& lines) {
	int indent = analyzeForFixit(lines);
	if (indent == 0) return MST::join(lines, MST::toMonoString(U'\n'));
	
	const MST::MonoString tabUnit = MST::toMonoString(U'\t');
	const MST::MonoString newline = MST::toMonoString(U'\n');
	
	MST::MonoString newText;
	for (int i = 0; i < lines.size(); i++) {
		const auto& origLine = lines[i];
		
		size_t origSpaces = 0;
		for (size_t c = 0; c < origLine.length; c++) {
			if (MST::char32At(origLine, c) == U' ') ++origSpaces;
			else if (!MST::skipIdx(origLine, c)) break;
		}
		
		int levels = origSpaces / indent;
		
		MST::MonoString fixedPrefix;
		for (int i = 0; i < levels; ++i) {
			fixedPrefix += tabUnit;
		}
		
		MST::MonoString rest = MST::substring(origLine, levels*indent, origLine.length);
		
		newText += fixedPrefix;
		newText += rest;
		if (i != lines.size()-1) {
			newText += newline;
		}
	}
	
	return newText;
}

static MST::MonoString undo_fixit_on_lines(std::vector<MST::MonoString> lines) {
	const MST::MonoString spaceUnit = MST::toMonoString("    ");
	const MST::MonoString newline = MST::toMonoString(U'\n');
	
	MST::MonoString newText;
	for (int i = 0; i < lines.size(); i++) {
		const auto& origLine = lines[i];
		
		size_t origTabs = 0;
		for (size_t c = 0; c < origLine.length; c++) {
			if (MST::char32At(origLine, c) == U'\t') ++origTabs;
			else if (!MST::skipIdx(origLine, c)) break;
		}
		
		MST::MonoString fixedPrefix;
		for (int i = 0; i < origTabs; ++i) {
			fixedPrefix += spaceUnit;
		}
		
		MST::MonoString rest = MST::substring(origLine, origTabs, origLine.length);
		
		newText += fixedPrefix;
		newText += rest;
		if (i != lines.size()-1) {
			newText += newline;
		}
	}
	
	return newText;
}

static MST::MonoString run_fixit_on_text(MST::MonoString text) {
	return run_fixit_on_lines( MST::split(text, U'\n') );
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

static void trim_decimal(std::string& s) {
	auto dot = s.find('.');
	if (dot == std::string::npos) return;
	while (!s.empty() && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
}

static MST::MonoString doubleToMonoString_pretty(double value) {
	if (std::isnan(value))  return MST::toMonoString("nan");
	if (std::isinf(value))  return MST::toMonoString(value < 0 ? "-inf" : "inf");
	if (value == 0.0)       return MST::toMonoString(U'0'); // avoid "-0"

	const double av = std::fabs(value);

	// Tune these thresholds to taste.
	// If the number is tiny or huge, use mantissa*10^exp form.
	const bool use_10 =
		(av < 1e-6) || (av >= 1e9);

	// Significant digits for the mantissa (pretty, not necessarily round-trip exact)
	constexpr int SIG = 9;

	if (!use_10) {
		// Defaultfloat gives a nice compact decimal in most cases.
		std::ostringstream oss;
		oss.setf(std::ios::fmtflags(0), std::ios::floatfield); // defaultfloat
		oss << std::setprecision(SIG) << value;

		std::string s = oss.str();
		// If defaultfloat chose scientific (rare with these thresholds), normalize it to decimal-ish:
		// but your parser doesn't accept 'e', so just force fixed if it happens.
		if (s.find_first_of("eE") != std::string::npos) {
			std::ostringstream oss2;
			oss2.setf(std::ios::fixed);
			oss2 << std::setprecision(SIG) << value;
			s = oss2.str();
		}
		trim_decimal(s);
		if (s == "-0") s = "0";
		return MST::toMonoString(s);
	}

	// Compute base-10 exponent and mantissa.
	int exp10 = static_cast<int>(std::floor(std::log10(av)));
	double mant = value / std::pow(10.0, exp10);

	// Rounding can push mantissa to 10.0; normalize if that happens.
	if (std::fabs(mant) >= 10.0) {
		mant /= 10.0;
		exp10 += 1;
	}

	// Format mantissa as plain decimal (no 'e'), then trim zeros.
	std::ostringstream moss;
	moss.setf(std::ios::fixed);
	// For mantissa in [1,10), fixed with (SIG-1) decimals gives ~SIG significant digits.
	moss << std::setprecision(std::max(0, SIG - 1)) << mant;

	std::string m = moss.str();
	trim_decimal(m);
	if (m == "-0") m = "0";

	// If exponent is 0, just return mantissa.
	if (exp10 == 0) {
		return MST::toMonoString(m);
	}

	// Emit parseable form for your evaluator: "<mantissa>*10^<exp>"
	std::string out = m + "*10^" + std::to_string(exp10);
	return MST::toMonoString(out);
}

static MST::MonoString doubleToMonoString(double value) {
	// Handle special cases explicitly (optional, but avoids weird outputs).
	if (std::isnan(value))  return MST::toMonoString("nan");
	if (std::isinf(value))  return MST::toMonoString(value < 0 ? "-inf" : "inf");
	if (value == 0.0)       return MST::toMonoString(U'0'); // avoids "-0"

	const double av = std::fabs(value);

	// We want enough significant digits that parsing back reproduces the same double.
	constexpr int P = std::numeric_limits<double>::max_digits10; // 17

	// e10 = floor(log10(|value|)) gives exponent in base-10.
	// For fixed formatting: digits_after_decimal needed to keep ~P significant digits.
	int e10 = static_cast<int>(std::floor(std::log10(av)));

	int digits_after_decimal;
	if (e10 >= 0) {
		// value has (e10+1) digits before decimal.
		digits_after_decimal = std::max(0, P - (e10 + 1));
	} else {
		// value is < 1. Need to skip leading zeros after decimal: -e10 places,
		// then add (P-1) more digits for significance.
		digits_after_decimal = (-e10) + (P - 1);
	}

	// Safety cap to avoid absurdly long strings if someone enters crazy-small numbers.
	// (You can raise this if you want.)
	digits_after_decimal = std::min(digits_after_decimal, 400);

	std::ostringstream oss;
	oss.setf(std::ios::fixed);
	oss << std::setprecision(digits_after_decimal) << value;

	std::string s = oss.str();

	// Trim trailing zeros and then a trailing decimal point.
	if (s.find('.') != std::string::npos) {
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
	}

	// If we trimmed down to "-0", normalize to "0".
	if (s == "-0") s = "0";

	return MST::toMonoString(s);
}

static double monoStringToDouble_quick(const MST::MonoString& str, bool& worked) {
	std::string ascii = MST::toString(str);
	try {
		worked = true;
		return std::stod(ascii);
	} catch (const std::exception&) {
		worked = false;
		return 0.0;
	}
}

static std::pair<bool, MST::MonoString> checkForAndEliminate(MST::MonoString expr, MST::u32 symbol, std::function<double(double,double)> run){
	MST::MonoString val1;
	MST::MonoString val2;

	MST::MonoString afterItterationExpression;

	MST::MonoString partsOfDigit = MST::toMonoString("0987654321");

	while (MST::index(expr, 0, symbol) != -1) {
		auto parts = MST::split(expr, symbol);
		auto p1 = parts[0];
		auto p2 = parts[1];

		MST::MonoString trueDig1;
		MST::MonoString trueDig2;
		
		bool seenDot = false;
		for (int i = p1.length-1; i >= 0; i--) {
			auto c = MST::char32At(p1, i);
			if (c == U'.') {
				if (seenDot) return {false, MST::MonoString()};
				seenDot = true;
			}else if (c == U'-') {
				trueDig1 = MST::toMonoString(c) + trueDig1;
				break;
			}else if (MST::index(partsOfDigit, 0, c) == MST::NOT_FOUND) break;

			trueDig1 = MST::toMonoString(c) + trueDig1;
		}

		seenDot = false;
		bool added = false;
		for (int i = 0; i < p2.length; i++) {
			auto c = MST::char32At(p2, i);
			if (c == U'.') {
				if (seenDot) return {false, MST::MonoString()};
				seenDot = true;
			}else if (c == U'-' && !added) {
			}else if (MST::index(partsOfDigit, 0, c) == MST::NOT_FOUND) break;

			trueDig2 += MST::toMonoString(c);
			if (c != U'-') {
				added = true;
			}
		}

		bool ok;
		double v1 = monoStringToDouble_quick(trueDig1, ok);
		if (!ok) return {false, MST::MonoString()};
		double v2 = monoStringToDouble_quick(trueDig2, ok);
		if (!ok) return {false, MST::MonoString()};

		double res = run(v1, v2);

		MST::MonoString startedWith = trueDig1+MST::toMonoString(symbol)+trueDig2;

		int index = MST::index(expr, 0, startedWith);
		if (index != -1) {
			expr = MST::substring(expr, 0, index) + doubleToMonoString(res) + MST::substring(expr, index + startedWith.length, expr.length);
		}
	}
	return {true, expr};
}

static int getLengthLeft(const MST::MonoString& str) {
	int openBrackets = 0;
	const MST::MonoString allowed = MST::toMonoString("0987654321.");
	
	size_t length = str.length;
	for (size_t i = 0; i < length; i++) {
		MST::u32 c = MST::char32At(str, length-i-1);
		
		if (c == ')') {
			openBrackets += 1;
		}else if (c == '(') {
			openBrackets -= 1;
			if (openBrackets < 0) {
				length = i;
				break;
			}else if (openBrackets == 0) {
				length = i + 1;
				break;
			}
		}else if (openBrackets == 0) {
			if (MST::index(allowed, 0, c) == MST::NOT_FOUND) {
				length = i;
				break;
			}
		}
	}
	
	return length;
}

static int getLengthRight(const MST::MonoString& str) {
	int openBrackets = 0;
	MST::MonoString allowed = MST::toMonoString("0987654321.");
	
	size_t length = str.length;
	for (size_t i = 0; i < length; i++) {
		MST::u32 c = MST::char32At(str, i);
		
		if (i == 0 && (c == '-' || c == '+')) { // only on the first char
			continue;
		}if (c == '(') {
			openBrackets += 1;
		}else if (c == ')') {
			openBrackets -= 1;
			if (openBrackets < 0) {
				length = i;
				break;
			}else if (openBrackets == 0) {
				length = i + 1;
				break;
			}
		}else if (openBrackets == 0) {
			if (MST::index(allowed, 0, c) == MST::NOT_FOUND) {
				length = i;
				break;
			}
		}
	}
	
	return length;
}

static MST::MonoString fixExponents(MST::MonoString exp) {
	int32_t curIdx = exp.length;
	
	while (true) {
		curIdx -= 1;
		
		if (curIdx < 0) {
			break;
		}
		
		if (MST::char32At(exp, curIdx) != U'^') {
			continue;
		}
		
		int rightLen = getLengthRight(MST::substring(exp, curIdx+1, exp.length));
		int leftLen = getLengthLeft(MST::substring(exp, 0, curIdx));
		
		exp = MST::insertAt(exp, curIdx+rightLen+1, MST::toMonoString(U')'));
		exp = MST::insertAt(exp, curIdx-leftLen,  MST::toMonoString(U'('));
		curIdx += 1;
	}
	
	return exp;
}

static std::pair<bool, double> calcExpression(MST::MonoString expression, bool doneExpFix = false) { // we are going to recurse on brackets...
	if (expression.length == 0) return {false, 0.0};
	
	expression = MST::stripOfChar(expression, U' ');
	expression = MST::stripOfChar(expression, U',');
	expression = MST::stripOfChar(expression, U'\t');
	expression = MST::stripOfChar(expression, MST::CONT);
	expression = MST::replaceAll(expression, MST::toMonoString(")("), MST::toMonoString(")*("));
	
	const MST::MonoString allowed = MST::toMonoString("0987654321+-/*()%^.");
	
	MST::MonoString newExpression;
	
	int openedBrackets = 0;
	MST::MonoString subExpression;
	
	// we need to go through our expression and get all exponents into this form: (a^b) because cases like this: -1^2 needs to be -(1^2) not (-1)^2
	
	if (!doneExpFix) {
		expression = fixExponents(expression);
	}
	
	for (size_t char_indx = 0; char_indx < expression.length; char_indx++) {
		auto c = MST::char32At(expression, char_indx);
		if (MST::index(allowed, 0, c) == MST::NOT_FOUND) return {false, 0.0};
		
		if (c == U'(') {
			openedBrackets += 1;
			if (openedBrackets == 1) {
				subExpression = MST::MonoString();
			}else{
				subExpression += MST::toMonoString(c);
			}
		}else if (c == U')') {
			openedBrackets -= 1;
			if (openedBrackets < 0) {
				return {false, 0.0};
			}else if (openedBrackets == 0) {
				auto [isValid, result] = calcExpression(subExpression, true);
				if (!isValid) return {false, 0.0};

				newExpression += doubleToMonoString(result);
			}else{
				subExpression += MST::toMonoString(c);
			}
		}else if (openedBrackets != 0) {
			subExpression += MST::toMonoString(c);
		}else {
			newExpression += MST::toMonoString(c);
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
	
	MST::MonoString mm = MST::toMonoString("--");
	MST::MonoString pp = MST::toMonoString("++");
	MST::MonoString pm = MST::toMonoString("+-");
	MST::MonoString mp = MST::toMonoString("-+");
	MST::MonoString p = MST::toMonoString(U'+');
	MST::MonoString m = MST::toMonoString(U'-');
	
	
	while (MST::index(newExpression, 0, mm) != -1 || MST::index(newExpression, 0, pp) != -1 || MST::index(newExpression, 0, pm) != -1 || MST::index(newExpression, 0, mp) != -1) {
		if (MST::index(newExpression, 0, mm) != -1) newExpression = MST::replaceAll(newExpression, mm, p);
		if (MST::index(newExpression, 0, pp) != -1) newExpression = MST::replaceAll(newExpression, pp, p);
		if (MST::index(newExpression, 0, pm) != -1) newExpression = MST::replaceAll(newExpression, pm, m);
		if (MST::index(newExpression, 0, mp) != -1) newExpression = MST::replaceAll(newExpression, mp, m);
	}
	
	double runningTotal = 0.0;
	MST::MonoString next;
	MST::MonoString op;
	
	const MST::MonoString partsOfDigit = MST::toMonoString("0987654321");
	
	for (size_t i = 0; i < newExpression.length; i++) {
		auto c = MST::char32At(newExpression, i);

		if (MST::index(partsOfDigit, 0, c) != -1 || c == U'.') {
			next += MST::toMonoString(c);
		}
		if (c == U'+' || c == U'-' || i == newExpression.length-1) {
			bool ok;
			double nextDub = monoStringToDouble_quick(next, ok);
			if (next.length == 0) {
				nextDub = 0;
			}else if (!ok) return {false, 0.0};
			
			if (op.length == 0) {
				runningTotal = nextDub;
				next = MST::MonoString{};
			}else {
				if (op == MST::toMonoString(U'+')) {
					runningTotal += nextDub;
				}else {
					runningTotal -= nextDub;
				}

				next = MST::MonoString();
			}

			op = MST::toMonoString(c);
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

static std::string trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
	return s.substr(b, e - b);
}

static bool fileExists(const std::string& path) {
	std::ifstream test(path.c_str());
	return test.good();
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
	
	c.r = (float)(vals[0])/255.0f;
	c.g = (float)(vals[1])/255.0f;
	c.b = (float)(vals[2])/255.0f;
	
	return c;
}

static bool URISEqual(std::string u1, std::string u2) {
	auto f1_p = fileUriToPath(u1);
	auto f2_p = fileUriToPath(u2);
	
	return areSameFile(f1_p, f2_p);
}

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
	if (s.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	if (len <= 0) return {};
	std::wstring wstr(len - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wstr.data(), len);
	return wstr;
}
#endif

inline bool atomicWriteReplace(const std::filesystem::path& target,
							   const std::string& bytes,
							   std::string* err = nullptr)
{
	std::error_code ec;
	auto dir  = target.parent_path();
	auto base = target.filename().string();
	auto tmp  = dir / (base + ".tmp~" + std::to_string(::getpid()));

	// 1) write tmp (C stdio to get a portable fd for fsync/_commit)
	{
		std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
		if (!f) { if (err) *err = "open tmp failed"; return false; }
		if (bytes.size() && std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
			if (err) *err = "write tmp failed";
			std::fclose(f); std::filesystem::remove(tmp, ec);
			return false;
		}
		std::fflush(f);
	#ifdef _WIN32
		_commit(_fileno(f));
	#else
		fsync(fileno(f));
	#endif
		std::fclose(f);
	}

#ifdef _WIN32
	std::wstring wtmp = widen(tmp.string());
	std::wstring wdst = widen(target.string());
	if (!ReplaceFileW(wdst.c_str(), wtmp.c_str(), nullptr,
					  REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
		// Fallback: MoveFileExW
		if (!MoveFileExW(wtmp.c_str(), wdst.c_str(),
						 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			if (err) *err = "ReplaceFile/MoveFileEx failed";
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
#else
	std::filesystem::rename(tmp, target, ec);
	if (ec) {
		if (err) *err = "rename failed: " + ec.message();
		std::filesystem::remove(tmp, ec);
		return false;
	}
#endif
	
	return true;
}

static std::vector<std::uint8_t> loadFileBytes(const std::string& filename, bool& worked) {
	std::ifstream file(filename, std::ios::binary | std::ios::ate);

	if (!file) {
		worked = false;
		return {};
	}

	std::streamsize size = file.tellg();
	if (size < 0) {
		worked = false;
		return {};
	}

	file.seekg(0, std::ios::beg);

	std::vector<std::uint8_t> data(static_cast<size_t>(size));

	if (size > 0) {
		file.read(reinterpret_cast<char*>(data.data()), size);

		if (!file) {
			worked = false;
			return {};
		}
	}

	worked = true;
	return data;
}
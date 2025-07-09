// Highlighter.h

#pragma once

#include "helper_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <set>
#include <regex>

#define ONIG_ESCAPE_UCHAR_COLLISION
#include <oniguruma.h>
#undef ONIG_ESCAPE_UCHAR_COLLISION

#include <unicode/unistr.h>

#include "json.hpp"

struct Rule;
struct Capture;

struct RegexInfo {
	regex_t* regex = nullptr;
	bool G = false;
	bool bangG = false;
};

struct ContextFrame {
	std::vector<std::shared_ptr<Rule>> patterns = {};
	RegexInfo* endReg = nullptr;
	RegexInfo* whileReg = nullptr;
	std::map<int,Capture> endCaptures;
	std::string contentName;
	bool closable = true;
	int64_t hash = 0;
	int start_char = 0;
	bool started_here = true;
};

struct RegexSegment {
	std::string segment;
	std::string delimiter;
	int delimiter_number;
};

struct Rule {
	int type_of_rule = -1;
	int64_t hash = 0;

	std::string name = "";
	std::string include;

	RegexInfo* matchReg = nullptr;
	RegexInfo* beginReg = nullptr;
	RegexInfo* endReg = nullptr;
	RegexInfo* whileReg = nullptr;

	std::string contentName = "";
	std::vector<std::shared_ptr<Rule>> patterns;

	std::map<int, Capture> captures;
	std::map<int, Capture> beginCaptures;
	std::map<int, Capture> endCaptures;
	bool needsInsertIntoEndRegex = false;
	std::vector<RegexSegment> uncompiledEndReg;
	std::map<int, Capture> whileCaptures;
};

struct TextMateInfo {
	std::vector<ContextFrame> contextStack = {};
};

struct Token {
	int start;
	int length;
	std::string name;
	int depth;
};

struct ColoredTokens {
	int start;
	int end;
	int color;
};

struct LineResult {
	TextMateInfo lineInfo;
	std::vector<ColoredTokens> tokens = {};
};

struct Capture {
	std::string name = "";
	std::vector<std::shared_ptr<Rule>> patterns;
};

struct Captured {
	int itm;
	Capture cap;
	int index;
	int length;
};

struct Match {
	int index;
	int length;
	std::shared_ptr<Rule> rule;
	
	bool is_end_of_segment;
	std::vector<Captured> captured;
	OnigRegion* region;
};

class Highlighter {
public:
	Highlighter()  = default;
	~Highlighter();
	
	bool loadGrammarFile(const std::string& path);
	
	LineResult highlightLine(icu::UnicodeString input_string, TextMateInfo currentInfo);
	
	TextMateInfo getDefaultLineInfo();
	std::string scopeName = "";

private:
	int chooseColorByScopes(std::string scopes, int def_col);
	
	int INCLUDE = 0;
	int RANGE = 1;
	int MATCH = 2;
	int GROUP = 3;
	
	std::unordered_map<std::string, std::shared_ptr<Rule>> repository;
	std::vector<std::shared_ptr<Rule>> activePatterns = {};
	ContextFrame root;
	std::shared_ptr<Rule> self;
	
	std::shared_ptr<Rule> compileRule(const nlohmann::json& r);
	Capture compileCapture(const nlohmann::json& j);
	RegexInfo* compileRegex(std::string patternStr);
	
	bool alreadyFoundPattern(std::shared_ptr<Rule> pattern);
	void fetchAllPatterns(const std::vector<std::shared_ptr<Rule>>& patterns);
	Match findEarliestPattern(std::string line, ContextFrame current, int handledUpTo, bool checkwhile, bool on_start);
	std::pair<std::vector<Token>,TextMateInfo> analizeSection(std::string section, TextMateInfo currentInfo, bool checkwhile);
	
	bool needsDelimiter(const std::string &pat);
	std::vector<RegexSegment> parseRegexSegments(const std::string &pattern);
};
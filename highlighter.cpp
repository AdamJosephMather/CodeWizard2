#include "highlighter.h"
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include "application.h"

std::set<char> punctuationset = {'!', '#', '$', '%', '&', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~'};

std::vector<std::vector<std::string>> matches = {
	{"type"},
	{"string"},
	{"comment"},
	{"variable", "paramater", "argument"},
	{"name.function", "function-call"},
	{"keyword"},
	{"punctuation"},
	{"literal", "number", "bool", "constant"}
};

std::vector<int> mapsTo = {
	4,
	1,
	2,
	3,
	5,
	6,
	7,
	8,
};


Highlighter::~Highlighter() {
	onig_end();
}

bool Highlighter::loadGrammarFile(const std::string& path) {
	std::cerr << "Loading grammar file: " << path << std::endl;
	
	std::ifstream in(path);
	if (!in.is_open()) {
		std::cerr << "Cannot open " << path << "\n";
		return false;
	}
	
	std::cerr << "File opened successfully" << std::endl;
	
	std::string s((std::istreambuf_iterator<char>(in)),
				   std::istreambuf_iterator<char>());
	
	std::cerr << "File read, size: " << s.size() << " bytes" << std::endl;
	
	nlohmann::json grammarJson;
	
	try {
		grammarJson = nlohmann::json::parse(s);
		std::cerr << "JSON parsed successfully" << std::endl;
	} catch (const nlohmann::json::parse_error& e) {
		std::cerr << "JSON parse error: " << e.what() << "\n";
		return false;
	} catch (const std::exception& e) {
		std::cerr << "Other parsing error: " << e.what() << "\n";
		return false;
	}
	
	std::cerr << "About to parse grammar..." << std::endl;
	
	OnigEncoding encs[] = { ONIG_ENCODING_UTF8 };
	int status = onig_initialize(encs, sizeof(encs)/sizeof(encs[0]));
	if (status != ONIG_NORMAL) {
		fprintf(stderr, "Oniguruma init failed: %d\n", status);
		return 1;
	}
	
	scopeName = grammarJson["scopeName"];
	
	std::cout << "scopeName:" << scopeName << std::endl;
	
	auto patterns = grammarJson["patterns"];
	// for every pattern, we need to recursively anaszize it and compile the regexes. We'll leave any include statemtents as is, so we don't hit circular import problems
	
	root = ContextFrame();
	root.contentName = scopeName;
	root.closable = false;
	
	self = std::make_shared<Rule>();
	self->type_of_rule = GROUP;
	self->name = "self";
	
	for (auto p : patterns) {
		auto compiled_p = compileRule(p);
		root.patterns.push_back(compiled_p);
		self->patterns.push_back(compiled_p);
	}
	
	auto r = grammarJson["repository"];
	
	for (auto it = r.begin(); it != r.end(); ++it) {
		auto id = it.key();
		auto repoRule = it.value();
		auto compiled_repo_rule = compileRule(repoRule);
		repository[id] = compiled_repo_rule;
	}
	
	repository["self"] = self;
	
	std::cout << patterns.size() << std::endl;
	std::cout << repository.size() << std::endl;
	
	return true;
}

bool Highlighter::alreadyFoundPattern(std::shared_ptr<Rule> pattern) {
	for (std::shared_ptr<Rule> p : activePatterns) {
		if (p == pattern) {
			return true;
		}
	}
	return false;
}

void Highlighter::fetchAllPatterns(const std::vector<std::shared_ptr<Rule>>& patterns) {
	for (auto const& p : patterns) {
		if (p->type_of_rule == INCLUDE) {
			auto it = repository.find(p->include);
			if (it != repository.end()) {
				fetchAllPatterns({ it->second });
			}else {
				std::cout << "FAILED TO FIND INCLUDE: " << p->include << std::endl;
			}
			continue;
		}
		
		if (!alreadyFoundPattern(p)) {
			if (p->type_of_rule == GROUP) {
				for (auto const& p1 : p->patterns) {
					fetchAllPatterns({p1});
				}
			}else{
				activePatterns.push_back(p);
			}
		}
	}
}

Match Highlighter::findEarliestPattern(std::string line, ContextFrame currentContext, int handledUpTo, bool checkWhile, bool on_start) {
	int first_index = -1;
	std::shared_ptr<Rule> first_rule;
	int length = -1;
	bool is_end_of_segment = false;
	std::vector<Captured> captured;
	OnigRegion* thisRegion = nullptr;
	
	const OnigUChar* str = reinterpret_cast<const OnigUChar*>(line.data());
	const OnigUChar* end = str + line.size();
	
	// first let's look for the end of the current contextframe
	
	bool skip = false;
	if (currentContext.closable) {
		RegexInfo* end_reg = currentContext.endReg;
		
		OnigRegion* region = onig_region_new();
		
		int r = onig_search(
			end_reg->regex,
			str, end,     // entire buffer
			str+handledUpTo, end,     // search from str to end
			region,
			ONIG_OPTION_NONE
		);
		
		if (r >= 0 && (on_start || !end_reg->G) && (!on_start || !end_reg->bangG)) {
			first_index = region->beg[0];
			length = region->end[0] - region->beg[0];
			is_end_of_segment = true;
			OnigRegion* copy = onig_region_new();
			onig_region_copy(copy, region);
			thisRegion = copy;
			
			captured = {};
			
			for (auto it : currentContext.endCaptures) {
				int itm = it.first;
				Capture cap = it.second;
				
				int indx = region->beg[itm];
				int len = region->end[itm]-region->beg[itm];
				
				if (indx < 0) {
					continue;
				}
				
				Captured cptrd = {itm, cap, indx, len};
				captured.push_back(cptrd);
			}
			
			if (first_index == handledUpTo) {
				skip = true;
			}
		}
		
		onig_region_free(region, 1);
		
		RegexInfo* while_reg = currentContext.whileReg;
		
		if (while_reg && checkWhile) {
			OnigRegion* region_while = onig_region_new();
			
			r = onig_search(
				while_reg->regex,
				str, end,     // entire buffer
				str+handledUpTo, end,     // search from str to end
				region_while,
				ONIG_OPTION_NONE
			);
			
			if (!skip && (r < 0 || (!on_start && end_reg->G) || (!on_start && end_reg->bangG))) {
				first_index        = handledUpTo;     // end right where we are
				length             = 0;               // zero-length (don’t consume text)
				first_rule.reset();                   // no explicit rule object
				is_end_of_segment  = true;            // tell caller to pop the context
				
				if (thisRegion)                       // discard any earlier region copy
					onig_region_free(thisRegion, 1);
				thisRegion = nullptr;
			
				captured.clear();                     // no capture data
				skip = true;                          // we’re done evaluating this line
				
			}
			
			onig_region_free(region_while, 1);
		}
	}
	
	for (auto const& p : activePatterns) {
		if (skip) {
			break;
		}
		
		regex_t* to_find;
		
		bool G = false;
		bool bangG = false;
		
		// there will be no includes here, we already resolved them (as well as 'groups')
		if (p->type_of_rule == MATCH) {
			to_find = p->matchReg->regex;
			G = p->matchReg->G;
			bangG = p->matchReg->bangG;
		}else if (p->type_of_rule == RANGE) {
			to_find = p->beginReg->regex;
			G = p->beginReg->G;
			bangG = p->beginReg->bangG;
		}else{
			std::cout << "Problem in fetch must have occured, got non match/range in search. " << p->type_of_rule << std::endl;
			continue;
		}
		
		OnigRegion* region = onig_region_new();
		
		int r = onig_search(
			to_find,
			str, end,     // entire buffer
			str+handledUpTo, end,     // search from str to end
			region,
			ONIG_OPTION_NONE
		);
		
		if (r >= 0 && (on_start || !G) && (!on_start || !bangG)) {
			int start = region->beg[0];
			int len = region->end[0] - region->beg[0];
			
			if (first_index == -1 || start < first_index) { // || (start == first_index && len > length) || (start == first_index && length == len && type_of_thing == RANGE && p->type_of_rule == MATCH)) {
				first_index = start;
				length = len;
				first_rule = p;
				is_end_of_segment = false;
				// create copy of the region
				OnigRegion* copy = onig_region_new();
				onig_region_copy(copy, region);
				if (thisRegion) {
					onig_region_free(thisRegion, 1);
				}
				thisRegion = copy;
				
				if (first_index == handledUpTo) {
					skip = true;
				}
				
				captured = {};
				
				std::map<int,Capture> captures = {};
				
				if (p->type_of_rule == MATCH) {
					captures = p->captures;
				}else{
					captures = p->beginCaptures;
				}
				
				captured = {};
				
				for (auto it : captures) {
					int itm = it.first;
					Capture cap = it.second;
					
					int indx = region->beg[itm];
					int len = region->end[itm]-region->beg[itm];
					
					if (indx < 0) {
						continue;
					}
					
					Captured cptrd = {itm, cap, indx, len};
					captured.push_back(cptrd);
				}
			}
		}
		
		onig_region_free(region, 1);
	}
	
	return Match{ first_index, length, first_rule, is_end_of_segment, captured, thisRegion };
}

bool Highlighter::needsDelimiter(const std::string &pat) {
	static const std::regex backref(R"(\\[1-9][0-9]*|\\k<[^>]+>)");
	return std::regex_search(pat, backref);
}

std::pair<std::vector<Token>,TextMateInfo> Highlighter::analizeSection(std::string section, TextMateInfo currentInfo, bool is_start_of_line) {
	bool need_to_find_patterns = true;
	
	int handledUpTo = 0;
	
	std::vector<Token> tokens = {};
	
	bool on_start_of_scope = true;
	
	while (true) {
		ContextFrame currentContext = currentInfo.contextStack.back();
		
		if (need_to_find_patterns) {
			activePatterns.clear();
			fetchAllPatterns(currentContext.patterns);
			need_to_find_patterns = false;
		}
		
		Match match = findEarliestPattern(section, currentContext, handledUpTo, is_start_of_line, on_start_of_scope);
		
		is_start_of_line = false;
		on_start_of_scope = false;
		
		if (match.index == -1) {
			if (match.region) {
				onig_region_free(match.region, 1);
			}
			break;
		}
		
		if (match.is_end_of_segment) {
			std::string name = currentInfo.contextStack.back().contentName;
			
			Token token_range;
			if (currentContext.started_here) {
				token_range.start = currentContext.start_char;
			}else{
				token_range.start = 0;
			}
			token_range.depth = currentInfo.contextStack.size();
			token_range.length = match.index-token_range.start+match.length;
			token_range.name = name;
			tokens.push_back(token_range);
			
//			std::cout << "Popping context from stack: " << currentInfo.contextStack.back().contentName;
			
			currentInfo.contextStack.pop_back();
			
//			std::cout << " back to context: " << currentInfo.contextStack.back().contentName << std::endl;
//			std::cout << "That context was initialized at: " << currentContext.start_char << " On this line? " << currentContext.started_here << std::endl;
//			std::cout << "Pop occured because of match at index: " << match.index << " with length and text of " << match.length << " and \"" << section.substr(match.index, match.length) << "\"" << std::endl;
//			std::cout << "Token to fill range spans from: " << token_range.start << " to "  << token_range.start+token_range.length << " w/ name " << token_range.name << std::endl;
			
			need_to_find_patterns = true;
		}else {
			auto rule = match.rule;
			
			if (rule->type_of_rule == RANGE){
				on_start_of_scope = true;
				
				ContextFrame newFrame;
				
				newFrame.patterns = rule->patterns;
				newFrame.hash = currentInfo.contextStack.back().hash ^ rule->hash;
				
				if (rule->needsInsertIntoEndRegex) {
					std::string new_reg = "";
					
					for (auto i : rule->uncompiledEndReg) {
						if (i.segment != "") {
							new_reg += i.segment;
						}else {
							int start_of_delim = match.region->beg[i.delimiter_number];
							int length_of_sub = match.region->end[i.delimiter_number]-start_of_delim;
							
							if (start_of_delim >= 0) {
								std::string delimn = section.substr(start_of_delim, length_of_sub);
								new_reg += delimn;
							}
						}
					}
					
					RegexInfo* reg = compileRegex(new_reg);
					
					newFrame.endReg = reg;
					newFrame.whileReg = rule->whileReg;
				}else{
					newFrame.endReg = rule->endReg;
				}
				
				newFrame.endCaptures = rule->endCaptures;
				if (rule->contentName != "") {
					newFrame.contentName = rule->contentName;
				}else{
					newFrame.contentName = rule->name;
				}
				
				newFrame.started_here = true;
				newFrame.start_char = match.index + match.length;
				
				currentInfo.contextStack.push_back(newFrame);
				need_to_find_patterns = true;
				
//				std::cout << "Pushed a new context to the stack: " << newFrame.contentName << "\n";
//				std::cout << "Said context was found at: " << match.index << " With len: " << match.length << " Text: " << section.substr(match.index, match.length) << std::endl;
			}
			
			if (match.rule->name != "") {
				Token token;
				token.start = match.index;
				token.length = match.length;
				token.name = match.rule->name;
				token.depth = currentInfo.contextStack.size();
				tokens.push_back(token);
			}
			
			if (match.region) {
				onig_region_free(match.region, 1);
			}
		}
		
		for (auto c : match.captured) {
			if (c.cap.name != "") {
				Token token;
				token.start = c.index;
				token.length = c.length;
				token.name = c.cap.name;
				token.depth = currentInfo.contextStack.size();
				tokens.push_back(token);
			}
			
			if (!c.cap.patterns.empty()) {
				auto save = activePatterns;
				
				TextMateInfo newInfo;
				ContextFrame cf;
				cf.patterns = c.cap.patterns;
				cf.contentName = c.cap.name;
				cf.closable = false;
				
				newInfo.contextStack = {cf};
				
				if (c.index > section.size()) {
					continue;
				}
				
				auto out = analizeSection(section.substr(c.index, c.length), newInfo, false);
				
				for (auto t : out.first) {
					t.start += c.index;
					tokens.push_back(t);
				}
				
				activePatterns = save; // restore old one.
			}
		}
		
		handledUpTo = match.index+match.length;
	}
	
	// now let's just take all the rest of the unhandled text and give it whatever the latest context frame is
	
	for (int depth = 0; depth < currentInfo.contextStack.size(); depth++) { // we're going to add a token for every scope in the current stack that wasn't closed yet.
		auto c = currentInfo.contextStack[depth];
		
		Token t;
		t.name = c.contentName;
		t.depth = depth+1;
		
		if (c.started_here) {
			t.start = c.start_char;
		}else{
			t.start = 0;
		}
		
		t.length = section.length()-t.start;
		
		tokens.insert(tokens.begin(), t);
	}
	
	return { tokens, currentInfo };
}

LineResult Highlighter::highlightLine(icu::UnicodeString input_string, TextMateInfo currentInfo) {
	for (int i = 0; i < currentInfo.contextStack.size(); i++) {
		currentInfo.contextStack[i].started_here = false;
	}
	
	std::string line_string;
	input_string.toUTF8String(line_string);
	line_string += "\n";
	
	auto out = analizeSection(line_string, currentInfo, true);
	
	auto tokens = out.first;
	currentInfo = out.second;
	
	LineResult ln_res;
	ln_res.lineInfo = currentInfo;
	
	int variable_color = 3;
	
	std::vector<ColoredTokens> outTokens;
	
	std::sort(tokens.begin(), tokens.end(), [](const auto& a, const auto& b) { return a.length > b.length; });
	
	outTokens.push_back({0, input_string.length(), variable_color}); // a single token covering all
//	std::cout << "\n\nHIGHLIGHT:\n\n";
	for (auto token : tokens) {
		if (token.name == "") {
			continue;
		}
		
//		std::cout << "Token: " << token.name << " text: " << line_string.substr(token.start, token.length) << std::endl;
		
		int newcolor = chooseColorByScopes(token.name, variable_color);
		if (newcolor == -1) {
			continue;
		}
		
		outTokens.push_back({token.start, token.start+token.length, newcolor});
	}
	
	ln_res.tokens = outTokens;
	
	return ln_res;
}

TextMateInfo Highlighter::getDefaultLineInfo() {
	return { {root} };
}

RegexInfo* Highlighter::compileRegex(std::string patternStr) {
	bool G = false;
	bool bangG = false;
	
	std::string orig = patternStr;
	
	if (patternStr.find("(?!\\G)") != std::string::npos) {
		bangG = true;
		std::string modifiedPatternStr = patternStr;
		size_t pos = modifiedPatternStr.find("(?!\\G)");
		while (pos != std::string::npos) {
			modifiedPatternStr.erase(pos, 6);
			pos = modifiedPatternStr.find("(?!\\G)");
		}
		patternStr = modifiedPatternStr;
	}
	
	if (patternStr.find("\\G") != std::string::npos) {
		G = true;
		std::string modifiedPatternStr = patternStr;
		size_t pos = modifiedPatternStr.find("\\G");
		while (pos != std::string::npos) {
			modifiedPatternStr.erase(pos, 2);
			pos = modifiedPatternStr.find("\\G");
		}
		patternStr = modifiedPatternStr;
	}
	
	OnigErrorInfo errorInfo;
	regex_t* regex = nullptr;
	
	const OnigUChar* pattern = reinterpret_cast<const OnigUChar*>(patternStr.c_str());
	const OnigUChar* patternEnd = pattern + patternStr.size();
	
	int result = onig_new(
		&regex,              // output compiled regex
		pattern,             // start of pattern
		patternEnd,          // end of pattern
		ONIG_OPTION_DEFAULT, // regex options (case sensitivity, etc.)
		ONIG_ENCODING_UTF8,  // character encoding
		ONIG_SYNTAX_RUBY,    // syntax (The one that textmate uses)
		&errorInfo           // error info
	);

	if (result != ONIG_NORMAL) {
		std::cout << "Uh oh...\n\n" << orig << "\n\n - \n\n" << pattern << "\n\n - REGEX COMPILATION FAILED " << std::endl;
		
		OnigUChar errorMessage[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str(errorMessage, result, &errorInfo);
		fprintf(stderr, "Oniguruma regex compile error: %s\n", errorMessage);
		
		return nullptr;	
	}
	
	RegexInfo* info = new RegexInfo();
	info->regex = regex;
	info->G = G;
	info->bangG = bangG;
	return info;
}

Capture Highlighter::compileCapture(const nlohmann::json& j) {
	Capture cap;
	if (j.contains("name")) cap.name = j["name"];
	if (j.contains("patterns")) {
		for (const auto& p : j["patterns"]) {
			cap.patterns.push_back(compileRule(p));
		}
	}
	return cap;
}

std::vector<RegexSegment> Highlighter::parseRegexSegments(const std::string &pattern) {
	// Matches \1, \2, … or \k<name>
	static const std::regex backrefPat(R"(\\([1-9][0-9]*)|\\k<([^>]+)>)");
	std::vector<RegexSegment> out;
	std::smatch m;
	std::size_t lastPos = 0;

	// Search through the pattern for backrefs
	while (std::regex_search(pattern.begin() + lastPos, pattern.end(), m, backrefPat)) {
		// m.position(0) is offset *from* (pattern.begin()+lastPos)
		auto matchPos  = lastPos + m.position(0);
		auto matchLen  = m.length(0);

		// 1) Literal text before this match
		if (matchPos > lastPos) {
			out.push_back(RegexSegment{
				pattern.substr(lastPos, matchPos - lastPos),
				/*delimiter=*/"",
				/*delimiter_number=*/0
			});
		}

		// 2) The back-reference itself
		if (m[1].matched) {
			// Numeric backref: \1, \2, …
			out.push_back(RegexSegment{
				/*segment=*/"",
				/*delimiter=*/"",
				/*delimiter_number=*/std::stoi(m.str(1))
			});
		}
		else if (m[2].matched) {
			std::cout << "################################################# We found a \\k item ################################################\n";
			// Named backref: \k<name>
			out.push_back(RegexSegment{
				/*segment=*/"",
				/*delimiter=*/m.str(2),
				/*delimiter_number=*/0
			});
		}

		// Advance past the match
		lastPos = matchPos + matchLen;
	}

	// 3) Any trailing literal text
	if (lastPos < pattern.size()) {
		out.push_back(RegexSegment{
			pattern.substr(lastPos),
			/*delimiter=*/"",
			/*delimiter_number=*/0
		});
	}

	return out;
}

std::shared_ptr<Rule> Highlighter::compileRule(const nlohmann::json& r) {
	auto rule = std::make_shared<Rule>();
	
	if (r.contains("patterns")
		&& !r.contains("include")
		&& !r.contains("match")
		&& !r.contains("begin"))
	{
		rule->type_of_rule = GROUP;               // new enum case
		for (const auto& pat : r["patterns"])
			rule->patterns.push_back(compileRule(pat));
		return rule;
	}
	
	// Optional metadata
	if (r.contains("name")) {
		rule->name = r["name"].get<std::string>();
	}
	
	// 1) Include rule
	if (r.contains("include")) {
		rule->type_of_rule = INCLUDE;
		std::string str = r["include"].get<std::string>();
		if (str == scopeName) {
			str = "$self";
		}
		rule->include   = str.substr(1);
		return rule;
	}
	
	// 2) Simple match rule
	if (r.contains("match")) {
		rule->type_of_rule = MATCH;
		rule->matchReg     = compileRegex(r["match"].get<std::string>());
		
		// captures
		if (r.contains("captures")) {
			for (auto it = r["captures"].begin(); it != r["captures"].end(); ++it) {
				int idx = std::stoi(it.key());
				rule->captures[idx] = compileCapture(it.value());
			}
		}
		return rule;
	}
	
	// 3) Begin/End (and optional While) rule
	if (r.contains("begin")) {
		rule->type_of_rule = RANGE;
		int64_t random_number;
		
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<int64_t> dis(0, INT64_MAX);
		random_number = dis(gen);
		
		rule->hash = random_number;
		
		// compile the three possible regexes
		rule->beginReg = compileRegex(r["begin"].get<std::string>());
		if (r.contains("end")) {
			if (needsDelimiter(r["end"])) {
				rule->needsInsertIntoEndRegex = true;
				rule->uncompiledEndReg = parseRegexSegments(r["end"]);
			}else{
				rule->endReg = compileRegex(r["end"].get<std::string>());
			}
		}
		if (r.contains("while")) {
			rule->whileReg = compileRegex(r["while"].get<std::string>());
		}
		
		// contentName (scopes the inner text)
		if (r.contains("contentName")) {
			rule->contentName = r["contentName"].get<std::string>();
		}
		
		// captures on begin / end / while
		if (r.contains("beginCaptures")) {
			for (auto it = r["beginCaptures"].begin(); it != r["beginCaptures"].end(); ++it) {
				int idx = std::stoi(it.key());
				rule->beginCaptures[idx] = compileCapture(it.value());
			}
		}
		if (r.contains("endCaptures")) {
			for (auto it = r["endCaptures"].begin(); it != r["endCaptures"].end(); ++it) {
				int idx = std::stoi(it.key());
				rule->endCaptures[idx] = compileCapture(it.value());
			}
		}
		if (r.contains("whileCaptures")) {
			for (auto it = r["whileCaptures"].begin(); it != r["whileCaptures"].end(); ++it) {
				int idx = std::stoi(it.key());
				rule->whileCaptures[idx] = compileCapture(it.value());
			}
		}
		
		// nested patterns
		if (r.contains("patterns")) {
			for (const auto& pat : r["patterns"]) {
				rule->patterns.push_back(compileRule(pat));
			}
		}
		
		return rule;
	}
	
	std::cout << "Could not parse rule - may want to add more logging.\n";
	
	// print json as string
	
	std::cout << r.dump(4) << std::endl;
	
	// 4) Fallback — you might want to log or throw here if nothing matched!
	return rule;
}

int Highlighter::chooseColorByScopes(std::string scopes_string, int default_color) {
	for (int i = 0; i < matches.size(); i++) {
		auto matchList = matches[i];
		for (int j = 0; j < matchList.size(); j++) {
			std::string match_item = matchList[j];
			if (scopes_string.find(match_item) != std::string::npos) {
				return mapsTo[i];
			}
		}
	}
	
	return -1;
}
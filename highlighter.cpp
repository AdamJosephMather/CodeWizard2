#include "highlighter.h"
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <limits>
#include "application.h"

// ------------------ helpers ------------------

namespace {
	inline void freeRegex(RegexInfo*& ri) {
		if (!ri) return;
		if (ri->regex) onig_free(ri->regex);
		delete ri;
		ri = nullptr;
	}
}

std::set<char> punctuationset = {
	'!', '#', '$', '%', '&', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';',
	'<', '=', '>', '?', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~'
};

std::vector<std::vector<std::string>> matches = {
	{"type"},
	{"string"},
	{"comment"},
	{"variable", "parameter", "argument"}, // fixed typo: "paramater" -> "parameter"
	{"name.function", "function-call"},
	{"keyword"},
	{"punctuation"},
	{"literal", "number", "bool", "constant"}
};

std::vector<int> mapsTo = {
	4, // type
	1, // string
	2, // comment
	3, // variable/parameter/argument
	5, // function
	6, // keyword
	7, // punctuation
	8  // literal/number/bool/constant
};

// ------------------ Rule destructor ------------------

Rule::~Rule() {
	freeRegex(matchReg);
	freeRegex(beginReg);
	freeRegex(endReg);
	freeRegex(whileReg);
}

// ------------------ Highlighter ------------------

Highlighter::~Highlighter() {
	// Free rule graph (shared_ptr will cascade; Rule::~Rule frees regexes)
	repository.clear();
	self.reset();

	// finalize oniguruma
	onig_end();
}

bool Highlighter::loadGrammarFile(const std::string& path) {
	std::cerr << "Loading grammar file: " << path << std::endl;

	std::ifstream in(path);
	if (!in.is_open()) {
		std::cerr << "Cannot open " << path << "\n";
		return false;
	}

	std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	nlohmann::json grammarJson;
	try {
		grammarJson = nlohmann::json::parse(s);
	} catch (const std::exception& e) {
		std::cerr << "JSON parse error: " << e.what() << "\n";
		return false;
	}

	OnigEncoding encs[] = { ONIG_ENCODING_UTF8 };
	int status = onig_initialize(encs, sizeof(encs)/sizeof(encs[0]));
	if (status != ONIG_NORMAL) {
		fprintf(stderr, "Oniguruma init failed: %d\n", status);
		return false; // FIX: was returning truthy value (1)
	}

	scopeName = grammarJson["scopeName"];

	auto patterns = grammarJson["patterns"];

	// Root context (not closable)
	root = ContextFrame();
	root.contentName = scopeName;
	root.closable = false;

	// "self" repository rule
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

	// Clear pattern cache (new grammar)
	patternCache.clear();

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

// NEW: pure function flattening (no member side effects)
std::vector<std::shared_ptr<Rule>> Highlighter::flattenPatterns(const std::vector<std::shared_ptr<Rule>>& patterns) {
	std::vector<std::shared_ptr<Rule>> out;
	out.reserve(patterns.size() * 2);

	std::unordered_set<const Rule*> seen;
	std::function<void(const std::vector<std::shared_ptr<Rule>>&)> rec =
		[&](const std::vector<std::shared_ptr<Rule>>& ps) {
			for (auto const& p : ps) {
				if (!p) continue;
				if (p->type_of_rule == INCLUDE) {
					auto it = repository.find(p->include);
					if (it != repository.end()) {
						rec({ it->second });
					}
					continue;
				}
				if (seen.insert(p.get()).second) {
					if (p->type_of_rule == GROUP) {
						rec(p->patterns);
					} else {
						out.push_back(p);
					}
				}
			}
		};
	rec(patterns);
	return out;
}

// NEW: hash helper for capture-only contexts (no RANGE hash)
uint64_t Highlighter::hashPatternPointers(const std::vector<std::shared_ptr<Rule>>& patterns) {
	uint64_t h = 1469598103934665603ull; // FNV-1a offset
	for (auto const& p : patterns) {
		auto addr = reinterpret_cast<uintptr_t>(p.get());
		h ^= static_cast<uint64_t>(addr);
		h *= 1099511628211ull; // FNV prime
	}
	return h ? h : 1ull;
}

// NEW: cache active patterns per context
std::vector<std::shared_ptr<Rule>> Highlighter::getActivePatternsFor(const ContextFrame& ctx) {
	int64_t key = ctx.hash ? ctx.hash : static_cast<int64_t>(hashPatternPointers(ctx.patterns));
	auto it = patternCache.find(key);
	if (it != patternCache.end()) return it->second;

	auto flat = flattenPatterns(ctx.patterns);
	patternCache.emplace(key, flat);
	return flat;
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

	// 1) Check whether current context ends here (end or while)
	bool skip = false;
	if (currentContext.closable) {
		RegexInfo* end_reg = currentContext.endReg;

		// END: search from handledUpTo to end of line
		if (end_reg) {
			OnigRegion* region_end = onig_region_new();
			int r = onig_search(
				end_reg->regex,
				str, end,
				str + handledUpTo, end,
				region_end,
				ONIG_OPTION_NONE
			);

			if (r >= 0 && (on_start || !end_reg->G) && (!on_start || !end_reg->bangG)) {
				first_index = region_end->beg[0];
				length = region_end->end[0] - region_end->beg[0];
				is_end_of_segment = true;

				OnigRegion* copy = onig_region_new();
				onig_region_copy(copy, region_end);
				thisRegion = copy;

				// capture the end-captures
				captured.clear();
				for (auto it : currentContext.endCaptures) {
					int itm = it.first;
					Capture cap = it.second;
					int indx = region_end->beg[itm];
					int len = region_end->end[itm] - region_end->beg[itm];
					if (indx >= 0) {
						captured.push_back({itm, cap, indx, len});
					}
				}
				if (first_index == handledUpTo) {
					skip = true;
				}
			}
			onig_region_free(region_end, 1);
		}

		// WHILE: only evaluated at the start of a line/iteration when requested
		RegexInfo* while_reg = currentContext.whileReg;
		if (while_reg && checkWhile) {
			OnigRegion* region_while = onig_region_new();
			// For while, test the whole line (grammar usually anchors with ^)
			int r = onig_search(
				while_reg->regex,
				str, end,
				str, end,
				region_while,
				ONIG_OPTION_NONE
			);

			// If the while condition does NOT match, we pop the context right here
			if (!skip && r < 0) {
				first_index       = handledUpTo;
				length            = 0; // zero-length token: don't consume
				first_rule.reset();
				is_end_of_segment = true;

				if (thisRegion) onig_region_free(thisRegion, 1);
				thisRegion = nullptr;

				captured.clear();
				skip = true;
			}
			onig_region_free(region_while, 1);
		}
	}

	// 2) Find earliest match among active patterns
	OnigRegion* region = onig_region_new();

	for (auto const& p : activePatterns) {
		if (skip) break;

		regex_t* to_find = nullptr;
		bool G = false, bangG = false;

		if (p->type_of_rule == MATCH) {
			to_find = p->matchReg ? p->matchReg->regex : nullptr;
			if (!to_find) continue;
			G = p->matchReg->G; bangG = p->matchReg->bangG;
		} else if (p->type_of_rule == RANGE) {
			to_find = p->beginReg ? p->beginReg->regex : nullptr;
			if (!to_find) continue;
			G = p->beginReg->G; bangG = p->beginReg->bangG;
		} else {
			continue;
		}

		onig_region_clear(region);
		int r = onig_search(
			to_find,
			str, end,
			str + handledUpTo, end,
			region,
			ONIG_OPTION_NONE
		);

		if (r >= 0 && (on_start || !G) && (!on_start || !bangG)) {
			int start = region->beg[0];
			int len = region->end[0] - region->beg[0];

			if (first_index == -1 || start < first_index) {
				first_index = start;
				length = len;
				first_rule = p;
				is_end_of_segment = false;

				// keep a copy of the winning region
				if (thisRegion) onig_region_free(thisRegion, 1);
				thisRegion = onig_region_new();
				onig_region_copy(thisRegion, region);

				// build capture info
				captured.clear();
				const std::map<int, Capture>& caps =
					(p->type_of_rule == MATCH) ? p->captures : p->beginCaptures;

				for (auto it : caps) {
					int itm = it.first;
					Capture cap = it.second;
					int indx = region->beg[itm];
					int len2 = region->end[itm] - region->beg[itm];
					if (indx >= 0) {
						captured.push_back({itm, cap, indx, len2});
					}
				}

				if (first_index == handledUpTo) {
					skip = true; // cannot do better than immediate
				}
			}
		}
	}

	onig_region_free(region, 1);
	return Match{ first_index, length, first_rule, is_end_of_segment, captured, thisRegion };
}

bool Highlighter::needsDelimiter(const std::string &pat) {
	static const std::regex backref(R"(\\[1-9][0-9]*|\\k<[^>]+>)");
	return std::regex_search(pat, backref);
}

std::pair<std::vector<Token>,TextMateInfo> Highlighter::analizeSection(std::string section, TextMateInfo currentInfo, bool is_start_of_line) {
	bool need_to_find_patterns = true;
	int handledUpTo = 0;

	std::vector<Token> tokens;
	tokens.reserve(32);

	bool on_start_of_scope = true;

	while (true) {
		ContextFrame currentContext = currentInfo.contextStack.back();

		if (need_to_find_patterns) {
			// PERF: cache flattened pattern sets by context hash/pointers
			activePatterns = getActivePatternsFor(currentContext);
			need_to_find_patterns = false;
		}

		Match match = findEarliestPattern(section, currentContext, handledUpTo, is_start_of_line, on_start_of_scope);

		is_start_of_line = false;
		on_start_of_scope = false;

		if (match.index == -1) {
			if (match.region) onig_region_free(match.region, 1);
			break;
		}

		if (match.is_end_of_segment) {
			// produce a token for the content range that is now closing
			std::string name = currentInfo.contextStack.back().contentName;

			Token token_range;
			token_range.start = currentContext.started_here ? currentContext.start_char : 0;
			token_range.depth = static_cast<int>(currentInfo.contextStack.size());
			token_range.length = match.index - token_range.start + match.length;
			token_range.name = name;
			tokens.push_back(token_range);

			// Free per-frame end regex if we created it dynamically
			if (currentContext.owns_end_regex) {
				freeRegex(currentContext.endReg);
			}

			// Pop the context
			currentInfo.contextStack.pop_back();

			need_to_find_patterns = true;
		} else {
			auto rule = match.rule;

			if (rule->type_of_rule == RANGE) {
				on_start_of_scope = true;

				ContextFrame newFrame;
				newFrame.patterns = rule->patterns;
				newFrame.hash = currentInfo.contextStack.back().hash ^ rule->hash;

				// end/while handling
				if (rule->needsInsertIntoEndRegex) {
					std::string new_reg;
					for (auto i : rule->uncompiledEndReg) {
						if (!i.segment.empty()) {
							new_reg += i.segment;
						} else {
							int start_of_delim = match.region->beg[i.delimiter_number];
							int length_of_sub = match.region->end[i.delimiter_number] - start_of_delim;
							if (start_of_delim >= 0) {
								new_reg += section.substr(start_of_delim, length_of_sub);
							}
						}
					}
					RegexInfo* reg = compileRegex(new_reg);
					newFrame.endReg = reg;
					newFrame.owns_end_regex = true; // we must free this at pop
				} else {
					newFrame.endReg = rule->endReg;   // owned by rule
				}

				// Always propagate while, if present (fix)
				newFrame.whileReg = rule->whileReg;

				newFrame.endCaptures = rule->endCaptures;
				newFrame.contentName = (!rule->contentName.empty()) ? rule->contentName : rule->name;
				newFrame.started_here = true;
				newFrame.start_char = match.index + match.length;

				currentInfo.contextStack.push_back(newFrame);
				need_to_find_patterns = true;
			}

			// token for the matched scope name (if any)
			if (!match.rule->name.empty()) {
				Token token;
				token.start = match.index;
				token.length = match.length;
				token.name = match.rule->name;
				token.depth = static_cast<int>(currentInfo.contextStack.size());
				tokens.push_back(token);
			}

			if (match.region) onig_region_free(match.region, 1);
		}

		// Emit captured sub-scopes and recursively analyze their patterns
		for (auto c : match.captured) {
			if (!c.cap.name.empty()) {
				Token token;
				token.start = c.index;
				token.length = c.length;
				token.name = c.cap.name;
				token.depth = static_cast<int>(currentInfo.contextStack.size());
				tokens.push_back(token);
			}

			if (!c.cap.patterns.empty()) {
				auto save = activePatterns;

				TextMateInfo newInfo;
				ContextFrame cf;
				cf.patterns = c.cap.patterns;
				cf.contentName = c.cap.name;
				cf.closable = false;
				// give captures a stable hash (for caching)
				cf.hash = static_cast<int64_t>(hashPatternPointers(cf.patterns));
				newInfo.contextStack = { cf };

				if (c.index <= static_cast<int>(section.size())) {
					auto out = analizeSection(section.substr(c.index, c.length), newInfo, false);
					for (auto t : out.first) {
						t.start += c.index;
						tokens.push_back(t);
					}
				}

				activePatterns = save; // restore
			}
		}

		handledUpTo = match.index + match.length;
	}

	// add tokens for any open contexts that didn't close on this line
	for (int depth = 0; depth < static_cast<int>(currentInfo.contextStack.size()); depth++) {
		auto c = currentInfo.contextStack[depth];

		Token t;
		t.name = c.contentName;
		t.depth = depth + 1;
		t.start = c.started_here ? c.start_char : 0;
		t.length = static_cast<int>(section.length()) - t.start;

		tokens.insert(tokens.begin(), t);
	}

	return { tokens, currentInfo };
}

LineResult Highlighter::highlightLine(icu::UnicodeString input_string, TextMateInfo currentInfo) {
	for (auto & cf : currentInfo.contextStack) {
		cf.started_here = false;
	}

	std::string line_string;
	input_string.toUTF8String(line_string);
	line_string += "\n";

	auto out = analizeSection(line_string, currentInfo, true);
	auto tokens = std::move(out.first);
	currentInfo = std::move(out.second);

	LineResult ln_res;
	ln_res.lineInfo = currentInfo;

	const int L = static_cast<int>(input_string.length());
	const int variable_color = 3;

	std::vector<ColoredTokens> outTokens;
	outTokens.reserve(tokens.size() + 1);

	// Largest-first tends to produce stable overpainting
	std::sort(tokens.begin(), tokens.end(),
		[](const Token& a, const Token& b) {
			if (a.length != b.length) return a.length > b.length;
			return a.start < b.start;
		});

	// default token covering the entire visible line
	outTokens.push_back({0, L, variable_color});

	for (const auto& token : tokens) {
		if (token.name.empty() || token.length <= 0) continue;

		int newcolor = chooseColorByScopes(token.name, variable_color);
		if (newcolor == -1) continue;

		int s = token.start;
		int e = token.start + token.length;

		// clamp to visible line (avoid '\n' overshoot)
		if (s < 0) s = 0;
		if (e < 0) continue;
		if (s > L) continue;
		if (e > L) e = L;
		if (e <= s) continue;

		outTokens.push_back({s, e, newcolor});
	}

	ln_res.tokens = std::move(outTokens);
	return ln_res;
}

TextMateInfo Highlighter::getDefaultLineInfo() {
	return { {root} };
}

RegexInfo* Highlighter::compileRegex(std::string patternStr) {
	bool G = false;
	bool bangG = false;

	std::string orig = patternStr;

	// emulate \G / (?!\G) by stripping and storing flags
	if (patternStr.find("(?!\\G)") != std::string::npos) {
		bangG = true;
		size_t pos = patternStr.find("(?!\\G)");
		while (pos != std::string::npos) {
			patternStr.erase(pos, 6);
			pos = patternStr.find("(?!\\G)");
		}
	}

	if (patternStr.find("\\G") != std::string::npos) {
		G = true;
		size_t pos = patternStr.find("\\G");
		while (pos != std::string::npos) {
			patternStr.erase(pos, 2);
			pos = patternStr.find("\\G");
		}
	}

	OnigErrorInfo errorInfo;
	regex_t* regex = nullptr;

	const OnigUChar* pattern = reinterpret_cast<const OnigUChar*>(patternStr.c_str());
	const OnigUChar* patternEnd = pattern + patternStr.size();

	int result = onig_new(
		&regex,
		pattern,
		patternEnd,
		ONIG_OPTION_DEFAULT,
		ONIG_ENCODING_UTF8,
		ONIG_SYNTAX_RUBY,
		&errorInfo
	);

	if (result != ONIG_NORMAL) {
		OnigUChar errorMessage[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str(errorMessage, result, &errorInfo);
		fprintf(stderr, "Oniguruma regex compile error: %s\n", errorMessage);
		fprintf(stderr, "Failed pattern: %s\n", orig.c_str());
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
	static const std::regex backrefPat(R"(\\([1-9][0-9]*)|\\k<([^>]+)>)");
	std::vector<RegexSegment> out;
	std::smatch m;
	std::size_t lastPos = 0;

	auto beginIt = pattern.begin();

	while (std::regex_search(beginIt + lastPos, pattern.end(), m, backrefPat)) {
		auto matchPos = static_cast<std::size_t>((beginIt + lastPos) - beginIt) + static_cast<std::size_t>(m.position(0));
		auto matchLen = static_cast<std::size_t>(m.length(0));

		if (matchPos > lastPos) {
			out.push_back(RegexSegment{ pattern.substr(lastPos, matchPos - lastPos), "", 0 });
		}

		if (m[1].matched) {
			out.push_back(RegexSegment{ "", "", std::stoi(m.str(1)) });
		} else if (m[2].matched) {
			// Named backref: \k<name>
			out.push_back(RegexSegment{ "", m.str(2), 0 });
		}

		lastPos = matchPos + matchLen;
	}

	if (lastPos < pattern.size()) {
		out.push_back(RegexSegment{ pattern.substr(lastPos), "", 0 });
	}

	return out;
}

std::shared_ptr<Rule> Highlighter::compileRule(const nlohmann::json& r) {
	auto rule = std::make_shared<Rule>();

	if (r.contains("patterns") && !r.contains("include") && !r.contains("match") && !r.contains("begin")) {
		rule->type_of_rule = GROUP;
		for (const auto& pat : r["patterns"])
			rule->patterns.push_back(compileRule(pat));
		return rule;
	}

	if (r.contains("name")) {
		rule->name = r["name"].get<std::string>();
	}

	// include
	if (r.contains("include")) {
		rule->type_of_rule = INCLUDE;
		std::string str = r["include"].get<std::string>();
		if (str == scopeName) {
			str = "$self";
		}
		rule->include = str.substr(1);
		return rule;
	}

	// match
	if (r.contains("match")) {
		rule->type_of_rule = MATCH;
		rule->matchReg = compileRegex(r["match"].get<std::string>());

		if (r.contains("captures")) {
			for (auto it = r["captures"].begin(); it != r["captures"].end(); ++it) {
				int idx = std::stoi(it.key());
				rule->captures[idx] = compileCapture(it.value());
			}
		}
		return rule;
	}

	// range
	if (r.contains("begin")) {
		rule->type_of_rule = RANGE;

		// random-ish hash to fold into ContextFrame hash
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<long long> dis(
			(std::numeric_limits<long long>::lowest)(), // avoids 'min' macro entirely
			(std::numeric_limits<long long>::max)()     // macro-safe call form
		);
		rule->hash = dis(gen);

		rule->beginReg = compileRegex(r["begin"].get<std::string>());

		if (r.contains("end")) {
			if (needsDelimiter(r["end"])) {
				rule->needsInsertIntoEndRegex = true;
				rule->uncompiledEndReg = parseRegexSegments(r["end"]);
			} else {
				rule->endReg = compileRegex(r["end"].get<std::string>());
			}
		}

		if (r.contains("while")) {
			rule->whileReg = compileRegex(r["while"].get<std::string>());
		}

		if (r.contains("contentName")) {
			rule->contentName = r["contentName"].get<std::string>();
		}

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

		if (r.contains("patterns")) {
			for (const auto& pat : r["patterns"]) {
				rule->patterns.push_back(compileRule(pat));
			}
		}
		return rule;
	}

	std::cout << "Could not parse rule.\n" << r.dump(4) << std::endl;
	return rule;
}

int Highlighter::chooseColorByScopes(std::string scopes_string, int /*default_color*/) {
	for (int i = 0; i < static_cast<int>(matches.size()); i++) {
		const auto& matchList = matches[i];
		for (const auto& item : matchList) {
			if (scopes_string.find(item) != std::string::npos) {
				return mapsTo[i];
			}
		}
	}
	return -1;
}
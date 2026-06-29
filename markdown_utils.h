#ifndef MARKDOWN_PROCESSOR_H
#define MARKDOWN_PROCESSOR_H

#include <algorithm>
#include <string>
#include <vector>
#include <stack>
#include "md4c.h"

enum class MarkdownElem {
	Header,
	Bold,
	Italic,
	Code,
	ListItem,
	Link,
	Paragraph
};

struct MarkdownSpan {
	MarkdownElem type;
	size_t start;
	size_t end;
	int level;
};

struct MarkdownRun {
	bool synthetic = false;

	// Range in the parser output / rendered MonoString.
	size_t outStart = 0;
	size_t outEnd = 0;

	// Range in the original MonoString, only valid when synthetic == false.
	size_t sourceStart = 0;
	size_t sourceEnd = 0;

	// Literal text to insert, only valid when synthetic == true.
	std::string syntheticText;
};

struct ProcessedMarkdown {
	std::vector<MarkdownRun> runs;
	std::vector<MarkdownSpan> spans;
	size_t cleanLength = 0;
};

class MarkdownParser {
public:
	static ProcessedMarkdown ProcessRanges(
		const std::string& alignedInput,
		size_t sourceOffset,
		size_t sourceLength
	) {
		MarkdownParser parser;

		parser.m_parseBase = alignedInput.data() + sourceOffset;
		parser.m_sourceBase = sourceOffset;

		MD_PARSER mdParser = {
			0,
			MD_DIALECT_COMMONMARK,
			parser.EnterBlockCallback,
			parser.LeaveBlockCallback,
			parser.EnterSpanCallback,
			parser.LeaveSpanCallback,
			parser.TextCallback,
			nullptr,
			nullptr
		};

		md_parse(
			parser.m_parseBase,
			static_cast<MD_SIZE>(sourceLength),
			&mdParser,
			&parser
		);

		std::sort(parser.m_spans.begin(), parser.m_spans.end(),
			[](const MarkdownSpan& a, const MarkdownSpan& b) {
				if (a.start != b.start) {
					return a.start < b.start;
				}
				return a.end > b.end;
			}
		);

		return {
			parser.m_runs,
			parser.m_spans,
			parser.m_outputLen
		};
	}

private:
	const char* m_parseBase = nullptr;
	size_t m_sourceBase = 0;

	// Output length in MonoString slots, not UTF-8 bytes.
	size_t m_outputLen = 0;

	// Shadow text is only used for spacing decisions.
	// Do not use m_shadow.length() for span indices.
	std::string m_shadow;

	std::vector<MarkdownRun> m_runs;
	std::vector<MarkdownSpan> m_spans;
	std::stack<MarkdownSpan> m_openSpans;

	void AppendSource(const MD_CHAR* text, MD_SIZE size) {
		if (size == 0) {
			return;
		}

		size_t localStart = static_cast<size_t>(text - m_parseBase);
		size_t srcStart = m_sourceBase + localStart;
		size_t srcEnd = srcStart + static_cast<size_t>(size);

		size_t outStart = m_outputLen;
		size_t outEnd = m_outputLen + static_cast<size_t>(size);

		if (!m_runs.empty()) {
			MarkdownRun& last = m_runs.back();
			if (!last.synthetic &&
				last.sourceEnd == srcStart &&
				last.outEnd == outStart) {
				last.sourceEnd = srcEnd;
				last.outEnd = outEnd;
			} else {
				m_runs.push_back({
					false,
					outStart,
					outEnd,
					srcStart,
					srcEnd,
					""
				});
			}
		} else {
			m_runs.push_back({
				false,
				outStart,
				outEnd,
				srcStart,
				srcEnd,
				""
			});
		}

		m_shadow.append(text, static_cast<size_t>(size));
		m_outputLen = outEnd;
	}

	void AppendSynthetic(const std::string& text, size_t visualLength) {
		if (text.empty() || visualLength == 0) {
			return;
		}

		m_runs.push_back({
			true,
			m_outputLen,
			m_outputLen + visualLength,
			0,
			0,
			text
		});

		m_shadow += text;
		m_outputLen += visualLength;
	}

	void EnsureSpacing(int count) {
		if (m_shadow.empty()) {
			return;
		}

		int existingNewlines = 0;
		for (auto it = m_shadow.rbegin(); it != m_shadow.rend(); ++it) {
			if (*it == '\n') {
				existingNewlines++;
			} else {
				break;
			}
		}

		while (existingNewlines < count) {
			AppendSynthetic("\n", 1);
			existingNewlines++;
		}
	}

	static int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);

		if (type == MD_BLOCK_H) {
			self->EnsureSpacing(2);

			auto* hDetail = static_cast<MD_BLOCK_H_DETAIL*>(detail);
			self->m_openSpans.push({
				MarkdownElem::Header,
				self->m_outputLen,
				0,
				static_cast<int>(hDetail->level)
			});
		}
		else if (type == MD_BLOCK_P) {
			self->EnsureSpacing(2);

			self->m_openSpans.push({
				MarkdownElem::Paragraph,
				self->m_outputLen,
				0,
				0
			});
		}
		else if (type == MD_BLOCK_LI) {
			self->EnsureSpacing(1);

			// Important: visual length is 3, not UTF-8 byte length.
			self->AppendSynthetic(" ● ", 3);

			self->m_openSpans.push({
				MarkdownElem::ListItem,
				self->m_outputLen,
				0,
				0
			});
		}

		return 0;
	}

	static int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);

		if (type == MD_BLOCK_H || type == MD_BLOCK_LI || type == MD_BLOCK_P) {
			if (!self->m_openSpans.empty()) {
				auto span = self->m_openSpans.top();
				self->m_openSpans.pop();

				span.end = self->m_outputLen;

				if (span.type != MarkdownElem::Paragraph) {
					self->m_spans.push_back(span);
				}
			}

			self->EnsureSpacing(1);
		}

		return 0;
	}

	static int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);

		MarkdownElem elem;

		switch (type) {
			case MD_SPAN_STRONG: elem = MarkdownElem::Bold; break;
			case MD_SPAN_EM:     elem = MarkdownElem::Italic; break;
			case MD_SPAN_CODE:   elem = MarkdownElem::Code; break;
			case MD_SPAN_A:      elem = MarkdownElem::Link; break;
			default: return 0;
		}

		self->m_openSpans.push({
			elem,
			self->m_outputLen,
			0,
			0
		});

		return 0;
	}

	static int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);

		if (type != MD_SPAN_STRONG &&
			type != MD_SPAN_EM &&
			type != MD_SPAN_CODE &&
			type != MD_SPAN_A) {
			return 0;
		}

		if (self->m_openSpans.empty()) {
			return 0;
		}

		auto span = self->m_openSpans.top();
		self->m_openSpans.pop();

		span.end = self->m_outputLen;
		self->m_spans.push_back(span);

		return 0;
	}

	static int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);

		switch (type) {
			case MD_TEXT_NORMAL:
			case MD_TEXT_CODE:
			case MD_TEXT_ENTITY:
				self->AppendSource(text, size);
				break;

			case MD_TEXT_SOFTBR:
				if (!self->m_shadow.empty() &&
					self->m_shadow.back() != ' ' &&
					self->m_shadow.back() != '\n') {
					self->AppendSynthetic(" ", 1);
				}
				break;

			case MD_TEXT_BR:
				self->AppendSynthetic("\n", 1);
				break;

			default:
				break;
		}

		return 0;
	}
};

#endif
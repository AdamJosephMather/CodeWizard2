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

struct ProcessedMarkdown {
	std::string cleanText;
	std::vector<MarkdownSpan> spans;
};

class MarkdownParser {
public:
	static ProcessedMarkdown Process(const std::string& input) {
		MarkdownParser parser;
		
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

		md_parse(input.c_str(), (MD_SIZE)input.size(), &mdParser, &parser);
		
		std::sort(parser.m_spans.begin(), parser.m_spans.end(), [](const MarkdownSpan& a, const MarkdownSpan& b) {
			if (a.start != b.start) {
				return a.start < b.start;
			}
			return a.end > b.end; 
		});
		
		return { parser.m_result, parser.m_spans };
	}

private:
	std::string m_result;
	std::vector<MarkdownSpan> m_spans;
	std::stack<MarkdownSpan> m_openSpans;

	// Ensures we have exactly 'count' newlines at the end of the current result
	void EnsureSpacing(int count) {
		if (m_result.empty()) return;
		
		int existingNewlines = 0;
		for (auto it = m_result.rbegin(); it != m_result.rend(); ++it) {
			if (*it == '\n') existingNewlines++;
			else break;
		}

		while (existingNewlines < count) {
			m_result += '\n';
			existingNewlines++;
		}
	}

	static int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);
		
		if (type == MD_BLOCK_H) {
			self->EnsureSpacing(2); // Headers get a gap above
			auto* hDetail = static_cast<MD_BLOCK_H_DETAIL*>(detail);
			self->m_openSpans.push({MarkdownElem::Header, self->m_result.length(), 0, (int)hDetail->level});
		} 
		else if (type == MD_BLOCK_P) {
			self->EnsureSpacing(2); // Paragraphs get a gap above
			self->m_openSpans.push({MarkdownElem::Paragraph, self->m_result.length(), 0, 0});
		}
		else if (type == MD_BLOCK_LI) {
			self->EnsureSpacing(1); // List items just need a fresh line
			self->m_result += " ● ";
			self->m_openSpans.push({MarkdownElem::ListItem, self->m_result.length(), 0, 0});
		}
		return 0;
	}

	static int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);
		
		if (type == MD_BLOCK_H || type == MD_BLOCK_LI || type == MD_BLOCK_P) {
			if (!self->m_openSpans.empty()) {
				auto span = self->m_openSpans.top();
				self->m_openSpans.pop();
				span.end = self->m_result.length();
				// We only save spans we actually want to color later
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

		self->m_openSpans.push({elem, self->m_result.length(), 0, 0});
		return 0;
	}

	static int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);
		
		// FIX: Guard the stack! Only pop if this is a span type we actually track.
		if (type != MD_SPAN_STRONG && type != MD_SPAN_EM && 
			type != MD_SPAN_CODE && type != MD_SPAN_A) {
			return 0;
		}
	
		if (self->m_openSpans.empty()) return 0;
	
		auto span = self->m_openSpans.top();
		self->m_openSpans.pop();
		span.end = self->m_result.length();
		self->m_spans.push_back(span);
		return 0;
	}

	static int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
		auto* self = static_cast<MarkdownParser*>(userdata);
		
		switch(type) {
			case MD_TEXT_NORMAL:
			case MD_TEXT_CODE:
			case MD_TEXT_ENTITY:
				self->m_result.append(text, size);
				break;
	
			case MD_TEXT_SOFTBR:
				// This handles the "Adam Mather.I am..." issue. 
				// If it's a soft break, we replace the newline with a single space.
				if (!self->m_result.empty() && self->m_result.back() != ' ' && self->m_result.back() != '\n') {
					self->m_result += ' ';
				}
				break;
	
			case MD_TEXT_BR:
				// This is a hard break (usually two spaces at the end of a line)
				self->m_result += '\n';
				break;
	
			default:
				break;
		}
		return 0;
	}
};

#endif
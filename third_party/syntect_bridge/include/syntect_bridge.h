#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CW_SyntaxEngine CW_SyntaxEngine;
typedef struct CW_SyntaxState CW_SyntaxState;

typedef enum CW_ColorRole {
	CW_ROLE_TEXT = 3, // same as vars
	CW_ROLE_STRING = 1,
	CW_ROLE_COMMENT = 2,
	CW_ROLE_VARIABLE = 3,
	CW_ROLE_TYPE = 4,
	CW_ROLE_FUNCTION = 5,
	CW_ROLE_KEYWORD = 6,
	CW_ROLE_PUNCTUATION = 7,
	CW_ROLE_LITERAL = 8,
	CW_ROLE_OPERATOR = 9,
	CW_ROLE_PREPROCESSOR = 10,
	CW_ROLE_INVALID = 11,
	CW_ROLE_COUNT = 12
} CW_ColorRole;

typedef struct CW_HighlightToken {
	uint32_t start_byte;
	uint32_t end_byte;
	int32_t role;
} CW_HighlightToken;

typedef struct CW_LineResult {
	CW_HighlightToken* tokens;
	size_t token_count;

	// Caller owns this. Free with cw_syntect_destroy_state().
	CW_SyntaxState* next_state;

	// Hash of next_state. Useful for quick line-cache comparisons.
	uint64_t state_hash;

	int status;
} CW_LineResult;

CW_SyntaxEngine* cw_syntect_setup(
	const char* syntax_extension_or_name,
	const char* syntax_folder,
	int lines_include_newline
);

void cw_syntect_destroy_engine(CW_SyntaxEngine* engine);

CW_SyntaxState* cw_syntect_initial_state(CW_SyntaxEngine* engine);
CW_SyntaxState* cw_syntect_clone_state(const CW_SyntaxState* state);
void cw_syntect_destroy_state(CW_SyntaxState* state);

uint64_t cw_syntect_state_hash(const CW_SyntaxState* state);
int cw_syntect_state_equal(const CW_SyntaxState* a, const CW_SyntaxState* b);

CW_LineResult cw_syntect_highlight_line(
	CW_SyntaxEngine* engine,
	const CW_SyntaxState* previous_state,
	const char* line_text
);

void cw_syntect_destroy_tokens(CW_HighlightToken* tokens, size_t token_count);

uint32_t cw_syntect_role_count(void);

#ifdef __cplusplus
}
#endif

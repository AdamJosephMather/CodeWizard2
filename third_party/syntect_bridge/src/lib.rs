use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::path::Path;
use syntect::dumps;
use std::ptr;
use std::str::FromStr;

use syntect::easy::ScopeRegionIterator;
use syntect::highlighting::ScopeSelectors;
use syntect::parsing::{ParseState, ScopeStack, SyntaxReference, SyntaxSet};

// ----------------------------
// Status codes
// ----------------------------

const CW_OK: c_int = 0;
const CW_ERR_NULL_ENGINE: c_int = 1;
const CW_ERR_NULL_STATE: c_int = 2;
const CW_ERR_NULL_LINE: c_int = 3;
const CW_ERR_HIGHLIGHT_FAILED: c_int = 4;
const CW_ERR_PANIC: c_int = 5;

// ----------------------------
// Role indices returned to C/C++
// ----------------------------

pub const CW_ROLE_TEXT: u32 = 3; // to match variable
pub const CW_ROLE_STRING: u32 = 1;
pub const CW_ROLE_COMMENT: u32 = 2;
pub const CW_ROLE_VARIABLE: u32 = 3;
pub const CW_ROLE_TYPE: u32 = 4;
pub const CW_ROLE_FUNCTION: u32 = 5;
pub const CW_ROLE_KEYWORD: u32 = 6;
pub const CW_ROLE_PUNCTUATION: u32 = 7;
pub const CW_ROLE_LITERAL: u32 = 8;
pub const CW_ROLE_OPERATOR: u32 = 9;
pub const CW_ROLE_PREPROCESSOR: u32 = 10;
pub const CW_ROLE_INVALID: u32 = 11;
pub const CW_ROLE_COUNT: u32 = 12;

// ----------------------------
// C ABI structs
// ----------------------------

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CW_HighlightToken {
	pub start_byte: u32,
	pub end_byte: u32,
	pub role: u32,
}

#[repr(C)]
pub struct CW_LineResult {
	pub tokens: *mut CW_HighlightToken,
	pub token_count: usize,

	// Caller owns this. Free with cw_syntect_destroy_state().
	pub next_state: *mut CW_SyntaxState,

	// Hash of next_state. Useful for quick line-cache comparisons.
	pub state_hash: u64,

	pub status: c_int,
}

// Opaque to C/C++.
pub struct CW_SyntaxEngine {
	syntax_set: SyntaxSet,
	syntax_name: String,
	selectors: CW_Selectors,
}

// Opaque to C/C++.
#[derive(Clone)]
pub struct CW_SyntaxState {
	parse_state: ParseState,
	scope_stack: ScopeStack,
	hash: u64,

	// Kept only so cw_syntect_state_equal() can do an exact compare if hashes match.
	// Do not serialize this or depend on it across Syntect versions.
	debug_text: String,
}

struct CW_Selectors {
	invalid: ScopeSelectors,
	comment: ScopeSelectors,
	string: ScopeSelectors,
	preprocessor: ScopeSelectors,
	literal: ScopeSelectors,
	ty: ScopeSelectors,
	function: ScopeSelectors,
	keyword: ScopeSelectors,
	operator: ScopeSelectors,
	variable: ScopeSelectors,
	punctuation: ScopeSelectors,
}

// ----------------------------
// Helpers
// ----------------------------

static CW_SYNTAXES_NONEWLINES_DUMP: &[u8] =
	include_bytes!(concat!(env!("OUT_DIR"), "/cw-syntect-syntaxes-nonewlines.packdump"));

static CW_SYNTAXES_NEWLINES_DUMP: &[u8] =
	include_bytes!(concat!(env!("OUT_DIR"), "/cw-syntect-syntaxes-newlines.packdump"));

fn load_embedded_syntax_set(lines_include_newline: bool) -> Result<SyntaxSet, String> {
	let dump = if lines_include_newline {
		CW_SYNTAXES_NEWLINES_DUMP
	} else {
		CW_SYNTAXES_NONEWLINES_DUMP
	};

	dumps::from_uncompressed_data(dump)
		.map_err(|err| format!("failed to load embedded syntax dump: {err}"))
}

fn c_string(ptr: *const c_char) -> Option<String> {
	if ptr.is_null() {
		return None;
	}

	unsafe { Some(CStr::from_ptr(ptr).to_string_lossy().into_owned()) }
}

fn to_u32_saturating(value: usize) -> u32 {
	if value > u32::MAX as usize {
		u32::MAX
	} else {
		value as u32
	}
}

fn fnv1a64(bytes: &[u8]) -> u64 {
	let mut hash: u64 = 0xcbf29ce484222325;

	for b in bytes {
		hash ^= *b as u64;
		hash = hash.wrapping_mul(0x100000001b3);
	}

	hash
}

fn build_state_debug_text(parse_state: &ParseState, scope_stack: &ScopeStack) -> String {
	// This is a practical state fingerprint for editor cache invalidation.
	// It is intentionally not a stable file format.
	format!("{:?}\n{:?}", parse_state, scope_stack)
}

fn state_hash_from_debug_text(debug_text: &str) -> u64 {
	fnv1a64(debug_text.as_bytes())
}

impl CW_SyntaxState {
	fn new(parse_state: ParseState, scope_stack: ScopeStack) -> Self {
		let debug_text = build_state_debug_text(&parse_state, &scope_stack);
		let hash = state_hash_from_debug_text(&debug_text);

		Self {
			parse_state,
			scope_stack,
			hash,
			debug_text,
		}
	}
}

impl CW_Selectors {
	fn new() -> Self {
		Self {
			invalid: selectors("invalid"),
			comment: selectors("comment"),
			string: selectors("string"),
			preprocessor: selectors(
				"meta.preprocessor, keyword.control.import, punctuation.definition.preprocessor",
			),
			literal: selectors(
				"constant.numeric, constant.language, constant.character, constant.other",
			),
			ty: selectors(
				"entity.name.type, entity.name.class, entity.name.struct, entity.name.enum, support.type, storage.type",
			),
			function: selectors(
				"entity.name.function, support.function, variable.function"
			),
			keyword: selectors("keyword, storage.modifier"),
			operator: selectors("keyword.operator"),
			variable: selectors("variable, variable.other, variable.parameter"),
			punctuation: selectors("punctuation"),
		}
	}
}

fn selectors(text: &str) -> ScopeSelectors {
	ScopeSelectors::from_str(text).expect("hardcoded Syntect selector should parse")
}

impl CW_SyntaxEngine {
	fn syntax(&self) -> &SyntaxReference {
		self.syntax_set
			.syntaxes()
			.iter()
			.find(|s| s.name == self.syntax_name)
			.unwrap_or_else(|| self.syntax_set.find_syntax_plain_text())
	}
}

fn choose_syntax<'a>(
	syntax_set: &'a SyntaxSet,
	requested: &str,
) -> Option<&'a SyntaxReference> {
	let trimmed = requested.trim();
	let extension = trimmed.trim_start_matches('.');

	if let Some(syntax) = syntax_set.find_syntax_by_extension(extension) {
		return Some(syntax);
	}

	syntax_set
		.syntaxes()
		.iter()
		.find(|s| s.name.eq_ignore_ascii_case(trimmed))
}

fn classify_scope_stack(selectors: &CW_Selectors, stack: &ScopeStack) -> u32 {
	let scopes = stack.as_slice();

	// Order matters. More specific / stronger classifications go first.
	if selectors.invalid.does_match(scopes).is_some() {
		CW_ROLE_INVALID
	} else if selectors.comment.does_match(scopes).is_some() {
		CW_ROLE_COMMENT
	} else if selectors.string.does_match(scopes).is_some() {
		CW_ROLE_STRING
	} else if selectors.preprocessor.does_match(scopes).is_some() {
		CW_ROLE_PREPROCESSOR
	} else if selectors.literal.does_match(scopes).is_some() {
		CW_ROLE_LITERAL
	} else if selectors.ty.does_match(scopes).is_some() {
		CW_ROLE_TYPE
	} else if selectors.function.does_match(scopes).is_some() {
		CW_ROLE_FUNCTION
	} else if selectors.operator.does_match(scopes).is_some() {
		CW_ROLE_OPERATOR
	} else if selectors.keyword.does_match(scopes).is_some() {
		CW_ROLE_KEYWORD
	} else if selectors.variable.does_match(scopes).is_some() {
		CW_ROLE_VARIABLE
	} else if selectors.punctuation.does_match(scopes).is_some() {
		CW_ROLE_PUNCTUATION
	} else {
		CW_ROLE_TEXT
	}
}

fn push_token(tokens: &mut Vec<CW_HighlightToken>, start_byte: u32, end_byte: u32, role: u32) {
	if start_byte == end_byte {
		return;
	}

	// Merge adjacent same-role regions. This reduces draw spans substantially.
	if let Some(last) = tokens.last_mut() {
		if last.end_byte == start_byte && last.role == role {
			last.end_byte = end_byte;
			return;
		}
	}

	tokens.push(CW_HighlightToken {
		start_byte,
		end_byte,
		role,
	});
}

fn empty_line_result(status: c_int) -> CW_LineResult {
	CW_LineResult {
		tokens: ptr::null_mut(),
		token_count: 0,
		next_state: ptr::null_mut(),
		state_hash: 0,
		status,
	}
}

fn vec_to_raw_parts(mut tokens: Vec<CW_HighlightToken>) -> (*mut CW_HighlightToken, usize) {
	if tokens.is_empty() {
		return (ptr::null_mut(), 0);
	}

	let token_count = tokens.len();
	let token_ptr = tokens.as_mut_ptr();

	std::mem::forget(tokens);

	(token_ptr, token_count)
}

// ----------------------------
// Public C API
// ----------------------------

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_setup(
	syntax_extension_or_name: *const c_char,
	syntax_folder: *const c_char,
	lines_include_newline: c_int,
) -> *mut CW_SyntaxEngine {
	let result = std::panic::catch_unwind(|| {
		let requested_syntax =
			c_string(syntax_extension_or_name).unwrap_or_else(|| "txt".to_string());

		let include_newlines = lines_include_newline != 0;

		let mut syntax_set = match load_embedded_syntax_set(include_newlines) {
			Ok(syntax_set) => syntax_set,
			Err(_) => return ptr::null_mut(),
		};

		// Optional: load additional .sublime-syntax files from a folder.
		// Pass NULL or "" to use Syntect's built-in defaults only.
		if let Some(folder) = c_string(syntax_folder) {
			let folder = folder.trim();

			if !folder.is_empty() {
				let mut builder = syntax_set.into_builder();

				if builder
					.add_from_folder(Path::new(folder), include_newlines)
					.is_err()
				{
					return ptr::null_mut();
				}

				syntax_set = builder.build();
			}
		}
		
		let Some(syntax) = choose_syntax(&syntax_set, &requested_syntax) else {
			return ptr::null_mut();
		};
		
		let syntax_name = syntax.name.clone();
		
		let engine = CW_SyntaxEngine {
			syntax_set,
			syntax_name,
			selectors: CW_Selectors::new(),
		};

		Box::into_raw(Box::new(engine))
	});

	match result {
		Ok(ptr) => ptr,
		Err(_) => ptr::null_mut(),
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_destroy_engine(engine: *mut CW_SyntaxEngine) {
	if engine.is_null() {
		return;
	}

	unsafe {
		drop(Box::from_raw(engine));
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_initial_state(engine: *mut CW_SyntaxEngine) -> *mut CW_SyntaxState {
	let result = std::panic::catch_unwind(|| {
		if engine.is_null() {
			return ptr::null_mut();
		}

		let engine = unsafe { &mut *engine };
		let parse_state = ParseState::new(engine.syntax());
		let scope_stack = ScopeStack::new();
		let state = CW_SyntaxState::new(parse_state, scope_stack);

		Box::into_raw(Box::new(state))
	});

	match result {
		Ok(ptr) => ptr,
		Err(_) => ptr::null_mut(),
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_clone_state(state: *const CW_SyntaxState) -> *mut CW_SyntaxState {
	if state.is_null() {
		return ptr::null_mut();
	}

	let state = unsafe { &*state };
	Box::into_raw(Box::new(state.clone()))
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_destroy_state(state: *mut CW_SyntaxState) {
	if state.is_null() {
		return;
	}

	unsafe {
		drop(Box::from_raw(state));
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_state_hash(state: *const CW_SyntaxState) -> u64 {
	if state.is_null() {
		return 0;
	}

	let state = unsafe { &*state };
	state.hash
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_state_equal(
	a: *const CW_SyntaxState,
	b: *const CW_SyntaxState,
) -> c_int {
	if a.is_null() || b.is_null() {
		return 0;
	}

	let a = unsafe { &*a };
	let b = unsafe { &*b };

	if a.hash != b.hash {
		return 0;
	}

	if a.debug_text == b.debug_text {
		1
	} else {
		0
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_highlight_line(
	engine: *mut CW_SyntaxEngine,
	previous_state: *const CW_SyntaxState,
	line_text: *const c_char,
) -> CW_LineResult {
	let result = std::panic::catch_unwind(|| {
		if engine.is_null() {
			return empty_line_result(CW_ERR_NULL_ENGINE);
		}

		if previous_state.is_null() {
			return empty_line_result(CW_ERR_NULL_STATE);
		}

		if line_text.is_null() {
			return empty_line_result(CW_ERR_NULL_LINE);
		}

		let line = c_string(line_text).unwrap_or_default();
		let engine = unsafe { &mut *engine };
		let previous_state = unsafe { &*previous_state };

		let mut parse_state = previous_state.parse_state.clone();
		let mut scope_stack = previous_state.scope_stack.clone();
		let mut tokens: Vec<CW_HighlightToken> = Vec::new();

		let ops = match parse_state.parse_line(&line, &engine.syntax_set) {
			Ok(ops) => ops,
			Err(_) => return empty_line_result(CW_ERR_HIGHLIGHT_FAILED),
		};

		let mut byte_pos: usize = 0;

		for (text, op) in ScopeRegionIterator::new(&ops, &line) {
			if scope_stack.apply(op).is_err() {
				return empty_line_result(CW_ERR_HIGHLIGHT_FAILED);
			}

			let start = byte_pos;
			let end = start + text.len();
			byte_pos = end;

			if text.is_empty() {
				continue;
			}

			let role = classify_scope_stack(&engine.selectors, &scope_stack);

			push_token(
				&mut tokens,
				to_u32_saturating(start),
				to_u32_saturating(end),
				role,
			);
		}

		let next_state = CW_SyntaxState::new(parse_state, scope_stack);
		let next_state_hash = next_state.hash;
		let next_state_ptr = Box::into_raw(Box::new(next_state));
		let (token_ptr, token_count) = vec_to_raw_parts(tokens);

		CW_LineResult {
			tokens: token_ptr,
			token_count,
			next_state: next_state_ptr,
			state_hash: next_state_hash,
			status: CW_OK,
		}
	});

	match result {
		Ok(line_result) => line_result,
		Err(_) => empty_line_result(CW_ERR_PANIC),
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_destroy_tokens(
	tokens: *mut CW_HighlightToken,
	token_count: usize,
) {
	if tokens.is_null() || token_count == 0 {
		return;
	}

	unsafe {
		let _ = Vec::from_raw_parts(tokens, token_count, token_count);
	}
}

#[unsafe(no_mangle)]
pub extern "C" fn cw_syntect_role_count() -> u32 {
	CW_ROLE_COUNT
}

use std::env;
use std::error::Error;
use std::fs;
use std::path::{Path, PathBuf};

use syntect::dumps;
use syntect::parsing::syntax_definition::Pattern;
use syntect::parsing::{SyntaxSet, SyntaxSetBuilder};

fn main() {
	if let Err(err) = real_main() {
		panic!("failed to build embedded syntect syntax dumps: {err}");
	}
}

fn real_main() -> Result<(), Box<dyn Error>> {
	let out_dir = PathBuf::from(env::var_os("OUT_DIR").ok_or("OUT_DIR was not set")?);
	let syntax_dir = Path::new("syntaxes");

	println!("cargo:rerun-if-changed=build.rs");

	if syntax_dir.exists() {
		println!("cargo:rerun-if-changed=syntaxes");
		print_rerun_for_files(syntax_dir)?;
	}

	build_dump(
		false,
		syntax_dir,
		&out_dir.join("cw-syntect-syntaxes-nonewlines.packdump"),
	)?;

	build_dump(
		true,
		syntax_dir,
		&out_dir.join("cw-syntect-syntaxes-newlines.packdump"),
	)?;

	Ok(())
}

fn build_dump(
	lines_include_newline: bool,
	syntax_dir: &Path,
	out_path: &Path,
) -> Result<(), Box<dyn Error>> {
	let defaults = if lines_include_newline {
		SyntaxSet::load_defaults_newlines()
	} else {
		SyntaxSet::load_defaults_nonewlines()
	};

	// Preserve syntax/context order so the defaults' linked context references
	// remain valid. Re-add definitions to allow a targeted bundled JS correction.
	let defaults = defaults.into_builder();
	let mut builder = SyntaxSetBuilder::new();
	for mut syntax in defaults.syntaxes().iter().cloned() {
		if syntax.scope.to_string() == "source.js" {
			for (name, context) in &mut syntax.contexts {
				if name.starts_with("#anon_function-call-params_") {
					for pattern in &mut context.patterns {
						if let Pattern::Match(pattern) = pattern {
							if pattern.regex.regex_str() == r"(,)\s+" {
								// A comma is valid with no following whitespace, including
								// at EOL when the caller supplies lines without newlines.
								pattern.regex = syntect::parsing::Regex::new(r"(,)\s*".to_owned());
							}
						}
					}
				}
			}
		}
		builder.add(syntax);
	}

	// Optional project-local syntaxes.
	//
	// This folder should contain .sublime-syntax files somewhere under it, e.g.
	//
	// syntaxes/
	//   Zig/
	//     Zig.sublime-syntax
	//
	// Syntect's add_from_folder() loads .sublime-syntax files recursively.
	if syntax_dir.exists() {
		builder.add_from_folder(syntax_dir, lines_include_newline)?;
	}

	let syntax_set = builder.build();
	dumps::dump_to_uncompressed_file(&syntax_set, out_path)?;

	Ok(())
}

fn print_rerun_for_files(dir: &Path) -> Result<(), Box<dyn Error>> {
	for entry in fs::read_dir(dir)? {
		let entry = entry?;
		let path = entry.path();

		if path.is_dir() {
			print_rerun_for_files(&path)?;
		} else {
			println!("cargo:rerun-if-changed={}", path.display());
		}
	}

	Ok(())
}

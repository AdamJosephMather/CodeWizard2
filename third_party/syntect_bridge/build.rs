use std::env;
use std::error::Error;
use std::fs;
use std::path::{Path, PathBuf};

use syntect::dumps;
use syntect::parsing::SyntaxSet;

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

	let mut builder = defaults.into_builder();

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
import os
import re
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(SCRIPT_DIR, "build", "windows-release")
INSTALLER_DIR = os.path.join(SCRIPT_DIR, "Installer")

EXCLUDE_PREFIXES = ("_", ".", "CMakeFiles")
EXCLUDE_EXTENSIONS = (".exp", ".pdb", ".cmake", ".txt", ".ninja", ".json", ".log", ".deps", ".lib", ".dll.a")


def find_output_dir():
	entries = os.listdir(INSTALLER_DIR)
	for name in sorted(entries, reverse=True):
		if os.path.isdir(os.path.join(INSTALLER_DIR, name)):
			return os.path.join(INSTALLER_DIR, name)
	raise RuntimeError("No versioned installer folder found in Installer/")


def should_include(name):
	if name.startswith(EXCLUDE_PREFIXES):
		return False
	if os.path.isdir(os.path.join(BUILD_DIR, name)):
		return True
	_, ext = os.path.splitext(name)
	if ext.lower() in EXCLUDE_EXTENSIONS:
		return False
	return True


def main():
	output_base = find_output_dir()
	dest_root = os.path.join(output_base, "CodeWizard", "CodeWizard")

	entries = os.listdir(BUILD_DIR)
	to_copy = [name for name in sorted(entries) if should_include(name)]

	print(f"Source:  {BUILD_DIR}")
	print(f"Dest:    {dest_root}")
	print(f"Files/folders to copy ({len(to_copy)}):")
	for name in to_copy:
		print(f"  - {name}")
	
	for name in to_copy:
		src = os.path.join(BUILD_DIR, name)
		dst = os.path.join(dest_root, name)
		if os.path.isfile(src):
			os.makedirs(os.path.dirname(dst), exist_ok=True)
			shutil.copy2(src, dst)
		elif os.path.isdir(src):
			if os.path.exists(dst):
				shutil.rmtree(dst)
			shutil.copytree(src, dst)

	print("Done.")


if __name__ == "__main__":
	main()

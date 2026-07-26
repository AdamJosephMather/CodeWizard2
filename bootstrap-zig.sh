#!/usr/bin/env bash
set -euo pipefail

zig_version="0.15.2"
expected_sha256="02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
install_dir="$root/zig-version/linux-x86_64"
zig="$install_dir/zig"

if [[ -x "$zig" ]]; then
	if [[ "$("$zig" version)" != "$zig_version" ]]; then
		echo "Expected Zig $zig_version at $zig" >&2
		exit 1
	fi
	printf '%s\n' "$zig"
	exit 0
fi

archive="$root/.zig-$zig_version-linux.tar.xz"
extract_root="$root/.zig-$zig_version-extract"
url="https://ziglang.org/download/$zig_version/zig-x86_64-linux-$zig_version.tar.xz"

curl --fail --location "$url" --output "$archive"
printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum --check --status
mkdir -p "$extract_root"
tar -xJf "$archive" -C "$extract_root"
mkdir -p "$(dirname "$install_dir")"
mv "$extract_root/zig-x86_64-linux-$zig_version" "$install_dir"
rm "$archive"
rmdir "$extract_root"

printf '%s\n' "$zig"

#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$HOME/.cargo/bin:$PATH"
for tool in curl gcc cargo rustup; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "$tool is required to build CodeWizard." >&2
		exit 1
	fi
done

zig="$(bash "$root/bootstrap-zig.sh")"
# Zig's atomic cache renames are not reliable on a repository mounted through
# WSL's DrvFS. Keep caches on the native Linux filesystem; artifacts still
# install into build/linux-release in the repository.
export ZIG_GLOBAL_CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/codewizard-zig-0.15.2"
export ZIG_LOCAL_CACHE_DIR="/tmp/codewizard-zig-${UID}"

rustup target add x86_64-unknown-linux-gnu >/dev/null
multiarch="$(gcc -print-multiarch)"
gcc_crt_dir="$(dirname "$(gcc -print-file-name=crtbegin.o)")"
libc_config="/tmp/codewizard-zig-libc-${UID}.conf"
printf '%s\n' \
	"include_dir=/usr/include/$multiarch" \
	"sys_include_dir=/usr/include" \
	"crt_dir=/usr/lib/$multiarch" \
	"msvc_lib_dir=" \
	"kernel32_lib_dir=" \
	"gcc_dir=$gcc_crt_dir" > "$libc_config"

cd "$root"
"$zig" build \
	-Dtarget=x86_64-linux-gnu \
	--libc "$libc_config" \
	--release=fast \
	--prefix "$root/build/linux-release"

param(
	[ValidateSet("windows-release", "linux-release")]
	[string]$Target = "windows-release",
	[switch]$Run
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if ($Target -eq "linux-release") {
	if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
		throw "The Linux build requires WSL. On Linux, run: bash build.sh"
	}
	$drive = [IO.Path]::GetPathRoot($root).Substring(0, 1).ToLowerInvariant()
	$relativeRoot = $root.Substring(3).Replace("\", "/")
	$wslRoot = "/mnt/$drive/$relativeRoot"
	& wsl.exe --cd $wslRoot -- bash build.sh
	if ($LASTEXITCODE -ne 0) {
		throw "Linux Zig build failed with exit code $LASTEXITCODE"
	}
	if ($Run) {
		& wsl.exe --cd $wslRoot -- ./build/linux-release/CodeWizard
	}
	exit 0
}

foreach ($tool in @("cargo", "rustup")) {
	if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
		throw "$tool is required to build the Syntect bridge. Install Rust with rustup."
	}
}
if ((& rustup target list --installed) -notcontains "x86_64-pc-windows-gnu") {
	& rustup target add x86_64-pc-windows-gnu
	if ($LASTEXITCODE -ne 0) {
		throw "Could not install the x86_64-pc-windows-gnu Rust target."
	}
}

$zig = & (Join-Path $root "bootstrap-zig.ps1")
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root ".zig-global-cache"
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root ".zig-cache"
$prefix = Join-Path $root "build/windows-release"

Push-Location $root
try {
	& $zig build -Dtarget=x86_64-windows-gnu --release=fast --prefix $prefix
	if ($LASTEXITCODE -ne 0) {
		throw "Zig build failed with exit code $LASTEXITCODE"
	}
	if ($Run) {
		& (Join-Path $prefix "CodeWizard.exe")
	}
} finally {
	Pop-Location
}

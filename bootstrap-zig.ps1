param()

$ErrorActionPreference = "Stop"
$zigVersion = "0.15.2"
$expectedSha256 = "3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c"
$root = $PSScriptRoot
$installDir = Join-Path $root "zig-version\windows-x86_64"
$zigExe = Join-Path $installDir "zig.exe"

if (Test-Path -LiteralPath $zigExe) {
	$installedVersion = & $zigExe version
	if ($installedVersion -eq $zigVersion) {
		Write-Output $zigExe
		exit 0
	}
	throw "Expected Zig $zigVersion at $zigExe, found $installedVersion"
}

$archive = Join-Path $root ".zig-$zigVersion-windows.zip"
$extractRoot = Join-Path $root ".zig-$zigVersion-extract"
$url = "https://ziglang.org/download/$zigVersion/zig-x86_64-windows-$zigVersion.zip"

Invoke-WebRequest -Uri $url -OutFile $archive
$actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
	throw "Zig archive checksum mismatch: expected $expectedSha256, got $actualSha256"
}

Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot
Move-Item -LiteralPath (Join-Path $extractRoot "zig-x86_64-windows-$zigVersion") -Destination $installDir
Remove-Item -LiteralPath $archive
Remove-Item -LiteralPath $extractRoot -Recurse

Write-Output $zigExe

# build.ps1 - build capture.exe and deploy the binary into electron-app
#
#   .\build.ps1             build Release and copy into electron-app
#   .\build.ps1 -Clean      wipe build\ and configure from scratch
#   .\build.ps1 -NoCopy     build only, do not copy
#
# Generator is pinned: Visual Studio 18 2026, x64 (matches the original build\CMakeCache.txt).
# NOTE: keep this file pure ASCII -- Windows PowerShell 5.1 reads .ps1 as ANSI and
# non-ASCII characters break parsing when the file is saved as UTF-8 without BOM.

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$NoCopy,
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root     = $PSScriptRoot
$buildDir = Join-Path $root 'build'
$target   = Join-Path $buildDir "$Config\capture.exe"
$destDir  = Join-Path $root 'electron-app'
$dest     = Join-Path $destDir 'capture.exe'

$generator = 'Visual Studio 18 2026'

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "!!! $msg" -ForegroundColor Red; exit 1 }

# ---- 0. tools ----
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail "cmake not found in PATH" }

# ---- 1. clean ----
if ($Clean -and (Test-Path $buildDir)) {
    Step "Removing $buildDir"
    Remove-Item $buildDir -Recurse -Force
}

# ---- 2. configure ----
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Step "Configuring CMake ($generator, x64)"
    cmake -S $root -B $buildDir -G $generator -A x64
    if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed (exit $LASTEXITCODE)" }
} else {
    Step "build\ already configured - skipping configure"
}

# ---- 3. build ----
Step "Building target 'capture' ($Config)"
cmake --build $buildDir --config $Config --target capture
if ($LASTEXITCODE -ne 0) { Fail "Build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path $target)) { Fail "Expected $target but the file is missing" }
$size = (Get-Item $target).Length
Step "Built: $target ($size bytes)"

# ---- 4. copy ----
if ($NoCopy) {
    Step "-NoCopy: skipping deploy"
    exit 0
}

if (-not (Test-Path $destDir)) { Fail "Directory not found: $destDir" }

# Copy only when the content actually differs, so we do not churn mtimes needlessly.
$needCopy = $true
if (Test-Path $dest) {
    $a = (Get-FileHash $target -Algorithm SHA256).Hash
    $b = (Get-FileHash $dest   -Algorithm SHA256).Hash
    if ($a -eq $b) { $needCopy = $false }
}

if ($needCopy) {
    Step "Deploying to electron-app\capture.exe"
    Copy-Item $target $dest -Force
    $hash = (Get-FileHash $dest -Algorithm SHA256).Hash.Substring(0, 16)
    Step "Deployed (SHA256 $hash...)"
} else {
    Step "electron-app\capture.exe already up to date - skipping copy"
}

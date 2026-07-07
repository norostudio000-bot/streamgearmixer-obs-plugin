# =============================================================================
# build-plugin.ps1 - sgm-audio-source (StreamGearMixer OBS plugin) build script
#
# Requirements:
#   - Visual Studio (C++ toolchain) installed
#   - OBS Studio installed at C:\Program Files\obs-studio
#   - git (to fetch libobs headers matching the installed OBS version)
#
# What it does:
#   1. Fetches obs-studio source (headers only needed) for the installed version
#   2. Generates obs.lib import library from the installed obs.dll
#   3. Compiles sgm-audio-source.c with cl.exe
#   4. Installs the DLL to %APPDATA%\obs-studio\plugins (no admin needed)
#
# Usage (from a normal PowerShell prompt):
#   powershell -ExecutionPolicy Bypass -File build-plugin.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
$pluginName = "sgm-audio-source"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $here "build"
New-Item -ItemType Directory -Force $buildDir | Out-Null

# --- 1. Installed OBS ---------------------------------------------------------
$obsDir = "C:\Program Files\obs-studio"
$obsDll = Join-Path $obsDir "bin\64bit\obs.dll"
if (-not (Test-Path $obsDll)) { throw "OBS Studio not found: $obsDll" }
$obsVer = (Get-Item (Join-Path $obsDir "bin\64bit\obs64.exe")).VersionInfo.FileVersion
Write-Host "Installed OBS: $obsVer"

# --- 2. libobs headers (obs-studio source at the matching tag) -----------------
$srcCache = Join-Path $env:LOCALAPPDATA "StreamGearMixer\obs-studio-src"
if (-not (Test-Path (Join-Path $srcCache "libobs\obs-module.h"))) {
    Write-Host "Fetching obs-studio $obsVer source (shallow)..."
    git clone --depth 1 --branch $obsVer --no-tags https://github.com/obsproject/obs-studio.git $srcCache
}
$libobsInc = Join-Path $srcCache "libobs"

# --- 3. Visual Studio toolchain ------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw "Visual Studio C++ toolchain not found" }
$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"

function Invoke-VcTool([string]$commandLine) {
    cmd /c "`"$vcvars`" >nul 2>&1 && $commandLine"
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $commandLine" }
}

# --- 4. Generate obs.lib from the installed obs.dll ----------------------------
$defFile = Join-Path $buildDir "obs.def"
$libFile = Join-Path $buildDir "obs.lib"
if (-not (Test-Path $libFile)) {
    Write-Host "Generating obs.lib from obs.dll exports..."
    $exports = cmd /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$obsDll`""
    $lines = @("LIBRARY obs", "EXPORTS")
    $inTable = $false
    foreach ($line in $exports) {
        if ($line -match "ordinal\s+hint\s+RVA\s+name") { $inTable = $true; continue }
        if ($inTable) {
            if ($line -match "^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)") { $lines += "  $($Matches[1])" }
            elseif ($line -match "^\s*Summary") { break }
        }
    }
    Set-Content -Path $defFile -Value $lines -Encoding ascii
    Invoke-VcTool "lib /nologo /def:`"$defFile`" /machine:x64 /out:`"$libFile`""
}

# --- 5. Compile ----------------------------------------------------------------
Write-Host "Compiling $pluginName.dll ..."
$src = Join-Path $here "$pluginName.c"
$out = Join-Path $buildDir "$pluginName.dll"
Invoke-VcTool ("cl /nologo /W3 /O2 /MT /LD /std:c17 /utf-8 " +
    "/I `"$here`" /I `"$libobsInc`" " +
    "`"$src`" /Fo:`"$buildDir\\`" /Fe:`"$out`" " +
    "/link `"$libFile`" kernel32.lib")

# --- 6. Install to the per-user OBS plugin folder (no admin required) ----------
$dest = Join-Path $env:APPDATA "obs-studio\plugins\$pluginName\bin\64bit"
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item $out $dest -Force
Write-Host ""
Write-Host "Installed: $dest\$pluginName.dll"
Write-Host "Restart OBS, then add the 'StreamGearMixer' audio source."

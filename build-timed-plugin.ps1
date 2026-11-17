param([switch]$Install)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $here 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$obsBin = 'C:\Program Files\obs-studio\bin\64bit'
$obsDll = Join-Path $obsBin 'obs.dll'
$obsVer = (Get-Item -LiteralPath (Join-Path $obsBin 'obs64.exe')).VersionInfo.FileVersion
if ($obsVer -notmatch '^\d+\.\d+\.\d+$') { throw "Unexpected OBS version: $obsVer" }
$headers = Join-Path $buildDir "obs-src-$obsVer"
if (-not (Test-Path -LiteralPath (Join-Path $headers '.git'))) {
    & git clone --depth 1 --branch $obsVer --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git $headers
    if ($LASTEXITCODE -ne 0) { throw 'OBS header download failed' }
}
& git -C $headers sparse-checkout set libobs deps
if ($LASTEXITCODE -ne 0) { throw 'OBS header checkout failed' }
$tag = & git -C $headers describe --tags --exact-match HEAD
if ($LASTEXITCODE -ne 0 -or $tag -ne $obsVer) { throw 'Cached headers do not match installed OBS' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ toolchain missing' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
function Invoke-VcTool([string]$commandLine) {
    & cmd /d /c "`"$vcvars`" >nul 2>&1 && $commandLine"
    if ($LASTEXITCODE -ne 0) { throw "Build command failed: $commandLine" }
}
# Always regenerate the import library; a cached lib from an older OBS version
# must not silently survive a major-version change.
$exports = & cmd /d /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$obsDll`""
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect obs.dll exports' }
$definitions = @('LIBRARY obs','EXPORTS')
foreach ($line in $exports) {
    if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { $definitions += "  $($Matches[1])" }
}
if ($definitions.Count -lt 10) { throw 'No OBS exports found' }
$defFile=Join-Path $buildDir 'obs.def'
$libFile=Join-Path $buildDir 'obs.lib'
Set-Content -LiteralPath $defFile -Value $definitions -Encoding ascii
Invoke-VcTool "lib /nologo /def:`"$defFile`" /machine:x64 /out:`"$libFile`""
$include=Join-Path $headers 'libobs'
$src=Join-Path $here 'sgm-audio-source.c'
$out=Join-Path $buildDir 'sgm-audio-source.dll'
Invoke-VcTool "cl /nologo /W4 /O2 /MT /LD /std:c17 /utf-8 /I`"$here`" /I`"$include`" `"$src`" /Fo`"$buildDir\\`" /Fe`"$out`" /link `"$libFile`" kernel32.lib avrt.lib"
Write-Host "Built for OBS $obsVer : $out"
if ($Install) { & (Join-Path $here 'install-plugin.ps1') }

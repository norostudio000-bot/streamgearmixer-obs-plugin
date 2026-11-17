param([string]$DllPath = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $DllPath) { $DllPath = Join-Path $here 'build\sgm-audio-source.dll' }
if (-not (Test-Path -LiteralPath $DllPath)) { throw "Built plugin missing: $DllPath" }
$targetDir = Join-Path $env:ProgramData 'obs-studio\plugins\sgm-audio-source\bin\64bit'
$target = Join-Path $targetDir 'sgm-audio-source.dll'
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
# No automatic OBS termination: the user could be streaming or recording.
# Renaming a mapped DLL may be permitted by Windows. If not, preserve the old
# DLL and tell the user to close OBS and rerun this installer.
if (Test-Path -LiteralPath $target) {
    if ((Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $DllPath).Hash) {
        Write-Host 'This plugin version is already installed.'
        return
    }
    $previous=Join-Path $targetDir ('sgm-audio-source.previous-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.dll.bak')
    try { Move-Item -LiteralPath $target -Destination $previous -ErrorAction Stop }
    catch { throw ('Cannot replace the existing plugin. Close OBS and run PowerShell as administrator if access is denied. The previous plugin is intact. Windows error: ' + $_.Exception.Message) }
    try { Copy-Item -LiteralPath $DllPath -Destination $target -ErrorAction Stop }
    catch { Move-Item -LiteralPath $previous -Destination $target; throw }
} else { Copy-Item -LiteralPath $DllPath -Destination $target }
if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $DllPath).Hash) { throw 'Installed hash mismatch' }
Write-Host "Installed and verified: $target"
Write-Host 'Restart OBS to load the updated plugin. Existing source names/settings are preserved.'

@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $script=Join-Path (Get-Location) 'install-plugin.ps1'; $installArguments='-NoProfile -ExecutionPolicy Bypass -File '+[char]34+$script+[char]34; $p=Start-Process powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $installArguments -PassThru -Wait; exit $p.ExitCode"
if errorlevel 1 echo Installation failed or was cancelled. The previous plugin is preserved.
pause

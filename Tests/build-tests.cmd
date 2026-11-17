@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /W4 /O2 /MT /std:c17 /utf-8 worker-test.c /Fe:worker-test.exe /link kernel32.lib avrt.lib
if errorlevel 1 exit /b 1
worker-test.exe
exit /b %errorlevel%

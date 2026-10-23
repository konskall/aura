@echo off
rem AURA desktop simulator for testing: builds what changed, then runs it.
rem   run.bat        answers from Codex (the Codex app must be logged in)
rem   run.bat mock   DEMO answers without Codex
setlocal
cd /d "%~dp0"
set "AURA_RUN_PROVIDER=%~1"
if "%AURA_RUN_PROVIDER%"=="" set "AURA_RUN_PROVIDER=codex"
tasklist /FI "IMAGENAME eq aura_simulator.exe" 2>nul | find /I "aura_simulator.exe" >nul
if not errorlevel 1 (
    echo AURA is already running. Close its window and run this again.
    goto failed
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-simulator.ps1"
if errorlevel 1 goto failed
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\dev-simulator.ps1" -Provider %AURA_RUN_PROVIDER%
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo AURA did not start. See the messages above.
pause
exit /b 1

@echo off
rem AURA desktop simulator for testing: builds what changed, then runs it.
rem   run.bat        answers from Codex (the Codex app must be logged in), speech
rem                  recognition by Groq and voices by Azure (keys in .env)
rem   run.bat mock   DEMO answers without Codex; needs no keys
rem   run.bat device (or run.bat mock device) the Aoraton account signs in
rem                  the way the board does, through
rem                  scripts\aoraton-device-sign-in.mjs over File transfer
setlocal
cd /d "%~dp0"
set "AURA_RUN_PROVIDER=%~1"
set "AURA_RUN_AORATON="
if /I "%~1"=="device" set "AURA_RUN_PROVIDER="
if /I "%~1"=="device" set "AURA_RUN_AORATON=-AoratonDevice"
if /I "%~2"=="device" set "AURA_RUN_AORATON=-AoratonDevice"
if "%AURA_RUN_PROVIDER%"=="" set "AURA_RUN_PROVIDER=codex"
set "AURA_RUN_VOICE=-SpeechProvider groq -TtsProvider azure"
if /I "%AURA_RUN_PROVIDER%"=="mock" set "AURA_RUN_VOICE="
tasklist /FI "IMAGENAME eq aura_simulator.exe" 2>nul | find /I "aura_simulator.exe" >nul
if not errorlevel 1 (
    echo AURA is already running. Close its window and run this again.
    goto failed
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-simulator.ps1"
if errorlevel 1 goto failed
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\dev-simulator.ps1" -Provider %AURA_RUN_PROVIDER% %AURA_RUN_VOICE% %AURA_RUN_AORATON%
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo AURA did not start. See the messages above.
pause
exit /b 1

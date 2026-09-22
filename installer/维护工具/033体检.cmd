@echo off
setlocal
set "TASK033_DATA=%~dp0"
if exist "%~dp0..\033\dlss5_install.ps1" set "TASK033_DATA=%~dp0..\033\"
rem 033 check-up: reads the runtime logs and Windows crash events in the game folder, explains in plain words
rem what ran, and writes one diagnostic .txt on the Desktop to send to the author. Read-only; never launches the game.
rem 2026-09-12: double-clicking used to print an English usage line and stop, and Windows does not allow dragging
rem a file INTO a console window - so the one tool we most need players to run was unusable. It now opens a file
rem picker when started with no argument. Dragging the game .exe onto this file still works.
if not exist "%TASK033_DATA%dlss5_install.ps1" (
  echo Missing dlss5_install.ps1. Unzip the WHOLE package into a folder first ^(do not run it from inside the zip^), then run this file from that folder.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%TASK033_DATA%dlss5_install.ps1" -Action Checkup -NoSplash %*
exit /b %ERRORLEVEL%

@echo off
rem ==================================================================
rem  DLSS5 one-click package - integrity check.
rem  Double-click to verify every file against the shipped manifest.
rem  (Pure ASCII on purpose: cmd.exe garbles non-ASCII source.)
rem ==================================================================
setlocal
set "TASK033_DATA=%~dp0"
if exist "%~dp0..\033\dlss5_install.ps1" set "TASK033_DATA=%~dp0..\033\"
if not exist "%TASK033_DATA%dlss5_verify.ps1" (
  echo [!] dlss5_verify.ps1 is MISSING next to this file.
  echo     The package was not fully extracted, or antivirus removed it.
  echo     Re-extract the WHOLE zip to a folder, then run again.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%TASK033_DATA%dlss5_verify.ps1"
exit /b %errorlevel%

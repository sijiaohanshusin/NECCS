@echo off
setlocal
start "" /wait cmd.exe /c call "%~dp0Build.bat" nopause
if errorlevel 1 goto failed

start "" /wait cmd.exe /c call "%~dp0Flash_DAP.bat" nopause
if errorlevel 1 goto failed

echo.
echo [OK] Build, flash, verify, and run completed.
pause
exit /b 0

:failed
echo.
echo [ERROR] Build or flash failed. Read the message above.
pause
exit /b 1

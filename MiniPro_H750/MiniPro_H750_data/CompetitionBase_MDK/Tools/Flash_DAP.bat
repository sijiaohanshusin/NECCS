@echo off
setlocal
set "KEIL_UV4=D:\Keil5\UV4\UV4.exe"
set "PROJECT=%~dp0..\Projects\MDK-ARM\atk_h750.uvprojx"
set "LOG=%~dp0..\Output\flash_dap.log"

if not exist "%KEIL_UV4%" (
  echo [ERROR] Keil not found: %KEIL_UV4%
  pause
  exit /b 1
)

echo Flashing with CMSIS-DAP...
start "" /wait "%KEIL_UV4%" -f "%PROJECT%" -j0 -o "%LOG%"
type "%LOG%"
findstr /C:"Verify OK" "%LOG%" >nul
if errorlevel 1 (
  echo [ERROR] Flash or verify failed.
  pause
  exit /b 1
)

echo [OK] Flash verified and application started.
if /I "%~1"=="nopause" exit /b 0
pause

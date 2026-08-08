@echo off
setlocal
set "KEIL_UV4=D:\Keil5\UV4\UV4.exe"
set "PROJECT=%~dp0..\Projects\MDK-ARM\atk_h750.uvprojx"

if not exist "%KEIL_UV4%" (
  echo [ERROR] Keil not found: %KEIL_UV4%
  echo Edit KEIL_UV4 in this file if Keil is installed elsewhere.
  pause
  exit /b 1
)

echo Building %PROJECT%
start "" /wait "%KEIL_UV4%" -r "%PROJECT%" -j0
if errorlevel 1 (
  echo [ERROR] Build failed. Check ..\Output\atk_h750.build_log.htm
  pause
  exit /b 1
)

echo [OK] Build completed.
echo HEX: %~dp0..\Output\atk_h750.hex
if /I "%~1"=="nopause" exit /b 0
pause

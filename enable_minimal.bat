@echo off
echo Enabling Minimal Test Layer
echo ===========================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo Changing registry value from 0 (disabled) to 1 (enabled)...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" /t REG_DWORD /d 1 /f

echo.
echo [OK] Layer enabled!
echo Now clear logs and launch MSFS:
echo   del C:\Temp\*.log
echo.
pause

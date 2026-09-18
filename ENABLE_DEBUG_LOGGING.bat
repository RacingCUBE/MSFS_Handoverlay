@echo off
REM This must be run as Administrator

echo ============================================
echo ENABLING OPENXR LOADER DEBUG LOGGING
echo ============================================
echo.

REM Check for admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script must be run as Administrator!
    echo Right-click this file and select "Run as administrator"
    pause
    exit /b 1
)

echo Step 1: Setting registry value for debug logging...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1" /v "LoaderDebug" /t REG_SZ /d "all" /f

echo.
echo Step 2: Verifying setting...
reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v "LoaderDebug"

echo.
echo Step 3: Setting environment variable for this session...
setx XR_LOADER_DEBUG "all" /M

echo.
echo ============================================
echo DEBUG LOGGING ENABLED!
echo ============================================
echo.
echo Now:
echo 1. Close ALL VR applications (MSFS, SteamVR, etc)
echo 2. Launch MSFS in VR
echo 3. Check for log file at: %%TEMP%%\openxr_loader_*.log
echo    (Usually: C:\Users\%USERNAME%\AppData\Local\Temp\)
echo.
echo Also check: %%USERPROFILE%%\.openxr\
echo.
pause

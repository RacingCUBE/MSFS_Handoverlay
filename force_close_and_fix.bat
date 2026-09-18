@echo off
echo Force Close SteamVR and Fix Manifest
echo =====================================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1] Killing SteamVR processes...
taskkill /F /IM vrmonitor.exe 2>nul
taskkill /F /IM vrserver.exe 2>nul
taskkill /F /IM vrdashboard.exe 2>nul
taskkill /F /IM vrcompositor.exe 2>nul
timeout /t 2 >nul

echo [2] Copying latest DLL...
copy /Y "build\bin\Release\MSFSHandOverlay_Layer.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul
copy /Y "build\bin\Release\openxr_loader.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul

echo [3] Copying correct manifest (with double backslashes)...
copy /Y "api-layer\MSFSHandOverlay_api_layer.json" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul

echo [4] Verifying manifest...
type "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" | findstr "library_path"

echo.
echo [5] Registry already set to 1 (enabled)
echo.
echo [OK] Ready! Clear logs and test:
echo   del C:\Temp\MSFS*.log
echo   Launch MSFS in VR
echo.
pause

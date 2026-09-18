@echo off
echo Testing MINIMAL Layer (no custom logic)
echo =======================================

REM Check admin  
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1/5] Uninstalling current layer...
call uninstall_layer.bat >nul 2>&1

echo [2/5] Copying minimal test DLL...
copy /Y "build\bin\Release\MSFSHandOverlay_Layer_Minimal.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_Layer.dll" >nul

echo [3/5] Registering as implicit layer...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /t REG_DWORD /d 0 /f >nul

echo [4/5] Clearing old logs...
del C:\Temp\MSFS*.log 2>nul
del C:\Temp\Minimal*.log 2>nul

echo [5/5] Ready!
echo.
echo This minimal layer does NOTHING except pass through.
echo If it STILL hangs, the problem is with OpenXR/SteamVR setup.
echo If it LOADS, the problem is in our custom layer code.
echo.
echo Now launch MSFS and check C:\Temp for MinimalLayer_*.log files
echo.
pause

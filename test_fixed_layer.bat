@echo off
echo Testing Fixed API Layer
echo =====================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1/4] Installing fixed DLL...
call install_api_layer.bat >nul

echo [2/4] Registering as implicit layer...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /t REG_DWORD /d 0 /f >nul

echo [3/4] Clearing old logs...
del C:\Temp\MSFS*.log 2>nul

echo [4/4] Ready to test!
echo.
echo Now launch MSFS in VR and watch for hang.
echo If it hangs, check C:\Temp for MSFSHandOverlay_GetProcAddr.log
echo.
pause

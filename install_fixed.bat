@echo off
echo Installing Full API Layer (with fix)
echo =====================================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1/5] Unregistering test layers...
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" /f >nul 2>&1

echo [2/5] Installing full layer DLL...
call install_api_layer.bat >nul

echo [3/5] Registering as ENABLED implicit layer (value=1)...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /t REG_DWORD /d 1 /f >nul

echo [4/5] Clearing logs...
del C:\Temp\MSFS*.log 2>nul

echo [5/5] Done!
echo.
echo Registry value set to 1 (enabled) this time.
echo Launch MSFS and check C:\Temp for logs.
echo.
pause

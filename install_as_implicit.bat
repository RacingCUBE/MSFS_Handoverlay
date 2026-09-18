@echo off
echo Installing as IMPLICIT API layer (auto-loads)...
echo.

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Requires administrator privileges!
    pause
    exit /b 1
)

REM Remove from explicit layers
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /f 2>nul

REM Add to IMPLICIT layers (auto-loads)
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /t REG_DWORD /d 0 /f

if %errorLevel% equ 0 (
    echo [OK] Registered as IMPLICIT layer
    echo Layer will now auto-load for ALL OpenXR apps
) else (
    echo [ERROR] Failed to register
)

pause

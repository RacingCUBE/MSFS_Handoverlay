@echo off
echo Unregistering OpenXR API Layer...

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Requires administrator privileges!
    pause
    exit /b 1
)

REM Remove from implicit layers
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /f 2>nul

REM Remove from explicit layers  
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /f 2>nul

echo [OK] Layer unregistered
echo MSFS should now load normally
pause

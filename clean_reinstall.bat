@echo off
echo Clean Reinstall of API Layer
echo ============================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1] Unregistering ALL test layers...
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" /f >nul 2>&1

echo [2] Waiting for file locks to release...
timeout /t 3 >nul

echo [3] Deleting old files...
del "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.dll" 2>nul
del "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" 2>nul

echo [4] Installing latest DLL...
copy /Y "build\bin\Release\MSFSHandOverlay_Layer.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul
copy /Y "build\bin\Release\openxr_loader.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul

echo [5] Installing manifest (properly formatted)...
(
echo {
echo     "file_format_version": "1.0.0",
echo     "api_layer": {
echo         "name": "XR_APILAYER_MSFS_HandOverlay",
echo         "library_path": ".\MSFSHandOverlay_Layer.dll",
echo         "api_version": "1.0",
echo         "implementation_version": "1",
echo         "description": "MSFS Hand Overlay",
echo         "functions": {
echo             "xrNegotiateLoaderApiLayerInterface": "xrNegotiateLoaderApiLayerInterface"
echo         },
echo         "disable_environment": "DISABLE_XR_APILAYER_MSFS_HandOverlay"
echo     }
echo }
) > "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json"

echo [6] Registering as ENABLED implicit layer...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /t REG_DWORD /d 1 /f >nul

echo [7] Clearing logs...
del C:\Temp\MSFS*.log 2>nul

echo.
echo [OK] Clean install complete!
echo Now launch MSFS and check C:\Temp
echo.
pause

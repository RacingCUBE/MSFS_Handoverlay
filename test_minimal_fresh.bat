@echo off
echo Testing Minimal Layer (fresh install)
echo ======================================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo [1/5] Unregistering old layer...
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" /f >nul 2>&1

echo [2/5] Copying minimal DLL with NEW filename...
copy /Y "build\bin\Release\MSFSHandOverlay_Layer_Minimal.dll" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.dll" >nul

echo [3/5] Creating new manifest...
echo {"file_format_version":"1.0.0","api_layer":{"name":"XR_APILAYER_MINIMAL_TEST","library_path":".\MinimalTest.dll","api_version":"1.0","implementation_version":"1","description":"Minimal test layer","functions":{"xrNegotiateLoaderApiLayerInterface":"xrNegotiateLoaderApiLayerInterface"},"disable_environment":"DISABLE_MINIMAL_TEST"}} > "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json"

echo [4/5] Registering as implicit...
reg add "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" /t REG_DWORD /d 0 /f >nul

echo [5/5] Clearing logs...
del C:\Temp\*.log 2>nul

echo.
echo Ready! Launch MSFS and check for MinimalLayer_*.log in C:\Temp
echo.
pause

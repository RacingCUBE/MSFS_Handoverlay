@echo off
echo ========================================
echo MSFS Hand Overlay - Debug Diagnostic
echo ========================================
echo.

echo [1] Checking if API layer DLL exists...
if exist "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_Layer.dll" (
    echo    [OK] DLL found
) else (
    echo    [ERROR] DLL not found! Run install_api_layer.bat first
    pause
    exit /b 1
)

echo [2] Checking if manifest exists...
if exist "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" (
    echo    [OK] Manifest found
) else (
    echo    [ERROR] Manifest not found! Run install_api_layer.bat first
    pause
    exit /b 1
)

echo [3] Checking if main app executable exists...
if exist "build\bin\Release\MSFSHandOverlay.exe" (
    echo    [OK] Main app found
) else (
    echo    [ERROR] Main app not found! Build the project first
    pause
    exit /b 1
)

echo [4] Checking OpenXR layer manifest content...
type "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json"
echo.

echo [5] Creating log directory...
if not exist "C:\Temp" mkdir "C:\Temp"
echo    [OK] Log directory ready at C:\Temp
echo.

echo ========================================
echo NEXT STEPS:
echo ========================================
echo.
echo 1. Start MSFSHandOverlay.exe
echo    - Leave it running in the background
echo    - Check console shows "Shared memory initialized"
echo.
echo 2. Launch MSFS in VR
echo.
echo 3. After MSFS loads, check this file:
echo    C:\Temp\MSFSHandOverlay_APILayer.log
echo.
echo 4. If the log file exists and has content:
echo    - The API layer IS loading!
echo    - The issue is positioning or rendering
echo.
echo 5. If the log file doesn't exist:
echo    - The API layer is NOT loading
echo    - Check OpenXR API Layers GUI
echo    - Make sure layer is enabled
echo.
echo Press any key to open log directory...
pause
explorer C:\Temp

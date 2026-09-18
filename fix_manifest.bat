@echo off
echo Fixing Manifest (copy source file directly)
echo ==========================================

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator!
    pause
    exit /b 1
)

echo Copying manifest from source (preserves double backslashes)...
copy /Y "api-layer\MSFSHandOverlay_api_layer.json" "C:\ProgramData\OpenXR\1\api_layers\explicit.d\" >nul

echo.
echo [OK] Manifest fixed!
echo Check the file - line 5 should have: "library_path": ".\MSFSHandOverlay_Layer.dll"
echo (with TWO backslashes)
echo.
echo Then clear logs and test: del C:\Temp\MSFS*.log
echo.
pause

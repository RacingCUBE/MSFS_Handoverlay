@echo off
REM MSFS Hand Overlay - OpenXR API Layer Installation Script
REM This script installs the API layer to the OpenXR explicit layers directory

echo ========================================
echo MSFS Hand Overlay - API Layer Installer
echo ========================================
echo.

REM Check for administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires administrator privileges!
    echo Please right-click and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

echo Running with administrator privileges...
echo.

REM Define paths
set "LAYER_DIR=%ProgramData%\OpenXR\1\api_layers\explicit.d"
set "SOURCE_DIR=%~dp0build\bin\Release"
set "DLL_FILE=MSFSHandOverlay_Layer.dll"
set "JSON_FILE=MSFSHandOverlay_api_layer.json"

echo Installation directories:
echo   Source: %SOURCE_DIR%
echo   Target: %LAYER_DIR%
echo.

REM Check if source files exist
if not exist "%SOURCE_DIR%\%DLL_FILE%" (
    echo ERROR: %DLL_FILE% not found in %SOURCE_DIR%
    echo Please build the project first using:
    echo   cmake --build build --config Release
    echo.
    pause
    exit /b 1
)

if not exist "%SOURCE_DIR%\%JSON_FILE%" (
    echo ERROR: %JSON_FILE% not found in %SOURCE_DIR%
    echo Please build the project first using:
    echo   cmake --build build --config Release
    echo.
    pause
    exit /b 1
)

REM Create target directory if it doesn't exist
if not exist "%LAYER_DIR%" (
    echo Creating directory: %LAYER_DIR%
    mkdir "%LAYER_DIR%"
    if %errorLevel% neq 0 (
        echo ERROR: Failed to create directory!
        pause
        exit /b 1
    )
)

REM Copy files
echo Installing API layer files...
echo.

copy /Y "%SOURCE_DIR%\%DLL_FILE%" "%LAYER_DIR%\" >nul
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy %DLL_FILE%
    pause
    exit /b 1
)
echo [OK] Copied %DLL_FILE%

copy /Y "%SOURCE_DIR%\%JSON_FILE%" "%LAYER_DIR%\" >nul
if %errorLevel% neq 0 (
    echo ERROR: Failed to copy %JSON_FILE%
    pause
    exit /b 1
)
echo [OK] Copied %JSON_FILE%

REM Copy openxr_loader.dll dependency
copy /Y "%SOURCE_DIR%\openxr_loader.dll" "%LAYER_DIR%\" >nul
if %errorLevel% neq 0 (
    echo WARNING: Failed to copy openxr_loader.dll (may already exist)
) else (
    echo [OK] Copied openxr_loader.dll
)

echo.
echo ========================================
echo Installation Complete!
echo ========================================
echo.
echo API layer installed to:
echo   %LAYER_DIR%
echo.
echo NEXT STEPS:
echo.
echo 1. Download and run OpenXR API Layers GUI:
echo    https://github.com/fredemmott/OpenXR-API-Layers-GUI/releases
echo.
echo 2. In the GUI, enable: "XR_APILAYER_MSFS_HandOverlay"
echo.
echo 3. Start MSFSHandOverlay.exe (the main app with camera feed)
echo.
echo 4. Launch Microsoft Flight Simulator in VR mode
echo.
echo 5. Your hands should appear in VR at the configured position!
echo.
echo For troubleshooting, check console output from both:
echo   - MSFSHandOverlay.exe (shows shared memory status)
echo   - MSFS (may show API layer initialization messages)
echo.
pause

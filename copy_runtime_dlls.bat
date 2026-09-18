@echo off
REM Permanent DLL copying script - called by CMake post-build
REM Usage: copy_runtime_dlls.bat <target_directory>

set "TARGET_DIR=%~1"

if "%TARGET_DIR%"=="" (
    echo ERROR: Target directory not specified
    exit /b 1
)

echo ========================================
echo Copying runtime DLLs to: %TARGET_DIR%
echo ========================================

REM Create target directory if it doesn't exist
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

REM Copy vcpkg DLLs (OpenCV, GLEW, GLFW, etc.)
echo Copying vcpkg DLLs...
xcopy "C:\vcpkg\installed\x64-windows\bin\*.dll" "%TARGET_DIR%\" /Y /Q /I >nul 2>&1
if errorlevel 1 (
    echo WARNING: Failed to copy vcpkg DLLs - check if C:\vcpkg\installed\x64-windows\bin exists
) else (
    echo   - vcpkg DLLs copied successfully
)

REM Copy OpenVR DLL
echo Copying OpenVR DLL...
set "OPENVR_DLL=%~dp0libs\openvr\openvr-master\bin\win64\openvr_api.dll"
if exist "%OPENVR_DLL%" (
    xcopy "%OPENVR_DLL%" "%TARGET_DIR%\" /Y /Q /I >nul
    echo   - openvr_api.dll copied successfully
) else (
    echo ERROR: OpenVR DLL not found at %OPENVR_DLL%
    exit /b 1
)

REM Verify critical DLLs are present
echo Verifying critical DLLs...
set "MISSING_DLLS="

for %%D in (
    opencv_core4.dll
    opencv_videoio4.dll
    opencv_imgproc4.dll
    opencv_imgcodecs4.dll
    glew32.dll
    glfw3.dll
    openvr_api.dll
) do (
    if not exist "%TARGET_DIR%\%%D" (
        echo   X MISSING: %%D
        set "MISSING_DLLS=1"
    ) else (
        echo   + Found: %%D
    )
)

if defined MISSING_DLLS (
    echo.
    echo ERROR: Some critical DLLs are missing!
    echo Please check your vcpkg installation: C:\vcpkg\installed\x64-windows\bin
    exit /b 1
)

echo.
echo ========================================
echo All DLLs copied successfully!
echo ========================================
exit /b 0

@echo off
echo ================================
echo MSFS Hand Overlay Build Script
echo ================================
echo.

REM Check if vcpkg path is set
if "%VCPKG_ROOT%"=="" (
    echo VCPKG_ROOT not set, using default: C:\vcpkg
    set VCPKG_ROOT=C:\vcpkg
)

REM Check if vcpkg exists
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: vcpkg not found at %VCPKG_ROOT%
    echo Please install vcpkg or set VCPKG_ROOT correctly.
    echo.
    pause
    exit /b 1
)

echo Using vcpkg at: %VCPKG_ROOT%
echo.

REM Create build directory
if not exist build mkdir build
cd build

echo Running CMake configuration...
cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

if %errorlevel% neq 0 (
    echo.
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo Building Release version...
cmake --build . --config Release

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ================================
echo Build completed successfully!
echo ================================
echo.
echo Executable location: build\bin\Release\MSFSHandOverlay.exe
echo.
pause

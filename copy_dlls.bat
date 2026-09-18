@echo off
REM Copy all required DLLs to the executable directory
REM Usage: copy_dlls.bat <target_directory>

set "TARGET_DIR=%~1"

echo Copying DLLs to %TARGET_DIR%...

REM Copy vcpkg DLLs
xcopy "C:\vcpkg\installed\x64-windows\bin\*.dll" "%TARGET_DIR%\" /Y /Q /I >nul

REM Copy OpenVR DLL
xcopy "%~dp0libs\openvr\openvr-master\bin\win64\openvr_api.dll" "%TARGET_DIR%\" /Y /Q /I >nul

echo DLL copy complete.

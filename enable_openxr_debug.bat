@echo off
echo Enabling OpenXR Loader Debug Logging
echo =====================================

reg add "HKLM\SOFTWARE\Khronos\OpenXR\1" /v LoaderDebug /t REG_SZ /d "all" /f

echo.
echo Debug logging enabled.
echo Now run MSFS and check: %TEMP%\openxr_loader.log
echo.
pause

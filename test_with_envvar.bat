@echo off
echo Testing with Environment Variable (bypasses implicit layer issues)
echo ===================================================================

REM Unregister implicit layer first
reg delete "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /v "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MinimalTest.json" /f >nul 2>&1

REM Clear logs
del C:\Temp\*.log 2>nul

echo Setting XR_ENABLE_API_LAYERS environment variable...
set XR_ENABLE_API_LAYERS=XR_APILAYER_MINIMAL_TEST

echo.
echo Environment variable set for this session.
echo Now launch MSFS from THIS window:
echo   cd "C:\Program Files (x86)\Steam\steamapps\common\MicrosoftFlightSimulator"
echo   FlightSimulator.exe
echo.
echo Or launch normally and check C:\Temp
echo.
pause

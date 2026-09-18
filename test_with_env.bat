@echo off
echo Testing with XR_ENABLE_API_LAYERS environment variable
echo ======================================================

SET XR_ENABLE_API_LAYERS=XR_APILAYER_MSFS_HandOverlay

echo Environment variable set.
echo Starting MSFSHandOverlay.exe...
start "" "build\bin\Release\MSFSHandOverlay.exe"

timeout /t 2

echo.
echo Now launch MSFS from Steam normally.
echo The layer should be forced to load via environment variable.
echo Check C:\Temp for logs after MSFS loads.
echo.
pause

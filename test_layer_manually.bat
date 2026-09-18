@echo off
REM Force enable our API layer via environment variable
echo Testing API layer loading with environment variable...

REM Set environment to force load explicit layers
set XR_ENABLE_API_LAYERS=XR_APILAYER_MSFS_HandOverlay

REM Clear temp
del C:\Temp\MSFS*.log 2>nul

echo Environment set: XR_ENABLE_API_LAYERS=%XR_ENABLE_API_LAYERS%
echo.
echo Now launch MSFS in VR and check C:\Temp\ for logs
echo.
pause

REM Keep environment active
cmd /k

@echo off
echo ========================================
echo Testing Overlay with VISIBLE Position
echo ========================================
echo.
echo This will temporarily change your overlay to:
echo   - Right in front of your face (0.5m forward)
echo   - Dead center
echo   - LARGE (1 meter wide!)
echo.
echo If you can see it with these settings, we know
echo the API layer is working and just needs positioning.
echo.
pause

REM Backup current settings
copy /Y "build\bin\Release\config\settings.ini" "build\bin\Release\config\settings.ini.backup" >nul

REM Update to test position
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'KneeboardPosX = -0.689', 'KneeboardPosX = 0.0' | Set-Content 'build\bin\Release\config\settings.ini'"
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'KneeboardPosY = 1.747', 'KneeboardPosY = 0.0' | Set-Content 'build\bin\Release\config\settings.ini'"
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'KneeboardPosZ = -0.535', 'KneeboardPosZ = 0.5' | Set-Content 'build\bin\Release\config\settings.ini'"
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'KneeboardYaw = 28.8', 'KneeboardYaw = 0.0' | Set-Content 'build\bin\Release\config\settings.ini'"
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'Width = 0.49', 'Width = 1.0' | Set-Content 'build\bin\Release\config\settings.ini'"
powershell -Command "(Get-Content 'build\bin\Release\config\settings.ini') -replace 'Height = 0.29', 'Height = 0.75' | Set-Content 'build\bin\Release\config\settings.ini'"

echo.
echo [OK] Test position applied!
echo.
echo TEST POSITION:
echo   X: 0.0m (centered)
echo   Y: 0.0m (eye level)
echo   Z: 0.5m (half meter in front)
echo   Size: 1.0m x 0.75m (HUGE!)
echo.
echo NOW:
echo 1. If MSFSHandOverlay.exe is running, close it
echo 2. Start MSFSHandOverlay.exe again
echo 3. Go into MSFS VR
echo 4. Look straight ahead - you should see a HUGE overlay!
echo.
echo To restore original position:
echo   run RESTORE_POSITION.bat
echo.
pause

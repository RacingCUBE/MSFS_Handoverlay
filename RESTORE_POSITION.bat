@echo off
echo Restoring original position...

if exist "build\bin\Release\config\settings.ini.backup" (
    copy /Y "build\bin\Release\config\settings.ini.backup" "build\bin\Release\config\settings.ini" >nul
    echo [OK] Original position restored!
    del "build\bin\Release\config\settings.ini.backup"
) else (
    echo [ERROR] No backup found. Please manually edit settings.ini
)

pause

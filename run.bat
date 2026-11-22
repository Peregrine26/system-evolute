@echo off
setlocal
cd /d "%~dp0"
if not exist "build\crossdesk.exe" (
    echo.
    echo [ERROR] build\crossdesk.exe not found.
    echo Run the build first, or tell your AI assistant the build folder is missing.
    echo.
    pause
    exit /b 1
)
start "" "build\crossdesk.exe"
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to launch crossdesk.exe
    echo.
    pause
    exit /b 1
)
endlocal

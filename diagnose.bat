@echo off
setlocal
cd /d "%~dp0"
echo ============================================
echo  CrossDesk self check
echo ============================================
echo.
if not exist "build\crossdesk.exe" (
    echo [FAIL] build\crossdesk.exe not found
    pause
    exit /b 1
)
echo [OK]   crossdesk.exe found
if not exist "build\Qt6Cored.dll" (
    echo [FAIL] Qt runtime DLLs missing in build folder
    pause
    exit /b 1
)
echo [OK]   Qt runtime DLLs present
if not exist "build\platforms\qwindowsd.dll" (
    echo [FAIL] Qt platform plugin missing
    pause
    exit /b 1
)
echo [OK]   Qt platform plugin present
echo.
echo Rendering one frame to %TEMP%\crossdesk_check.png ...
echo.
"build\crossdesk.exe" --fake --shot "%TEMP%\crossdesk_check.png"
set RC=%ERRORLEVEL%
echo Exit code: %RC%
if exist "%TEMP%\crossdesk_check.png" (
    echo [OK]   screenshot written: %TEMP%\crossdesk_check.png
) else (
    echo [FAIL] no screenshot produced - the program could not start
)
echo.
pause
endlocal

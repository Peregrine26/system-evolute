@echo off
REM 双击此文件即可运行 CrossDesk（已配好 Qt DLL 路径 + 假数据演示）
set PATH=D:\Qt\6.8.3\msvc2022_64\bin;%PATH%
cd /d "%~dp0"
if not exist "build\Release\crossdesk.exe" (
    echo.
    echo [ERROR] build\Release\crossdesk.exe not found.
    echo.
    pause
    exit /b 1
)
build\Release\crossdesk.exe --fake-count 4 --windowed

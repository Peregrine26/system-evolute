@echo off
REM ============================================================
REM  CrossDesk - Windows one-shot build helper (MSVC + Qt6)
REM
REM  NOTE: keep this file ASCII-only. cmd.exe reads .bat with the
REM  OEM code page, so non-ASCII comments can break parsing.
REM
REM  Usage:
REM    scripts\build-windows.bat            configure + build Release
REM    scripts\build-windows.bat --test     also run the unit tests
REM    scripts\build-windows.bat --clean    wipe build/ first
REM
REM  Paths are auto-detected. Override them by setting QTDIR and/or
REM  VSDIR before calling this script, for example:
REM    set QTDIR=D:\Qt\6.8.3\msvc2022_64
REM    scripts\build-windows.bat
REM ============================================================
setlocal enableextensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "DO_TEST=0"
set "DO_CLEAN=0"

REM --- parse arguments -----------------------------------------
:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--test"  set "DO_TEST=1"
if /I "%~1"=="--clean" set "DO_CLEAN=1"
shift
goto parse
:parsed

REM --- locate Visual Studio ------------------------------------
if not defined VSDIR call :find_vs
if not defined VSDIR (
    echo [ERROR] Visual Studio not found. Set VSDIR to its install root.
    exit /b 1
)
echo [info] Visual Studio : %VSDIR%

REM --- locate Qt6 ----------------------------------------------
if not defined QTDIR call :find_qt
if not defined QTDIR (
    echo [ERROR] Qt6 msvc2022_64 not found. Set QTDIR, for example:
    echo         set QTDIR=D:\Qt\6.8.3\msvc2022_64
    exit /b 1
)
echo [info] Qt             : %QTDIR%

REM --- enter the MSVC environment ------------------------------
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed. Is the C++ workload installed?
    exit /b 1
)

REM --- Some shells export both HTTP_PROXY and http_proxy. MSBuild
REM     builds a .NET Hashtable from the environment and throws
REM     MSB6001 on the case-insensitive duplicate, which surfaces
REM     as a bogus "CL.exe command line is invalid" error and the
REM     misleading "No CMAKE_CXX_COMPILER could be found". A local
REM     build never needs a proxy, so drop the lowercase copies.
set "http_proxy="
set "https_proxy="

set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
set "CTEST=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "%CTEST%" set "CTEST=ctest"

if "%DO_CLEAN%"=="1" (
    if exist "%BUILD%" (
        echo [info] removing %BUILD%
        rmdir /s /q "%BUILD%"
    )
)

"%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD%" --config Release
if errorlevel 1 exit /b 1

if "%DO_TEST%"=="1" goto run_tests
goto report_done

:run_tests
echo.
echo [info] running unit tests
set "PATH=%QTDIR%\bin;%PATH%"
REM ctest captures the test output and prints it, which also works
REM when this script's own output is redirected.
"%CTEST%" --test-dir "%BUILD%" -C Release --output-on-failure
if errorlevel 1 exit /b 1

:report_done
echo.
echo [done] Start the app with:
echo          set PATH=%QTDIR%\bin;%%PATH%%
echo          %BUILD%\bin\crossdesk.exe
endlocal
exit /b 0

REM ============================================================
REM  Subroutines (must stay below the main flow)
REM ============================================================

:find_vs
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
exit /b 0

:find_qt
call :probe_qt "C:\Qt"
call :probe_qt "D:\Qt"
call :probe_qt "%USERPROFILE%\Qt"
call :probe_qt "C:\Program Files\Qt"
exit /b 0

:probe_qt
if defined QTDIR exit /b 0
if not exist "%~1" exit /b 0
for /d %%d in ("%~1\6.*") do call :take_qt "%%~d"
exit /b 0

:take_qt
if defined QTDIR exit /b 0
if not exist "%~1\msvc2022_64" exit /b 0
set "QTDIR=%~1\msvc2022_64"
exit /b 0

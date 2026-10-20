@echo off
rem Live mode: reads real top-level windows through the read-only scanner.
rem No hide/restore/raise - that needs the core module.
start "" "%~dp0build\crossdesk.exe" --live

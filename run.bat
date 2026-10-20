@echo off
rem 真实窗口模式（默认）：列表来自 Windows 扫描器，能列出当前桌面上的窗口。
rem 定位 / 确认排列会提示"等待 A 的接口"——因为那两件事属于 A 的平台命令。
start "" "%~dp0build\crossdesk.exe"

@echo off
rem 假数据模式：不依赖 A 的命令接口，可完整演示"勾两个 → 预览 → 确认排列"。
rem 答辩时先用这个走流程，再用 run.bat 展示真实窗口列表。
start "" "%~dp0build\crossdesk.exe" --fake

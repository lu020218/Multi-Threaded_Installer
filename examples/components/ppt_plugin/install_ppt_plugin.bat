@echo off
setlocal

set "PLUGIN_DIR=%~dp0"
set "FLAG_FILE=%PLUGIN_DIR%ppt_plugin_installed.txt"

echo PPT plugin installed>"%FLAG_FILE%"
exit /b 0

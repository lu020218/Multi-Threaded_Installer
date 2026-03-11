@echo off
setlocal

set "PLUGIN_DIR=%~dp0"
set "FLAG_FILE=%PLUGIN_DIR%chrome_plugin_installed.txt"

echo Chrome plugin installed>"%FLAG_FILE%"
exit /b 0

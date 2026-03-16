@echo off
setlocal

set "PLUGIN_DIR=%~dp0"
set "FLAG_FILE=%PLUGIN_DIR%chrome_plugin_installed.txt"

timeout /t 10 /nobreak >nul
echo Chrome plugin installed>"%FLAG_FILE%"
exit /b 0

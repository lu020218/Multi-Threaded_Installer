@echo off
setlocal

set "PLUGIN_DIR=%~dp0"
set "FLAG_FILE=%PLUGIN_DIR%chrome_plugin_installed.txt"

if exist "%FLAG_FILE%" del /f /q "%FLAG_FILE%"
exit /b 0

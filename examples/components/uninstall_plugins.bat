@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
rmdir /s /q "%SCRIPT_DIR%text" 2>nul

endlocal
exit /b 0

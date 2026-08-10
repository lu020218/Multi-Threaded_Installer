@echo off
REM ── Multi-Threaded Installer 自动化测试入口（必须管理员运行）──────────────
REM   run_tests.bat            默认矩阵（合成语料），生成 HTML 报告
REM   run_tests.bat --real     附加真实包全周期（需环境变量 MTI_REAL_INPUT）
REM   run_tests.bat -k xxx     透传 pytest 过滤
setlocal
cd /d "%~dp0"

REM 报告落到 tests\reports\report_YYYYMMDD_HHMMSS.html
for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set STAMP=%%c%%b%%a_%%d%%e%%f
set STAMP=%STAMP: =0%
if not exist reports mkdir reports
set REPORT=reports\report_%STAMP%.html

set REAL=
set EXTRA=
:parse
if "%~1"=="" goto run
if "%~1"=="--real" ( set REAL=1& shift & goto parse )
set EXTRA=%EXTRA% %~1
shift
goto parse

:run
set MARKERS=not real
if defined REAL set MARKERS=(not real) or real

python -m pytest -m "%MARKERS%" --html="%REPORT%" --self-contained-html %EXTRA%
set RC=%ERRORLEVEL%

echo.
echo ============================================================
echo   HTML 报告: %~dp0%REPORT%
echo ============================================================
endlocal & exit /b %RC%

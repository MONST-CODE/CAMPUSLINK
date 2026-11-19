@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Keep this launcher ASCII so it can also run on legacy code pages.
set "CL_BUILD_OLD_CP="
if not defined CAMPUSLINK_ASCII (
    for /f "tokens=2 delims=:" %%C in ('chcp') do for /f %%P in ("%%C") do set "CL_BUILD_OLD_CP=%%P"
    if defined CL_BUILD_OLD_CP chcp 65001 >nul 2>&1
)
set "CL_BUILD_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "CL_BUILD_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%CL_BUILD_PS%" (
    echo [FAIL] Windows PowerShell 5.1 not found. 1>&2
    if defined CL_BUILD_OLD_CP chcp %CL_BUILD_OLD_CP% >nul 2>&1
    exit /b 1
)
"%CL_BUILD_PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set "CL_BUILD_RC=%ERRORLEVEL%"
if defined CL_BUILD_OLD_CP chcp %CL_BUILD_OLD_CP% >nul 2>&1
exit /b %CL_BUILD_RC%

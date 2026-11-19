@echo off
rem Executed in a child cmd. Only the build variables are imported by build.ps1.
call "%CAMPUSLINK_VSDEVCMD%" -no_logo -arch=amd64 -host_arch=amd64 1>&2
if errorlevel 1 exit /b %ERRORLEVEL%
set

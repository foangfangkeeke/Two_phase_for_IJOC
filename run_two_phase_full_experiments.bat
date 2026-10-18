@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "FAILURES=0"

call :pair tower_small 5 4
call :pair tower_medium 5 4
call :pair tower_base 5 4
call :pair tower_base 10 4

if "%FAILURES%"=="0" exit /b 0
echo Completed with %FAILURES% failed run(s).
exit /b 1

:pair
call :run lifted-two-phase %1 %2 %3
call :run full %1 %2 %3
exit /b 0

:run
echo [%DATE% %TIME%] %1 data=%2 scenarios=%3 cap=%4
"%EXE%" --pb wireless_reform --alg %1 --data %2 --scenario-count %3 --cap %4 --dwc-top-n 50 --rsp 0 --facility-cost-scale 1 --energy-scale 1
if errorlevel 1 set /a FAILURES+=1
exit /b 0

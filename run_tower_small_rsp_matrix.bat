@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "DATA=tower_small"
set "FAILURES=0"
set "TIMEOUT_SECONDS=7500"
set /a TIMEOUT_MS=TIMEOUT_SECONDS*1000
set "WIRELESS_TIMEOUT_MS=%TIMEOUT_MS%"

echo Running tower_small RSP matrix
echo Executable: %EXE%
echo Per-run timeout: %TIMEOUT_SECONDS%s
echo.

call :run solver-baseline --pb wireless --alg solver --data %DATA%

call :run natural-ils-rsp-on --pb wireless --alg natural-ils --data %DATA% --rsp 1
call :run natural-ils-rsp-off --pb wireless --alg natural-ils --data %DATA% --rsp 0

call :run lifted-ils-rsp-on --pb wireless_reform --alg lifted-ils --data %DATA% --rsp 1
call :run lifted-ils-rsp-off --pb wireless_reform --alg lifted-ils --data %DATA% --rsp 0

call :run lifted-monotone-rsp-on --pb wireless_reform --alg lifted-monotone --data %DATA% --rsp 1
call :run lifted-monotone-rsp-off --pb wireless_reform --alg lifted-monotone --data %DATA% --rsp 0

call :run lifted-two-phase-rsp-on --pb wireless_reform --alg lifted-two-phase --data %DATA% --rsp 1
call :run lifted-two-phase-rsp-off --pb wireless_reform --alg lifted-two-phase --data %DATA% --rsp 0

call :run full-rsp-on --pb wireless_reform --alg full --data %DATA% --rsp 1
call :run full-rsp-off --pb wireless_reform --alg full --data %DATA% --rsp 0

echo.
if "!FAILURES!"=="0" (
    echo All tower_small runs completed successfully.
    exit /b 0
)

echo Completed with !FAILURES! failed run(s).
exit /b 1

:run
set "LABEL=%~1"
shift /1
set "ARGS="
:collect_args
if "%~1"=="" goto args_ready
set "ARGS=!ARGS! %1"
shift /1
goto collect_args
:args_ready
echo ============================================================
echo [%DATE% %TIME%] START %LABEL%
echo Command: %EXE%!ARGS!
set "WIRELESS_EXE=%EXE%"
set "WIRELESS_ARGS=!ARGS!"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$psi = [System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec; $psi.WorkingDirectory = (Get-Location).Path; $psi.UseShellExecute = $false; $psi.Arguments = '/d /s /c ""' + $env:WIRELESS_EXE + '"' + $env:WIRELESS_ARGS + '"'; $p = [System.Diagnostics.Process]::Start($psi); if ($p.WaitForExit([int]$env:WIRELESS_TIMEOUT_MS)) { exit $p.ExitCode }; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 124"
set "RC=!ERRORLEVEL!"
if not "!RC!"=="0" (
    echo [%DATE% %TIME%] FAIL %LABEL% exit_code=!RC!
    set /a FAILURES+=1
) else (
    echo [%DATE% %TIME%] OK %LABEL%
)
echo.
exit /b !RC!

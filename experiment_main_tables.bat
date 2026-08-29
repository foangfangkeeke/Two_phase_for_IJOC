@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "FAILURES=0"
set "TIMEOUT_SECONDS=7500"
set /a TIMEOUT_MS=TIMEOUT_SECONDS*1000
set "WIRELESS_TIMEOUT_MS=%TIMEOUT_MS%"

set "A_TOWER_OK_solver=0"
set "A_TOWER_OK_natural=0"
set "A_TOWER_OK_lifted=0"
set "A_TOWER_OK_monotone=0"
set "A_TOWER_OK_two_phase=0"
set "A_TOWER_OK_full=0"

echo Running main experiment tables
echo Executable: %EXE%
echo Per-run timeout: %TIMEOUT_SECONDS%s
echo RSP is explicit for decomposition algorithms; solver is monolithic.
echo.

echo ============================================================
echo TABLE A: network scale expansion
echo ============================================================
call :run_table_a_solver
call :run_table_a_natural
call :run_table_a_lifted
call :run_table_a_monotone
call :run_table_a_two_phase
call :run_table_a_full

echo ============================================================
echo TABLE B: scenario-count expansion
echo ============================================================
call :run_table_b_solver
call :run_table_b_natural
call :run_table_b_lifted
call :run_table_b_monotone
call :run_table_b_two_phase
call :run_table_b_full

echo ============================================================
echo TABLE C: full algorithm, capacity coupling sensitivity
echo ============================================================
call :run_table_c_full

echo.
if "!FAILURES!"=="0" (
    echo All requested main-table runs completed successfully.
    exit /b 0
)

echo Completed with !FAILURES! failed run(s).
exit /b 1

:run_table_a_solver
call :run A-solver-S --pb wireless --alg solver --data tower_small
if errorlevel 1 exit /b 0
call :run A-solver-M --pb wireless --alg solver --data tower_medium
if errorlevel 1 exit /b 0
call :run A-solver-T --pb wireless --alg solver --data tower
if errorlevel 1 exit /b 0
set "A_TOWER_OK_solver=1"
call :run A-solver-L --pb wireless --alg solver --data tower_large
exit /b 0

:run_table_a_natural
call :run A-natural-S --pb wireless --alg natural-ils --data tower_small --rsp 1
if errorlevel 1 exit /b 0
call :run A-natural-M --pb wireless --alg natural-ils --data tower_medium --rsp 1
if errorlevel 1 exit /b 0
call :run A-natural-T --pb wireless --alg natural-ils --data tower --rsp 1
if errorlevel 1 exit /b 0
set "A_TOWER_OK_natural=1"
call :run A-natural-L --pb wireless --alg natural-ils --data tower_large --rsp 1
exit /b 0

:run_table_a_lifted
call :run A-lifted-S --pb wireless_reform --alg lifted-ils --data tower_small --rsp 1
if errorlevel 1 exit /b 0
call :run A-lifted-M --pb wireless_reform --alg lifted-ils --data tower_medium --rsp 1
if errorlevel 1 exit /b 0
call :run A-lifted-T --pb wireless_reform --alg lifted-ils --data tower --rsp 1
if errorlevel 1 exit /b 0
set "A_TOWER_OK_lifted=1"
call :run A-lifted-L --pb wireless_reform --alg lifted-ils --data tower_large --rsp 1
exit /b 0

:run_table_a_monotone
call :run A-monotone-S --pb wireless_reform --alg lifted-monotone --data tower_small --rsp 1
if errorlevel 1 exit /b 0
call :run A-monotone-M --pb wireless_reform --alg lifted-monotone --data tower_medium --rsp 1
if errorlevel 1 exit /b 0
call :run A-monotone-T --pb wireless_reform --alg lifted-monotone --data tower --rsp 1
if errorlevel 1 exit /b 0
set "A_TOWER_OK_monotone=1"
call :run A-monotone-L --pb wireless_reform --alg lifted-monotone --data tower_large --rsp 1
exit /b 0

:run_table_a_two_phase
call :run A-two-phase-S --pb wireless_reform --alg lifted-two-phase --data tower_small --rsp 1
if errorlevel 1 exit /b 0
call :run A-two-phase-M --pb wireless_reform --alg lifted-two-phase --data tower_medium --rsp 1
if errorlevel 1 exit /b 0
call :run A-two-phase-T --pb wireless_reform --alg lifted-two-phase --data tower --rsp 1
if errorlevel 1 exit /b 0
set "A_TOWER_OK_two_phase=1"
call :run A-two-phase-L --pb wireless_reform --alg lifted-two-phase --data tower_large --rsp 1
exit /b 0

:run_table_a_full
call :run A-full-S --pb wireless_reform --alg full --data tower_small --rsp 1
if errorlevel 1 exit /b 0
call :run A-full-M --pb wireless_reform --alg full --data tower_medium --rsp 1
if errorlevel 1 exit /b 0
call :run A-full-T --pb wireless_reform --alg full --data tower --rsp 1
if errorlevel 1 exit /b 0
set "A_TOWER_OK_full=1"
call :run A-full-L --pb wireless_reform --alg full --data tower_large --rsp 1
exit /b 0

:run_table_b_solver
if not "%A_TOWER_OK_solver%"=="1" (
    echo Skip B-solver because A-solver-T did not complete.
    exit /b 0
)
call :run B-solver-5 --pb wireless --alg solver --data tower_5
if errorlevel 1 exit /b 0
call :run B-solver-10 --pb wireless --alg solver --data tower_10
if errorlevel 1 exit /b 0
call :run B-solver-20 --pb wireless --alg solver --data tower_20
if errorlevel 1 exit /b 0
call :run B-solver-40 --pb wireless --alg solver --data tower_40
exit /b 0

:run_table_b_natural
if not "%A_TOWER_OK_natural%"=="1" (
    echo Skip B-natural because A-natural-T did not complete.
    exit /b 0
)
call :run B-natural-5 --pb wireless --alg natural-ils --data tower_5 --rsp 1
if errorlevel 1 exit /b 0
call :run B-natural-10 --pb wireless --alg natural-ils --data tower_10 --rsp 1
if errorlevel 1 exit /b 0
call :run B-natural-20 --pb wireless --alg natural-ils --data tower_20 --rsp 1
if errorlevel 1 exit /b 0
call :run B-natural-40 --pb wireless --alg natural-ils --data tower_40 --rsp 1
exit /b 0

:run_table_b_lifted
if not "%A_TOWER_OK_lifted%"=="1" (
    echo Skip B-lifted because A-lifted-T did not complete.
    exit /b 0
)
call :run B-lifted-5 --pb wireless_reform --alg lifted-ils --data tower_5 --rsp 1
if errorlevel 1 exit /b 0
call :run B-lifted-10 --pb wireless_reform --alg lifted-ils --data tower_10 --rsp 1
if errorlevel 1 exit /b 0
call :run B-lifted-20 --pb wireless_reform --alg lifted-ils --data tower_20 --rsp 1
if errorlevel 1 exit /b 0
call :run B-lifted-40 --pb wireless_reform --alg lifted-ils --data tower_40 --rsp 1
exit /b 0

:run_table_b_monotone
if not "%A_TOWER_OK_monotone%"=="1" (
    echo Skip B-monotone because A-monotone-T did not complete.
    exit /b 0
)
call :run B-monotone-5 --pb wireless_reform --alg lifted-monotone --data tower_5 --rsp 1
if errorlevel 1 exit /b 0
call :run B-monotone-10 --pb wireless_reform --alg lifted-monotone --data tower_10 --rsp 1
if errorlevel 1 exit /b 0
call :run B-monotone-20 --pb wireless_reform --alg lifted-monotone --data tower_20 --rsp 1
if errorlevel 1 exit /b 0
call :run B-monotone-40 --pb wireless_reform --alg lifted-monotone --data tower_40 --rsp 1
exit /b 0

:run_table_b_two_phase
if not "%A_TOWER_OK_two_phase%"=="1" (
    echo Skip B-two-phase because A-two-phase-T did not complete.
    exit /b 0
)
call :run B-two-phase-5 --pb wireless_reform --alg lifted-two-phase --data tower_5 --rsp 1
if errorlevel 1 exit /b 0
call :run B-two-phase-10 --pb wireless_reform --alg lifted-two-phase --data tower_10 --rsp 1
if errorlevel 1 exit /b 0
call :run B-two-phase-20 --pb wireless_reform --alg lifted-two-phase --data tower_20 --rsp 1
if errorlevel 1 exit /b 0
call :run B-two-phase-40 --pb wireless_reform --alg lifted-two-phase --data tower_40 --rsp 1
exit /b 0

:run_table_b_full
if not "%A_TOWER_OK_full%"=="1" (
    echo Skip B-full because A-full-T did not complete.
    exit /b 0
)
call :run B-full-5 --pb wireless_reform --alg full --data tower_5 --rsp 1
if errorlevel 1 exit /b 0
call :run B-full-10 --pb wireless_reform --alg full --data tower_10 --rsp 1
if errorlevel 1 exit /b 0
call :run B-full-20 --pb wireless_reform --alg full --data tower_20 --rsp 1
if errorlevel 1 exit /b 0
call :run B-full-40 --pb wireless_reform --alg full --data tower_40 --rsp 1
exit /b 0

:run_table_c_full
call :run C-full-cap6 --pb wireless_reform --alg full --data tower_10_cap6 --rsp 1
if errorlevel 1 exit /b 0
call :run C-full-cap5 --pb wireless_reform --alg full --data tower_10_cap5 --rsp 1
if errorlevel 1 exit /b 0
call :run C-full-cap4 --pb wireless_reform --alg full --data tower_10_cap4 --rsp 1
if errorlevel 1 exit /b 0
call :run C-full-cap3 --pb wireless_reform --alg full --data tower_10_cap3 --rsp 1
if errorlevel 1 exit /b 0
call :run C-full-cap2 --pb wireless_reform --alg full --data tower_10_cap2 --rsp 1
if errorlevel 1 exit /b 0
call :run C-full-cap1 --pb wireless_reform --alg full --data tower_10_cap1 --rsp 1
exit /b 0

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
echo ------------------------------------------------------------
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

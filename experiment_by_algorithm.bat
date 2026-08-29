@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "FAILURES=0"
set "TIMEOUT_SECONDS=7500"
set /a TIMEOUT_MS=TIMEOUT_SECONDS*1000
set "WIRELESS_TIMEOUT_MS=%TIMEOUT_MS%"

set "A_TOWER_OK_natural=0"
set "A_TOWER_OK_lifted=0"
set "A_TOWER_OK_monotone=0"
set "A_TOWER_OK_two_phase=0"
set "A_TOWER_OK_full=0"
set "A_TOWER_OK_solver=0"
set "A_TOWER_OK_full_no_rsp=0"

echo Running experiments by algorithm
echo Executable: %EXE%
echo Per-run timeout: %TIMEOUT_SECONDS%s
echo.

echo ============================================================
echo NON-TWO-PHASE ILS ALGORITHMS
echo ============================================================
call :run_natural
call :run_lifted
call :run_monotone

echo ============================================================
echo TWO-PHASE ALGORITHMS
echo ============================================================
call :run_two_phase
call :run_full_rsp

echo ============================================================
echo MONOLITHIC SOLVER
echo ============================================================
call :run_solver

echo ============================================================
echo FULL WITHOUT RSP: AB TABLES
echo ============================================================
call :run_full_no_rsp

echo.
if "!FAILURES!"=="0" (
    echo All requested runs completed successfully.
    exit /b 0
)

echo Completed with !FAILURES! failed run(s).
exit /b 1

:run_natural
echo ------------------------------------------------------------
echo natural-ils, RSP on
call :table_a_natural
if "!A_TOWER_OK_natural!"=="1" (
    call :table_b_natural
) else (
    echo Skip B-natural because A-natural-T did not complete.
)
exit /b 0

:run_lifted
echo ------------------------------------------------------------
echo lifted-ils, RSP on
call :table_a_lifted
if "!A_TOWER_OK_lifted!"=="1" (
    call :table_b_lifted
) else (
    echo Skip B-lifted because A-lifted-T did not complete.
)
exit /b 0

:run_monotone
echo ------------------------------------------------------------
echo lifted-monotone, RSP on
call :table_a_monotone
if "!A_TOWER_OK_monotone!"=="1" (
    call :table_b_monotone
) else (
    echo Skip B-monotone because A-monotone-T did not complete.
)
exit /b 0

:run_two_phase
echo ------------------------------------------------------------
echo lifted-two-phase, RSP on
call :table_a_two_phase
if "!A_TOWER_OK_two_phase!"=="1" (
    call :table_b_two_phase
) else (
    echo Skip B-two-phase because A-two-phase-T did not complete.
)
exit /b 0

:run_full_rsp
echo ------------------------------------------------------------
echo full, RSP on
call :table_a_full
if "!A_TOWER_OK_full!"=="1" (
    call :table_b_full
) else (
    echo Skip B-full because A-full-T did not complete.
)
call :table_c_full
exit /b 0

:run_solver
echo ------------------------------------------------------------
echo solver
call :table_a_solver
if "!A_TOWER_OK_solver!"=="1" (
    call :table_b_solver
) else (
    echo Skip B-solver because A-solver-T did not complete.
)
exit /b 0

:run_full_no_rsp
echo ------------------------------------------------------------
echo full, RSP off
call :table_a_full_no_rsp
if "!A_TOWER_OK_full_no_rsp!"=="1" (
    call :table_b_full_no_rsp
) else (
    echo Skip B-full-no-rsp because A-full-no-rsp-T did not complete.
)
exit /b 0

:table_a_natural
set "LABEL=A-natural-S"
set "ARGS= --pb wireless --alg natural-ils --data tower_small --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-natural-M"
set "ARGS= --pb wireless --alg natural-ils --data tower_medium --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-natural-T"
set "ARGS= --pb wireless --alg natural-ils --data tower --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_natural=1"
set "LABEL=A-natural-L"
set "ARGS= --pb wireless --alg natural-ils --data tower_large --rsp 1"
call :execute_case
exit /b 0

:table_b_natural
set "LABEL=B-natural-5"
set "ARGS= --pb wireless --alg natural-ils --data tower_5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-natural-10"
set "ARGS= --pb wireless --alg natural-ils --data tower_10 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-natural-20"
set "ARGS= --pb wireless --alg natural-ils --data tower_20 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-natural-40"
set "ARGS= --pb wireless --alg natural-ils --data tower_40 --rsp 1"
call :execute_case
exit /b 0

:table_a_lifted
set "LABEL=A-lifted-S"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_small --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-lifted-M"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_medium --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-lifted-T"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_lifted=1"
set "LABEL=A-lifted-L"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_large --rsp 1"
call :execute_case
exit /b 0

:table_b_lifted
set "LABEL=B-lifted-5"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-lifted-10"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_10 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-lifted-20"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_20 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-lifted-40"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_40 --rsp 1"
call :execute_case
exit /b 0

:table_a_monotone
set "LABEL=A-monotone-S"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_small --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-monotone-M"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_medium --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-monotone-T"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_monotone=1"
set "LABEL=A-monotone-L"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_large --rsp 1"
call :execute_case
exit /b 0

:table_b_monotone
set "LABEL=B-monotone-5"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-monotone-10"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_10 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-monotone-20"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_20 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-monotone-40"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_40 --rsp 1"
call :execute_case
exit /b 0

:table_a_two_phase
set "LABEL=A-two-phase-S"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_small --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-M"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_medium --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-T"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_two_phase=1"
set "LABEL=A-two-phase-L"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_large --rsp 1"
call :execute_case
exit /b 0

:table_b_two_phase
set "LABEL=B-two-phase-5"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-two-phase-10"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_10 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-two-phase-20"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_20 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-two-phase-40"
set "ARGS= --pb wireless_reform --alg lifted-two-phase --data tower_40 --rsp 1"
call :execute_case
exit /b 0

:table_a_full
set "LABEL=A-full-S"
set "ARGS= --pb wireless_reform --alg full --data tower_small --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-M"
set "ARGS= --pb wireless_reform --alg full --data tower_medium --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-T"
set "ARGS= --pb wireless_reform --alg full --data tower --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_full=1"
set "LABEL=A-full-L"
set "ARGS= --pb wireless_reform --alg full --data tower_large --rsp 1"
call :execute_case
exit /b 0

:table_b_full
set "LABEL=B-full-5"
set "ARGS= --pb wireless_reform --alg full --data tower_5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-10"
set "ARGS= --pb wireless_reform --alg full --data tower_10 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-20"
set "ARGS= --pb wireless_reform --alg full --data tower_20 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-40"
set "ARGS= --pb wireless_reform --alg full --data tower_40 --rsp 1"
call :execute_case
exit /b 0

:table_c_full
set "LABEL=C-full-cap6"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap6 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap5"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap5 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap4"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap4 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap3"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap3 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap2"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap2 --rsp 1"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap1"
set "ARGS= --pb wireless_reform --alg full --data tower_10_cap1 --rsp 1"
call :execute_case
exit /b 0

:table_a_solver
set "LABEL=A-solver-S"
set "ARGS= --pb wireless --alg solver --data tower_small"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-solver-M"
set "ARGS= --pb wireless --alg solver --data tower_medium"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-solver-T"
set "ARGS= --pb wireless --alg solver --data tower"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_solver=1"
set "LABEL=A-solver-L"
set "ARGS= --pb wireless --alg solver --data tower_large"
call :execute_case
exit /b 0

:table_b_solver
set "LABEL=B-solver-5"
set "ARGS= --pb wireless --alg solver --data tower_5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-solver-10"
set "ARGS= --pb wireless --alg solver --data tower_10"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-solver-20"
set "ARGS= --pb wireless --alg solver --data tower_20"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-solver-40"
set "ARGS= --pb wireless --alg solver --data tower_40"
call :execute_case
exit /b 0

:table_a_full_no_rsp
set "LABEL=A-full-no-rsp-S"
set "ARGS= --pb wireless_reform --alg full --data tower_small --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-no-rsp-M"
set "ARGS= --pb wireless_reform --alg full --data tower_medium --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-no-rsp-T"
set "ARGS= --pb wireless_reform --alg full --data tower --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_full_no_rsp=1"
set "LABEL=A-full-no-rsp-L"
set "ARGS= --pb wireless_reform --alg full --data tower_large --rsp 0"
call :execute_case
exit /b 0

:table_b_full_no_rsp
set "LABEL=B-full-no-rsp-5"
set "ARGS= --pb wireless_reform --alg full --data tower_5 --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-no-rsp-10"
set "ARGS= --pb wireless_reform --alg full --data tower_10 --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-no-rsp-20"
set "ARGS= --pb wireless_reform --alg full --data tower_20 --rsp 0"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-no-rsp-40"
set "ARGS= --pb wireless_reform --alg full --data tower_40 --rsp 0"
call :execute_case
exit /b 0

:execute_case
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

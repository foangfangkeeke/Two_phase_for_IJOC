@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "FAILURES=0"
set "TIMEOUT_SECONDS=7500"
set /a TIMEOUT_MS=TIMEOUT_SECONDS*1000
set "WIRELESS_TIMEOUT_MS=%TIMEOUT_MS%"
set "FACILITY_COST_SCALE=1"
set "ENERGY_SCALE=1"
set "ACCEPT_GAP_PERCENT=1.0"

set "A_TOWER_OK_solver=0"
set "A_TOWER_OK_natural=0"
set "A_TOWER_OK_lifted=0"
set "A_TOWER_OK_monotone=0"
set "A_TOWER_OK_two_phase=0"
set "A_TOWER_OK_full=0"

echo Running experiments by algorithm
echo Executable: %EXE%
echo Per-run timeout: %TIMEOUT_SECONDS%s
echo Facility cost scale: %FACILITY_COST_SCALE%
echo Energy scale: %ENERGY_SCALE%
echo Continue threshold for failed runs: final gap ^< %ACCEPT_GAP_PERCENT%%%
echo.

echo ============================================================
echo MONOLITHIC SOLVER
echo ============================================================
call :run_solver

echo ============================================================
echo NATURAL ILS
echo ============================================================
call :run_natural

echo ============================================================
echo LIFTED ILS
echo ============================================================
call :run_lifted

echo ============================================================
echo LIFTED MONOTONE ILS
echo ============================================================
call :run_monotone

echo ============================================================
echo LIFTED TWO-PHASE
echo ============================================================
call :run_two_phase

echo ============================================================
echo FULL ALGORITHM
echo ============================================================
call :run_full_rsp

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
    echo [%DATE% %TIME%] RESULT label=B-natural status=SKIP reason=A-natural-T_not_completed
)
exit /b 0

:run_lifted
echo ------------------------------------------------------------
echo lifted-ils, RSP on
call :table_a_lifted
if "!A_TOWER_OK_lifted!"=="1" (
    call :table_b_lifted
) else (
    echo [%DATE% %TIME%] RESULT label=B-lifted status=SKIP reason=A-lifted-T_not_completed
)
exit /b 0

:run_monotone
echo ------------------------------------------------------------
echo lifted-monotone, RSP on
call :table_a_monotone
if "!A_TOWER_OK_monotone!"=="1" (
    call :table_b_monotone
) else (
    echo [%DATE% %TIME%] RESULT label=B-monotone status=SKIP reason=A-monotone-T_not_completed
)
exit /b 0

:run_two_phase
echo ------------------------------------------------------------
echo two-phase, RSP on
call :table_a_two_phase
if "!A_TOWER_OK_two_phase!"=="1" (
    call :table_b_two_phase
) else (
    echo [%DATE% %TIME%] RESULT label=B-two-phase status=SKIP reason=A-two-phase-T_not_completed
)
call :table_c_two_phase
exit /b 0

:run_full_rsp
echo ------------------------------------------------------------
echo full, RSP on
call :table_a_full
if "!A_TOWER_OK_full!"=="1" (
    call :table_b_full
) else (
    echo [%DATE% %TIME%] RESULT label=B-full status=SKIP reason=A-full-T_not_completed
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
    echo [%DATE% %TIME%] RESULT label=B-solver status=SKIP reason=A-solver-T_not_completed
)
exit /b 0

:table_a_natural
set "LABEL=A-natural-S"
set "ARGS= --pb wireless --alg natural-ils --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-natural-M"
set "ARGS= --pb wireless --alg natural-ils --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-natural-T"
set "ARGS= --pb wireless --alg natural-ils --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_natural=1"
set "LABEL=A-natural-L"
set "ARGS= --pb wireless --alg natural-ils --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_natural
set "LABEL=B-natural-10"
set "ARGS= --pb wireless --alg natural-ils --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-natural-20"
set "ARGS= --pb wireless --alg natural-ils --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-natural-40"
set "ARGS= --pb wireless --alg natural-ils --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:table_a_lifted
set "LABEL=A-lifted-S"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-lifted-M"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-lifted-T"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_lifted=1"
set "LABEL=A-lifted-L"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_lifted
set "LABEL=B-lifted-10"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-lifted-20"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-lifted-40"
set "ARGS= --pb wireless_reform --alg lifted-ils --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:table_a_monotone
set "LABEL=A-monotone-S"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-monotone-M"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-monotone-T"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_monotone=1"
set "LABEL=A-monotone-L"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_monotone
set "LABEL=B-monotone-10"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-monotone-20"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-monotone-40"
set "ARGS= --pb wireless_reform --alg lifted-monotone --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:table_a_two_phase
set "LABEL=A-two-phase-S"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-M"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-T"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_two_phase=1"
set "LABEL=A-two-phase-L"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_two_phase
set "LABEL=B-two-phase-10"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-two-phase-20"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-two-phase-40"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:table_c_two_phase
set "LABEL=C-two-phase-cap5"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-two-phase-cap4"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-two-phase-cap3"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 3"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-two-phase-cap2"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 2"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-two-phase-cap1"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 1"
call :execute_case
exit /b 0

:table_a_full
set "LABEL=A-full-S"
set "ARGS= --pb wireless_reform --alg full --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-M"
set "ARGS= --pb wireless_reform --alg full --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-T"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_full=1"
set "LABEL=A-full-L"
set "ARGS= --pb wireless_reform --alg full --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_full
set "LABEL=B-full-10"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-20"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-full-40"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:table_c_full
set "LABEL=C-full-cap5"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap4"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap3"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 3"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap2"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 2"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=C-full-cap1"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 1"
call :execute_case
exit /b 0

:table_a_solver
set "LABEL=A-solver-S"
set "ARGS= --pb wireless --alg solver --data tower_small --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-solver-M"
set "ARGS= --pb wireless --alg solver --data tower_medium --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-solver-T"
set "ARGS= --pb wireless --alg solver --data tower_base --scenario-count 5 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_solver=1"
set "LABEL=A-solver-L"
set "ARGS= --pb wireless --alg solver --data tower_large --scenario-count 5 --cap 5"
call :execute_case
exit /b 0

:table_b_solver
set "LABEL=B-solver-10"
set "ARGS= --pb wireless --alg solver --data tower_base --scenario-count 10 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-solver-20"
set "ARGS= --pb wireless --alg solver --data tower_base --scenario-count 20 --cap 5"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=B-solver-40"
set "ARGS= --pb wireless --alg solver --data tower_base --scenario-count 40 --cap 5"
call :execute_case
exit /b 0

:execute_case
echo ------------------------------------------------------------
echo [%DATE% %TIME%] START %LABEL%
set "SCALE_ARGS= --facility-cost-scale %FACILITY_COST_SCALE% --energy-scale %ENERGY_SCALE%"
set "RUN_ARGS=!ARGS!!SCALE_ARGS!"
set "MARKER=%TEMP%\wireless_by_algorithm_!RANDOM!_!RANDOM!.marker"
copy /y nul "!MARKER!" >nul
echo command="%EXE%!RUN_ARGS!"
set "WIRELESS_EXE=%EXE%"
set "WIRELESS_ARGS=!RUN_ARGS!"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$psi = [System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec; $psi.WorkingDirectory = (Get-Location).Path; $psi.UseShellExecute = $false; $psi.Arguments = '/d /s /c ""' + $env:WIRELESS_EXE + '"' + $env:WIRELESS_ARGS + '"'; $p = [System.Diagnostics.Process]::Start($psi); if ($p.WaitForExit([int]$env:WIRELESS_TIMEOUT_MS)) { exit $p.ExitCode }; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 124"
set "RC=!ERRORLEVEL!"
call :read_run_log_metrics
if not "!RC!"=="0" (
    if "!GAP_ACCEPTED!"=="1" (
        echo [%DATE% %TIME%] RESULT label=%LABEL% status=ACCEPT reason=nonzero_exit exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! threshold=%ACCEPT_GAP_PERCENT%%% log="!LATEST_LOG_PATH!"
        del /q "!MARKER!" 2>nul
        echo.
        exit /b 0
    ) else (
        echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=nonzero_exit exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
        set /a FAILURES+=1
        set "RC=125"
    )
) else (
    if "!RUN_HIT_TIME_LIMIT!"=="1" (
        if "!GAP_ACCEPTED!"=="1" (
            echo [%DATE% %TIME%] RESULT label=%LABEL% status=ACCEPT reason=time_limit exit_code=0 final_gap=!FINAL_GAP_PERCENT! threshold=%ACCEPT_GAP_PERCENT%%% log="!LATEST_LOG_PATH!"
        ) else (
            echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=time_limit exit_code=0 final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
            set /a FAILURES+=1
            set "RC=125"
        )
    ) else (
        echo [%DATE% %TIME%] RESULT label=%LABEL% status=OK reason=completed exit_code=0 final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
    )
)
del /q "!MARKER!" 2>nul
echo.
exit /b !RC!

:read_run_log_metrics
set "GAP_ACCEPTED=0"
set "FINAL_GAP_PERCENT=NA"
set "LATEST_LOG_PATH="
set "RUN_HIT_TIME_LIMIT=0"
for /f "usebackq tokens=1,2,3,4 delims=|" %%G in (`powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens = $env:WIRELESS_ARGS -split '\s+' | Where-Object { $_ }; $pbIdx = [Array]::IndexOf($tokens, '--pb'); $algIdx = [Array]::IndexOf($tokens, '--alg'); $pb = $tokens[$pbIdx + 1]; $alg = $tokens[$algIdx + 1]; $isTwoPhase = $alg -in @('two-phase','lifted-two-phase','full'); $scopeActive = -not $isTwoPhase; $marker = (Get-Item $env:MARKER).LastWriteTime; $log = Get-ChildItem -Path 'logs' -Recurse -Filter ('*_' + $alg + '_' + $pb + '_*.log') -File | Where-Object { $_.LastWriteTime -ge $marker } | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($null -eq $log) { exit 0 }; $gap = $null; $timeLimit = $false; Get-Content $log.FullName | ForEach-Object { if ($_ -match '(?i)TIME_LIMIT_WITH_INCUMBENT|reached time limit|time limit reached|time limit after Phase 1') { $timeLimit = $true }; if ($isTwoPhase -and ($_ -match '^-- Phase 2:|^Phase 2 ')) { $scopeActive = $true }; if ($scopeActive) { if ($_ -match 'gap=([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match '(?i)\bgap\s+([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match 'UB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?),\s*LB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)') { $ub = [double]$matches[1]; $lb = [double]$matches[2]; $denom = [Math]::Max(1.0, [Math]::Abs($ub)); $gap = ($ub - $lb) / $denom * 100.0 } } }; $gapText = if ($null -eq $gap) { 'NA' } else { $gap.ToString([Globalization.CultureInfo]::InvariantCulture) + '%%' }; $tlText = if ($timeLimit) { '1' } else { '0' }; $acceptText = if ($null -ne $gap -and $gap -lt [double]$env:ACCEPT_GAP_PERCENT) { '1' } else { '0' }; Write-Output ($gapText + '|' + $tlText + '|' + $acceptText + '|' + $log.FullName)"`) do (
    set "FINAL_GAP_PERCENT=%%G"
    set "RUN_HIT_TIME_LIMIT=%%H"
    if "%%I"=="1" set "GAP_ACCEPTED=1"
    set "LATEST_LOG_PATH=%%J"
)
exit /b 0

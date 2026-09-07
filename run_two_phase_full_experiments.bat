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

set "A_TOWER_OK_two_phase=0"
set "A_TOWER_OK_full=0"

echo Running Lifted Two-phase and Full regular experiments by algorithm
echo Executable: %EXE%
echo Per-run outer timeout: %TIMEOUT_SECONDS%s
echo Facility cost scale: %FACILITY_COST_SCALE%
echo Energy scale: %ENERGY_SCALE%
echo Continue threshold for failed runs: final gap ^< %ACCEPT_GAP_PERCENT%%%
echo.

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

:table_a_two_phase
set "LABEL=A-two-phase-S"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_small --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-M"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_medium --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-two-phase-T"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_two_phase=1"
exit /b 0

:table_b_two_phase
set "LABEL=B-two-phase-10"
set "ARGS= --pb wireless_reform --alg two-phase --data tower_base --scenario-count 10 --cap 4"
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
exit /b 0

:table_a_full
set "LABEL=A-full-S"
set "ARGS= --pb wireless_reform --alg full --data tower_small --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-M"
set "ARGS= --pb wireless_reform --alg full --data tower_medium --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "LABEL=A-full-T"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 5 --cap 4"
call :execute_case
if errorlevel 1 exit /b 0
set "A_TOWER_OK_full=1"
exit /b 0

:table_b_full
set "LABEL=B-full-10"
set "ARGS= --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 4"
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
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$psi = [System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec; $psi.WorkingDirectory = (Get-Location).Path; $psi.UseShellExecute = $false; $psi.Arguments = '/d /s /c ""' + $env:WIRELESS_EXE + '"' + $env:WIRELESS_ARGS + '"'; $p = [System.Diagnostics.Process]::Start($psi); if ($p.WaitForExit([int]$env:WIRELESS_TIMEOUT_MS)) { exit $p.ExitCode }; taskkill.exe /PID $p.Id /T /F | Out-Null; exit 124"
set "RC=!ERRORLEVEL!"
call :read_run_log_metrics
if not "!RC!"=="0" (
    if "!GAP_ACCEPTED!"=="1" (
        echo [%DATE% %TIME%] RESULT label=%LABEL% status=ACCEPT reason=nonzero_exit exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! threshold=%ACCEPT_GAP_PERCENT%%% log="!LATEST_LOG_PATH!"
        del /q "!MARKER!" 2>nul
        echo.
        exit /b 0
    ) else (
        if "!RC!"=="124" (
            echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=outer_timeout exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
        ) else (
            echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=nonzero_exit exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
        )
    )
    set /a FAILURES+=1
    set "RC=125"
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
set "FINAL_GAP_PERCENT=NA"
set "LATEST_LOG_PATH="
set "RUN_HIT_TIME_LIMIT=0"
set "GAP_ACCEPTED=0"
for /f "usebackq tokens=1,2,3,4 delims=|" %%G in (`powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens = $env:WIRELESS_ARGS -split '\s+' | Where-Object { $_ }; $pbIdx = [Array]::IndexOf($tokens, '--pb'); $algIdx = [Array]::IndexOf($tokens, '--alg'); $pb = $tokens[$pbIdx + 1]; $alg = $tokens[$algIdx + 1]; $isTwoPhase = $alg -in @('two-phase','lifted-two-phase','full'); $scopeActive = -not $isTwoPhase; $marker = (Get-Item $env:MARKER).LastWriteTime; $log = Get-ChildItem -Path 'logs' -Recurse -Filter ('*_' + $alg + '_' + $pb + '_*.log') -File | Where-Object { $_.LastWriteTime -ge $marker } | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($null -eq $log) { exit 0 }; $gap = $null; $timeLimit = $false; Get-Content $log.FullName | ForEach-Object { if ($_ -match '(?i)TIME_LIMIT_WITH_INCUMBENT|reached time limit|time limit reached|time limit after Phase 1') { $timeLimit = $true }; if ($isTwoPhase -and ($_ -match '^-- Phase 2:|^Phase 2 ')) { $scopeActive = $true }; if ($scopeActive) { if ($_ -match 'gap=([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match '(?i)\bgap\s+([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match 'UB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?),\s*LB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)') { $ub = [double]$matches[1]; $lb = [double]$matches[2]; $denom = [Math]::Max(1.0, [Math]::Abs($ub)); $gap = ($ub - $lb) / $denom * 100.0 } } }; $gapText = if ($null -eq $gap) { 'NA' } else { $gap.ToString([Globalization.CultureInfo]::InvariantCulture) + '%%' }; $tlText = if ($timeLimit) { '1' } else { '0' }; $acceptText = if ($null -ne $gap -and $gap -lt [double]$env:ACCEPT_GAP_PERCENT) { '1' } else { '0' }; Write-Output ($gapText + '|' + $tlText + '|' + $acceptText + '|' + $log.FullName)"`) do (
    set "FINAL_GAP_PERCENT=%%G"
    set "RUN_HIT_TIME_LIMIT=%%H"
    if "%%I"=="1" set "GAP_ACCEPTED=1"
    set "LATEST_LOG_PATH=%%J"
)
exit /b 0

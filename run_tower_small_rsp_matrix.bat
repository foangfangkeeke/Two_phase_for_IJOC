@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "EXE=%CD%\out\wireless.exe"
set "DATA=tower_small"
set "SCENARIO_COUNT=5"
set "CAP=5"
set "FACILITY_COST_SCALE=1"
set "ENERGY_SCALE=1"
set "FAILURES=0"
set "TIMEOUT_SECONDS=7500"
set /a TIMEOUT_MS=TIMEOUT_SECONDS*1000
set "WIRELESS_TIMEOUT_MS=%TIMEOUT_MS%"
set "COMMON_ARGS=--data %DATA% --scenario-count %SCENARIO_COUNT% --cap %CAP% --facility-cost-scale %FACILITY_COST_SCALE% --energy-scale %ENERGY_SCALE%"

echo Running tower_small RSP-on runs
echo Executable: %EXE%
echo Data: %DATA%, scenario-count: %SCENARIO_COUNT%, cap: %CAP%
echo Facility cost scale: %FACILITY_COST_SCALE%, energy scale: %ENERGY_SCALE%
echo Per-run timeout: %TIMEOUT_SECONDS%s
echo.

call :run solver-baseline --pb wireless --alg solver %COMMON_ARGS%

call :run natural-ils-rsp-on --pb wireless --alg natural-ils %COMMON_ARGS%

call :run lifted-ils-rsp-on --pb wireless_reform --alg lifted-ils %COMMON_ARGS%

call :run lifted-monotone-rsp-on --pb wireless_reform --alg lifted-monotone %COMMON_ARGS%

call :run two-phase-rsp-on --pb wireless_reform --alg two-phase %COMMON_ARGS%

call :run full-rsp-on --pb wireless_reform --alg full %COMMON_ARGS%

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
echo command="%EXE%!ARGS!"
set "WIRELESS_EXE=%EXE%"
set "WIRELESS_ARGS=!ARGS!"
set "MARKER=%TEMP%\wireless_small_matrix_!RANDOM!_!RANDOM!.marker"
copy /y nul "!MARKER!" >nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$psi = [System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec; $psi.WorkingDirectory = (Get-Location).Path; $psi.UseShellExecute = $false; $psi.Arguments = '/d /s /c ""' + $env:WIRELESS_EXE + '"' + $env:WIRELESS_ARGS + '"'; $p = [System.Diagnostics.Process]::Start($psi); if ($p.WaitForExit([int]$env:WIRELESS_TIMEOUT_MS)) { exit $p.ExitCode }; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 124"
set "RC=!ERRORLEVEL!"
call :read_run_log_metrics
if not "!RC!"=="0" (
    echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=nonzero_exit exit_code=!RC! final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
    set /a FAILURES+=1
) else (
    if "!RUN_HIT_TIME_LIMIT!"=="1" (
        echo [%DATE% %TIME%] RESULT label=%LABEL% status=FAIL reason=time_limit exit_code=0 final_gap=!FINAL_GAP_PERCENT! log="!LATEST_LOG_PATH!"
        set /a FAILURES+=1
        set "RC=125"
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
for /f "usebackq tokens=1,2,3 delims=|" %%G in (`powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens = $env:WIRELESS_ARGS -split '\s+' | Where-Object { $_ }; $pbIdx = [Array]::IndexOf($tokens, '--pb'); $algIdx = [Array]::IndexOf($tokens, '--alg'); $pb = $tokens[$pbIdx + 1]; $alg = $tokens[$algIdx + 1]; $isTwoPhase = $alg -in @('two-phase','lifted-two-phase','full'); $scopeActive = -not $isTwoPhase; $marker = (Get-Item $env:MARKER).LastWriteTime; $log = Get-ChildItem -Path 'logs' -Recurse -Filter ('*_' + $alg + '_' + $pb + '_*.log') -File | Where-Object { $_.LastWriteTime -ge $marker } | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($null -eq $log) { exit 0 }; $gap = $null; $timeLimit = $false; Get-Content $log.FullName | ForEach-Object { if ($_ -match '(?i)TIME_LIMIT_WITH_INCUMBENT|reached time limit|time limit reached|time limit after Phase 1') { $timeLimit = $true }; if ($isTwoPhase -and ($_ -match '^-- Phase 2:|^Phase 2 ')) { $scopeActive = $true }; if ($scopeActive) { if ($_ -match 'gap=([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match '(?i)\bgap\s+([-+]?[0-9]*\.?[0-9]+)%%') { $gap = [double]$matches[1] }; if ($_ -match 'UB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?),\s*LB:\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)') { $ub = [double]$matches[1]; $lb = [double]$matches[2]; $denom = [Math]::Max(1.0, [Math]::Abs($ub)); $gap = ($ub - $lb) / $denom * 100.0 } } }; $gapText = if ($null -eq $gap) { 'NA' } else { $gap.ToString([Globalization.CultureInfo]::InvariantCulture) + '%%' }; $tlText = if ($timeLimit) { '1' } else { '0' }; Write-Output ($gapText + '|' + $tlText + '|' + $log.FullName)"`) do (
    set "FINAL_GAP_PERCENT=%%G"
    set "RUN_HIT_TIME_LIMIT=%%H"
    set "LATEST_LOG_PATH=%%I"
)
exit /b 0

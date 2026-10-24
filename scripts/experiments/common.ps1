Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-ExperimentGroup {
    param(
        [Parameter(Mandatory = $true)]
        [string]$GroupName,

        [Parameter(Mandatory = $true)]
        [array]$Cases
    )

    $root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $executable = Join-Path $root 'out\wireless.exe'
    $runStamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $runDirectory = Join-Path $root "results\experiments\$GroupName\$runStamp"
    $progressPath = Join-Path $runDirectory 'progress.log'
    $manifestPath = Join-Path $runDirectory 'manifest.tsv'
    $summaryPath = Join-Path $runDirectory 'summary.csv'
    $logDirectory = Join-Path $runDirectory 'logs'
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
    New-Item -ItemType Directory -Path $logDirectory | Out-Null

    Set-Content -Path $manifestPath -Encoding utf8 -Value (
        "instance_id`talgorithm`tdata`tscenarios`tcapacity`tdwc_top_n" +
        "`tbattery_step`trsp`tstart_time`tend_time`telapsed_seconds`texit_code" +
        "`tsolver_log`tcommand"
    )

    $originalLocation = Get-Location
    Set-Location $root
    try {
        $failedCase = $null
        $failedExitCode = 0
        $groupStart = Get-Date
        $header = "[{0}] GROUP START name={1} cases={2}" -f (
            $groupStart.ToString('yyyy-MM-dd HH:mm:ss'), $GroupName, $Cases.Count
        )
        Set-Content -Path $progressPath -Encoding utf8 -Value $header
        Write-Host $header

        foreach ($case in $Cases) {
            $start = Get-Date
            $arguments = [string[]]$case.Arguments
            $command = "$executable " + ($arguments -join ' ')
            $startLine = "[{0}] START instance={1} algorithm={2} command={3}" -f (
                $start.ToString('yyyy-MM-dd HH:mm:ss'),
                $case.Id,
                $case.Algorithm,
                $command
            )
            Add-Content -Path $progressPath -Encoding utf8 -Value $startLine
            Write-Host $startLine

            $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
            $consoleOutput = @(& $executable @arguments 2>&1)
            $exitCode = $LASTEXITCODE
            $stopwatch.Stop()

            foreach ($line in $consoleOutput) {
                $text = $line.ToString()
                Add-Content -Path $progressPath -Encoding utf8 -Value "[solver] $text"
                Write-Host $text
            }

            $solverLogLine = ($consoleOutput | ForEach-Object { $_.ToString() } |
                Select-String -Pattern '^Log file: ' | Select-Object -Last 1).Line
            $solverLog = $solverLogLine.Substring('Log file: '.Length)
            $archivedLog = Join-Path $logDirectory ("{0}_{1}.log" -f $case.Id, $case.Algorithm)
            Move-Item -LiteralPath (Join-Path $root $solverLog) -Destination $archivedLog
            $end = Get-Date
            $doneLine = "[{0}] DONE instance={1} algorithm={2} exit={3} elapsed_s={4:F3} solver_log={5}" -f (
                $end.ToString('yyyy-MM-dd HH:mm:ss'),
                $case.Id,
                $case.Algorithm,
                $exitCode,
                $stopwatch.Elapsed.TotalSeconds,
                $solverLog
            )
            Add-Content -Path $progressPath -Encoding utf8 -Value $doneLine
            Write-Host $doneLine

            $row = @(
                $case.Id,
                $case.Algorithm,
                $case.Data,
                $case.Scenarios,
                $case.Capacity,
                $case.DwcTopN,
                $case.BatteryStep,
                $case.Rsp,
                $start.ToString('o'),
                $end.ToString('o'),
                ('{0:F3}' -f $stopwatch.Elapsed.TotalSeconds),
                $exitCode,
                $archivedLog,
                $command
            ) -join "`t"
            Add-Content -Path $manifestPath -Encoding utf8 -Value $row

            if ($exitCode -ne 0) {
                $failedCase = $case
                $failedExitCode = $exitCode
                break
            }
        }

        $groupEnd = Get-Date
        $groupStatus = if ($null -eq $failedCase) { 'DONE' } else { 'STOPPED' }
        $footer = "[{0}] GROUP {1} name={2} elapsed_s={3:F3}" -f (
            $groupEnd.ToString('yyyy-MM-dd HH:mm:ss'),
            $groupStatus,
            $GroupName,
            ($groupEnd - $groupStart).TotalSeconds
        )
        Add-Content -Path $progressPath -Encoding utf8 -Value $footer
        Write-Host $footer

        & python (Join-Path $root 'scripts\experiments\summarize_group.py') `
            --manifest $manifestPath --output $summaryPath
        Write-Host "Summary: $summaryPath"

        if ($null -ne $failedCase) {
            throw "Experiment failed: instance=$($failedCase.Id), algorithm=$($failedCase.Algorithm), exit=$failedExitCode"
        }
    } finally {
        Set-Location $originalLocation
    }
}


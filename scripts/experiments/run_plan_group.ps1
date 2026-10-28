param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'main',
        'scenario',
        'facility',
        'capacity',
        'main-c3',
        'scenario-c3',
        'facility-c3'
    )]
    [string]$Group,

    [ValidateSet(
        'ef',
        'natural-ils',
        'lifted-ils',
        'lifted-two-phase',
        'full'
    )]
    [string]$Algorithm,

    [string]$InstanceId
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$plan = Import-Csv (Join-Path $PSScriptRoot 'benchmark_instances.csv')
$rows = @($plan | Where-Object { $_.group -eq $Group })
if ($InstanceId) {
    $rows = @($rows | Where-Object { $_.instance_id -eq $InstanceId })
}
if ($Algorithm) {
    $rows = @($rows | Where-Object {
        ([string[]]($_.algorithms -split ';')) -contains $Algorithm -and
        ([string[]]($_.completed_algorithms -split ';')) -notcontains $Algorithm
    })
} elseif (-not $InstanceId) {
    throw 'Specify -Algorithm for an algorithm batch or -InstanceId for one complete table row.'
}
if ($rows.Count -eq 0) {
    Write-Host "No pending runs match group=$Group algorithm=$Algorithm instance=$InstanceId"
    exit 0
}

$timeLimitLine = Get-Content (Join-Path $root 'solverCfg.txt') |
    Where-Object { $_ -match '^timeLimit=' } |
    Select-Object -First 1
$configuredTimeLimit = [int](($timeLimitLine -split '=', 2)[1])

$cases = @(foreach ($row in $rows) {
    if ([int]$row.time_limit_seconds -ne $configuredTimeLimit) {
        throw "solverCfg time limit does not match plan for $($row.instance_id)"
    }

    $completed = [string[]]($row.completed_algorithms -split ';')
    $rowAlgorithms = if ($Algorithm) {
        @($Algorithm)
    } else {
        @([string[]]($row.algorithms -split ';') | Where-Object {
            $_ -and $completed -notcontains $_
        })
    }
    foreach ($rowAlgorithm in $rowAlgorithms) {
        $useRsp = if ($rowAlgorithm -eq 'ef') { 0 } else { 1 }
        $arguments = @(
            '--pb', 'wireless_reform',
            '--alg', $rowAlgorithm,
            '--data', $row.data,
            '--scenario-count', $row.N,
            '--scenario-start', '0',
            '--cap', $row.C,
            '--battery-step', $row.battery_step,
            '--dwc-top-n', $row.F,
            '--energy-scale', $row.energy_scale,
            '--rsp', [string]$useRsp
        )
        if ($useRsp -eq 1) {
            $arguments += @('--rsp-workers', '3', '--rsp-threads', '4')
        }

        @{
            Id = $row.instance_id
            Algorithm = $rowAlgorithm
            Data = $row.data
            Scenarios = [int]$row.N
            Capacity = [int]$row.C
            DwcTopN = [int]$row.F
            BatteryStep = [int]$row.battery_step
            Rsp = $useRsp
            Arguments = $arguments
        }
    }
})

if ($cases.Count -eq 0) {
    Write-Host "No pending cases match group=$Group algorithm=$Algorithm instance=$InstanceId"
    exit 0
}

$groupName = $Group
if ($Algorithm) {
    $groupName += "_${Algorithm}"
}
if ($InstanceId) {
    $groupName += "_${InstanceId}"
}
Invoke-ExperimentGroup -GroupName $groupName -Cases $cases

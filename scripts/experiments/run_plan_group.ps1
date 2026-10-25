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

    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'ef',
        'natural-ils-no-rsp',
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
$rows = @($plan | Where-Object {
    $_.group -eq $Group -and
    ([string[]]($_.algorithms -split ';')) -contains $Algorithm -and
    ([string[]]($_.completed_algorithms -split ';')) -notcontains $Algorithm
})
if ($InstanceId) {
    $rows = @($rows | Where-Object { $_.instance_id -eq $InstanceId })
}
if ($rows.Count -eq 0) {
    Write-Host "No pending runs match group=$Group algorithm=$Algorithm instance=$InstanceId"
    exit 0
}

$timeLimitLine = Get-Content (Join-Path $root 'solverCfg.txt') |
    Where-Object { $_ -match '^timeLimit=' } |
    Select-Object -First 1
$configuredTimeLimit = [int](($timeLimitLine -split '=', 2)[1])

$actualAlgorithm = if ($Algorithm -eq 'natural-ils-no-rsp') {
    'natural-ils'
} else {
    $Algorithm
}
$useRsp = if ($Algorithm -in @('ef', 'natural-ils-no-rsp')) { 0 } else { 1 }

$cases = foreach ($row in $rows) {
    if ([int]$row.time_limit_seconds -ne $configuredTimeLimit) {
        throw "solverCfg time limit does not match plan for $($row.instance_id)"
    }

    $arguments = @(
        '--pb', 'wireless_reform',
        '--alg', $actualAlgorithm,
        '--data', $row.data,
        '--scenario-count', $row.N,
        '--scenario-start', '0',
        '--cap', $row.C,
        '--battery-step', $row.battery_step,
        '--dwc-top-n', $row.F,
        '--energy-scale', $row.energy_scale,
        '--rsp', [string]$useRsp
    )
    if ($useRsp -eq 1 -and $actualAlgorithm -in @('lifted-two-phase', 'full')) {
        $arguments += @('--rsp-workers', '3', '--rsp-threads', '4')
    }

    @{
        Id = $row.instance_id
        Algorithm = $Algorithm
        Data = $row.data
        Scenarios = [int]$row.N
        Capacity = [int]$row.C
        DwcTopN = [int]$row.F
        BatteryStep = [int]$row.battery_step
        Rsp = $useRsp
        Arguments = $arguments
    }
}

$groupName = "${Group}_${Algorithm}"
if ($InstanceId) {
    $groupName += "_${InstanceId}"
}
Invoke-ExperimentGroup -GroupName $groupName -Cases $cases

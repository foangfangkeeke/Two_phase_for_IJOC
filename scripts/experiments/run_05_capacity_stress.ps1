. (Join-Path $PSScriptRoot 'common.ps1')

$cases = @()
foreach ($capacity in @(2, 3, 5)) {
    $common = @(
        '--pb', 'wireless_reform',
        '--data', 'tower_base',
        '--scenario-count', '10',
        '--cap', $capacity.ToString(),
        '--battery-step', '20',
        '--dwc-top-n', '50',
        '--energy-scale', '1',
        '--rsp', '0',
        '--phase-progress-log', '0',
        '--phase2-master-log', '0'
    )
    foreach ($algorithm in @('lifted-two-phase', 'full')) {
        $shortName = if ($algorithm -eq 'lifted-two-phase') { 'l2p' } else { 'full' }
        $cases += @{
            Id = "F50-R12-C$capacity-S10-$shortName"
            Algorithm = $algorithm
            Data = 'tower_base'
            Scenarios = 10
            Capacity = $capacity
            DwcTopN = 50
            BatteryStep = 20
            Rsp = 0
            Arguments = $common + @('--alg', $algorithm)
        }
    }
}

Invoke-ExperimentGroup -GroupName '05_capacity_stress' -Cases $cases


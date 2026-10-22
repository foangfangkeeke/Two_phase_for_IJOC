. (Join-Path $PSScriptRoot 'common.ps1')

$cases = @()
foreach ($scenarioCount in @(5, 10, 20, 40)) {
    $common = @(
        '--pb', 'wireless_reform',
        '--data', 'tower_base',
        '--scenario-count', $scenarioCount.ToString(),
        '--cap', '4',
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
            Id = "F50-R12-C4-S$scenarioCount-$shortName"
            Algorithm = $algorithm
            Data = 'tower_base'
            Scenarios = $scenarioCount
            Capacity = 4
            DwcTopN = 50
            BatteryStep = 20
            Rsp = 0
            Arguments = $common + @('--alg', $algorithm)
        }
    }
}

Invoke-ExperimentGroup -GroupName '04_scenario_scale' -Cases $cases


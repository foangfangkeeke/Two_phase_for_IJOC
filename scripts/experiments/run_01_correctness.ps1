. (Join-Path $PSScriptRoot 'common.ps1')

$common = @(
    '--pb', 'wireless_reform',
    '--data', 'tower_small',
    '--scenario-count', '1',
    '--cap', '4',
    '--battery-step', '20',
    '--dwc-top-n', '20',
    '--energy-scale', '1',
    '--rsp', '0',
    '--phase-progress-log', '0',
    '--phase2-master-log', '0'
)

$cases = @(
    @{ Id = 'F20-R3-C4-S1-ef'; Algorithm = 'ef'; Data = 'tower_small'; Scenarios = 1; Capacity = 4; DwcTopN = 20; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'ef') },
    @{ Id = 'F20-R3-C4-S1-natural-ils'; Algorithm = 'natural-ils'; Data = 'tower_small'; Scenarios = 1; Capacity = 4; DwcTopN = 20; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'natural-ils') },
    @{ Id = 'F20-R3-C4-S1-lifted-ils'; Algorithm = 'lifted-ils'; Data = 'tower_small'; Scenarios = 1; Capacity = 4; DwcTopN = 20; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'lifted-ils') },
    @{ Id = 'F20-R3-C4-S1-l2p'; Algorithm = 'lifted-two-phase'; Data = 'tower_small'; Scenarios = 1; Capacity = 4; DwcTopN = 20; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'lifted-two-phase') },
    @{ Id = 'F20-R3-C4-S1-full'; Algorithm = 'full'; Data = 'tower_small'; Scenarios = 1; Capacity = 4; DwcTopN = 20; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'full') }
)

Invoke-ExperimentGroup -GroupName '01_correctness' -Cases $cases


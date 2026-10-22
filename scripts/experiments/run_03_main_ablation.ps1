. (Join-Path $PSScriptRoot 'common.ps1')

$common = @(
    '--pb', 'wireless_reform',
    '--data', 'tower_base',
    '--scenario-count', '10',
    '--cap', '4',
    '--battery-step', '20',
    '--dwc-top-n', '50',
    '--energy-scale', '1',
    '--rsp', '0',
    '--phase-progress-log', '0',
    '--phase2-master-log', '0'
)

$cases = @(
    @{ Id = 'F50-R12-C4-S10-ef'; Algorithm = 'ef'; Data = 'tower_base'; Scenarios = 10; Capacity = 4; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'ef') },
    @{ Id = 'F50-R12-C4-S10-natural-ils'; Algorithm = 'natural-ils'; Data = 'tower_base'; Scenarios = 10; Capacity = 4; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'natural-ils') },
    @{ Id = 'F50-R12-C4-S10-lifted-ils'; Algorithm = 'lifted-ils'; Data = 'tower_base'; Scenarios = 10; Capacity = 4; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'lifted-ils') },
    @{ Id = 'F50-R12-C4-S10-l2p'; Algorithm = 'lifted-two-phase'; Data = 'tower_base'; Scenarios = 10; Capacity = 4; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'lifted-two-phase') },
    @{ Id = 'F50-R12-C4-S10-full'; Algorithm = 'full'; Data = 'tower_base'; Scenarios = 10; Capacity = 4; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'full') }
)

Invoke-ExperimentGroup -GroupName '03_main_ablation' -Cases $cases


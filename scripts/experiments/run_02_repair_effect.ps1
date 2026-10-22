. (Join-Path $PSScriptRoot 'common.ps1')

$common = @(
    '--pb', 'wireless_reform',
    '--data', 'tower_medium',
    '--scenario-count', '3',
    '--cap', '2',
    '--battery-step', '20',
    '--dwc-top-n', '50',
    '--energy-scale', '1',
    '--rsp', '0',
    '--phase-progress-log', '0',
    '--phase2-master-log', '0'
)

$cases = @(
    @{ Id = 'F50-R28-C2-S3-l2p'; Algorithm = 'lifted-two-phase'; Data = 'tower_medium'; Scenarios = 3; Capacity = 2; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'lifted-two-phase') },
    @{ Id = 'F50-R28-C2-S3-full'; Algorithm = 'full'; Data = 'tower_medium'; Scenarios = 3; Capacity = 2; DwcTopN = 50; BatteryStep = 20; Rsp = 0; Arguments = $common + @('--alg', 'full') }
)

Invoke-ExperimentGroup -GroupName '02_repair_effect' -Cases $cases


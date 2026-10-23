. (Join-Path $PSScriptRoot 'common.ps1')

$common = @(
    '--pb', 'wireless_reform',
    '--data', 'tower_medium',
    '--scenario-count', '3',
    '--cap', '2',
    '--battery-step', '50',
    '--dwc-top-n', '20',
    '--energy-scale', '1',
    '--rsp', '0',
    '--phase-progress-log', '0',
    '--phase2-master-log', '0'
)

$cases = @(
    @{ Id = 'F20-R28-C2-S3-ef'; Algorithm = 'ef'; Data = 'tower_medium'; Scenarios = 3; Capacity = 2; DwcTopN = 20; BatteryStep = 50; Rsp = 0; Arguments = $common + @('--alg', 'ef') },
    @{ Id = 'F20-R28-C2-S3-l2p'; Algorithm = 'lifted-two-phase'; Data = 'tower_medium'; Scenarios = 3; Capacity = 2; DwcTopN = 20; BatteryStep = 50; Rsp = 0; Arguments = $common + @('--alg', 'lifted-two-phase') },
    @{ Id = 'F20-R28-C2-S3-full'; Algorithm = 'full'; Data = 'tower_medium'; Scenarios = 3; Capacity = 2; DwcTopN = 20; BatteryStep = 50; Rsp = 0; Arguments = $common + @('--alg', 'full') }
)

Invoke-ExperimentGroup -GroupName '03_medium_top20_step50' -Cases $cases


[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1') -Configuration Release
}

$root = Join-Path $repo 'artifacts\v10_2'
$screens = Join-Path $root 'screenshots\bmp'
$motion = Join-Path $root 'motion'
$performance = Join-Path $root 'performance'
$interaction = Join-Path $root 'interaction'
$typography = Join-Path $root 'typography'
$statusMatrix = Join-Path $root 'status-matrix'
New-Item -ItemType Directory -Force -Path $screens,$motion,$performance,$interaction,$typography,$statusMatrix | Out-Null

$scenarios = @(
    'home','listening','thinking','speaking','answer','answer-long',
    'quick-actions','notifications','settings','audio','aoraton','aoraton-chat',
    'aoraton-compose','aoraton-confirm','offline','error','battery','diagnostics',
    'wifi','wifi-password','wifi-connecting','wifi-failed','wifi-details','wifi-unavailable'
)
foreach ($scenario in $scenarios) {
    & $exe --capture $scenario --screenshot (Join-Path $screens ("aura-{0}.bmp" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Capture failed for $scenario with exit code $LASTEXITCODE" }
}

foreach ($page in 1..3) {
    & $exe --capture ("typography-proof-{0}" -f $page) `
        --screenshot (Join-Path $typography ("typography-proof-{0}.bmp" -f $page))
    if ($LASTEXITCODE -ne 0) { throw "Typography proof $page failed with exit code $LASTEXITCODE" }
}

& $exe --interaction-test (Join-Path $interaction 'pointer-1x.json')
if ($LASTEXITCODE -ne 0) { throw "1x pointer coverage failed with exit code $LASTEXITCODE" }
& $exe --scale 2 --interaction-test (Join-Path $interaction 'pointer-2x.json')
if ($LASTEXITCODE -ne 0) { throw "2x pointer coverage failed with exit code $LASTEXITCODE" }

foreach ($scenario in @('home','listening','thinking','speaking')) {
    $directory = Join-Path $motion $scenario
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    & $exe --motion-sequence $scenario --motion-dir $directory 2>&1 |
        Tee-Object -FilePath (Join-Path $performance ("{0}.log" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Motion capture failed for $scenario with exit code $LASTEXITCODE" }
}

foreach ($value in 0..15) {
    $bits = [Convert]::ToString($value, 2).PadLeft(4, '0')
    $scenario = "status-$bits"
    & $exe --capture $scenario --screenshot (Join-Path $statusMatrix ("{0}.bmp" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Status capture failed for $scenario with exit code $LASTEXITCODE" }
}

python (Join-Path $repo 'scripts\make-v10_2-evidence.py')
if ($LASTEXITCODE -ne 0) { throw "V10.2 evidence validation failed with exit code $LASTEXITCODE" }
Write-Host 'Captured and verified AURA v10.2 screens, typography, pointer routes, status matrix and Mercury motion.'

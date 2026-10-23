[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1') -Configuration Release
}

$root = Join-Path $repo 'artifacts\v10_3'
$screens = Join-Path $root 'screenshots\bmp'
$motion = Join-Path $root 'motion'
$performance = Join-Path $root 'performance'
$interaction = Join-Path $root 'interaction'
New-Item -ItemType Directory -Force -Path $screens,$motion,$performance,$interaction | Out-Null

$scenarios = @(
    'weather-success','weather-loading','weather-no-city','weather-offline',
    'weather-error','weather-location','weather-results','quick-actions','weather-from-quick-actions'
)
foreach ($scenario in $scenarios) {
    & $exe --capture $scenario --screenshot (Join-Path $screens ("aura-{0}.bmp" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Weather capture failed for $scenario with exit code $LASTEXITCODE" }
}

& $exe --interaction-test (Join-Path $interaction 'pointer-1x.json') --scale 1
if ($LASTEXITCODE -ne 0) { throw "1x weather pointer coverage failed with exit code $LASTEXITCODE" }
& $exe --interaction-test (Join-Path $interaction 'pointer-2x.json') --scale 2
if ($LASTEXITCODE -ne 0) { throw "2x weather pointer coverage failed with exit code $LASTEXITCODE" }

foreach ($scenario in @('weather-clear','weather-rain','weather-snow','weather-wind')) {
    $directory = Join-Path $motion $scenario
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    & $exe --motion-sequence $scenario --motion-dir $directory 2>&1 |
        Tee-Object -FilePath (Join-Path $performance ("{0}.log" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Weather motion capture failed for $scenario with exit code $LASTEXITCODE" }
}

python (Join-Path $repo 'scripts\make-v10_3-weather-evidence.py')
if ($LASTEXITCODE -ne 0) { throw "V10.3 weather evidence validation failed with exit code $LASTEXITCODE" }
Write-Host 'Captured and verified AURA v10.3 weather screens, pointer routes and motion.'

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}

$interaction = Join-Path $repo 'artifacts\v8\interaction'
New-Item -ItemType Directory -Force -Path $interaction | Out-Null
& $exe --interaction-test (Join-Path $interaction 'pointer-1x.json')
if ($LASTEXITCODE -ne 0) { throw "1x pointer coverage failed with exit code $LASTEXITCODE" }
& $exe --scale 2 --interaction-test (Join-Path $interaction 'pointer-2x.json')
if ($LASTEXITCODE -ne 0) { throw "2x pointer coverage failed with exit code $LASTEXITCODE" }

$motion = Join-Path $repo 'artifacts\v8\motion'
$performance = Join-Path $repo 'artifacts\v8\performance'
New-Item -ItemType Directory -Force -Path $performance | Out-Null
foreach ($scenario in @('home','listening','thinking','speaking')) {
    $directory = Join-Path $motion $scenario
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    & $exe --motion-sequence $scenario --motion-dir $directory 2>&1 |
        Tee-Object -FilePath (Join-Path $performance ("{0}.log" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Motion capture failed for $scenario with exit code $LASTEXITCODE" }
}

python (Join-Path $repo 'scripts\make-v8-evidence.py') --motion-only
if ($LASTEXITCODE -ne 0) { throw "V8 motion evidence validation failed with exit code $LASTEXITCODE" }
Write-Host 'Captured and verified SDL pointer and LVGL motion evidence.'

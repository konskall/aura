[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}

$directory = Join-Path $repo 'artifacts\v8\screenshots\bmp'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$scenarios = @(
    'home','listening','thinking','speaking','answer','answer-long',
    'quick-actions','notifications','settings','audio','aoraton','aoraton-chat',
    'aoraton-compose','aoraton-confirm','offline','error','battery','diagnostics'
)
foreach ($scenario in $scenarios) {
    $output = Join-Path $directory ("aura-{0}.bmp" -f $scenario)
    & $exe --capture $scenario --screenshot $output
    if ($LASTEXITCODE -ne 0) { throw "Capture failed for $scenario with exit code $LASTEXITCODE" }
}

python (Join-Path $repo 'scripts\make-v8-evidence.py') --screenshots-only
if ($LASTEXITCODE -ne 0) { throw "V8 screenshot gallery generation failed with exit code $LASTEXITCODE" }
Write-Host "Captured $($scenarios.Count) actual square LVGL views in artifacts\v8\screenshots"

[CmdletBinding()]
param()

# Captures the v13 "Companion" screens from the real native LVGL renderer and
# validates them (scripts/make-v13-companion-evidence.py).
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1') -Configuration Release
}

$screens = Join-Path $repo 'artifacts\v13\screenshots\bmp'
New-Item -ItemType Directory -Force -Path $screens | Out-Null
$scenarios = @(
    'quick-actions','home-timer','transcribing','answer-action','voice-error','clock',
    'clock-new','timer-new','alarm-new','reminder-new','clock-alert','notes',
    'note-detail','note-record','briefing','translate','always-on','phone-call',
    'notifications-phone','phone-notification','phone-link','display-settings','update'
)
foreach ($scenario in $scenarios) {
    & $exe --capture $scenario --screenshot (Join-Path $screens ("aura-{0}.bmp" -f $scenario))
    if ($LASTEXITCODE -ne 0) { throw "Capture failed for $scenario with exit code $LASTEXITCODE" }
}

python (Join-Path $repo 'scripts\make-v13-companion-evidence.py')
if ($LASTEXITCODE -ne 0) { throw "V13 companion evidence validation failed with exit code $LASTEXITCODE" }
Write-Host 'Captured and verified the AURA v13 companion screens.'

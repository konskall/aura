[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# The evidence validators need Python with Pillow; fail early with a clear message.
& python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('PIL') else 1)"
if ($LASTEXITCODE -ne 0) { throw 'Python with Pillow is required: python -m pip install --user pillow==12.2.0' }
& (Join-Path $PSScriptRoot 'test-aura-env.ps1')
& (Join-Path $PSScriptRoot 'build-simulator.ps1') -Configuration Release
& (Join-Path $PSScriptRoot 'check-firmware-flags.ps1')

$cmake = Join-Path $repo '.tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe'
$build = Join-Path $repo 'build\native'
& $cmake --build $build --target test
if ($LASTEXITCODE -ne 0) { throw "Native tests failed with exit code $LASTEXITCODE" }

Push-Location (Join-Path $repo 'bridge')
try {
    npm test
    if ($LASTEXITCODE -ne 0) { throw "Bridge tests failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

Push-Location (Join-Path $repo 'simulator\controls')
try {
    npm test
    if ($LASTEXITCODE -ne 0) { throw "Control server tests failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

$shotDir = Join-Path $repo 'simulator\screenshots'
New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
$shot = Join-Path $shotDir 'aura-smoke.bmp'
& (Join-Path $build 'aura_simulator.exe') --smoke --screenshot $shot
if ($LASTEXITCODE -ne 0) { throw "UI smoke run failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $shot)) { throw "UI smoke run did not produce $shot" }
& (Join-Path $build 'aura_simulator.exe') --ui-stress
if ($LASTEXITCODE -ne 0) { throw "UI navigation stress failed with exit code $LASTEXITCODE" }
& (Join-Path $PSScriptRoot 'capture-v10_2-evidence.ps1')
if ($LASTEXITCODE -ne 0) { throw "V10.2 visual/pointer evidence failed with exit code $LASTEXITCODE" }
& (Join-Path $PSScriptRoot 'capture-v10_3_1-weather-evidence.ps1')
if ($LASTEXITCODE -ne 0) { throw "V10.3.1 weather evidence failed with exit code $LASTEXITCODE" }
& (Join-Path $PSScriptRoot 'capture-v13-companion-evidence.ps1')
if ($LASTEXITCODE -ne 0) { throw "V13 companion evidence failed with exit code $LASTEXITCODE" }
Write-Host "All runnable checks passed. Screenshot: $shot"

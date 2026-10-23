[CmdletBinding()]
param(
    [ValidateSet('mock','codex')][string]$Provider = 'mock',
    [switch]$NoBrowser,
    [switch]$Smoke
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}

$runtime = Join-Path $repo 'simulator\runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$events = Join-Path $runtime 'control-events.log'
Set-Content -LiteralPath $events -Value '' -Encoding UTF8

$bytes = New-Object byte[] 32
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$token = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
$env:AURA_BRIDGE_TOKEN = $token
$env:AURA_BRIDGE_PORT = '4778'
$env:AURA_CONTROLS_PORT = '4777'
$env:AURA_CONTROL_EVENTS = $events
$env:AURA_PROVIDER = $Provider
# The Codex desktop app ships its CLI outside PATH; the newest copy is used
# for this run only, unless AURA_CODEX_PATH or a codex on PATH already exists.
if ($Provider -eq 'codex' -and -not $env:AURA_CODEX_PATH -and -not (Get-Command codex -ErrorAction SilentlyContinue)) {
    $appCodex = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\bin') -Recurse -Filter 'codex.exe' `
        -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -ne $appCodex) { $env:AURA_CODEX_PATH = $appCodex.FullName }
}
$env:AURA_BRIDGE_WORKDIR = Join-Path $repo 'bridge\runtime\assistant_sandbox'

$bridge = $null
$controls = $null
try {
    $bridge = Start-Process -FilePath 'node' -ArgumentList 'src/server.mjs' -WorkingDirectory (Join-Path $repo 'bridge') -PassThru -WindowStyle Hidden
    $controls = Start-Process -FilePath 'node' -ArgumentList 'server.mjs' -WorkingDirectory (Join-Path $repo 'simulator\controls') -PassThru -WindowStyle Hidden

    $health = 'http://127.0.0.1:4777/health'
    $bridgeHealth = 'http://127.0.0.1:4778/health'
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $null = Invoke-RestMethod -Method Get -Uri $health -TimeoutSec 1
            $null = Invoke-RestMethod -Method Get -Uri $bridgeHealth -TimeoutSec 5
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $ready) { throw 'AURA development control panel did not become ready.' }

    $controlUrl = "http://127.0.0.1:4777/?token=$token"
    Write-Host "AURA control panel: $controlUrl"
    Write-Host "Provider: $Provider"
    $whisper = Join-Path $repo '.tools\whisper\bin\whisper-cli.exe'
    if (-not (Test-Path -LiteralPath $whisper)) {
        Write-Host 'Speech recognition: not installed (run scripts\setup-voice.ps1)'
    }
    if (-not $NoBrowser) { Start-Process $controlUrl | Out-Null }

    if ($Smoke) {
        $shot = Join-Path $repo 'simulator\screenshots\aura-dev-command.bmp'
        & $exe --smoke --screenshot $shot
    } else {
        & $exe
    }
    if ($LASTEXITCODE -ne 0) { throw "Simulator exited with code $LASTEXITCODE" }
}
finally {
    foreach ($process in @($controls,$bridge)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

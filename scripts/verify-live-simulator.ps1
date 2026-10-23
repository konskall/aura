[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}

$bytes = New-Object byte[] 32
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$token = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
$env:AURA_BRIDGE_TOKEN = $token
$env:AURA_BRIDGE_PORT = '4778'
$env:AURA_CONTROLS_PORT = '4777'
$env:AURA_PROVIDER = 'codex'
$env:AURA_BRIDGE_WORKDIR = Join-Path $repo 'bridge\runtime\assistant_sandbox'

$server = Start-Process -FilePath 'node' -ArgumentList 'src/server.mjs' `
    -WorkingDirectory (Join-Path $repo 'bridge') -PassThru -WindowStyle Hidden
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:4778/health' -TimeoutSec 5
            if ($health.cli.installed -and $health.cli.loggedIn) { $ready = $true; break }
        } catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $ready) { throw 'Authenticated Codex CLI bridge did not become ready.' }
    $shotDir = Join-Path $repo 'simulator\screenshots'
    New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
    $shot = Join-Path $shotDir 'aura-live-codex.bmp'
    & $exe --smoke --screenshot $shot
    if ($LASTEXITCODE -ne 0) { throw "Live simulator verification failed with exit code $LASTEXITCODE" }
    Write-Output $shot
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}


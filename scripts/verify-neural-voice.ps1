[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $repo 'simulator\runtime'
$exe = Join-Path $repo 'build\native\aura_simulator.exe'
$package = Join-Path $repo '.tools\edge-tts\edge_tts\__init__.py'
if (-not (Test-Path -LiteralPath $exe)) { & (Join-Path $PSScriptRoot 'build-simulator.ps1') }
if (-not (Test-Path -LiteralPath $package)) {
    throw 'Neural voices are not installed. Run .\scripts\setup-neural-voice.ps1 first.'
}

New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$events = Join-Path $runtime 'neural-e2e-events.log'
$simOut = Join-Path $runtime 'neural-e2e-simulator.out.log'
$simErr = Join-Path $runtime 'neural-e2e-simulator.err.log'
$bridgeOut = Join-Path $runtime 'neural-e2e-bridge.out.log'
$bridgeErr = Join-Path $runtime 'neural-e2e-bridge.err.log'
foreach ($file in @($events, $simOut, $simErr, $bridgeOut, $bridgeErr)) {
    Set-Content -LiteralPath $file -Value '' -Encoding UTF8
}

$env:AURA_BRIDGE_TOKEN = 'neural-simulator-e2e'
$env:AURA_BRIDGE_PORT = '4788'
$env:AURA_CONTROL_EVENTS = $events
$env:AURA_PROVIDER = 'mock'
$env:AURA_TTS_PROVIDER = 'edge-neural'
$env:AURA_EDGE_TTS_PYTHONPATH = Join-Path $repo '.tools\edge-tts'
$env:AURA_COMPANION_STORE = Join-Path $runtime 'neural-e2e-store'

$bridge = $null
$simulator = $null
try {
    $bridge = Start-Process -FilePath 'node' -ArgumentList 'src/server.mjs' `
        -WorkingDirectory (Join-Path $repo 'bridge') -RedirectStandardOutput $bridgeOut `
        -RedirectStandardError $bridgeErr -PassThru -WindowStyle Hidden
    $health = $null
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $health = Invoke-RestMethod 'http://127.0.0.1:4788/health' -TimeoutSec 2
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    if (-not $health.ok -or $health.speech.tts_provider -ne 'edge-neural' -or -not $health.speech.tts_el) {
        throw 'Neural bridge did not become ready.'
    }

    $simulator = Start-Process -FilePath $exe -RedirectStandardOutput $simOut `
        -RedirectStandardError $simErr -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
    Add-Content -LiteralPath $events -Value ("{0}`tui_view`tvoice" -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -Encoding UTF8
    Add-Content -LiteralPath $events -Value ("{0}`tvoice_preview`tnestoras" -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -Encoding UTF8

    $played = $false
    $log = ''
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        Start-Sleep -Milliseconds 250
        $log = Get-Content -LiteralPath $simErr -Raw -ErrorAction SilentlyContinue
        if ($log -match 'AURA speech playback started') {
            $played = $true
            break
        }
    }
    if (-not $played) {
        throw "Native simulator did not confirm neural PCM playback. Log: $log"
    }
    $line = ($log -split "`r?`n" | Where-Object { $_ -match 'AURA speech playback started' } | Select-Object -Last 1)
    Write-Host "PASSED: edge-neural -> HTTP MP3 -> minimp3 -> Windows audio ($line)"
}
finally {
    if ($null -ne $simulator -and -not $simulator.HasExited) {
        Stop-Process -Id $simulator.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $bridge -and -not $bridge.HasExited) {
        Stop-Process -Id $bridge.Id -Force -ErrorAction SilentlyContinue
    }
}


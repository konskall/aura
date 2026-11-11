[CmdletBinding()]
param(
    [ValidateSet('mock','codex','groq')][string]$Provider = 'mock',
    [ValidateSet('local','groq')][string]$SpeechProvider = 'local',
    [ValidateSet('auto','windows','neural','azure')][string]$TtsProvider = 'auto',
    [switch]$EnableIphoneAncs,
    [switch]$TraceIphoneAncs,
    [switch]$NoBrowser,
    [switch]$Smoke,
    # The Aoraton account signs in the way the board does: through
    # scripts\aoraton-device-sign-in.mjs over File transfer (ADR 0010).
    [switch]$AoratonDevice
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'aura-env.ps1')
Import-AuraLocalEnv -Path (Join-Path $repo '.env')
$built = Join-Path $repo 'build\native\aura_simulator.exe'
if (-not (Test-Path -LiteralPath $built)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}
# AURA runs from a copy, so a build can replace build\native\aura_simulator.exe
# while it is open (Windows locks a running program's file). The copy needs no
# other files: the simulator links statically and reads its data from the repo.
$runDirectory = Join-Path $repo 'build\native\run'
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
$exe = Join-Path $runDirectory 'aura_simulator.exe'
Copy-Item -LiteralPath $built -Destination $exe -Force

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
$env:AURA_PROVIDER = if ($Provider -eq 'mock') { 'mock' } else { 'codex' }
$env:AURA_BRIDGE_AI_PROVIDER = if ($Provider -eq 'groq') { 'groq' } else { 'codex' }
if ($Provider -eq 'groq') { $SpeechProvider = 'groq' }
if ($Provider -eq 'groq' -or $SpeechProvider -eq 'groq') {
    if (-not $env:GROQ_API_KEY) {
        throw 'GROQ_API_KEY is required in the process environment or ignored project .env for Groq chat or speech.'
    }
    if ($env:GROQ_API_KEY -notmatch '^gsk_[A-Za-z0-9_-]{20,}$' -or $env:GROQ_API_KEY.Length -gt 256) {
        throw 'GROQ_API_KEY does not have a valid Groq key shape.'
    }
}
$env:AURA_STT_PROVIDER = $SpeechProvider
# The EU Weekly Oil Bulletin is weekly: renew the cached Greek averages in the
# background when they are missing or more than a week old, so the Today card
# never depends on someone running the update by hand. The bridge re-reads
# the cache and the simulator asks again on its own.
$fuelCache = Join-Path $repo 'bridge\runtime\fuel-prices.json'
$fuelCurrent = $false
if (Test-Path -LiteralPath $fuelCache) {
    try {
        $period = [datetime]::ParseExact((Get-Content -LiteralPath $fuelCache -Raw | ConvertFrom-Json).period_date,
            'yyyy-MM-dd', [System.Globalization.CultureInfo]::InvariantCulture)
        $fuelCurrent = ((Get-Date) - $period).TotalDays -le 7
    } catch {
        $fuelCurrent = $false
    }
}
if (-not $fuelCurrent) {
    Start-Process -FilePath 'powershell' -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $PSScriptRoot 'update-eu-fuel-prices.ps1')) | Out-Null
}
$neuralPackage = Join-Path $repo '.tools\edge-tts\edge_tts\__init__.py'
if ($TtsProvider -eq 'auto') {
    $azureReady = $env:AZURE_SPEECH_KEY -and ($env:AZURE_SPEECH_REGION -or $env:AZURE_SPEECH_ENDPOINT)
    # The 'neural' voices use the unofficial Microsoft Edge read-aloud service,
    # so they are never chosen automatically: official Azure, else Windows.
    $TtsProvider = if ($azureReady) { 'azure' } else { 'windows' }
}
if ($TtsProvider -eq 'azure') {
    if (-not $env:AZURE_SPEECH_KEY) {
        throw 'AZURE_SPEECH_KEY is required in the process environment or ignored project .env for Azure Speech.'
    }
    if ($env:AZURE_SPEECH_KEY.Length -lt 32 -or $env:AZURE_SPEECH_KEY.Length -gt 256 -or
        $env:AZURE_SPEECH_KEY -match '\s') {
        throw 'AZURE_SPEECH_KEY does not have a plausible key shape.'
    }
    if (-not $env:AZURE_SPEECH_REGION -and -not $env:AZURE_SPEECH_ENDPOINT) {
        throw 'AZURE_SPEECH_REGION or AZURE_SPEECH_ENDPOINT is required for Azure Speech.'
    }
    $env:AURA_TTS_PROVIDER = 'azure'
    Remove-Item Env:AURA_EDGE_TTS_PYTHONPATH -ErrorAction SilentlyContinue
} elseif ($TtsProvider -eq 'neural') {
    if (-not (Test-Path -LiteralPath $neuralPackage)) {
        throw 'Neural voices are not installed. Run .\scripts\setup-neural-voice.ps1 first.'
    }
    Write-Warning 'The neural voices use the unofficial Microsoft Edge read-aloud service, not a supported API. Prefer -TtsProvider azure.'
    $env:AURA_TTS_PROVIDER = 'edge-neural'
    $env:AURA_EDGE_TTS_PYTHONPATH = Join-Path $repo '.tools\edge-tts'
} else {
    $env:AURA_TTS_PROVIDER = 'windows'
    Remove-Item Env:AURA_EDGE_TTS_PYTHONPATH -ErrorAction SilentlyContinue
}
if ($EnableIphoneAncs) {
    $env:AURA_ANCS_ENABLED = '1'
}
if ($TraceIphoneAncs) {
    $env:AURA_ANCS_TRACE = '1'
}
# The Codex desktop app ships its CLI outside PATH; the newest copy is used
# for this run only, unless AURA_CODEX_PATH or a codex on PATH already exists.
if ($Provider -eq 'codex' -and -not $env:AURA_CODEX_PATH -and -not (Get-Command codex -ErrorAction SilentlyContinue)) {
    $appCodex = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\bin') -Recurse -Filter 'codex.exe' `
        -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -ne $appCodex) { $env:AURA_CODEX_PATH = $appCodex.FullName }
}
$env:AURA_BRIDGE_WORKDIR = Join-Path $repo 'bridge\runtime\assistant_sandbox'
# Your own aoraton.com account: config\aoraton.local.json comes from setup-aoraton.ps1
# (public Supabase URL and anon key only). Without it the Aoraton screens show DEMO rooms.
$aoratonConfig = Join-Path $repo 'config\aoraton.local.json'
if ($AoratonDevice -and -not $Smoke) {
    # Like the board: the project's values are built in (from the same file, below,
    # so the phone alone can sign in) unless the PC helper sends its own.
    $env:AURA_AORATON_MODE = 'live'
    $env:AURA_AORATON_SIGN_IN = 'device'
}
$aoratonDeviceMode = $env:AURA_AORATON_SIGN_IN -eq 'device'
if (-not $Smoke -and ($aoratonDeviceMode -or -not $env:AURA_AORATON_MODE) -and (Test-Path -LiteralPath $aoratonConfig)) {
    $aoraton = Get-Content -LiteralPath $aoratonConfig -Raw -Encoding UTF8 | ConvertFrom-Json
    $env:AURA_AORATON_URL = $aoraton.url
    $env:AURA_AORATON_ANON_KEY = $aoraton.anon_key
    $env:AURA_AORATON_MODE = 'live'
    # Opt-in: "inco_enabled": true lets this device answer its own @inco messages.
    # The question and recent messages then leave end-to-end encryption for Gemini.
    if (-not $env:AURA_INCO_ENABLED -and $aoraton.inco_enabled -eq $true) { $env:AURA_INCO_ENABLED = '1' }
}

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

    $controlProvider = $Provider
    $controlUrl = "http://127.0.0.1:4777/?token=$token&provider=$controlProvider"
    Write-Host "AURA control panel: $controlUrl"
    Write-Host "Provider: $Provider"
    Write-Host "Speech recognition: $SpeechProvider"
    Write-Host "Speech synthesis: $TtsProvider"
    if ($EnableIphoneAncs) { Write-Host 'iPhone ANCS: ON (private in-memory Windows transport)' }
    if ($env:AURA_AORATON_SIGN_IN -eq 'device') {
        Write-Host 'Aoraton: device mode, signed in the way the board is:'
        Write-Host '  1. In AURA: Messages > account > Sign in with Google (it opens File transfer).'
        Write-Host '  2. Phone on the same Wi-Fi: scan the QR File transfer shows and sign in with Google.'
        Write-Host '     Google returns it through aoraton.com/aura-phone/, which hands the code to AURA'
        Write-Host '     (docs\ADR\0010); Windows may ask to let AURA through its firewall on private networks.'
        Write-Host '     Or this PC: node scripts\aoraton-device-sign-in.mjs --address <address> --code <code>'
        Write-Host '     (the address and the six digits File transfer shows; add --invite "<link>" for a room).'
    } elseif ($env:AURA_AORATON_MODE -eq 'live') {
        Write-Host 'Aoraton: your aoraton.com account (Aoraton > Sign in with Google)'
        if ($env:AURA_INCO_ENABLED -eq '1') {
            Write-Host 'inco: ON - AURA answers its own @inco messages (sent to Gemini through inco-ai)'
        } else {
            Write-Host 'inco: off (set "inco_enabled": true in config\aoraton.local.json to turn it on)'
        }
    } else {
        Write-Host 'Aoraton: DEMO rooms (run scripts\setup-aoraton.ps1 to use your account)'
    }
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

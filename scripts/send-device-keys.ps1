[CmdletBinding()]
param(
    # The address the device shows under Settings > File transfer, e.g. http://192.168.1.23:8788
    [Parameter(Mandatory = $true)][string]$Address,
    # The six-digit code the device shows beside it
    [Parameter(Mandatory = $true)][string]$Code
)

# Sends the assistant's keys from the ignored .env to the device once, over the
# code-protected file transfer session on the local network. The device keeps
# them in encrypted NVS and then listens (Groq), answers (Groq) and speaks
# (Azure) without the PC. No key is printed. The local network carries them as
# it carries transferred files, so use a trusted Wi-Fi.

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'aura-env.ps1')
Import-AuraLocalEnv -Path (Join-Path $repo '.env')

if ($Address -notmatch '^http://(\d{1,3}\.){3}\d{1,3}:\d{1,5}$') {
    throw 'Give the address the device shows, for example http://192.168.1.23:8788'
}
if ($Code -notmatch '^\d{6}$') { throw 'The code is the six digits the device shows.' }
if (-not $env:GROQ_API_KEY) { throw 'GROQ_API_KEY is missing from .env; the device needs it to listen and answer.' }

$region = "$env:AZURE_SPEECH_REGION".Trim().ToLowerInvariant()
if (-not $region -and "$env:AZURE_SPEECH_ENDPOINT" -match '^https://([a-z0-9-]+)\.tts\.speech\.microsoft\.com') {
    $region = $Matches[1]
}
$azureKey = "$env:AZURE_SPEECH_KEY".Trim()
if (-not $azureKey -or -not $region) {
    Write-Host 'No Azure Speech key and region in .env: the device will answer on screen without a voice.'
    $azureKey = ''
    $region = ''
}

try {
    $session = Invoke-RestMethod -Method Post -Uri "$Address/api/session" -ContentType 'application/json' `
        -Body (@{ pin = $Code } | ConvertTo-Json -Compress) -TimeoutSec 15
} catch {
    throw 'The device refused the code or cannot be reached. Open File transfer on the device and check its address and code.'
}
$body = @{
    groq_api_key = "$env:GROQ_API_KEY".Trim()
    azure_speech_key = $azureKey
    azure_speech_region = $region
} | ConvertTo-Json -Compress
try {
    $result = Invoke-RestMethod -Method Post -Uri "$Address/api/keys" -ContentType 'application/json' -Body $body `
        -Headers @{ Authorization = "Bearer $($session.token)" } -TimeoutSec 15
} catch {
    throw 'The device did not keep the keys: check that they are in the formats Groq and Azure give out.'
} finally {
    $body = $null
}
if (-not $result.ok) { throw 'The device did not keep the keys.' }
Write-Host 'The device has its keys. Its assistant now listens, answers and speaks without the PC.'

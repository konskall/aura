[CmdletBinding()]
param(
    [string]$Prompt = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Prompt)) {
    $encodedPrompt = 'zqDOtc+CIM68zr/PhSDPg861IM60z43OvyDPgM+Bzr/PhM6sz4POtc65z4Igz4TOuSDOtc6vzr3Osc65IM6/IM6kzrHOsM6zzrXPhM6/z4IuIM6czrfOvSDPh8+BzrfPg865zrzOv8+Azr/Ouc6uz4POtc65z4IgzrbPic69z4TOsc69zq4gzrHOvc6xzrbOrs+EzrfPg863Lg=='
    $Prompt = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encodedPrompt))
}
$bytes = New-Object byte[] 32
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$token = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
$env:AURA_BRIDGE_TOKEN = $token
$env:AURA_BRIDGE_PORT = '4778'
$env:AURA_CONTROLS_PORT = '4777'
$env:AURA_BRIDGE_WORKDIR = Join-Path $repo 'bridge\runtime\assistant_sandbox'

$server = Start-Process -FilePath 'node' -ArgumentList 'src/server.mjs' `
    -WorkingDirectory (Join-Path $repo 'bridge') -PassThru -WindowStyle Hidden
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:4778/health' -TimeoutSec 5
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    if (-not $ready) { throw 'Bridge health endpoint did not become ready.' }
    Write-Host ("HEALTH service={0} installed={1} loggedIn={2}" -f `
        $health.service,$health.cli.installed,$health.cli.loggedIn)
    $headers = @{ Authorization = "Bearer $token"; Origin = 'http://127.0.0.1' }
    $json = @{ id = 'verification-greek-1'; prompt = $Prompt } | ConvertTo-Json -Compress
    $body = [Text.Encoding]::UTF8.GetBytes($json)
    $reply = Invoke-RestMethod -Method Post `
        -Uri 'http://127.0.0.1:4778/v1/assistant/turn' `
        -Headers $headers -ContentType 'application/json; charset=utf-8' `
        -Body $body -TimeoutSec 90
    Write-Host ("TURN ok={0} bytes={1}" -f $reply.ok,[Text.Encoding]::UTF8.GetByteCount($reply.text))
    Write-Output $reply.text
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}

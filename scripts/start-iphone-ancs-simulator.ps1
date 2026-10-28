[CmdletBinding()]
param(
    [switch]$OpenBluetoothSettings,
    [switch]$ProbeOnly
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$simulator = Join-Path $repo 'build\native\aura_simulator.exe'
$probe = Join-Path $repo 'build\native\aura_ancs_probe.exe'

$adapter = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
    Where-Object { $_.Status -eq 'OK' -and $_.FriendlyName -match 'Adapter' } |
    Select-Object -First 1
if (-not $adapter) { throw 'No working Windows Bluetooth adapter was found.' }
Write-Host "Bluetooth adapter: $($adapter.FriendlyName)"

if ($OpenBluetoothSettings) {
    Start-Process 'ms-settings:bluetooth'
    Write-Host 'Pair and authorise the iPhone, then run this script again.'
    return
}

if (-not (Test-Path -LiteralPath $probe) -or -not (Test-Path -LiteralPath $simulator)) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1') -Configuration Release
}

if ($ProbeOnly) {
    & $probe
    exit $LASTEXITCODE
}

$env:AURA_ANCS_ENABLED = '1'
Write-Host 'Starting AURA with the private in-memory Windows ANCS transport.'
& $simulator

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$target = Join-Path $repo '.tools\edge-tts'
$helper = Join-Path $repo 'bridge\scripts\neural_tts.py'

New-Item -ItemType Directory -Force -Path $target | Out-Null
& python -m pip install --disable-pip-version-check --upgrade --target $target 'edge-tts==7.3.1'
if ($LASTEXITCODE -ne 0) { throw "edge-tts installation failed with exit code $LASTEXITCODE" }

$env:PYTHONPATH = $target
$probe = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('Η νευρωνική φωνή του AURA είναι έτοιμη.'))
$byteCount = & python $helper --text-base64 $probe --voice 'el-GR-NestorasNeural' --probe
$parsedByteCount = 0
if ($LASTEXITCODE -ne 0 -or -not [int]::TryParse([string]$byteCount, [ref]$parsedByteCount) -or
    $parsedByteCount -lt 128) {
    throw 'The neural voice client was installed, but the live Edge Read Aloud probe returned no audio.'
}
Write-Host 'AURA neural voice ready: el-GR-NestorasNeural'

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'idf.py is not available. Install and activate ESP-IDF 5.5.5, then rerun this script.'
}

Push-Location (Join-Path $repo 'firmware')
try {
    idf.py set-target esp32s3
    if ($LASTEXITCODE -ne 0) { throw "idf.py set-target failed with exit code $LASTEXITCODE" }
    idf.py build
    if ($LASTEXITCODE -ne 0) { throw "idf.py build failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }


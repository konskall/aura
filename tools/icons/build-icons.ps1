[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$toolRoot = Join-Path $repo '.tools\icon-build'
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot 'node_modules\lucide-static'))) {
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    npm install --prefix $toolRoot --no-audit --no-fund `
        '@resvg/resvg-js@2.6.2' 'lucide-static@1.59.0' 'pngjs@7.0.0'
    if ($LASTEXITCODE -ne 0) { throw "Icon tool install failed: $LASTEXITCODE" }
}
# Icon families of the Marble, Aurora and Graphite appearances.
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot 'node_modules\@iconify-json\material-symbols'))) {
    npm install --prefix $toolRoot --no-audit --no-fund --save-exact `
        '@iconify-json/ph@1.2.2' '@iconify-json/solar@1.2.13' '@iconify-json/material-symbols@1.2.97'
    if ($LASTEXITCODE -ne 0) { throw "Icon family install failed: $LASTEXITCODE" }
}
Push-Location $repo
try {
    node (Join-Path $PSScriptRoot 'generate-icons.mjs')
    if ($LASTEXITCODE -ne 0) { throw "Icon generation failed: $LASTEXITCODE" }
    node (Join-Path $PSScriptRoot 'generate-appearance-icons.mjs')
    if ($LASTEXITCODE -ne 0) { throw "Appearance icon generation failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
Copy-Item -LiteralPath (Join-Path $toolRoot 'node_modules\lucide-static\LICENSE') `
    -Destination (Join-Path $repo 'shared\assets\licenses\LUCIDE_ISC.txt') -Force


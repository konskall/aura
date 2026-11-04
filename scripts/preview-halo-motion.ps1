[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$taskRepo = Split-Path -Parent $PSScriptRoot
$taskPreview = Join-Path $taskRepo 'simulator\previews\halo-motion\index.html'
if (-not (Test-Path -LiteralPath $taskPreview -PathType Leaf)) {
    throw 'Halo motion preview is missing.'
}
# The owner explicitly requests a visible, interactive local browser preview.
# No HTTP listener, credentials, simulator settings or provider is involved.
Start-Process -FilePath $taskPreview

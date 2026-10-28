[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SigningKey,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repo = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$firmware = Join-Path $repo 'firmware'
$build = Join-Path $firmware 'build-production'

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'idf.py is not available. Activate the repository-approved ESP-IDF 5.5.5 environment first.'
}
$idfVersionFile = if ($env:IDF_PATH) { Join-Path $env:IDF_PATH 'tools\cmake\version.cmake' } else { $null }
if (-not $idfVersionFile -or -not (Test-Path -LiteralPath $idfVersionFile)) {
    throw 'The active ESP-IDF environment does not expose IDF_PATH/tools/cmake/version.cmake.'
}
$idfVersionSource = Get-Content -LiteralPath $idfVersionFile -Raw
if ($idfVersionSource -notmatch 'IDF_VERSION_MAJOR\s+5\)' -or
    $idfVersionSource -notmatch 'IDF_VERSION_MINOR\s+5\)' -or
    $idfVersionSource -notmatch 'IDF_VERSION_PATCH\s+5\)') {
    throw 'AURA production builds require ESP-IDF v5.5.5; the active environment reports another version.'
}

$resolvedKey = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $SigningKey).Path)
$repoPrefix = $repo.TrimEnd('\') + '\'
if ($resolvedKey.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The production signing key must be stored outside the repository.'
}
if ([System.IO.Path]::GetExtension($resolvedKey) -ne '.pem') {
    throw 'The Secure Boot V2 signing key must be a PEM file.'
}

if ($Clean -and (Test-Path -LiteralPath $build)) {
    $resolvedBuild = [System.IO.Path]::GetFullPath($build)
    $expectedBuild = [System.IO.Path]::GetFullPath((Join-Path $firmware 'build-production'))
    if ($resolvedBuild -ne $expectedBuild) { throw 'Refusing to clean an unexpected build path.' }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('aura-production-' + [guid]::NewGuid().ToString('N'))
$temporaryDefaults = Join-Path $temporaryRoot 'signing-key.defaults'
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    $cmakeKey = $resolvedKey.Replace('\', '/')
    Set-Content -LiteralPath $temporaryDefaults -Encoding Ascii -Value `
        ('CONFIG_SECURE_BOOT_SIGNING_KEY="' + $cmakeKey + '"')

    $baseDefaults = (Join-Path $firmware 'sdkconfig.defaults').Replace('\', '/')
    $productionDefaults = (Join-Path $firmware 'sdkconfig.production.defaults').Replace('\', '/')
    $keyDefaults = $temporaryDefaults.Replace('\', '/')
    $sdkconfig = (Join-Path $build 'sdkconfig').Replace('\', '/')
    $defaults = "$baseDefaults;$productionDefaults;$keyDefaults"

    Push-Location $firmware
    try {
        idf.py -B $build "-DSDKCONFIG=$sdkconfig" "-DSDKCONFIG_DEFAULTS=$defaults" set-target esp32s3 build
        if ($LASTEXITCODE -ne 0) { throw "Production firmware build failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
} finally {
    $resolvedTemporary = [System.IO.Path]::GetFullPath($temporaryRoot)
    $systemTemporary = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolvedTemporary.StartsWith($systemTemporary, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemporary).StartsWith('aura-production-')) {
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'Production image built only; nothing was flashed and no eFuse was modified.'
Write-Host 'Manufacturing must preserve the external signing key and follow the ESP-IDF Secure Boot/Flash Encryption first-boot procedure.'

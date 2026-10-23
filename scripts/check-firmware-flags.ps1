[CmdletBinding()]
param()

# Compiles the shared core/localization/UI sources with the ESP-IDF 5.5 default
# C++ mode and warning policy (tools/cmake/build.cmake: -std=gnu++2b, -Wall,
# -Wextra, -Werror with IDF's exceptions). This catches firmware-only compile
# failures on Windows. It is a syntax/diagnostic check, not an ESP-IDF build.

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$gxx = Join-Path $repo '.tools\w64devkit\bin\g++.exe'
$lvgl = Join-Path $repo 'build\native\_deps\lvgl-src'
foreach ($required in @($gxx, $lvgl)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing $required. Run .\scripts\build-simulator.ps1 first."
    }
}
$env:PATH = "$(Split-Path $gxx);$env:PATH"

$flags = @(
    # sdkconfig.defaults disables C++ exceptions and RTTI on the device.
    '-std=gnu++2b', '-fsyntax-only', '-fno-exceptions', '-fno-rtti',
    '-Wall', '-Werror',
    '-Wno-error=unused-function', '-Wno-error=unused-variable',
    '-Wno-error=unused-but-set-variable', '-Wno-error=deprecated-declarations',
    '-Wextra', '-Wno-error=extra', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-enum-conversion',
    '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE'
)
$includes = @(
    'shared\core\include', 'shared\localization\include', 'shared\ui\include',
    'shared\assets\fonts', 'shared\assets\icons', 'simulator\config'
) | ForEach-Object { '-I' + (Join-Path $repo $_) }
$includes += '-I' + $lvgl

$sources = @(
    Get-ChildItem -LiteralPath (Join-Path $repo 'shared\core\src') -Filter '*.cpp'
    Get-ChildItem -LiteralPath (Join-Path $repo 'shared\localization\src') -Filter '*.cpp'
    Get-ChildItem -LiteralPath (Join-Path $repo 'shared\ui\src') -Filter '*.cpp'
)
$failed = @()
foreach ($source in $sources) {
    & $gxx @flags @includes $source.FullName
    if ($LASTEXITCODE -ne 0) { $failed += $source.Name }
}
if ($failed.Count -gt 0) {
    throw "Shared sources fail under ESP-IDF 5.5 C++ flags: $($failed -join ', ')"
}
Write-Host "ESP-IDF flag check passed for $($sources.Count) shared sources (gnu++2b, no exceptions/RTTI, -Wall -Wextra -Werror)."

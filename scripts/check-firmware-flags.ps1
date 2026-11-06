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
    'shared\assets\fonts', 'shared\assets\icons', 'simulator\config',
    'shared\assets\branding'
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
# MP3 decoding runs on the device's small task stacks (the assistant's
# worker has 12 KB): no function of the decoder may need more than 6 KB.
$stackObject = Join-Path ([System.IO.Path]::GetTempPath()) 'aura-mp3-stack-check.o'
& $gxx -std=gnu++2b -O2 -fno-exceptions -fno-rtti '-Werror=stack-usage=6144' @includes -c `
    (Join-Path $repo 'shared\core\src\mp3_decoder.cpp') -o $stackObject
$stackExit = $LASTEXITCODE
Remove-Item -LiteralPath $stackObject -ErrorAction SilentlyContinue
if ($stackExit -ne 0) { throw 'The MP3 decoder needs more than 6 KB of stack (shared/core/src/mp3_decoder.cpp).' }
Write-Host "ESP-IDF flag check passed for $($sources.Count) shared sources (gnu++2b, no exceptions/RTTI, -Wall -Wextra -Werror); the MP3 decoder fits a 6 KB stack."

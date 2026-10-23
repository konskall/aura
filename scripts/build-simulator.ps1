[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$cmake = Join-Path $repo '.tools\cmake-4.4.4-windows-x86_64\bin\cmake.exe'
$ninja = Join-Path $repo '.tools\ninja\ninja.exe'
$gcc = Join-Path $repo '.tools\w64devkit\bin\gcc.exe'
$gxx = Join-Path $repo '.tools\w64devkit\bin\g++.exe'

foreach ($required in @($cmake,$ninja,$gcc,$gxx)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing local build tool: $required. Run .\scripts\setup-windows.ps1 first."
    }
}

$env:PATH = "$(Split-Path $gcc);$(Split-Path $cmake);$(Split-Path $ninja);$env:PATH"
$build = Join-Path $repo 'build\native'
New-Item -ItemType Directory -Force -Path $build | Out-Null

& $cmake -Wno-deprecated -S $repo -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_C_COMPILER=$gcc" `
    "-DCMAKE_CXX_COMPILER=$gxx"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& $cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { throw "Simulator build failed with exit code $LASTEXITCODE" }

Write-Host "Built: $(Join-Path $build 'aura_simulator.exe')"

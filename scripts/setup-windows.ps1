[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $repo '.tools'
$downloads = Join-Path $tools 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

function Get-VerifiedArchive {
    param(
        [Parameter(Mandatory=$true)][string]$Url,
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Sha256
    )
    if (Test-Path -LiteralPath $Path) {
        $existing = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($existing -ne $Sha256.ToLowerInvariant()) {
            $resolved = [IO.Path]::GetFullPath($Path)
            $downloadRoot = [IO.Path]::GetFullPath($downloads) + [IO.Path]::DirectorySeparatorChar
            if (-not $resolved.StartsWith($downloadRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to replace archive outside the project download cache: $resolved"
            }
            Write-Host "Discarding incomplete archive $(Split-Path -Leaf $Path)..."
            Remove-Item -LiteralPath $resolved -Force
        }
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Host "Downloading $(Split-Path -Leaf $Path)..."
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Path
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $Path. Expected $Sha256, got $actual"
    }
}

$w64Root = Join-Path $tools 'w64devkit'
$w64Exe = Join-Path $w64Root 'bin\gcc.exe'
if (-not (Test-Path -LiteralPath $w64Exe)) {
    $archive = Join-Path $downloads 'w64devkit-x64-2.10.1.7z.exe'
    Get-VerifiedArchive `
        -Url 'https://github.com/skeeto/w64devkit/releases/download/v2.10.1/w64devkit-x64-2.10.1.7z.exe' `
        -Path $archive `
        -Sha256 'add1edf984ce4d473cfeb7c52e0ab38db73a855bba6c7f564d5e16648eb2896a'
    Write-Host 'Extracting project-local GCC toolchain...'
    $process = Start-Process -FilePath $archive -ArgumentList '-y',("-o{0}" -f $tools) -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $w64Exe)) {
        throw "w64devkit extraction failed with exit code $($process.ExitCode)"
    }
}

$cmakeRoot = Join-Path $tools 'cmake-4.4.4-windows-x86_64'
$cmakeExe = Join-Path $cmakeRoot 'bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) {
    $archive = Join-Path $downloads 'cmake-4.4.4-windows-x86_64.zip'
    Get-VerifiedArchive `
        -Url 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip' `
        -Path $archive `
        -Sha256 'bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536'
    Write-Host 'Extracting project-local CMake...'
    Expand-Archive -LiteralPath $archive -DestinationPath $tools -Force
}

$ninjaRoot = Join-Path $tools 'ninja'
$ninjaExe = Join-Path $ninjaRoot 'ninja.exe'
if (-not (Test-Path -LiteralPath $ninjaExe)) {
    $archive = Join-Path $downloads 'ninja-win-1.13.2.zip'
    Get-VerifiedArchive `
        -Url 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' `
        -Path $archive `
        -Sha256 '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
    New-Item -ItemType Directory -Force -Path $ninjaRoot | Out-Null
    Write-Host 'Extracting project-local Ninja...'
    Expand-Archive -LiteralPath $archive -DestinationPath $ninjaRoot -Force
}

$lvglArchive = Join-Path $downloads 'lvgl-9.6.0.tar.gz'
Get-VerifiedArchive `
    -Url 'https://codeload.github.com/lvgl/lvgl/tar.gz/80ca777e37a2b176770726a02e07a6fb79ef0b39' `
    -Path $lvglArchive `
    -Sha256 '1c10f77b8e5e7776366d56de5645958f3312b3926b71a2a811d819fc0ad0cbaa'

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Warning 'Python 3 is not on PATH; scripts\test.ps1 needs it for evidence validation.'
} else {
    & python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('PIL') else 1)"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'Pillow is missing; install it with: python -m pip install --user pillow==12.2.0'
    }
}

Write-Host "GCC:   $(& $w64Exe --version | Select-Object -First 1)"
Write-Host "CMake: $(& $cmakeExe --version | Select-Object -First 1)"
Write-Host "Ninja: $(& $ninjaExe --version)"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-simulator.ps1')
}

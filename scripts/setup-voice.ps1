[CmdletBinding()]
param([switch]$Force)

# Installs local, offline speech recognition for the simulator under the
# ignored .tools folder: the whisper.cpp v1.9.2 CPU build, the multilingual
# "large-v3-turbo" model (q5_0, used for its much better Greek) and "small"
# (q5_1, the fallback). Every download is pinned by SHA-256. Remove
# .tools\whisper to uninstall. Nothing outside the repository is changed.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = Split-Path -Parent $PSScriptRoot
$downloads = Join-Path $repo '.tools\downloads'
$root = Join-Path $repo '.tools\whisper'
$bin = Join-Path $root 'bin'
$models = Join-Path $root 'models'
New-Item -ItemType Directory -Force -Path $downloads, $models | Out-Null

$items = @(
    @{
        Name = 'whisper-bin-x64-v1.9.2.zip'
        Url = 'https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.2/whisper-bin-x64.zip'
        Sha256 = '49dcc16de826f20bd53d44f947a1ae49dfa81f86cad67a64d80820cb192d674a'
    },
    @{
        Name = 'ggml-small-q5_1.bin'
        Url = 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin'
        Sha256 = 'ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb'
    },
    @{
        Name = 'ggml-large-v3-turbo-q5_0.bin'
        Url = 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin'
        Sha256 = '394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2'
    }
)

function Get-Pinned([hashtable]$item) {
    $target = Join-Path $downloads $item.Name
    if ((Test-Path -LiteralPath $target) -and -not $Force -and
        (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ieq $item.Sha256) {
        return $target
    }
    Write-Host "Downloading $($item.Name)..."
    $partial = "$target.part"
    Invoke-WebRequest -Uri $item.Url -OutFile $partial -UseBasicParsing
    $hash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash
    if ($hash -ine $item.Sha256) {
        Remove-Item -LiteralPath $partial -Force
        throw "SHA-256 mismatch for $($item.Name): $hash"
    }
    Move-Item -LiteralPath $partial -Destination $target -Force
    return $target
}

$archive = Get-Pinned $items[0]
$model = Get-Pinned $items[1]
$turbo = Get-Pinned $items[2]

# A fresh extraction into its own folder, then only the CLI and its DLLs.
$staging = Join-Path $root 'staging'
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
Expand-Archive -LiteralPath $archive -DestinationPath $staging
$cli = Get-ChildItem -LiteralPath $staging -Recurse -Filter 'whisper-cli.exe' | Select-Object -First 1
if ($null -eq $cli) { throw 'whisper-cli.exe was not found in the release archive.' }
if (Test-Path -LiteralPath $bin) { Remove-Item -LiteralPath $bin -Recurse -Force }
New-Item -ItemType Directory -Force -Path $bin | Out-Null
Get-ChildItem -LiteralPath $cli.DirectoryName -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    Copy-Item -Destination $bin
Remove-Item -LiteralPath $staging -Recurse -Force
Copy-Item -LiteralPath $model -Destination (Join-Path $models 'ggml-small-q5_1.bin') -Force
Copy-Item -LiteralPath $turbo -Destination (Join-Path $models 'ggml-large-v3-turbo-q5_0.bin') -Force

# whisper logs its backend to stderr; Windows PowerShell must not treat that as an error.
$ErrorActionPreference = 'Continue'
& (Join-Path $bin 'whisper-cli.exe') --help 2>&1 | Out-Null
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code -ne 0) { throw "whisper-cli.exe does not run (exit $code)." }
Write-Host "Local speech recognition ready: $bin (model ggml-large-v3-turbo-q5_0, $([math]::Round((Get-Item $turbo).Length / 1MB)) MB; fallback ggml-small-q5_1)."

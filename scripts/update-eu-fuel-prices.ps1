[CmdletBinding()]
param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repo 'bridge\runtime\fuel-prices.json' }
$sourcePage = 'https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('aura-fuel-' + [guid]::NewGuid().ToString('N'))

try {
    $page = Invoke-WebRequest -Uri $sourcePage -UseBasicParsing
    $match = [regex]::Match($page.Content, '(?is)Prices with taxes latest prices.*?href="([^"]+\.xlsx[^"]*)"')
    if (-not $match.Success) { throw 'Could not locate the official prices-with-taxes XLSX link.' }
    $relative = [System.Net.WebUtility]::HtmlDecode($match.Groups[1].Value)
    $download = [uri]::new([uri]$sourcePage, $relative).AbsoluteUri

    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    $xlsx = Join-Path $tempRoot 'weekly-oil-bulletin.xlsx'
    Invoke-WebRequest -Uri $download -OutFile $xlsx -UseBasicParsing
    $expanded = Join-Path $tempRoot 'xlsx'
    [System.IO.Compression.ZipFile]::ExtractToDirectory($xlsx, $expanded)

    [xml]$sharedXml = Get-Content -LiteralPath (Join-Path $expanded 'xl\sharedStrings.xml')
    [xml]$sheetXml = Get-Content -LiteralPath (Join-Path $expanded 'xl\worksheets\sheet1.xml')
    $namespaces = New-Object System.Xml.XmlNamespaceManager($sheetXml.NameTable)
    $namespaces.AddNamespace('x', 'http://schemas.openxmlformats.org/spreadsheetml/2006/main')
    $shared = @($sharedXml.sst.si | ForEach-Object {
        if ($_.t.'#text') { [string]$_.t.'#text' } else { [string]$_.t }
    })
    function Cell-Value($cell) {
        $value = [string]$cell.v
        if ($cell.t -eq 's') { return $shared[[int]$value] }
        return $value
    }
    $rows = $sheetXml.SelectNodes('//x:sheetData/x:row', $namespaces)
    $dateCell = $rows[1].c | Where-Object { $_.r -eq 'A2' } | Select-Object -First 1
    if (-not $dateCell) { throw 'The bulletin period date is missing.' }
    $period = [DateTime]::FromOADate([double](Cell-Value $dateCell)).ToString('yyyy-MM-dd')
    $greece = $null
    foreach ($row in $rows) {
        $first = $row.c | Where-Object { $_.r -match '^A\d+$' } | Select-Object -First 1
        if ($first -and (Cell-Value $first) -eq 'Greece') { $greece = $row; break }
    }
    if (-not $greece) { throw 'The Greece row is missing from the official workbook.' }
    function Product-Price([string]$column, [bool]$required) {
        $cell = $greece.c | Where-Object { $_.r -match ('^' + $column + '\d+$') } | Select-Object -First 1
        if (-not $cell -or [string]::IsNullOrWhiteSpace((Cell-Value $cell))) {
            if ($required) { throw "Required product $column is missing for Greece." }
            return $null
        }
        $rounded = [int][Math]::Round([double](Cell-Value $cell), [MidpointRounding]::AwayFromZero)
        if ($rounded -lt 500 -or $rounded -gt 5000) { throw "Product $column is outside the accepted range." }
        return $rounded
    }
    $payload = [ordered]@{
        schema = 1
        country = 'GR'
        period_date = $period
        source = 'European Commission Weekly Oil Bulletin'
        source_url = $sourcePage
        unleaded95_millieur_per_litre = Product-Price 'B' $true
        diesel_millieur_per_litre = Product-Price 'C' $true
        lpg_millieur_per_litre = Product-Price 'G' $false
    }
    $parent = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $temporary = $OutputPath + '.tmp'
    [System.IO.File]::WriteAllText($temporary, ($payload | ConvertTo-Json -Compress),
        [System.Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $OutputPath -Force
    Write-Host "AURA fuel cache updated: $period -> $OutputPath"
    Write-Host "Unleaded95=$($payload.unleaded95_millieur_per_litre) Diesel=$($payload.diesel_millieur_per_litre) LPG=$($payload.lpg_millieur_per_litre) millieur/litre"
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}

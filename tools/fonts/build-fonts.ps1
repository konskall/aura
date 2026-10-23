[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sourceDir = Join-Path $repo 'shared\assets\fonts\source'
$outputDir = Join-Path $repo 'shared\assets\fonts'
$licenseDir = Join-Path $repo 'shared\assets\licenses'
New-Item -ItemType Directory -Force -Path $sourceDir,$outputDir,$licenseDir | Out-Null

$ibmCommit = '71f558992315a12ca1f06a2215d57962a1992162'
$spaceCommit = '03507d024a01282884232081fc6011c09ff4e849'

function Get-PinnedFile([string]$Uri, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $Path
    }
}

$ibmBase = "https://raw.githubusercontent.com/IBM/plex/$ibmCommit/packages/plex-sans/fonts/complete/ttf"
foreach ($weight in @('Light','Regular','Medium','SemiBold')) {
    $name = "IBMPlexSans-$weight.ttf"
    Get-PinnedFile "$ibmBase/$name" (Join-Path $sourceDir $name)
}
Get-PinnedFile "https://raw.githubusercontent.com/IBM/plex/$ibmCommit/LICENSE.txt" (Join-Path $licenseDir 'IBM_PLEX_OFL.txt')
Get-PinnedFile "https://raw.githubusercontent.com/floriankarsten/space-grotesk/$spaceCommit/fonts/ttf/static/SpaceGrotesk-Medium.ttf" (Join-Path $sourceDir 'SpaceGrotesk-Medium.ttf')
Get-PinnedFile "https://raw.githubusercontent.com/floriankarsten/space-grotesk/$spaceCommit/OFL.txt" (Join-Path $licenseDir 'SPACE_GROTESK_OFL.txt')

# v12 "Halo" type scale for the 2.16" panel (~12.4 px/mm): the smallest text is
# 22 px (~1.8 mm em) and reading text 26 px. The two Light numeric subsets keep
# the clock and the big numbers affordable; they fall back to the title font.
$uiRange = '0x20-0xFF,0x300-0x36F,0x370-0x3FF,0x2010-0x2026,0x20AC,0x2122,0x2190-0x2193,0x2212'
$jobs = @(
    @{ Font='IBMPlexSans-Regular.ttf';  Size=22; Name='aura_font_plex_regular_22'; Range=$uiRange },
    @{ Font='IBMPlexSans-Regular.ttf';  Size=26; Name='aura_font_plex_regular_26'; Range=$uiRange },
    @{ Font='IBMPlexSans-Medium.ttf';   Size=28; Name='aura_font_plex_medium_28';  Range=$uiRange },
    @{ Font='IBMPlexSans-SemiBold.ttf'; Size=32; Name='aura_font_plex_semibold_32';Range=$uiRange },
    @{ Font='IBMPlexSans-Light.ttf';    Size=72; Name='aura_font_plex_light_72';
       Range='0x20,0x25,0x2B,0x2D-0x3A,0x43,0xB0,0x2212'; Fallback='aura_font_plex_semibold_32' },
    @{ Font='IBMPlexSans-Light.ttf';    Size=96; Name='aura_font_plex_light_96';
       Range='0x20,0x2D,0x30-0x3A'; Fallback='aura_font_plex_semibold_32' },
    @{ Font='SpaceGrotesk-Medium.ttf';  Size=26; Name='aura_font_space_medium_26'; Range='0x20,0x41,0x52,0x55' }
)

Push-Location $sourceDir
try {
    foreach ($job in $jobs) {
        $output = Join-Path $outputDir ($job.Name + '.c')
        npx --yes lv_font_conv@1.5.3 `
            --font $job.Font `
            --size $job.Size `
            --bpp 4 `
            --format lvgl `
            --range $job.Range `
            --no-compress `
            --force-fast-kern-format `
            -o $output
        if ($LASTEXITCODE -ne 0) { throw "Font generation failed for $($job.Name): $LASTEXITCODE" }

        # lv_font_conv 1.5.3 predates lv_font_t::static_bitmap (LVGL 9.3).
        # These uncompressed C arrays are immutable and must be marked static;
        # otherwise LVGL 9.6 treats their glyph data as transient draw storage.
        $generated = [System.IO.File]::ReadAllText($output)
        $descriptor = '    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */'
        $staticBitmap = @'
#if LV_VERSION_CHECK(9, 3, 0)
    .static_bitmap = 1,       /*Uncompressed const C bitmap data*/
#endif

    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
'@
        if (-not $generated.Contains($descriptor)) {
            throw "Could not add LVGL 9 static bitmap metadata to $($job.Name)"
        }
        $generated = $generated.Replace($descriptor, $staticBitmap.TrimEnd())
        if ($job.ContainsKey('Fallback')) {
            # Subset fonts draw anything outside their range with the title font
            # instead of an empty box.
            $noFallback = '    .fallback = NULL,'
            $publicFont = '/*Initialize a public general font descriptor*/'
            if (-not $generated.Contains($noFallback) -or -not $generated.Contains($publicFont)) {
                throw "Could not add the fallback font to $($job.Name)"
            }
            $generated = $generated.Replace($noFallback, "    .fallback = &$($job.Fallback),")
            $generated = $generated.Replace($publicFont, "LV_FONT_DECLARE($($job.Fallback));`n`n$publicFont")
        }
        [System.IO.File]::WriteAllText($output, $generated, [System.Text.UTF8Encoding]::new($false))
    }
} finally {
    Pop-Location
}

python (Join-Path $PSScriptRoot 'verify-font-sources.py') $sourceDir
if ($LASTEXITCODE -ne 0) { throw "Font coverage verification failed: $LASTEXITCODE" }
Write-Host "Generated and verified $($jobs.Count) LVGL fonts in $outputDir"

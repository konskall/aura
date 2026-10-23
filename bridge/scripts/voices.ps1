[CmdletBinding()]
param()

# Lists the language tags (e.g. el-GR) of the installed Windows voices: the
# modern OneCore voices first, then the classic SAPI (desktop) voices.
$ErrorActionPreference = 'Stop'
$tags = New-Object System.Collections.Generic.List[string]
try {
    $null = [Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType = WindowsRuntime]
    [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | ForEach-Object { $tags.Add($_.Language) }
} catch { }
try {
    Add-Type -AssemblyName System.Speech
    $sapi = New-Object System.Speech.Synthesis.SpeechSynthesizer
    try {
        $sapi.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { $tags.Add($_.VoiceInfo.Culture.Name) }
    } finally { $sapi.Dispose() }
} catch { }
$tags | Sort-Object -Unique

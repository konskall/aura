[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TextBase64,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-z]{2}-[A-Z]{2}$')][string]$Language,
    [Parameter(Mandatory = $true)][string]$OutFile
)

# Speaks text with an installed Windows (OneCore) voice into a WAV file. The
# bridge passes the text as Base64 so it never meets command-line quoting.
$ErrorActionPreference = 'Stop'
$text = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($TextBase64))

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime]

$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
function Wait-WinRt($operation, [Type]$resultType) {
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($operation))
    $null = $task.Wait(25000)
    if (-not $task.IsCompleted) { throw 'Speech synthesis timed out.' }
    return $task.Result
}

$voice = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices |
    Where-Object { $_.Language -eq $Language } | Select-Object -First 1
if ($null -eq $voice) {
    # No OneCore voice: try a classic SAPI (desktop) voice, 16 kHz mono 16-bit.
    Add-Type -AssemblyName System.Speech
    $sapi = New-Object System.Speech.Synthesis.SpeechSynthesizer
    try {
        $match = $sapi.GetInstalledVoices() | Where-Object { $_.Enabled -and $_.VoiceInfo.Culture.Name -eq $Language } |
            Select-Object -First 1
        if ($null -eq $match) { exit 3 }
        $sapi.SelectVoice($match.VoiceInfo.Name)
        $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,
            [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
        $sapi.SetOutputToWaveFile($OutFile, $format)
        $sapi.Speak($text)
        $sapi.SetOutputToNull()
    } finally { $sapi.Dispose() }
    exit 0
}

$synthesizer = New-Object Windows.Media.SpeechSynthesis.SpeechSynthesizer
try {
    $synthesizer.Voice = $voice
    $stream = Wait-WinRt ($synthesizer.SynthesizeTextToStreamAsync($text)) ([Windows.Media.SpeechSynthesis.SpeechSynthesisStream])
    $size = [uint32]$stream.Size
    $reader = New-Object Windows.Storage.Streams.DataReader($stream.GetInputStreamAt(0))
    $null = Wait-WinRt ($reader.LoadAsync($size)) ([uint32])
    $buffer = New-Object byte[] $size
    $reader.ReadBytes($buffer)
    [IO.File]::WriteAllBytes($OutFile, $buffer)
} finally {
    $synthesizer.Dispose()
}

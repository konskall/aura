[CmdletBinding(DefaultParameterSetName = 'Region')]
param(
    [ValidateSet('mock','codex','groq')][string]$Provider = 'codex',
    [ValidateSet('local','groq')][string]$SpeechProvider = 'local',
    [Parameter(Mandatory, ParameterSetName = 'Region')][ValidatePattern('^[a-zA-Z0-9-]{2,40}$')][string]$Region,
    [Parameter(Mandatory, ParameterSetName = 'Endpoint')][ValidatePattern('^https://')][string]$Endpoint,
    [switch]$KeyFromClipboard,
    [switch]$GroqKeyFromClipboard,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'

if ($PSCmdlet.ParameterSetName -eq 'Endpoint') {
    $uri = [Uri]$Endpoint
    $allowedHost = $uri.Host.EndsWith('.tts.speech.microsoft.com', [StringComparison]::OrdinalIgnoreCase) -or
        $uri.Host.EndsWith('.cognitiveservices.azure.com', [StringComparison]::OrdinalIgnoreCase)
    $path = $uri.AbsolutePath.TrimEnd('/')
    if (-not $allowedHost -or $uri.Scheme -ne 'https' -or ($path -and $path -ne '/cognitiveservices/v1')) {
        throw 'Endpoint must be an Azure Speech *.tts.speech.microsoft.com or *.cognitiveservices.azure.com endpoint.'
    }
}

$pointer = [IntPtr]::Zero
$groqPointer = [IntPtr]::Zero
$plainKey = $null
$groqPlainKey = $null
$groqInjected = $false
try {
    if ($KeyFromClipboard) {
        Add-Type -AssemblyName System.Windows.Forms
        if ($GroqKeyFromClipboard) { $null = Read-Host 'Copy the new Azure Speech key, then press Enter' }
        $plainKey = [Windows.Forms.Clipboard]::GetText().Trim()
        [Windows.Forms.Clipboard]::Clear()
    } else {
        $secure = Read-Host 'New Azure Speech key (input is hidden)' -AsSecureString
        if ($secure.Length -lt 32) {
            throw 'Azure Speech key input was too short. Use -KeyFromClipboard if console paste enters one character.'
        }
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer).Trim()
    }
    if ($plainKey.Length -lt 32 -or $plainKey.Length -gt 256 -or $plainKey -match '\s') {
        throw 'Clipboard/input does not contain a plausible Azure Speech key.'
    }
    $env:AZURE_SPEECH_KEY = $plainKey
    $plainKey = $null
    $needsGroq = $Provider -eq 'groq' -or $SpeechProvider -eq 'groq'
    if ($needsGroq -and -not $env:GROQ_API_KEY) {
        if ($GroqKeyFromClipboard) {
            Add-Type -AssemblyName System.Windows.Forms
            $null = Read-Host 'Copy the new Groq key, then press Enter'
            $groqPlainKey = [Windows.Forms.Clipboard]::GetText().Trim()
            [Windows.Forms.Clipboard]::Clear()
        } else {
            $groqSecure = Read-Host 'New Groq key (input is hidden)' -AsSecureString
            if ($groqSecure.Length -lt 24) {
                throw 'Groq key input was too short. Use -GroqKeyFromClipboard if console paste enters one character.'
            }
            $groqPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($groqSecure)
            $groqPlainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($groqPointer).Trim()
        }
        if ($groqPlainKey -notmatch '^gsk_[A-Za-z0-9_-]{20,}$' -or $groqPlainKey.Length -gt 256) {
            throw 'Clipboard/input does not contain a plausible Groq key.'
        }
        $env:GROQ_API_KEY = $groqPlainKey
        $groqPlainKey = $null
        $groqInjected = $true
    }
    if ($PSCmdlet.ParameterSetName -eq 'Region') {
        $env:AZURE_SPEECH_REGION = $Region.ToLowerInvariant()
        Remove-Item Env:AZURE_SPEECH_ENDPOINT -ErrorAction SilentlyContinue
    } else {
        $env:AZURE_SPEECH_ENDPOINT = $Endpoint
        Remove-Item Env:AZURE_SPEECH_REGION -ErrorAction SilentlyContinue
    }
    & (Join-Path $PSScriptRoot 'dev-simulator.ps1') -Provider $Provider -SpeechProvider $SpeechProvider `
        -TtsProvider azure -NoBrowser:$NoBrowser
} finally {
    if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
    if ($groqPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($groqPointer) }
    $plainKey = $null
    $groqPlainKey = $null
    Remove-Item Env:AZURE_SPEECH_KEY -ErrorAction SilentlyContinue
    if ($groqInjected) { Remove-Item Env:GROQ_API_KEY -ErrorAction SilentlyContinue }
}

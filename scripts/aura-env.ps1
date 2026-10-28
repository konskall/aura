function Import-AuraLocalEnv {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }

    $allowed = @{
        GROQ_API_KEY = $true
        AZURE_SPEECH_KEY = $true
        AZURE_SPEECH_REGION = $true
        AZURE_SPEECH_ENDPOINT = $true
        AURA_GROQ_MODEL = $true
    }
    $lineNumber = 0
    foreach ($rawLine in Get-Content -LiteralPath $Path -Encoding UTF8) {
        ++$lineNumber
        $line = $rawLine.Trim()
        if (-not $line -or $line.StartsWith('#')) { continue }
        if ($line -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') {
            throw "Invalid .env syntax on line $lineNumber. Expected NAME=value."
        }
        $name = $Matches[1]
        $value = $Matches[2].Trim()
        if (-not $allowed.ContainsKey($name)) {
            throw "Unsupported AURA .env setting '$name' on line $lineNumber."
        }
        if ($value.Length -ge 2 -and (($value[0] -eq '"' -and $value[-1] -eq '"') -or
                ($value[0] -eq "'" -and $value[-1] -eq "'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        if ($value -match '[\r\n\x00]') { throw "Invalid control character in .env line $lineNumber." }
        if ($value -and -not [Environment]::GetEnvironmentVariable($name, 'Process')) {
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

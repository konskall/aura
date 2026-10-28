[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'aura-env.ps1')

$names = @('GROQ_API_KEY','AZURE_SPEECH_KEY','AZURE_SPEECH_REGION','AZURE_SPEECH_ENDPOINT','AURA_GROQ_MODEL')
$saved = @{}
foreach ($name in $names) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$testFile = Join-Path ([IO.Path]::GetTempPath()) ("aura-env-{0}.env" -f [Guid]::NewGuid().ToString('N'))

try {
    [IO.File]::WriteAllText($testFile, @'
# test-only values
GROQ_API_KEY=gsk_test_only_abcdefghijklmnopqrstuvwxyz
AZURE_SPEECH_KEY=
AZURE_SPEECH_REGION=swedencentral
AURA_GROQ_MODEL='test/model'
'@, [Text.UTF8Encoding]::new($false))
    Import-AuraLocalEnv -Path $testFile
    if ($env:GROQ_API_KEY -ne 'gsk_test_only_abcdefghijklmnopqrstuvwxyz') { throw 'Groq value was not imported.' }
    if ($env:AZURE_SPEECH_KEY) { throw 'An empty value must remain unset.' }
    if ($env:AZURE_SPEECH_REGION -ne 'swedencentral') { throw 'Azure region was not imported.' }
    if ($env:AURA_GROQ_MODEL -ne 'test/model') { throw 'Quoted value was not normalized.' }

    [Environment]::SetEnvironmentVariable('AZURE_SPEECH_REGION', 'shell-wins', 'Process')
    Import-AuraLocalEnv -Path $testFile
    if ($env:AZURE_SPEECH_REGION -ne 'shell-wins') { throw 'Process environment must override .env.' }

    [IO.File]::WriteAllText($testFile, "UNSUPPORTED_SECRET=value`n", [Text.UTF8Encoding]::new($false))
    try {
        Import-AuraLocalEnv -Path $testFile
        throw 'Unsupported setting was accepted.'
    } catch {
        if ($_.Exception.Message -eq 'Unsupported setting was accepted.') { throw }
    }
    Write-Host 'AURA local .env loader tests passed.'
} finally {
    Remove-Item -LiteralPath $testFile -Force -ErrorAction SilentlyContinue
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}

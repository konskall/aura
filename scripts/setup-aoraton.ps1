[CmdletBinding()]
param(
    # The aoraton web app's own client config; read only, never modified.
    [string]$Source = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'incognitochat\services\supabaseConfig.ts')
)

# Writes the git-ignored config\aoraton.local.json with the Supabase URL and the
# public anon key that aoraton.com already ships to every browser. With it,
# dev-simulator.ps1 (and run.bat) sign in to your own aoraton.com account.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$target = Join-Path $repo 'config\aoraton.local.json'

$url = $null
$key = $null
if (Test-Path -LiteralPath $Source) {
    $text = Get-Content -LiteralPath $Source -Raw -Encoding UTF8
    if ($text -match "SUPABASE_URL\s*=\s*['""]([^'""]+)['""]") { $url = $Matches[1] }
    if ($text -match "SUPABASE_ANON_KEY\s*=\s*['""]([^'""]+)['""]") { $key = $Matches[1] }
    if ($url -and $key) { Write-Host "Read the public Supabase settings from $Source" }
}
if (-not $url) { $url = Read-Host 'Supabase URL of aoraton.com (https://<project>.supabase.co)' }
if (-not $key) { $key = Read-Host 'Public anon key of aoraton.com' }

$url = $url.Trim().TrimEnd('/')
$key = $key.Trim()
if ($url -notmatch '^https://[A-Za-z0-9.-]+$') { throw 'The Supabase URL must look like https://<project>.supabase.co' }
if ($key -notmatch '^[A-Za-z0-9._-]{20,}$') { throw 'The anon key does not look like a Supabase key.' }

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
$json = [ordered]@{ url = $url; anon_key = $key } | ConvertTo-Json
[IO.File]::WriteAllText($target, $json + "`n", (New-Object Text.UTF8Encoding($false)))
Write-Host "Saved $target (git-ignored)."
Write-Host "Next: run.bat, then Aoraton > Sign in with Google. If the browser stays on aoraton.com,"
Write-Host "add http://127.0.0.1:4779/** to Supabase > Authentication > URL Configuration > Redirect URLs."

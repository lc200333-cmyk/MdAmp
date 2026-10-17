[CmdletBinding()]
param([string]$File = (Join-Path $PSScriptRoot '..\VERSION'), [switch]$Increment)
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath $File -Raw).Trim()
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid VERSION: expected major.minor.patch' }
if ($Increment) {
    $parts = $version.Split('.')
    $patch = [long]::Parse($parts[2])
    if ($patch -eq [long]::MaxValue) { throw 'Patch version overflow' }
    $version = $parts[0] + '.' + $parts[1] + '.' + ($patch + 1).ToString([Globalization.CultureInfo]::InvariantCulture)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($File), $version + "`n", [Text.UTF8Encoding]::new($false))
}
Write-Output $version

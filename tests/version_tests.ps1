$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('mdamp-version-' + [guid]::NewGuid().ToString('N') + '.txt')
$script = Join-Path $PSScriptRoot '..\tools\version.ps1'
try {
    [IO.File]::WriteAllText($fixture, "0.1.9`n")
    if ((& $script -File $fixture -Increment) -ne '0.1.10') { throw 'Patch increment failed' }
    if ((& $script -File $fixture) -ne '0.1.10') { throw 'Version read failed' }
    [IO.File]::WriteAllText($fixture, 'invalid')
    $rejected = $false
    try { & $script -File $fixture -Increment | Out-Null } catch { $rejected = $true }
    if (-not $rejected -or [IO.File]::ReadAllText($fixture) -ne 'invalid') { throw 'Invalid version changed' }
    Write-Output 'Version read/increment/rejection checks passed.'
} finally {
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Force }
}

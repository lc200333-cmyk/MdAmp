[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer not found. Install Desktop development with C++, Windows SDK and CMake tools.'
}
$instances = & $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'vswhere failed.' }

$selected = $null
foreach ($instance in $instances) {
    $installation = $instance.installationPath
    $vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
    $cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninja = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    $compilerRoot = Join-Path $installation 'VC\Tools\MSVC'
    $compilers = @(Get-ChildItem -LiteralPath $compilerRoot -Directory -ErrorAction SilentlyContinue | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe')
    })
    if ($compilers.Count -gt 0 -and (Test-Path -LiteralPath $vcvars) -and (Test-Path -LiteralPath $cmake) -and (Test-Path -LiteralPath $ninja)) {
        $selected = @{ Name=$instance.displayName; Vcvars=$vcvars; CMake=$cmake; Ninja=$ninja }
        break
    }
}
if ($null -eq $selected) {
    throw 'No complete x64 MSVC/CMake/Ninja toolchain found. Modify Visual Studio: install Desktop development with C++ and C++ CMake tools for Windows.'
}
Write-Host ('Toolchain: ' + $selected.Name)
$helper = Join-Path $PSScriptRoot 'build-native.cmd'
& $helper $selected.Vcvars $selected.CMake $selected.Ninja $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build or tests failed (exit $LASTEXITCODE)." }

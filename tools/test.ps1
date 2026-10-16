<#
.SYNOPSIS
  Builds and runs the pure-logic tests in tests\ (no game files, no ReXGlue runtime) with Visual Studio's clang++.

.EXAMPLE
  .\tools\test.ps1                      # build and run every test
  .\tools\test.ps1 -Filter user_data    # only tests whose name matches
#>
param(
    [string]$Filter = '',
    [string]$BuildDir  # relative to the repository root; default: out\tests
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-common.ps1')
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'out\tests' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }

$Toolchain = Get-BuildToolchain
$SavedEnv = Save-BuildEnvironment
$Failure = $null
try {
    Import-BuildEnvironment $Toolchain
    & $Toolchain.CMake -S (Join-Path $Root 'tests') -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
        "-DCMAKE_CXX_COMPILER:FILEPATH=$($Toolchain.ClangXX)" "-DCMAKE_LINKER:FILEPATH=$($Toolchain.LldLink)" `
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($Toolchain.Ninja)" | Out-Host
    if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
    & $Toolchain.CMake --build $BuildDir --target crackdown_tests | Out-Host
    if ($LASTEXITCODE) { throw "Test build failed ($LASTEXITCODE)" }
    $ctest = Join-Path (Split-Path $Toolchain.CMake) 'ctest.exe'
    $ctestArgs = @('--test-dir', $BuildDir, '--output-on-failure')
    if ($Filter) { $ctestArgs += @('-R', $Filter) }
    & $ctest @ctestArgs | Out-Host
    if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
} catch {
    $Failure = $_.Exception.Message
} finally {
    Restore-BuildEnvironment $SavedEnv
}
if ($Failure) {
    Write-Host "ERROR: $Failure" -ForegroundColor Red
    exit 1
}

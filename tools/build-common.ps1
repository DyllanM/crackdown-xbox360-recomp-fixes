# Helpers shared by build.ps1, codegen.ps1 and the ReXGlue runtime scripts.

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-TextSha256([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))) -replace '-', '' }
    finally { $sha.Dispose() }
}

function Get-NormalizedPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Test-SamePath([string]$Path, [string]$Other) {
    return [bool]$Path -and [bool]$Other -and ((Get-NormalizedPath $Path) -eq (Get-NormalizedPath $Other))
}

function Test-PathUnder([string]$Path, [string]$Folder) {
    return [bool]$Path -and [bool]$Folder -and
        (Get-NormalizedPath $Path).StartsWith((Get-NormalizedPath $Folder) + '\', [StringComparison]::OrdinalIgnoreCase)
}

# KEY=VALUE lines in file order.
function Read-KeyValueFile([string]$Path) {
    $values = [ordered]@{}
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*([A-Za-z0-9_./-]+)\s*=\s*(.*?)\s*$') { $values[$Matches[1]] = $Matches[2] }
    }
    return $values
}

# The keys added, changed or removed between two name/value sets.
function Get-ValueChanges($Old, $New) {
    $changes = @()
    foreach ($key in $New.Keys) {
        if (-not $Old.Contains($key)) { $changes += "$key added" }
        elseif ($Old[$key] -ne $New[$key]) { $changes += "$key changed" }
    }
    foreach ($key in $Old.Keys) {
        if (-not $New.Contains($key)) { $changes += "$key removed" }
    }
    return $changes
}

function Get-BuildToolchain {
    if (($env:PATHEXT -split ';') -notcontains '.EXE') {
        $env:PATHEXT = ".EXE;$env:PATHEXT"
        Write-Host 'Windows build environment: restored .EXE in PATHEXT.'
    }
    if ($script:BuildToolchain) { return $script:BuildToolchain }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) not found.' }
    $query = @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        'Microsoft.VisualStudio.Component.VC.Llvm.Clang')
    $vsRoot = "$(& $vswhere @query -property installationPath)".Trim()
    if (-not $vsRoot) { throw 'Visual Studio with the C++ x64 tools and C++ Clang tools is required.' }
    $vsVersion = "$(& $vswhere @query -property installationVersion)".Trim()
    $llvm = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin'
    $toolchain = @{
        VsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
        LlvmBin  = $llvm
        Clang    = Join-Path $llvm 'clang.exe'
        ClangXX  = Join-Path $llvm 'clang++.exe'
        LldLink  = Join-Path $llvm 'lld-link.exe'
        CMake    = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        Ninja    = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    }
    foreach ($tool in @($toolchain.VsDevCmd, $toolchain.Clang, $toolchain.ClangXX, $toolchain.LldLink, $toolchain.CMake, $toolchain.Ninja)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "Required tool not found: $tool" }
    }
    $clangVersion = "$(& $toolchain.ClangXX --version | Select-Object -First 1)".Trim()
    $lldVersion = "$(& $toolchain.LldLink --version | Select-Object -First 1)".Trim()
    $toolchain.Id = "Visual Studio $vsVersion; $clangVersion; $lldVersion"
    $script:BuildToolchain = $toolchain
    return $toolchain
}

function Save-BuildEnvironment {
    return [Environment]::GetEnvironmentVariables()
}

function Restore-BuildEnvironment($Saved) {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $Saved.Contains($name)) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
    }
    foreach ($name in $Saved.Keys) { Set-Item -LiteralPath "Env:$name" -Value $Saved[$name] }
}

function Import-BuildEnvironment($Toolchain) {
    # Keep MSYS2's gcc, cmake and ninja out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' -and $_ -notmatch '^C:\\msys64' }) -join ';'
    # VsDevCmd calls vswhere.exe by bare name; cmd does not resolve it from the current folder.
    $env:PATH = "$env:PATH;$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer')"
    foreach ($line in (cmd.exe /c "`"$($Toolchain.VsDevCmd)`" -arch=x64 -host_arch=x64 -no_logo >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }
    $env:PATH = "$($Toolchain.LlvmBin);$(Split-Path $Toolchain.CMake);$(Split-Path $Toolchain.Ninja);$env:PATH"
    $env:CC = $Toolchain.Clang
    $env:CXX = $Toolchain.ClangXX
}

function Read-CMakeCache([string]$BuildDir) {
    $entries = @{}
    $path = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $entries }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ($line -match '^([^/#:=]+):[A-Z]+=(.*)$') { $entries[$Matches[1]] = $Matches[2] }
    }
    return $entries
}

# Why an existing CMake cache cannot be reused for this configuration and SDK; an empty list means it can.
function Get-CMakeCacheProblems($Cache, [string]$Config, $Toolchain, [string]$SdkRoot) {
    $problems = @()
    if ($Cache['CMAKE_BUILD_TYPE'] -ne $Config) { $problems += "CMAKE_BUILD_TYPE is '$($Cache['CMAKE_BUILD_TYPE'])', not $Config" }
    if (-not (Test-SamePath $Cache['CMAKE_CXX_COMPILER'] $Toolchain.ClangXX)) { $problems += "CMAKE_CXX_COMPILER is '$($Cache['CMAKE_CXX_COMPILER'])'" }
    if (-not (Test-SamePath $Cache['CMAKE_LINKER'] $Toolchain.LldLink)) { $problems += "CMAKE_LINKER is '$($Cache['CMAKE_LINKER'])'" }
    foreach ($name in 'rexglue_DIR', 'fmt_DIR', 'spdlog_DIR', 'SDL3_DIR', 'utf8cpp_DIR') {
        if ($Cache.ContainsKey($name) -and -not (Test-PathUnder $Cache[$name] $SdkRoot)) {
            $problems += "$name is outside tools\rexglue: $($Cache[$name])"
        }
    }
    return $problems
}

# True when the generated Ninja graph runs ReXGlue codegen.
function Test-BuildGraphRunsCodegen([string]$BuildDir) {
    $ninja = Join-Path $BuildDir 'build.ninja'
    return (Test-Path -LiteralPath $ninja -PathType Leaf) -and
        [bool](Select-String -LiteralPath $ninja -Pattern 'rexglue(\.exe)?["'']?\s+codegen\b' -Quiet)
}

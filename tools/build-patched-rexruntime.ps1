<#
.SYNOPSIS
  Builds ReXGlue's rexruntime and Xenos GPU plugin from the pinned SDK source with the patches in
  patches\rexglue-sdk, and installs the requested configuration into tools\rexglue\win-amd64.

.DESCRIPTION
  tools\build.ps1 runs this by itself when the installed files are not current. Run it by hand only to diagnose
  (-Check) or to put the stock release files back (-Restore). With no patches the installed files are the stock
  release, and no build is needed.

.EXAMPLE
  .\tools\build-patched-rexruntime.ps1           # build and install the patched files
  .\tools\build-patched-rexruntime.ps1 -Check    # report whether the installed files are current
  .\tools\build-patched-rexruntime.ps1 -Check -Staged build   # also require the DLLs in a build folder to be those files
  .\tools\build-patched-rexruntime.ps1 -Restore  # put the stock release files back
  .\tools\build-patched-rexruntime.ps1 -Codegen  # build the codegen tool from the codegen patches (tools\codegen.ps1 runs this)
#>
param(
    [string]$WorkDir,  # ReXGlue source and build folder; default: out\rexglue-sdk-src
    [switch]$Check,
    [string]$Staged,  # with -Check: a build folder whose DLLs must be the installed ones
    [switch]$Restore,
    [switch]$Codegen,
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-common.ps1')
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
. (Join-Path $PSScriptRoot 'codegen-inputs.ps1')
$Layout = Get-RexglueLayout $Root $Config
if (-not (Test-Path -LiteralPath $Layout.SdkConfigDir)) { throw 'Missing tools\rexglue (run tools\codegen.ps1 first)' }

function Invoke-Native([string]$What, [scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE) { throw "$What failed ($LASTEXITCODE)" }
}

# Windows checks out libmspack's symlinks as text files; the build needs the files they point to.
function Repair-SymlinkCopies([string]$Folder) {
    foreach ($line in (git -C $Folder ls-files -s)) {
        if ($line -notmatch '^120000 \S+ \d\t(.+)$') { continue }
        $file = Join-Path $Folder $Matches[1]
        $item = Get-Item -LiteralPath $file
        if ($item.LinkType -or $item.Length -gt 260) { continue }
        $target = Join-Path (Split-Path $file) (Get-Content -LiteralPath $file -Raw).Trim()
        if (Test-Path -LiteralPath $target -PathType Leaf) { Copy-Item -LiteralPath $target $file -Force }
    }
}

function Initialize-RexglueSource($Layout, [string]$Src, $Patches) {
    $baseline = $Layout.Baseline
    if (-not (Test-Path -LiteralPath (Join-Path $Src '.git'))) {
        Write-Host "Fetching ReXGlue $($baseline['SDK_TAG']) from $($baseline['SDK_REPO']) (network)..."
        New-Item -ItemType Directory -Force (Split-Path $Src) | Out-Null
        Invoke-Native 'git clone' { git clone --branch $baseline['SDK_TAG'] --depth 1 --recurse-submodules --shallow-submodules --jobs 8 $baseline['SDK_REPO'] $Src }
    }
    $commit = "$(git -C $Src rev-parse HEAD)".Trim()
    if ($commit -ne $baseline['SDK_COMMIT']) {
        throw "$Src is at $commit; expected $($baseline['SDK_TAG']) $($baseline['SDK_COMMIT']) (delete the folder to fetch it again)"
    }
    # The build tree lives in out\ and is kept; everything else untracked or modified is reset to the pinned commit.
    Invoke-Native 'git checkout' { git -C $Src checkout -- . }
    Invoke-Native 'git clean' { git -C $Src clean -fdq -e out }
    Repair-SymlinkCopies (Join-Path $Src 'thirdparty\libmspack')
    foreach ($patch in $Patches) {
        & git -C $Src apply --check $patch.Path
        if ($LASTEXITCODE) { throw "patches\rexglue-sdk\$($patch.Name) no longer applies to ReXGlue $($baseline['SDK_TAG'])" }
        Invoke-Native "git apply $($patch.Name)" { git -C $Src apply $patch.Path }
    }
}

function Build-RexglueRuntime($Layout, [string]$Src, $Toolchain, [int]$Jobs) {
    $flags = "$RexglueRuntimeBuildFlags -ffile-prefix-map=$($Src.Replace('\', '/'))=."
    $parallel = @(if ($Jobs -gt 0) { '--parallel', $Jobs } else { '--parallel' })
    Push-Location $Src
    try {
        Invoke-Native 'ReXGlue configure' {
            & $Toolchain.CMake --preset $RexglueRuntimePreset `
                "-DCMAKE_C_COMPILER:FILEPATH=$($Toolchain.Clang)" "-DCMAKE_CXX_COMPILER:FILEPATH=$($Toolchain.ClangXX)" `
                "-DCMAKE_LINKER:FILEPATH=$($Toolchain.LldLink)" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($Toolchain.Ninja)" `
                "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags"
        }
        foreach ($config in @($Layout.Config)) {
            Invoke-Native "ReXGlue build ($config)" {
                & $Toolchain.CMake --build "out/build/$RexglueRuntimePreset" --config $config --target $RexglueRuntimeTargets @parallel
            }
        }
    } finally { Pop-Location }
}

function Build-RexglueCodegen([string]$Src, $Toolchain, [int]$Jobs, [string]$Dest) {
    $flags = "$RexglueRuntimeBuildFlags -ffile-prefix-map=$($Src.Replace('\', '/'))=."
    $parallel = @(if ($Jobs -gt 0) { '--parallel', $Jobs } else { '--parallel' })
    Push-Location $Src
    try {
        Invoke-Native 'ReXGlue configure' {
            & $Toolchain.CMake --preset $RexglueRuntimePreset `
                "-DCMAKE_C_COMPILER:FILEPATH=$($Toolchain.Clang)" "-DCMAKE_CXX_COMPILER:FILEPATH=$($Toolchain.ClangXX)" `
                "-DCMAKE_LINKER:FILEPATH=$($Toolchain.LldLink)" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($Toolchain.Ninja)" `
                "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags"
        }
        Invoke-Native 'ReXGlue codegen build' {
            & $Toolchain.CMake --build "out/build/$RexglueRuntimePreset" --config Release --target rexglue @parallel
        }
    } finally { Pop-Location }
    $destDir = Split-Path $Dest
    New-Item -ItemType Directory -Force $destDir | Out-Null
    # rexglue.exe imports rexruntime.dll, so the matching build goes next to it. The exe goes last: it marks the install.
    foreach ($name in @('rexruntime.dll', 'rexglue.exe')) {
        $built = Join-Path $Src "out\win-amd64\Release\$name"
        if (-not (Test-Path -LiteralPath $built -PathType Leaf)) { throw "ReXGlue build output is missing: $built" }
        $target = Join-Path $destDir $name
        Copy-Item -LiteralPath $built -Destination "$target.tmp" -Force
        if ((Get-FileSha256 "$target.tmp") -ne (Get-FileSha256 $built)) { throw "The copy of $name does not match its build output" }
        Move-InstalledFile "$target.tmp" $target
    }
    Write-Host "Patched ReXGlue codegen installed: $Dest"
}

# The build outputs of every managed file, checked against the stock files and for embedded build paths.
function Get-RexglueBuiltFiles($Layout, [string]$Src) {
    $built = @{}
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
        $path = Join-Path $Src ('out\win-amd64\{0}\{1}' -f $file.Config, (Split-Path $file.Leaf -Leaf))
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "ReXGlue build output is missing: $path" }
        $stock = $Layout.Baseline["SHA256_$($file.Leaf)"]
        if ($file.IsRuntime -and (Get-FileSha256 $path) -eq $stock) {
            throw "$($file.Leaf) came out identical to the stock release; the patch was not built in"
        }
        $built[$file.Leaf] = $path
    }
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config | Where-Object { $_.Config -eq 'Release' -and $_.Leaf -like 'bin/*' })) {
        $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($built[$file.Leaf]))
        foreach ($needle in @($Src, $Src.Replace('\', '/'))) {
            if ($text.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "$(Split-Path $file.Leaf -Leaf) embeds the build folder"
            }
        }
    }
    return $built
}

function Install-RexglueRuntime($Layout, $Built, $Inputs) {
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
        $dest = Join-Path $Layout.SdkDir $file.Leaf
        Copy-Item -LiteralPath $Built[$file.Leaf] -Destination "$dest.tmp" -Force
        if ((Get-FileSha256 "$dest.tmp") -ne (Get-FileSha256 $Built[$file.Leaf])) {
            throw "The copy of $($file.Leaf) does not match its build output"
        }
        Move-InstalledFile "$dest.tmp" $dest
    }
    Write-RexglueStamp $Layout $Inputs
    $problems = @(Get-RexglueInstallProblems $Layout 'patched')
    if ($problems.Count) { throw "The installed runtime and GPU plugin do not match patches\rexglue-sdk: $($problems -join '; ')" }
    Write-Host "Patched ReXGlue runtime and GPU plugin installed ($(Get-RexglueSummary $Layout))."
}

if ($Check) {
    $problems = @(Get-RexglueInstallProblems $Layout)
    if ($Staged) { $problems += @(Get-RexglueStagingProblems $Layout $Staged $Config) }
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
    if ($problems.Count) { exit 1 }
    Write-Host "ReXGlue patches: current ($(Get-RexglueSummary $Layout))"
    exit 0
}

$patches = @(Get-RexglueRuntimePatches $Layout.PatchDir)
$src = if (-not $WorkDir) { Join-Path $Root 'out\rexglue-sdk-src' } elseif ([IO.Path]::IsPathRooted($WorkDir)) { $WorkDir } else { Join-Path $Root $WorkDir }

if ($Codegen) {
    $identity = Get-CodegenToolIdentity $Root
    if ($identity -eq 'stock') { throw 'No patch in patches\rexglue-sdk changes the codegen tool' }
    $toolchain = Get-BuildToolchain
    Import-BuildEnvironment $toolchain
    Initialize-RexglueSource $Layout $src $patches
    Build-RexglueCodegen $src $toolchain $Jobs (Get-PatchedCodegenPath $Root $identity)
    exit 0
}

if ($Restore -or -not $patches.Count) {
    Install-StockRexglueRuntime $Layout
    exit 0
}

$toolchain = Get-BuildToolchain
$inputs = Get-RexglueRuntimeInputs $Layout 'patched'
Import-BuildEnvironment $toolchain
Remove-Item -LiteralPath $Layout.StampFile -ErrorAction SilentlyContinue
Initialize-RexglueSource $Layout $src $patches
Build-RexglueRuntime $Layout $src $toolchain $Jobs
Install-RexglueRuntime $Layout (Get-RexglueBuiltFiles $Layout $src) $inputs

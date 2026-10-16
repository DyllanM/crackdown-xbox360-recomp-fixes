<#
.SYNOPSIS
  Builds build\crackdown.exe with Visual Studio's clang++, lld-link, CMake and Ninja. -Run launches the game.
  Guest code is never regenerated here: run tools\codegen.ps1 when it is stale. The ReXGlue runtime and GPU plugin
  are checked before the build and rebuilt or restored when they are not current.

.EXAMPLE
  .\tools\build.ps1              # incremental build only
  .\tools\build.ps1 -Clean       # clean configure and build
  .\tools\build.ps1 -Run         # build, then launch the game (the launcher shows unless disabled in its settings)
  .\tools\build.ps1 -Run -SkipLauncher  # build, then start the game directly, without the launcher
  .\tools\build.ps1 -Target crackdown_physical_heap_stress   # build another target
  .\tools\build.ps1 -Run -NoIntro  # launch without the startup logo movies
  .\tools\build.ps1 -Clean -Run
  .\tools\build.ps1 -Config Release -BuildDir build-release   # separate tree

Exit codes: 0 success, 1 configure, build or runtime check failed, 2 a prerequisite is missing or stale.
#>
param(
    [switch]$Clean,
    [switch]$Run,
    [switch]$SkipLauncher,  # with -Run: start the game directly instead of showing the launcher
    [string[]]$Target,  # CMake targets to build; default: all
    [switch]$NoIntro,  # launches with --crackdown_skip_intro=true (the launcher's Gameplay > Skip intro toggle)
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [int]$Jobs = 0,
    [string]$BuildDir  # relative to the repository root; default: build
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-common.ps1')
. (Join-Path $PSScriptRoot 'codegen-inputs.ps1')
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')
$BuildDir = if (-not $BuildDir) { Join-Path $Root 'build' } elseif ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
$GameData = Join-Path $Root 'game\assets'
$UserData = Join-Path $BuildDir 'user_data'
$Exe = Join-Path $BuildDir 'crackdown.exe'
$Layout = Get-RexglueLayout $Root $Config
$SdkVersion = $Layout.Baseline['SDK_VERSION']

function Stop-Build([int]$Code, [string]$Message) {
    Write-Host ''
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit $Code
}

function Stop-GuestCodeStale([string[]]$Reasons) {
    Write-Host 'Guest code: stale'
    Write-Host "Reason: $($Reasons -join '; ')"
    Write-Host 'Run:'
    Write-Host '  .\tools\codegen.ps1'
    Write-Host "  .\tools\build.ps1$(if ($Run) { ' -Run' })"
    exit 2
}

function Get-CrackdownOutputProblems {
    $problems = @()
    foreach ($name in @('crackdown.exe', 'crackdown.toml') + (Get-RexglueStagedNames $Config)) {
        $path = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $problems += "missing: $path" }
    }
    return $problems
}

function Get-PreRunProblems {
    $problems = @()
    $guest = Get-CodegenFreshness $Root $SdkVersion
    if (-not $guest.Current) { $problems += "guest code is stale: $($guest.Reasons -join '; ')" }
    $problems += @(Get-RexglueInstallProblems $Layout | ForEach-Object { "ReXGlue runtime: $_" })
    $problems += @(Get-RexglueStagingProblems $Layout $BuildDir $Config)
    $problems += @(Get-CrackdownOutputProblems)
    if (Test-BuildGraphRunsCodegen $BuildDir) { $problems += 'the build graph runs ReXGlue codegen' }
    return $problems
}

$Xex = Join-Path $GameData 'Default.xex'
if (-not (Test-Path -LiteralPath $Xex)) { Stop-Build 2 "Missing $Xex. Copy the disc contents into game\assets." }
if ((Get-FileSha256 $Xex) -ne $CrackdownXexSha256) { Stop-Build 2 "Unsupported $Xex (expected SHA-256 $CrackdownXexSha256)." }
$installedSdk = Get-InstalledRexglueVersion $Root
if (-not $installedSdk) { Stop-Build 2 'Missing tools\rexglue. Run tools\codegen.ps1 first.' }
if ($installedSdk -ne $SdkVersion) {
    Stop-Build 2 "tools\rexglue is ReXGlue $installedSdk; the baseline is $SdkVersion. Delete tools\rexglue, then run tools\codegen.ps1."
}
if (-not (Test-Path (Join-Path $Root 'generated\default\sources.cmake'))) { Stop-Build 2 'Missing generated\default. Run tools\codegen.ps1 first.' }

$guest = Get-CodegenFreshness $Root $SdkVersion
if (-not $guest.Current) { Stop-GuestCodeStale $guest.Reasons }
Write-Host 'Guest code: current'

try { $Toolchain = Get-BuildToolchain } catch { Stop-Build 2 $_.Exception.Message }
try { Update-RexglueRuntime $Layout $Jobs } catch { Stop-Build 1 $_.Exception.Message }

$SavedEnv = Save-BuildEnvironment
$Failure = $null
try {
    Import-BuildEnvironment $Toolchain
    if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
        Write-Host 'Cleaning build (keeping build\user_data)...'
        Get-ChildItem -LiteralPath $BuildDir -Force | Where-Object { $_.Name -ne 'user_data' } | Remove-Item -Recurse -Force
    }

    $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    $cache = Read-CMakeCache $BuildDir
    $problems = @(Get-CMakeCacheProblems $cache $Config $Toolchain $Layout.SdkDir)
    $configured = (Test-Path -LiteralPath (Join-Path $BuildDir 'build.ninja')) -and ($cache.Count -gt 0) -and ($problems.Count -eq 0)
    if (-not $configured) {
        if ($problems.Count -and (Test-Path -LiteralPath $cacheFile)) {
            Write-Host "Reconfiguring: $($problems -join '; ')"
            Remove-Item -LiteralPath $cacheFile -Force
        }
        Write-Host 'Configuring...'
        & $Toolchain.CMake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
            "-DCMAKE_CXX_COMPILER:FILEPATH=$($Toolchain.ClangXX)" "-DCMAKE_LINKER:FILEPATH=$($Toolchain.LldLink)" `
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($Toolchain.Ninja)" "-Drexglue_DIR:PATH=$($Layout.SdkConfigDir)"
        if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
        $problems = @(Get-CMakeCacheProblems (Read-CMakeCache $BuildDir) $Config $Toolchain $Layout.SdkDir)
        if ($problems.Count) { throw "CMake is not configured for this build: $($problems -join '; ')" }
    }
    if (Test-BuildGraphRunsCodegen $BuildDir) { throw 'The build graph runs ReXGlue codegen. Only tools\codegen.ps1 may run it.' }

    Write-Host "Building Crackdown $Config..."
    $parallel = if ($Jobs -gt 0) { @('--parallel', $Jobs) } else { @() }
    $targets = @(if ($Target) { '--target'; @($Target | ForEach-Object { $_ -split ',' }) })
    & $Toolchain.CMake --build $BuildDir @targets @parallel
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

    Sync-RexglueStaging $Layout $BuildDir $Config
    $problems = @(Get-CrackdownOutputProblems) + @(Get-RexglueStagingProblems $Layout $BuildDir $Config)
    if ($problems.Count) { throw "Build output is not usable: $($problems -join '; ')" }
    Write-Host 'Runtime staging: current'
} catch {
    $Failure = $_.Exception.Message
} finally {
    Restore-BuildEnvironment $SavedEnv
}
if ($Failure) { Stop-Build 1 $Failure }

Write-Host ''
Write-Host '===================================================='
Write-Host ' BUILD SUCCESSFUL'
Write-Host '===================================================='
Write-Host "  $Exe"
Write-Host ''
if (-not $Run) { exit 0 }

$problems = @(Get-PreRunProblems)
if ($problems.Count) {
    Write-Host 'PRE-RUN CHECK FAILED: crackdown.exe was not launched.' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
    exit 1
}
$running = Get-Process -Name 'crackdown' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe }
if ($running -and $NoIntro) { Stop-Build 1 "crackdown.exe is already running (PID $($running.Id -join ', ')). Close it so -NoIntro starts a new process." }
if ($running) {
    Write-Warning "crackdown.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    exit 0
}

$gameArgs = @("--game_data_root=`"$GameData`"", "--user_data_root=`"$UserData`"")
if ($SkipLauncher) { $gameArgs += '--crackdown_skip_launcher=true' }
if ($NoIntro) { $gameArgs += '--crackdown_skip_intro=true' }
Write-Host 'Launching:'
Write-Host "  $Exe $($gameArgs -join ' ')"
Write-Host "  working directory: $BuildDir"
Write-Host "  logs: $(Join-Path $BuildDir 'logs')"
Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $BuildDir
exit 0

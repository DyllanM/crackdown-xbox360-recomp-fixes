# ReXGlue runtime and GPU plugin freshness. Without patches the installed files are the stock release; with patches
# they are the files built from the baseline, the build settings and patches\rexglue-sdk.

$RexglueRuntimePlatform = 'windows'
$RexglueRuntimePreset = 'win-amd64'
$RexglueRuntimeBuildFlags = '-march=x86-64-v2'
$RexglueRuntimeTargets = @('rexruntime', 'rexgpu-xenos')
$RexglueRuntimeStampName = 'rexruntime-patch.txt'

function Get-RexglueConfigSuffix([string]$Config) {
    return @{ Debug = 'd'; RelWithDebInfo = 'rd'; Release = '' }[$Config]
}

function Get-RexglueLayout([string]$Root, [string]$Config = 'RelWithDebInfo') {
    $sdkDir = Join-Path $Root 'tools\rexglue\win-amd64'
    $patchDir = Join-Path $Root 'patches\rexglue-sdk'
    $baseline = Read-KeyValueFile (Join-Path $patchDir 'baseline.env')
    $stampName = if ($Config -eq 'RelWithDebInfo') { $RexglueRuntimeStampName } else { "rexruntime-patch-$Config.txt" }
    return @{
        Root         = $Root
        Config       = $Config
        PatchDir     = $patchDir
        Baseline     = $baseline
        SdkDir       = $sdkDir
        SdkConfigDir = Join-Path $sdkDir 'lib\cmake\rexglue'
        SdkZip       = Join-Path $Root "tools\rexglue\rexglue-sdk-$($baseline['SDK_VERSION'])-win-amd64.zip"
        StampFile    = Join-Path $sdkDir $stampName
    }
}

# The patches that apply on Windows, in ordinal name order. The Linux ones belong to build-patched-rexruntime.sh.
function Get-RexglueRuntimePatches([string]$PatchDir) {
    return Get-PlatformPatches $PatchDir $RexglueRuntimePlatform
}

# Runtime, import library and GPU plugin for the requested configuration.
function Get-RexglueManagedFiles([string]$Config = 'RelWithDebInfo') {
    foreach ($config in @($Config)) {
        $suffix = Get-RexglueConfigSuffix $config
        [pscustomobject]@{ Config = $config; Leaf = "bin/rexruntime$suffix.dll"; IsRuntime = $true }
        [pscustomobject]@{ Config = $config; Leaf = "lib/rexruntime$suffix.lib"; IsRuntime = $true }
        [pscustomobject]@{ Config = $config; Leaf = "bin/rexgpu-xenos$suffix.dll"; IsRuntime = $false }
    }
}

# The state the patch folder asks for: 'stock' without patches, otherwise 'patched'.
function Get-RexglueMode($Layout) {
    if (@(Get-RexglueRuntimePatches $Layout.PatchDir).Count) { return 'patched' }
    return 'stock'
}

# Everything the installed files depend on, as ordered name=value pairs. The stock files need no toolchain.
function Get-RexglueRuntimeInputs($Layout, [string]$Mode) {
    if (-not $Mode) { $Mode = Get-RexglueMode $Layout }
    $inputs = [ordered]@{}
    $baselineKeys = [string[]]@($Layout.Baseline.Keys)
    [Array]::Sort($baselineKeys, [StringComparer]::Ordinal)
    foreach ($key in $baselineKeys) { $inputs["baseline.$key"] = $Layout.Baseline[$key] }
    $inputs['mode'] = $Mode
    $inputs['platform'] = $RexglueRuntimePlatform
    if ($Mode -eq 'stock') { return $inputs }
    $inputs['build.preset'] = $RexglueRuntimePreset
    $inputs['build.flags'] = $RexglueRuntimeBuildFlags
    $inputs['build.targets'] = $RexglueRuntimeTargets -join ','
    $inputs['build.configs'] = $Layout.Config
    $inputs['toolchain'] = (Get-BuildToolchain).Id
    foreach ($patch in @(Get-RexglueRuntimePatches $Layout.PatchDir)) { $inputs["patch.$($patch.Name)"] = Get-FileSha256 $patch.Path }
    return $inputs
}

function Get-RexglueInputsSha256($Inputs) {
    return Get-TextSha256 ((@($Inputs.Keys | ForEach-Object { "$_=$($Inputs[$_])" })) -join "`n")
}

function Read-RexglueStamp([string]$StampFile) {
    if (-not (Test-Path -LiteralPath $StampFile -PathType Leaf)) { return $null }
    $stamp = @{ InputsSha256 = ''; Inputs = [ordered]@{}; Files = @{} }
    foreach ($line in [IO.File]::ReadAllLines($StampFile)) {
        if ($line -match '^inputs_sha256=([0-9A-F]{64})$') { $stamp.InputsSha256 = $Matches[1] }
        elseif ($line -match '^input ([^=]+)=(.*)$') { $stamp.Inputs[$Matches[1]] = $Matches[2] }
        elseif ($line -match '^file ([0-9A-F]{64})  (.+)$') { $stamp.Files[$Matches[2]] = $Matches[1] }
    }
    return $stamp
}

function Write-RexglueStamp($Layout, $Inputs) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("inputs_sha256=$(Get-RexglueInputsSha256 $Inputs)")
    foreach ($key in $Inputs.Keys) { $lines.Add("input $key=$($Inputs[$key])") }
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
        $lines.Add("file $(Get-FileSha256 (Join-Path $Layout.SdkDir $file.Leaf))  $($file.Leaf)")
    }
    [IO.File]::WriteAllText($Layout.StampFile, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
}

function Move-InstalledFile([string]$Temp, [string]$Dest) {
    try { Move-Item -LiteralPath $Temp -Destination $Dest -Force }
    catch { throw "Could not replace $Dest (is crackdown.exe still running?): $($_.Exception.Message)" }
}

# What is wrong with the installed runtime and GPU plugin; an empty list means they are current.
function Get-RexglueInstallProblems($Layout, [string]$Mode) {
    if (-not $Mode) { $Mode = Get-RexglueMode $Layout }
    $problems = New-Object System.Collections.Generic.List[string]
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
        if (-not $Layout.Baseline.Contains("SHA256_$($file.Leaf)")) { throw "patches\rexglue-sdk\baseline.env has no SHA256_$($file.Leaf)" }
    }
    $stamp = Read-RexglueStamp $Layout.StampFile
    if (-not $stamp) {
        $problems.Add("$RexglueRuntimeStampName is missing in $($Layout.SdkDir)")
        return $problems.ToArray()
    }
    $inputs = Get-RexglueRuntimeInputs $Layout $Mode
    if ($stamp.InputsSha256 -ne (Get-RexglueInputsSha256 $inputs)) {
        $changes = @(Get-ValueChanges $stamp.Inputs $inputs)
        $detail = if ($changes.Count) { $changes -join ', ' } else { 'the input list' }
        $problems.Add("built from other inputs: $detail")
    }
    $stock = $Mode -eq 'stock'
    foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
        $name = $file.Leaf.Replace('/', '\')
        $path = Join-Path $Layout.SdkDir $file.Leaf
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $problems.Add("missing: $name"); continue }
        if (-not $stamp.Files.ContainsKey($file.Leaf)) { $problems.Add("$name is not in $RexglueRuntimeStampName"); continue }
        $hash = Get-FileSha256 $path
        if ($hash -ne $stamp.Files[$file.Leaf]) { $problems.Add("$name was replaced or modified after it was installed") }
        $isStock = $hash -eq $Layout.Baseline["SHA256_$($file.Leaf)"]
        if ($stock -and -not $isStock) { $problems.Add("$name is not the stock release file") }
        if (-not $stock -and $file.IsRuntime -and $isStock) { $problems.Add("$name is the stock release file") }
    }
    return $problems.ToArray()
}

# Describes the installed files from their stamp.
function Get-RexglueSummary($Layout) {
    $stamp = Read-RexglueStamp $Layout.StampFile
    $patchCount = @($stamp.Inputs.Keys | Where-Object { $_ -like 'patch.*' }).Count
    $what = if ($stamp.Inputs['mode'] -eq 'stock') { 'stock release files' } else { "$patchCount patches" }
    return "$what, fingerprint $($stamp.InputsSha256.Substring(0, 12))"
}

# Puts the stock release runtime and GPU plugin back from the pinned SDK download, and stamps them.
function Install-StockRexglueRuntime($Layout) {
    if (-not (Test-Path -LiteralPath $Layout.SdkZip -PathType Leaf)) { throw "Missing $($Layout.SdkZip) (run tools\codegen.ps1)" }
    if ((Get-FileSha256 $Layout.SdkZip) -ne $Layout.Baseline['SDK_ZIP_SHA256']) {
        throw "$($Layout.SdkZip) is not the official ReXGlue $($Layout.Baseline['SDK_VERSION']) release"
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Remove-Item -LiteralPath $Layout.StampFile -ErrorAction SilentlyContinue
    $archive = [IO.Compression.ZipFile]::OpenRead($Layout.SdkZip)
    try {
        foreach ($file in @(Get-RexglueManagedFiles $Layout.Config)) {
            $dest = Join-Path $Layout.SdkDir $file.Leaf
            $stock = $Layout.Baseline["SHA256_$($file.Leaf)"]
            if ((Test-Path -LiteralPath $dest -PathType Leaf) -and (Get-FileSha256 $dest) -eq $stock) { continue }
            $entry = $archive.GetEntry("win-amd64/$($file.Leaf)")
            if (-not $entry) { throw "$($file.Leaf) is not in the SDK download" }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$dest.tmp", $true)
            if ((Get-FileSha256 "$dest.tmp") -ne $stock) { throw "$($file.Leaf) in the SDK download is not the stock release file" }
            Move-InstalledFile "$dest.tmp" $dest
        }
    } finally { $archive.Dispose() }
    Write-RexglueStamp $Layout (Get-RexglueRuntimeInputs $Layout 'stock')
    $problems = @(Get-RexglueInstallProblems $Layout 'stock')
    if ($problems.Count) { throw "The restored runtime does not match the stock release: $($problems -join '; ')" }
    Write-Host "Stock ReXGlue runtime and GPU plugin installed ($(Get-RexglueSummary $Layout))."
}

# Makes the installed runtime and GPU plugin current, rebuilding or restoring them when they are not, and stops if
# they still are not afterwards.
function Update-RexglueRuntime($Layout, [int]$Jobs = 0) {
    $mode = Get-RexglueMode $Layout
    $problems = @(Get-RexglueInstallProblems $Layout $mode)
    if ($problems.Count) {
        Write-Host 'ReXGlue patches: stale'
        foreach ($problem in $problems) { Write-Host "  $problem" }
        if ($mode -eq 'patched') {
            Write-Host 'Rebuilding the ReXGlue runtime and GPU plugin from patches\rexglue-sdk...'
            $builder = Join-Path $Layout.Root 'tools\build-patched-rexruntime.ps1'
            $builderArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $builder, '-Config', $Layout.Config)
            if ($Jobs -gt 0) { $builderArgs += @('-Jobs', $Jobs) }
            & (Get-Process -Id $PID).Path @builderArgs
            if ($LASTEXITCODE) { throw "Building the patched ReXGlue runtime failed ($LASTEXITCODE)" }
        } else {
            Write-Host 'Restoring the stock ReXGlue runtime and GPU plugin...'
            Install-StockRexglueRuntime $Layout
        }
        $problems = @(Get-RexglueInstallProblems $Layout $mode)
        if ($problems.Count) {
            foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
            throw 'The ReXGlue runtime and GPU plugin still do not match patches\rexglue-sdk'
        }
    }
    Write-Host "ReXGlue patches: current ($(Get-RexglueSummary $Layout))"
}

# The DLLs crackdown.exe needs next to it for a configuration.
function Get-RexglueStagedNames([string]$Config) {
    $suffix = Get-RexglueConfigSuffix $Config
    $names = @("rexruntime$suffix.dll", "rexgpu-xenos$suffix.dll")
    if ($Config -ne 'Release') { $names += "TracyClient$suffix.dll" }
    return $names
}

# What is wrong with the DLLs next to crackdown.exe; an empty list means they are the installed files.
function Get-RexglueStagingProblems($Layout, [string]$BuildDir, [string]$Config) {
    $problems = New-Object System.Collections.Generic.List[string]
    foreach ($name in Get-RexglueStagedNames $Config) {
        $installed = Join-Path $Layout.SdkDir "bin\$name"
        $staged = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { $problems.Add("missing in tools\rexglue: $installed"); continue }
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) { $problems.Add("$name is not next to crackdown.exe: $staged"); continue }
        $expected = Get-FileSha256 $installed
        $actual = Get-FileSha256 $staged
        if ($actual -ne $expected) {
            $problems.Add("$name next to crackdown.exe is not the installed file`n    expected: $installed`n    actual:   $staged`n    expected SHA-256: $expected`n    actual SHA-256:   $actual`n    reason: a stale copy from an earlier build")
        }
    }
    return $problems.ToArray()
}

# Copies the installed DLLs over the staged ones that differ. CMake stages them only when crackdown relinks.
function Sync-RexglueStaging($Layout, [string]$BuildDir, [string]$Config) {
    foreach ($name in Get-RexglueStagedNames $Config) {
        $installed = Join-Path $Layout.SdkDir "bin\$name"
        $staged = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { continue }
        if ((Test-Path -LiteralPath $staged -PathType Leaf) -and (Get-FileSha256 $staged) -eq (Get-FileSha256 $installed)) { continue }
        try { Copy-Item -LiteralPath $installed -Destination $staged -Force }
        catch { throw "Could not replace $staged (is crackdown.exe still running?): $($_.Exception.Message)" }
        Write-Host "Restaged $name from tools\rexglue"
    }
}

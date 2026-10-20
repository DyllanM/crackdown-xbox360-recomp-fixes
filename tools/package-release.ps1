<#
.SYNOPSIS
  Builds the Release configuration and packages the Windows release ZIP into dist\.
  Local only: nothing is published, tagged or uploaded.

.DESCRIPTION
  Builds into build-release\ (the normal build\ folder is not touched), stages the files the program needs, refuses
  to continue if the staging folder holds game or user files, then writes dist\Crackdown-v<version>-windows-x64.zip
  and its .sha256 file. The version is the project VERSION in CMakeLists.txt. The build rebuilds the patched ReXGlue
  runtime and GPU plugin first when they do not match patches\rexglue-sdk; the packaging stops unless they do.

.EXAMPLE
  .\tools\package-release.ps1              # incremental Release build, then package
  .\tools\package-release.ps1 -Clean       # clean Release build first
  .\tools\package-release.ps1 -SkipBuild   # package the existing build-release\ output
  .\tools\package-release.ps1 -VerifyOnly dist\Crackdown-v0.4.0-windows-x64
#>
param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [string]$VerifyOnly  # only check a staged folder for game or user files, then exit
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$ReleaseBuild = Join-Path $Root 'build-release'
$Packaging = Join-Path $Root 'packaging'
. (Join-Path $PSScriptRoot 'build-common.ps1')
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')

$Utf8NoBom = New-Object System.Text.UTF8Encoding $false
$RequiredRootFiles = @('crackdown.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'crackdown.toml', 'README.txt')
$OptionalRootFiles = @('LICENSE')
$LicenseFiles = @('ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt')
$GlyphFiles = @('button_glyphs.dds', 'button_glyphs.toml')
$AssetFiles = @('assets\launcher\header-art.png')
# Names that mean game data, saves, settings, caches or logs.
$ForbiddenNames = @('default.xex', 'game', 'game-install-temp', 'game-install-backup', 'user_data', 'cache', 'logs',
    'settings.toml', '4D5307DC', '$SystemUpdate')
$ForbiddenPatterns = @('*.xex', '*.iso', '*.dvd', '*.dff', '*.xwb', '*.xzp', '*.bik', '*.tpk', '*.spk', '*.log', '*.dmp',
    '*.pdb', '*.xsh', '*.xpso', '*.bak', '*.tmp', '*.sav', '*.invalid', 'settings.toml.*', 'trg-update-*')
# First bytes of an Xbox 360 package or executable.
$ForbiddenMagic = @('LIVE', 'PIRS', 'CON ', 'XEX2')

# This machine's folders (the repository and the user profile), as text or UTF-16, in either slash style.
$Latin1 = [Text.Encoding]::GetEncoding(28591)
$LocalPaths = foreach ($path in @($Root, $env:USERPROFILE) | Where-Object { $_ }) {
    foreach ($style in @($path, $path.Replace('\', '/'))) {
        $style
        $Latin1.GetString([Text.Encoding]::Unicode.GetBytes($style))
    }
}

function Test-ReleaseFolder([string]$Dir) {
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { throw "Not a folder: $Dir" }
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\')
    $allowedRoot = $RequiredRootFiles + $OptionalRootFiles
    foreach ($item in Get-ChildItem -LiteralPath $base -Recurse -Force) {
        $relative = $item.FullName.Substring($base.Length + 1)
        if (-not $item.PSIsContainer) {
            $text = $Latin1.GetString([IO.File]::ReadAllBytes($item.FullName))
            foreach ($path in $LocalPaths) {
                if ($text.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $problems.Add("local path in file: $relative"); break }
            }
            if ($text.IndexOf('DEBUG_SAULO', [StringComparison]::Ordinal) -ge 0) { $problems.Add("DEBUG_SAULO marker in file: $relative") }
            if ($item.Length -ge 4 -and ($ForbiddenMagic -contains $text.Substring(0, 4))) { $problems.Add("Xbox 360 package or executable: $relative") }
        }
        if ($ForbiddenNames -contains $item.Name) { $problems.Add("forbidden name: $relative") }
        foreach ($pattern in $ForbiddenPatterns) {
            if ($item.Name -like $pattern) { $problems.Add("forbidden file type: $relative") }
        }
        $parts = $relative -split '\\'
        if ($parts.Count -eq 1) {
            if ($item.PSIsContainer) {
                if (@('licenses', 'glyphs', 'assets') -notcontains $item.Name) { $problems.Add("unexpected folder: $relative") }
            }
            elseif ($allowedRoot -notcontains $item.Name) { $problems.Add("unexpected file: $relative") }
        } elseif ($parts[0] -eq 'assets') {
            if ($item.PSIsContainer) {
                if ($relative -ne 'assets\launcher') { $problems.Add("unexpected folder: $relative") }
            } elseif ($AssetFiles -notcontains $relative) {
                $problems.Add("unexpected item: $relative")
            }
        } elseif ($parts.Count -gt 2 -or $item.PSIsContainer -or
                  -not (($parts[0] -eq 'licenses' -and $LicenseFiles -contains $item.Name) -or
                        ($parts[0] -eq 'glyphs' -and $GlyphFiles -contains $item.Name))) {
            $problems.Add("unexpected item: $relative")
        }
    }
    foreach ($leaf in $RequiredRootFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $leaf) -PathType Leaf)) { $problems.Add("missing: $leaf") }
    }
    foreach ($leaf in $LicenseFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base "licenses\$leaf") -PathType Leaf)) { $problems.Add("missing: licenses\$leaf") }
    }
    foreach ($leaf in $GlyphFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base "glyphs\$leaf") -PathType Leaf)) { $problems.Add("missing: glyphs\$leaf") }
    }
    foreach ($asset in $AssetFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $asset) -PathType Leaf)) { $problems.Add("missing: $asset") }
    }
    return $problems
}

function Write-Problems($problems) {
    Write-Host ''
    Write-Host 'RELEASE CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
}

if ($VerifyOnly) {
    $problems = Test-ReleaseFolder $VerifyOnly
    if ($problems.Count) { Write-Problems $problems; exit 1 }
    Write-Host "Release folder is clean: $VerifyOnly"
    return
}

$cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^\s*project\(\s*crackdown\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Project VERSION not found in CMakeLists.txt' }
$Version = $Matches[1]
$Name = "Crackdown-v$Version-windows-x64"
$Dist = Join-Path $Root 'dist'
$Stage = Join-Path $Dist $Name
$Zip = Join-Path $Dist "$Name.zip"
$ShaFile = Join-Path $Dist "$Name.sha256"

Write-Host ''
Write-Host "Crackdown $Version - Windows release package (local only)"

if (-not $SkipBuild) {
    $build = @{ Config = 'Release'; BuildDir = $ReleaseBuild }
    if ($Clean) { $build.Clean = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @build
    if ($LASTEXITCODE) { throw "The Release build failed ($LASTEXITCODE)" }
}

# rexruntime.dll and rexgpu-xenos.dll must be the project's patched builds of the current patches.
$layout = Get-RexglueLayout $Root 'Release'
$runtimeProblems = @(Get-RexglueInstallProblems $layout)
if ($runtimeProblems.Count) { Write-Problems $runtimeProblems; throw 'tools\rexglue does not hold the current patched Release runtime and GPU plugin (build without -SkipBuild to update them)' }

foreach ($leaf in @('crackdown.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'crackdown.toml')) {
    if (-not (Test-Path (Join-Path $ReleaseBuild $leaf))) { throw "Release build output is missing $leaf (run without -SkipBuild)" }
}

New-Item -ItemType Directory -Force $Dist | Out-Null
if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory $Stage, (Join-Path $Stage 'licenses'), (Join-Path $Stage 'glyphs') | Out-Null

foreach ($leaf in @('crackdown.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'crackdown.toml')) {
    Copy-Item (Join-Path $ReleaseBuild $leaf) $Stage
}
foreach ($leaf in $GlyphFiles) {
    Copy-Item (Join-Path $ReleaseBuild "glyphs\$leaf") (Join-Path $Stage 'glyphs')
}
$AssetDir = Join-Path $Stage 'assets\launcher'
New-Item -ItemType Directory -Force $AssetDir | Out-Null
foreach ($asset in $AssetFiles) {
    Copy-Item -LiteralPath (Join-Path $Root $asset) $AssetDir
}
foreach ($leaf in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
    if ((Get-FileSha256 (Join-Path $Stage $leaf)) -ne (Get-FileSha256 (Join-Path $layout.SdkDir "bin\$leaf"))) {
        throw "$leaf is not the file installed in tools\rexglue"
    }
}
$RootLicense = Join-Path $Root 'LICENSE'
if (Test-Path -LiteralPath $RootLicense -PathType Leaf) {
    Copy-Item -LiteralPath $RootLicense $Stage
} else {
    Write-Warning 'The repository has no LICENSE file: the package carries none. Add one before publishing.'
}
foreach ($leaf in $LicenseFiles) {
    $source = Join-Path $Packaging "licenses\$leaf"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing license file: $source" }
    Copy-Item -LiteralPath $source (Join-Path $Stage 'licenses')
}
$readme = (Get-Content (Join-Path $Packaging 'README.txt') -Raw).Replace('@VERSION@', $Version) -replace "\r?\n", "`r`n"
[IO.File]::WriteAllText((Join-Path $Stage 'README.txt'), $readme, $Utf8NoBom)

$problems = Test-ReleaseFolder $Stage
if ($problems.Count) { Write-Problems $problems; throw 'The staging folder holds files that must not be released' }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip }
$files = Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') }
$stream = [IO.File]::Open($Zip, [IO.FileMode]::CreateNew)
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $entry = $file.FullName.Substring($Stage.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }

$archive = [IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName })
} finally { $archive.Dispose() }
$expected = @($files | ForEach-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') })
if (Compare-Object $expected $entries) { throw 'The ZIP does not hold exactly the staged files' }
if ($entries | Where-Object { $_ -match '^[\\/]|\.\.|:|\\' }) { throw 'The ZIP holds an unsafe entry name' }

$hash = (Get-FileSha256 $Zip).ToLowerInvariant()
[IO.File]::WriteAllText($ShaFile, "$hash  $Name.zip`n", $Utf8NoBom)

Write-Host ''
Write-Host '===================================================='
Write-Host ' RELEASE PACKAGE READY (nothing was published)'
Write-Host '===================================================='
Write-Host "  ZIP:     $Zip ($([math]::Round((Get-Item $Zip).Length / 1MB, 1)) MB)"
Write-Host "  SHA-256: $hash"
Write-Host "  Hash file: $ShaFile"
Write-Host '  Contents:'
foreach ($entry in $entries) { Write-Host "    $entry" }
Write-Host ''

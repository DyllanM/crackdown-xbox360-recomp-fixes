# Guest code freshness. codegen.ps1 writes the stamp after a successful run; build.ps1 only compares.

$CodegenStampPath = 'generated\codegen-inputs.stamp'
$CrackdownXexPath = 'game\assets\Default.xex'
$CrackdownXexSha256 = '7AAA7A8920D1C3C737E58CF583DEBAFE6013BE0A96BEC3EC136E5DE7E3377E5A'

function Get-InstalledRexglueVersion([string]$Root) {
    $file = Join-Path $Root 'tools\rexglue\win-amd64\lib\cmake\rexglue\rexglueConfigVersion.cmake'
    if (-not (Test-Path $file)) { return '' }
    [regex]::Match([IO.File]::ReadAllText($file), 'set\(PACKAGE_VERSION "([^"]+)"\)').Groups[1].Value
}

# Quoted paths in the includes arrays of a TOML file. ReXGlue resolves them from that file's folder.
function Get-TomlIncludes([string]$Text) {
    $paths = @()
    foreach ($list in [regex]::Matches($Text, '(?m)^\s*includes\s*=\s*\[([^\]]*)\]')) {
        foreach ($item in [regex]::Matches($list.Groups[1].Value, '["'']([^"'']+)["'']')) { $paths += $item.Groups[1].Value }
    }
    return $paths
}

function Get-CodegenInputKey([string]$Root, [string]$Path) {
    $rootPath = Get-NormalizedPath $Root
    if ($Path.StartsWith("$rootPath\", [StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($rootPath.Length + 1).Replace('\', '/')
    }
    return $Path
}

# Adds the includes of a TOML file depth first, each file once.
function Add-CodegenIncludes([string]$Root, [string]$TomlPath, [hashtable]$Seen, [System.Collections.Generic.List[object]]$Items) {
    if (-not (Test-Path -LiteralPath $TomlPath -PathType Leaf)) { return }
    $folder = Split-Path -Parent $TomlPath
    foreach ($include in Get-TomlIncludes ([IO.File]::ReadAllText($TomlPath))) {
        $path = Get-NormalizedPath ([IO.Path]::Combine($folder, $include))
        if ($Seen.ContainsKey($path)) { continue }
        $Seen[$path] = $true
        $Items.Add([pscustomobject]@{ Key = Get-CodegenInputKey $Root $path; Path = $path })
        Add-CodegenIncludes $Root $path $Seen $Items
    }
}

# The files codegen reads, in stamp order: the manifest, the XEX, then the manifest's includes.
function Get-CodegenInputItems([string]$Root) {
    $manifest = Get-NormalizedPath (Join-Path $Root 'crackdown_manifest.toml')
    $xex = Get-NormalizedPath (Join-Path $Root $CrackdownXexPath)
    $items = New-Object System.Collections.Generic.List[object]
    $items.Add([pscustomobject]@{ Key = Get-CodegenInputKey $Root $manifest; Path = $manifest })
    $items.Add([pscustomobject]@{ Key = Get-CodegenInputKey $Root $xex; Path = $xex })
    $seen = @{}
    $seen[$manifest] = $true
    $seen[$xex] = $true
    Add-CodegenIncludes $Root $manifest $seen $items
    return $items
}

# The patches in patches\rexglue-sdk that change the codegen tool, in ordinal name order.
function Get-CodegenPatches([string]$Root) {
    return @(Get-PlatformPatches (Join-Path $Root 'patches\rexglue-sdk') 'windows' |
        Where-Object { Select-String -LiteralPath $_.Path -Pattern '^diff --git a/src/codegen/' -Quiet })
}

# 'stock', or a name for the codegen tool built from the codegen patches.
function Get-CodegenToolIdentity([string]$Root) {
    $patches = @(Get-CodegenPatches $Root)
    if (-not $patches.Count) { return 'stock' }
    $lines = @($patches | ForEach-Object { "$($_.Name)=$(Get-FileSha256 $_.Path)" })
    return 'patched-' + (Get-TextSha256 ($lines -join "`n")).Substring(0, 12).ToLowerInvariant()
}

function Get-PatchedCodegenPath([string]$Root, [string]$Identity) {
    return Join-Path $Root "out\rexglue-codegen\$Identity\rexglue.exe"
}

function Get-CodegenInputStamp([string]$Root, [string]$SdkVersion) {
    $tool = Get-CodegenToolIdentity $Root
    $lines = @(if ($tool -eq 'stock') { "rexglue  $SdkVersion" } else { "rexglue  $SdkVersion $tool" })
    foreach ($item in @(Get-CodegenInputItems $Root)) {
        $hash = if (Test-Path -LiteralPath $item.Path -PathType Leaf) { Get-FileSha256 $item.Path } else { 'missing' }
        $lines += "$hash  $($item.Key)"
    }
    return $lines -join "`n"
}

function Read-CodegenStamp([string]$Text) {
    $values = [ordered]@{}
    foreach ($line in $Text -split "`n") {
        $parts = $line -split '  ', 2
        if ($parts.Count -ne 2) { continue }
        if ($parts[0] -eq 'rexglue') { $values['rexglue'] = $parts[1] } else { $values[$parts[1]] = $parts[0] }
    }
    return $values
}

# Whether the guest code matches its inputs, and which inputs changed when it does not.
function Get-CodegenFreshness([string]$Root, [string]$SdkVersion) {
    $stampFile = Join-Path $Root $CodegenStampPath
    if (-not (Test-Path -LiteralPath $stampFile -PathType Leaf)) {
        return [pscustomobject]@{ Current = $false; Reasons = @('no successful codegen record') }
    }
    $current = Get-CodegenInputStamp $Root $SdkVersion
    $stored = [IO.File]::ReadAllText($stampFile)
    if ($stored -ceq $current) { return [pscustomobject]@{ Current = $true; Reasons = @() } }
    $reasons = @(Get-ValueChanges (Read-CodegenStamp $stored) (Read-CodegenStamp $current))
    if (-not $reasons.Count) { $reasons = @('the codegen inputs changed') }
    return [pscustomobject]@{ Current = $false; Reasons = @($reasons | ForEach-Object { $_.Replace('/', '\') }) }
}

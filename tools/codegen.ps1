<#
.SYNOPSIS
  Downloads the pinned ReXGlue SDK, checks game\assets\Default.xex and
  generates the recompiled C++ sources into generated\default. This is the only entry point that regenerates them.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-common.ps1')
. (Join-Path $PSScriptRoot 'codegen-inputs.ps1')

$baseline = Read-KeyValueFile (Join-Path $root 'patches\rexglue-sdk\baseline.env')
$sdkVersion = $baseline['SDK_VERSION']
$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$xex = Join-Path $root $CrackdownXexPath
$log = Join-Path $root 'generated\codegen.log'
$stamp = Join-Path $root $CodegenStampPath
if (Test-Path $stamp) { Remove-Item -LiteralPath $stamp }

if (-not (Test-Path $rexglue)) {
    $zip = Join-Path $sdkDir "rexglue-sdk-$sdkVersion-win-amd64.zip"
    if (-not (Test-Path $zip)) {
        New-Item -ItemType Directory -Force $sdkDir | Out-Null
        $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$sdkVersion/rexglue-sdk-$sdkVersion-win-amd64.zip"
        Write-Host "Downloading ReXGlue SDK $sdkVersion..."
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if ((Get-FileSha256 $zip) -ne $baseline['SDK_ZIP_SHA256']) { throw "$zip is not the official ReXGlue $sdkVersion release" }
    Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
}
$installed = "$(& $rexglue --version)".Trim()
if ($installed -ne $sdkVersion) { throw "tools\rexglue has ReXGlue $installed; expected $sdkVersion (delete tools\rexglue to re-download)" }
if ((Get-FileSha256 $rexglue) -ne $baseline['SHA256_bin/rexglue.exe']) {
    throw "$rexglue is not the stock ReXGlue $sdkVersion codegen (delete tools\rexglue to re-download)"
}

if (-not (Test-Path $xex)) { throw "Missing $xex (copy the disc contents into game\assets first)" }
$hash = Get-FileSha256 $xex
if ($hash -ne $CrackdownXexSha256) { throw "Unsupported Default.xex (SHA-256 $hash). Expected $CrackdownXexSha256 (Crackdown TU0)." }
Write-Host 'Default.xex is the supported version.'

New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Push-Location $root
try {
    # rexglue logs to stderr; in Windows PowerShell 5.1 that would trip 'Stop', so judge by exit code.
    $ErrorActionPreference = 'Continue'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $rexglue codegen crackdown_manifest.toml 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $exitCode = $LASTEXITCODE
    Write-Host ("codegen exit code {0} after {1:N1} s (log: {2})" -f $exitCode, $timer.Elapsed.TotalSeconds, $log)
    if ($exitCode -ne 0) { throw "codegen failed ($exitCode)" }
    [IO.File]::WriteAllText($stamp, (Get-CodegenInputStamp $root $sdkVersion), (New-Object Text.UTF8Encoding $false))
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }

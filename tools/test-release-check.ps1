<#
.SYNOPSIS
  Checks that package-release.ps1 -VerifyOnly accepts a fresh extraction and refuses player data, logs and game
  files. Scratch folders only; nothing is built, launched or published.
#>
$ErrorActionPreference = 'Stop'
$ReleaseCheck = Join-Path $PSScriptRoot 'package-release.ps1'
$StageRoot = Join-Path ([IO.Path]::GetTempPath()) 'crackdown_release_check'
$Cases = [ordered]@{
    'fresh extraction' = $null
    'empty user_data'  = 'user_data\'
    'save data'        = 'user_data\4D5307DC\profile.bin'
    'settings'         = 'user_data\settings.toml'
    'cache'            = 'cache\shaders\a.bin'
    'log'              = 'logs\crackdown_001.log'
    'game file'        = 'game\Default.xex'
    'disc image'       = 'DVD.iso'
    'stray executable' = 'Default.xex'
}
$failures = 0
try {
    foreach ($case in $Cases.Keys) {
        $stage = Join-Path $StageRoot ($case -replace '\W', '_')
        New-Item -ItemType Directory -Force (Join-Path $stage 'licenses'), (Join-Path $stage 'glyphs') | Out-Null
        foreach ($leaf in 'crackdown.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'crackdown.toml', 'README.txt', 'LICENSE',
            'licenses\ReXGlue-LICENSE.txt', 'licenses\THIRD-PARTY-LICENSES.txt', 'glyphs\button_glyphs.dds',
            'glyphs\button_glyphs.toml') {
            Set-Content -LiteralPath (Join-Path $stage $leaf) -Value 'placeholder'
        }
        $extra = $Cases[$case]
        if ($extra) {
            $target = Join-Path $stage $extra
            if ($extra.EndsWith('\')) {
                New-Item -ItemType Directory -Force $target | Out-Null
            } else {
                New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
                Set-Content -LiteralPath $target -Value 'placeholder'
            }
        }
        $global:LASTEXITCODE = 0
        & $ReleaseCheck -VerifyOnly $stage *> $null
        $accepted = $LASTEXITCODE -eq 0
        if ($accepted -ne ($null -eq $extra)) {
            Write-Host "  FAIL $case"
            $failures++
        }
    }
} finally {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "$($Cases.Count) release check cases, $failures failures"
if ($failures) { throw 'Release check tests failed' }
$global:LASTEXITCODE = 0

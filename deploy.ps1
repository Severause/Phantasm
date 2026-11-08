# deploy.ps1 - copies the built plugin (and, with -Probe, the development probe client) into a MO2 mod folder.
#
#   .\deploy.ps1 -Mods <dir>           Phantasm into <dir>\Phantasm (<dir>: an MO2 instance's mods folder)
#   .\deploy.ps1 -Mods <dir> -Probe    also PhantasmProbe into <dir>\PhantasmProbe
#
# A new mod folder must be enabled once in MO2 (or "+Phantasm" added to the profile's modlist.txt while MO2 is closed).
# DLL copies fail while the game runs, so the script refuses then.

param(
    [switch]$Probe,
    [Parameter(Mandatory = $true, HelpMessage = "an MO2 instance's mods folder")]
    [string]$Mods
)

$ErrorActionPreference = "Stop"
if (Get-Process SkyrimSE, SkyrimVR -ErrorAction SilentlyContinue) { Write-Host "The game is running: close it first." -ForegroundColor Red; exit 1 }
# A mistyped folder would otherwise be created, with the mods inside it.
if (-not (Test-Path -LiteralPath $Mods -PathType Container)) { Write-Host "$Mods is not a folder." -ForegroundColor Red; exit 1 }

$root  = $PSScriptRoot
$build = if ($env:PHANTASM_BUILD_DIR) { $env:PHANTASM_BUILD_DIR } else { "C:\b\phantasm" }

function Copy-Into([string]$from, [string]$to) {
    New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
    Copy-Item $from $to -Force
    Write-Host "  $to"
}

$mod = Join-Path $Mods "Phantasm"
Write-Host "Phantasm -> $mod"
Copy-Into (Join-Path $build "Phantasm.dll") (Join-Path $mod "SKSE\Plugins\Phantasm.dll")
Copy-Into (Join-Path $build "Phantasm.pdb") (Join-Path $mod "SKSE\Plugins\Phantasm.pdb")
Get-ChildItem (Join-Path $root "data") -Recurse -File | ForEach-Object {
    Copy-Into $_.FullName (Join-Path $mod $_.FullName.Substring((Join-Path $root "data").Length + 1))
}

if ($Probe) {
    $pmod = Join-Path $Mods "PhantasmProbe"
    Write-Host "PhantasmProbe -> $pmod"
    Copy-Into (Join-Path $build "PhantasmProbe.dll") (Join-Path $pmod "SKSE\Plugins\PhantasmProbe.dll")
    $pdata = Join-Path $root "testclient\data"
    Get-ChildItem $pdata -Recurse -File | ForEach-Object {
        Copy-Into $_.FullName (Join-Path $pmod $_.FullName.Substring($pdata.Length + 1))
    }
}
Write-Host "Deployed." -ForegroundColor Green

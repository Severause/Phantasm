# package.ps1 - the release zips, from a build of this checkout (docs\RELEASING.md):
#   dist\Phantasm-<version>.zip       laid out like Data, for a mod manager or a manual install
#   dist\Phantasm-<version>-pdb.zip   Phantasm.pdb and PhantasmNRInProc.pdb, kept with the release builds to read
#                                     crash logs (never uploaded)
#
#   .\package.ps1                     the build in C:\b\phantasm (PHANTASM_BUILD_DIR overrides it, as in build.ps1)
#   .\package.ps1 -BuildDir <dir>     another build directory
#   .\package.ps1 -Force              replace this version's zips in dist\
#   .\package.ps1 -AllowDirty         package a checkout with uncommitted or untracked files (a test zip only)
#   .\package.ps1 -AllowOtherTree     package a DLL another checkout built (a test zip only)
#   .\package.ps1 -AllowNoNeural      package a build without neural rendering's PhantasmNRInProc.dll (a test zip only)
#
# The version is native\CMakeLists.txt's; native\vcpkg.json and the DLL's file version must agree with it. The zip holds
# the DLL, neural rendering's PhantasmNRInProc.dll, data\ as git sees it, Phantasm's LICENSE and a VERSION.txt; never
# a PDB or PhantasmProbe.dll.

param(
    [string]$BuildDir = $(if ($env:PHANTASM_BUILD_DIR) { $env:PHANTASM_BUILD_DIR } else { "C:\b\phantasm" }),
    [switch]$Force,
    [switch]$AllowDirty,
    [switch]$AllowOtherTree,
    [switch]$AllowNoNeural
)

$ErrorActionPreference = "Stop"
function Fail([string]$msg) { Write-Host $msg -ForegroundColor Red; exit 1 }

# Windows PowerShell turns a native tool's redirected stderr into error records, which "Stop" would make fatal.
function Invoke-Git([string]$dir, [string[]]$gitArgs) {
    $ErrorActionPreference = "Continue"
    $out = & git -C $dir @gitArgs 2>$null
    if ($LASTEXITCODE -ne 0) { Fail "git $($gitArgs -join ' ') failed in $dir" }
    if ($null -ne $out) { $out }
}

$root = $PSScriptRoot
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "git was not found (VERSION.txt names the commit)" }

# --- Version -------------------------------------------------------------------------------------------------------
$cmake = Get-Content -Raw -LiteralPath (Join-Path $root "native\CMakeLists.txt")
if ($cmake -notmatch '(?s)\bproject\s*\(\s*Phantasm\s+VERSION\s+(\d+\.\d+\.\d+)(?![\d.])') {
    Fail "native\CMakeLists.txt has no project(Phantasm VERSION x.y.z)"
}
$version = $Matches[1]
$manifest = Get-Content -Raw -LiteralPath (Join-Path $root "native\vcpkg.json") | ConvertFrom-Json
$manifestVersion = @($manifest.version, $manifest.'version-semver', $manifest.'version-string') |
    Where-Object { $_ } | Select-Object -First 1
if ($manifestVersion -ne $version) { Fail "native\vcpkg.json says $manifestVersion and native\CMakeLists.txt $version (bump both)" }
# The zip names Phantasm's own licence file by its type, Phantasm-MIT.txt, like the third-party ones beside it.
$licence = Join-Path $root "LICENSE"
if ((Get-Content -Raw -LiteralPath $licence) -notmatch '^MIT License') {
    Fail "LICENSE is not MIT any more: rename Phantasm-MIT.txt here"
}

# --- Build ---------------------------------------------------------------------------------------------------------
$dll = Join-Path $BuildDir "Phantasm.dll"
$pdb = Join-Path $BuildDir "Phantasm.pdb"
foreach ($f in $dll, $pdb) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { Fail "$f was not found (build with native\build.ps1, or pass -BuildDir)" }
}
$dllItem = Get-Item -LiteralPath $dll
$pdbItem = Get-Item -LiteralPath $pdb
$vi = $dllItem.VersionInfo
$dllVersion = "{0}.{1}.{2}.{3}" -f $vi.FileMajorPart, $vi.FileMinorPart, $vi.FileBuildPart, $vi.FilePrivatePart
if ($vi.ProductName -ne "Phantasm" -or $dllVersion -ne "$version.0") {
    Fail "$dll is file version $dllVersion of '$($vi.ProductName)', not Phantasm $version.0 (rebuild)"
}
# The linker writes both in one link; a PDB from another link cannot read this DLL's crash logs.
if ([math]::Abs(($dllItem.LastWriteTime - $pdbItem.LastWriteTime).TotalMinutes) -gt 2) {
    Fail "Phantasm.dll and Phantasm.pdb in $BuildDir come from different links (rebuild)"
}

# The cache names the checkout the build came from (build.ps1 starts clean when another checkout configured it).
$tree = $null
$cache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path -LiteralPath $cache) {
    $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($line) { $tree = Split-Path -Parent ([IO.Path]::GetFullPath($line.Matches[0].Groups[1].Value.Trim())) }
}
$treeProblem = $null
if (-not $tree) { $treeProblem = "$BuildDir has no CMakeCache.txt naming the checkout it was built from" }
elseif ($tree.TrimEnd('\') -ne $root.TrimEnd('\')) { $treeProblem = "the build in $BuildDir is from $tree, not this checkout" }
if ($treeProblem) {
    if (-not $AllowOtherTree) { Fail "$treeProblem (build here with native\build.ps1, or pass -AllowOtherTree for a test zip)" }
    Write-Host "WARNING: $treeProblem (-AllowOtherTree)" -ForegroundColor Yellow
}

# --- Neural rendering's library ------------------------------------------------------------------------------------
# PhantasmNRInProc.dll is built only when PHANTASM_NR_VULKAN names Khronos's Vulkan headers (native\CMakeLists.txt), and
# Phantasm.ini's [NeuralRendering] does nothing without it, so a release ships it. A copy in a build directory whose
# last configure had no headers is left over from an earlier build of other sources: it is never shipped.
$nrDll = Join-Path $BuildDir "PhantasmNRInProc.dll"
$nrPdb = Join-Path $BuildDir "PhantasmNRInProc.pdb"
$nrConfigured = $null
if (Test-Path -LiteralPath $cache) {
    $line = Select-String -LiteralPath $cache -Pattern '^PHANTASM_NR_VULKAN:[A-Z]+=(.*)$' | Select-Object -First 1
    $nrValue = if ($line) { $line.Matches[0].Groups[1].Value.Trim() } else { "" }
    # CMake's if() reads these values as false too, and then makes no library target.
    $nrConfigured = [bool]($nrValue -and $nrValue -notmatch '^(0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|.*-NOTFOUND)$')
}
$nrProblem = $null
if ($nrConfigured -eq $false) { $nrProblem = "the build in $BuildDir was configured without PHANTASM_NR_VULKAN, so it built no PhantasmNRInProc.dll" }
elseif (-not (Test-Path -LiteralPath $nrDll -PathType Leaf)) { $nrProblem = "$nrDll was not found" }
elseif (-not (Test-Path -LiteralPath $nrPdb -PathType Leaf)) { $nrProblem = "$nrPdb was not found" }
elseif ([math]::Abs(((Get-Item -LiteralPath $nrDll).LastWriteTime - (Get-Item -LiteralPath $nrPdb).LastWriteTime).TotalMinutes) -gt 2) {
    $nrProblem = "PhantasmNRInProc.dll and PhantasmNRInProc.pdb in $BuildDir come from different links"
}
$shipNeural = -not $nrProblem
if ($nrProblem) {
    if (-not $AllowNoNeural) {
        Fail "$nrProblem (build with PHANTASM_NR_VULKAN set, see native\build.ps1, or pass -AllowNoNeural for a test zip)"
    }
    Write-Host "WARNING: $nrProblem; this zip has no neural rendering (-AllowNoNeural)" -ForegroundColor Yellow
}

# --- Checkout ------------------------------------------------------------------------------------------------------
$status = @(Invoke-Git $root @("status", "--porcelain"))
if ($status.Count -gt 0) {
    $status | ForEach-Object { Write-Host "  $_" }
    if (-not $AllowDirty) {
        Fail "A release is built from a clean checkout of its tag: commit or remove the files above, or pass -AllowDirty for a test zip"
    }
    Write-Host "WARNING: uncommitted or untracked files (-AllowDirty)" -ForegroundColor Yellow
}

# VERSION.txt names the commit the DLL was built from, in the shape of the copy SeverActions bundles (less its suffix).
$rev = "unknown"
if ($tree -and (Test-Path -LiteralPath (Join-Path $tree ".git"))) {
    $rev = [string](Invoke-Git $tree @("rev-parse", "--short=12", "HEAD"))
    if (@(Invoke-Git $tree @("status", "--porcelain")).Count -gt 0) { $rev += "+dirty" }
}
$versionText = "Phantasm {0} {1} (built {2:yyyy-MM-dd HH:mm})" -f $version, $rev, $dllItem.LastWriteTime
Write-Host "Packaging $versionText from $BuildDir"

# --- Contents ------------------------------------------------------------------------------------------------------
$entries = New-Object System.Collections.Generic.List[object]
$entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/Phantasm.dll"; Source = $dll; Text = $null })
# In the folder "Phantasm" beside Phantasm.dll, where NeuralStills looks for it.
if ($shipNeural) {
    $entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/Phantasm/PhantasmNRInProc.dll"; Source = $nrDll; Text = $null })
}
# data\ as git sees it: an ignored file (a stray zip, an editor's file) never ships, and the clean check cannot see one.
$data = Invoke-Git $root @("-c", "core.quotepath=off", "ls-files", "--cached", "--others", "--exclude-standard", "--", "data")
foreach ($rel in @($data)) {
    $src = Join-Path $root $rel
    if (Test-Path -LiteralPath $src -PathType Leaf) {
        $entries.Add([pscustomobject]@{ Name = $rel.Substring("data/".Length); Source = $src; Text = $null })
    }
}
$entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/Phantasm/Licenses/Phantasm-MIT.txt"; Source = $licence; Text = $null })
$entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/Phantasm/VERSION.txt"; Source = $null; Text = $versionText })

$names = @($entries | ForEach-Object { $_.Name })
# Phantasm.dll compiles SKSE Menu Framework's API header in (the settings panel), which carries Dear ImGui's
# declarations as cimgui words them: the three licences ship with it.
$musts = @("SKSE/Plugins/Phantasm.ini", "Interface/phantasm.swf", "Magelight/Phantasm/views/settings/index.html",
    "SKSE/Plugins/Phantasm/Licenses/SKSE-Menu-Framework-MIT.txt", "SKSE/Plugins/Phantasm/Licenses/Dear-ImGui-MIT.txt",
    "SKSE/Plugins/Phantasm/Licenses/cimgui-MIT.txt")
# PhantasmNRInProc.dll is compiled against Khronos's Vulkan headers: their licence ships with it.
if ($shipNeural) { $musts += "SKSE/Plugins/Phantasm/Licenses/Vulkan-Headers-MIT.txt" }
foreach ($must in $musts) {
    if ($names -notcontains $must) { Fail "data/$must is missing" }
}
$dupes = @($names | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
if ($dupes.Count -gt 0) { Fail "data\ holds a file this script writes itself: $($dupes -join ', ')" }

# --- Zips ----------------------------------------------------------------------------------------------------------
$dist = Join-Path $root "dist"
$zipPath = Join-Path $dist "Phantasm-$version.zip"
$pdbZipPath = Join-Path $dist "Phantasm-$version-pdb.zip"
foreach ($z in $zipPath, $pdbZipPath) {
    if ((Test-Path -LiteralPath $z) -and -not $Force) { Fail "$z exists (pass -Force to replace it)" }
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

# Entry names come from the list above, with forward slashes: some unzip tools take a backslash as part of a file name.
# The zip is written under another name and moved into place, so a failed run never leaves one that looks finished.
function Write-Zip([string]$path, $items) {
    $partial = "$path.partial"
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    $zip = [System.IO.Compression.ZipFile]::Open($partial, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($i in $items) {
            if ($null -ne $i.Text) {
                $entry = $zip.CreateEntry($i.Name, [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $dllItem.LastWriteTime
                $bytes = [Text.Encoding]::ASCII.GetBytes($i.Text + "`r`n")
                $stream = $entry.Open()
                try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
            } else {
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $i.Source, $i.Name,
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
        }
    } finally { $zip.Dispose() }
    Move-Item -LiteralPath $partial -Destination $path -Force
}

Write-Zip $zipPath $entries
$pdbEntries = New-Object System.Collections.Generic.List[object]
$pdbEntries.Add([pscustomobject]@{ Name = "Phantasm.pdb"; Source = $pdb; Text = $null })
if ($shipNeural) { $pdbEntries.Add([pscustomobject]@{ Name = "PhantasmNRInProc.pdb"; Source = $nrPdb; Text = $null }) }
Write-Zip $pdbZipPath $pdbEntries

foreach ($z in $zipPath, $pdbZipPath) {
    Write-Host ""
    Write-Host $z -ForegroundColor Green
    $read = [System.IO.Compression.ZipFile]::OpenRead($z)
    try {
        foreach ($e in $read.Entries) {
            if ($e.FullName.Contains('\')) { Fail "$z has an entry with a backslash: $($e.FullName)" }
            Write-Host ("  {0,12:N0}  {1}" -f $e.Length, $e.FullName)
        }
    } finally { $read.Dispose() }
    Write-Host ("  SHA-256 {0}" -f (Get-FileHash -LiteralPath $z -Algorithm SHA256).Hash.ToLowerInvariant())
}

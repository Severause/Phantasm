# package.ps1 - Phantasm Showcase's release zips, from a build of this checkout (..\docs\RELEASING.md):
#   ..\dist\Phantasm-Showcase-<version>.zip       laid out like Data: the optional download beside Phantasm
#   ..\dist\Phantasm-Showcase-<version>-pdb.zip   PhantasmShowcase.pdb alone, to read crash logs
#
#   .\package.ps1                     the build in C:\b\phantasmshowcase (PS_BUILD_DIR overrides it, as in build.ps1)
#   .\package.ps1 -BuildDir <dir>     another build directory
#   .\package.ps1 -Force              replace this version's zips in ..\dist\
#   .\package.ps1 -AllowDirty         package a checkout with uncommitted or untracked files (a test zip only)
#   .\package.ps1 -AllowOtherTree     package a DLL another checkout built (a test zip only)
#
# The version is native\CMakeLists.txt's; native\vcpkg.json and the DLL's file version must agree with it. The zip holds
# the DLL, data\ as git sees it (the pages, built by `npm run build` in ui\), the licences of what the DLL compiles in
# and a VERSION.txt; never the PDB.

param(
    [string]$BuildDir = $(if ($env:PS_BUILD_DIR) { $env:PS_BUILD_DIR } else { "C:\b\phantasmshowcase" }),
    [switch]$Force,
    [switch]$AllowDirty,
    [switch]$AllowOtherTree
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
$repo = Split-Path -Parent $root
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "git was not found (VERSION.txt names the commit)" }

# --- Version -------------------------------------------------------------------------------------------------------
$cmake = Get-Content -Raw -LiteralPath (Join-Path $root "native\CMakeLists.txt")
if ($cmake -notmatch '(?s)\bproject\s*\(\s*PhantasmShowcase\s+VERSION\s+(\d+\.\d+\.\d+)(?![\d.])') {
    Fail "native\CMakeLists.txt has no project(PhantasmShowcase VERSION x.y.z)"
}
$version = $Matches[1]
$manifest = Get-Content -Raw -LiteralPath (Join-Path $root "native\vcpkg.json") | ConvertFrom-Json
if ($manifest.'version-string' -ne $version) {
    Fail "native\vcpkg.json says $($manifest.'version-string') and native\CMakeLists.txt $version (bump both)"
}

# --- Build ---------------------------------------------------------------------------------------------------------
$dll = Join-Path $BuildDir "PhantasmShowcase.dll"
$pdb = Join-Path $BuildDir "PhantasmShowcase.pdb"
foreach ($f in $dll, $pdb) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { Fail "$f was not found (build with native\build.ps1, or pass -BuildDir)" }
}
$dllItem = Get-Item -LiteralPath $dll
$pdbItem = Get-Item -LiteralPath $pdb
$vi = $dllItem.VersionInfo
$dllVersion = "{0}.{1}.{2}.{3}" -f $vi.FileMajorPart, $vi.FileMinorPart, $vi.FileBuildPart, $vi.FilePrivatePart
if ($vi.ProductName -ne "PhantasmShowcase" -or $dllVersion -ne "$version.0") {
    Fail "$dll is file version $dllVersion of '$($vi.ProductName)', not PhantasmShowcase $version.0 (rebuild)"
}
# The linker writes both in one link; a PDB from another link cannot read this DLL's crash logs.
if ([math]::Abs(($dllItem.LastWriteTime - $pdbItem.LastWriteTime).TotalMinutes) -gt 2) {
    Fail "PhantasmShowcase.dll and PhantasmShowcase.pdb in $BuildDir come from different links (rebuild)"
}

# The cache names the source folder the build came from (native\ of some checkout's showcase\).
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

# --- Checkout ------------------------------------------------------------------------------------------------------
$status = @(Invoke-Git $repo @("status", "--porcelain"))
if ($status.Count -gt 0) {
    $status | ForEach-Object { Write-Host "  $_" }
    if (-not $AllowDirty) {
        Fail "A release is built from a clean checkout of its tag: commit or remove the files above, or pass -AllowDirty for a test zip"
    }
    Write-Host "WARNING: uncommitted or untracked files (-AllowDirty)" -ForegroundColor Yellow
}

$rev = "unknown"
if ($tree -and (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $tree) ".git"))) {
    $rev = [string](Invoke-Git $tree @("rev-parse", "--short=12", "HEAD"))
    if (@(Invoke-Git $tree @("status", "--porcelain")).Count -gt 0) { $rev += "+dirty" }
}
$versionText = "Phantasm Showcase {0} {1} (built {2:yyyy-MM-dd HH:mm})" -f $version, $rev, $dllItem.LastWriteTime
Write-Host "Packaging $versionText from $BuildDir"

# --- Contents ------------------------------------------------------------------------------------------------------
$entries = New-Object System.Collections.Generic.List[object]
$entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/PhantasmShowcase.dll"; Source = $dll; Text = $null })
# data\ as git sees it: an ignored file (a stray zip, an editor's file) never ships, and the clean check cannot see one.
$data = Invoke-Git $root @("-c", "core.quotepath=off", "ls-files", "--cached", "--others", "--exclude-standard", "--", "data")
foreach ($rel in @($data)) {
    $src = Join-Path $root $rel
    if (Test-Path -LiteralPath $src -PathType Leaf) {
        $entries.Add([pscustomobject]@{ Name = $rel.Substring("data/".Length); Source = $src; Text = $null })
    }
}
# The licences of what the DLL compiles in: the Showcase itself, the API headers' and the libraries' (Phantasm ships the
# same texts; CBPC's and DirectXTex's are Phantasm's alone).
$lic = "SKSE/Plugins/PhantasmShowcase/Licenses"
$entries.Add([pscustomobject]@{ Name = "$lic/PhantasmShowcase-MIT.txt"; Source = (Join-Path $root "LICENSE"); Text = $null })
$entries.Add([pscustomobject]@{ Name = "$lic/MagelightUI-API-MIT.txt"; Source = (Join-Path $root "native\extern\magelight\LICENSE.txt"); Text = $null })
$entries.Add([pscustomobject]@{ Name = "$lic/SKSEMenuFramework-API-MIT.txt"; Source = (Join-Path $root "native\extern\skse-menu-framework\LICENSE.txt"); Text = $null })
foreach ($name in "CommonLibSSE-NG-MIT", "DirectXMath-MIT", "DirectXTK-MIT", "fmt-MIT", "nlohmann-json-MIT", "openvr-BSD3",
                  "rapidcsv-BSD3", "spdlog-MIT", "xbyak-BSD3") {
    $src = Join-Path $repo "data\SKSE\Plugins\Phantasm\Licenses\$name.txt"
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { Fail "$src is missing" }
    $entries.Add([pscustomobject]@{ Name = "$lic/$name.txt"; Source = $src; Text = $null })
}
$entries.Add([pscustomobject]@{ Name = "SKSE/Plugins/PhantasmShowcase/VERSION.txt"; Source = $null; Text = $versionText })

$names = @($entries | ForEach-Object { $_.Name })
foreach ($must in "Magelight/PhantasmShowcase/views/main/index.html", "PrismaUI/views/PhantasmShowcase/index.html") {
    if ($names -notcontains $must) { Fail "data/$must is missing (build the page: cd ui; npm run build)" }
}
$dupes = @($names | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
if ($dupes.Count -gt 0) { Fail "data\ holds a file this script writes itself: $($dupes -join ', ')" }

# --- Zips ----------------------------------------------------------------------------------------------------------
$dist = Join-Path $repo "dist"
$zipPath = Join-Path $dist "Phantasm-Showcase-$version.zip"
$pdbZipPath = Join-Path $dist "Phantasm-Showcase-$version-pdb.zip"
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
Write-Zip $pdbZipPath @([pscustomobject]@{ Name = "PhantasmShowcase.pdb"; Source = $pdb; Text = $null })

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

# build.ps1 - builds PhantasmShowcase.dll (and, with -Tests, builds and runs the engine-free self-tests).
#
#   .\build.ps1            Release DLL into the build directory, copied next to this script
#   .\build.ps1 -Tests     also build and run ShowcaseTests.exe; a failing check fails the build
#   .\build.ps1 -Clean     start from an empty build directory
#
# Build directory: C:\b\phantasmshowcase (short, so vcpkg's paths stay under MAX_PATH); PS_BUILD_DIR overrides it.
# vcpkg: VCPKG_ROOT as set when the script starts, else the standalone one at C:\vcpkg (VS's bundled vcpkg, which
# vcvars64.bat would set, does not detect VS 18).

param(
    [switch]$Clean,
    [switch]$Tests
)

$ErrorActionPreference = "Stop"
$started = Get-Date

function Fail([string]$msg) { Write-Host $msg -ForegroundColor Red; exit 1 }

# --- Visual Studio environment -------------------------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) {
    foreach ($p in @(
        "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools",
        "C:\Program Files\Microsoft Visual Studio\18\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise")) {
        if (Test-Path (Join-Path $p "VC\Auxiliary\Build\vcvars64.bat")) { $vsPath = $p; break }
    }
}
if (-not $vsPath) { Fail "Visual Studio with the C++ workload was not found" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
# vcvars64.bat points VCPKG_ROOT at VS's bundled vcpkg, so the caller's choice is read before its environment comes in.
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:\$($matches[1])" -Value $matches[2] }
}
$env:VCPKG_VISUAL_STUDIO_PATH = $vsPath

# --- Tools ---------------------------------------------------------------------------------------------------------
$env:VCPKG_ROOT = $vcpkgRoot
if (-not (Test-Path "$env:VCPKG_ROOT\vcpkg.exe")) {
    Fail ("vcpkg was not found at $env:VCPKG_ROOT. Clone and bootstrap it there, or point VCPKG_ROOT at a bootstrapped " +
        "vcpkg: `$env:VCPKG_ROOT = 'D:\vcpkg' for this shell, setx VCPKG_ROOT D:\vcpkg for new ones.")
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\Program Files\CMake\bin\cmake.exe") { $env:Path = "C:\Program Files\CMake\bin;" + $env:Path }
    else { Fail "CMake was not found (winget install Kitware.CMake)" }
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $n = Get-ChildItem "$env:VCPKG_ROOT\downloads\tools" -Filter ninja.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($n) { $env:Path = $n.DirectoryName + ";" + $env:Path } else { Fail "Ninja was not found (winget install Ninja-build.Ninja)" }
}

# --- Configure and build -------------------------------------------------------------------------------------------
# From here the native tools' stderr (a CMake warning, a compiler note) is output, not a failure: Windows PowerShell
# turns redirected stderr into error records, which "Stop" would treat as fatal. Exit codes and the log decide.
$ErrorActionPreference = "Continue"
$buildDir = if ($env:PS_BUILD_DIR) { $env:PS_BUILD_DIR } else { "C:\b\phantasmshowcase" }
if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

# A cache configured from another checkout would mix its object files into this build.
$cache = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=' | Select-Object -First 1
    if ($line) {
        $cached = ($line.Line -replace '^CMAKE_HOME_DIRECTORY:INTERNAL=', '').Trim().ToLower() -replace '\\', '/'
        if ($cached -ne (($PSScriptRoot.ToLower()) -replace '\\', '/')) {
            Write-Host "The build directory belongs to another checkout; starting clean." -ForegroundColor Yellow
            Remove-Item -Recurse -Force $buildDir
        }
    }
}
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

& cmake -G Ninja -B $buildDir -S $PSScriptRoot `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static" `
    "-DCMAKE_C_COMPILER=cl.exe" "-DCMAKE_CXX_COMPILER=cl.exe" `
    "-DPS_BUILD_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })"
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed" }

# main.cpp carries the build time the log prints: touch it so every build stamps a new one.
(Get-Item (Join-Path $PSScriptRoot "src\main.cpp")).LastWriteTime = Get-Date

$log = Join-Path $buildDir "build_output.log"
& cmake --build $buildDir --config Release 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { Fail "Build failed (see $log)" }
# Ninja has exited 0 with errors in its output before: trust the log over the exit code.
$errors = Select-String -Path $log -Pattern '\berror C\d+|\berror LNK\d+|\bfatal error\b'
if ($errors) { $errors | Select-Object -First 10 | ForEach-Object { Write-Host $_.Line -ForegroundColor Red }; Fail "Build failed" }

if ($Tests) {
    & (Join-Path $buildDir "ShowcaseTests.exe")
    if ($LASTEXITCODE -ne 0) { Fail "Self-tests FAILED" }
    Write-Host "Self-tests passed" -ForegroundColor Green
}

$dll = Get-Item (Join-Path $buildDir "PhantasmShowcase.dll") -ErrorAction SilentlyContinue
if (-not $dll) { Fail "PhantasmShowcase.dll was not built" }
if ((Select-String -Path $log -Pattern '^\[\d+/\d+\] ' -Quiet) -and $dll.LastWriteTime -lt $started) {
    Fail "Objects were compiled but PhantasmShowcase.dll was not relinked (see $log)"
}
Copy-Item $dll.FullName -Destination $PSScriptRoot -Force
Copy-Item (Join-Path $buildDir "PhantasmShowcase.pdb") -Destination $PSScriptRoot -Force -ErrorAction SilentlyContinue
Write-Host "BUILD SUCCESSFUL: $($dll.FullName) ($([math]::Round($dll.Length / 1KB)) KB)" -ForegroundColor Green

# Releasing Phantasm

A release is two zips built from a clean checkout of the release commit: `Phantasm-X.Y.Z.zip` for players and
`Phantasm-X.Y.Z-pdb.zip`, the PDB that reads that DLL's crash logs. Phantasm Showcase, the optional demo in
`showcase/`, ships beside it as two more (see "The Showcase" below).

## Steps

1. **Version.** Set `X.Y.Z` in `native/CMakeLists.txt` (`project(Phantasm VERSION X.Y.Z)`: the DLL's file version, its
   SKSE plugin version, the API's `pluginVersion` and the log's `Phantasm X.Y.Z loaded` line) and in
   `native/vcpkg.json` (`version-string`).
2. **Changelog.** Add the `X.Y.Z` entry to `CHANGELOG.md`. Both steps reach `main` through a PR like any change.
3. **Build and test** from a clean checkout of that commit: `native\build.ps1 -Tests`. The build directory is
   `C:\b\phantasm` unless `PHANTASM_BUILD_DIR` names another.
4. **Package**: `.\package.ps1` (`-BuildDir <dir>` for another build directory). It checks that `native/CMakeLists.txt`,
   `native/vcpkg.json` and the DLL's file version agree, that the build came from this checkout and that the checkout
   is clean, then writes both zips to `dist\` and prints their contents and SHA-256. It never replaces a zip without
   `-Force`; `-AllowDirty` and `-AllowOtherTree` make test zips, never a release.
5. **Tag** the release commit and push the tag: `git tag -a vX.Y.Z -m "Phantasm X.Y.Z: <one-line summary>"`, then
   `git push origin vX.Y.Z`.
6. **GitHub release** on that tag, titled `Phantasm X.Y.Z`, with the changelog entry as its notes and both zips
   attached:

   ```
   gh release create vX.Y.Z dist\Phantasm-X.Y.Z.zip dist\Phantasm-X.Y.Z-pdb.zip --title "Phantasm X.Y.Z" --notes-file <entry>
   ```
7. **Nexus**: upload `Phantasm-X.Y.Z.zip` as the main file, version `X.Y.Z`, with the changelog entry. The PDB stays
   on GitHub.

## The Showcase

`showcase/` has its own version (`showcase/native/CMakeLists.txt` and `showcase/native/vcpkg.json`), its own build and
its own `package.ps1`, which writes `dist\Phantasm-Showcase-X.Y.Z.zip` and `dist\Phantasm-Showcase-X.Y.Z-pdb.zip`
with the same checks. When it ships with a release:

1. Build the page, `cd showcase\ui; npm ci; npm run build` (it writes `showcase/data/`, which is committed), then the
   plugin, `showcase\native\build.ps1 -Tests` (build directory `C:\b\phantasmshowcase`, or `PS_BUILD_DIR`).
2. `showcase\package.ps1`, then attach both zips to the GitHub release.
3. On Nexus, upload `Phantasm-Showcase-X.Y.Z.zip` as an optional file of Phantasm's page, its description naming what
   it needs (Magelight UI 1.5.0 or newer, or PrismaUI; SKSE Menu Framework 3 for its ImGui panel).

The Showcase builds against `api/Phantasm_API.h` in this repository, so an API change that breaks it shows up in its
build.

## What the zip holds

| Path in the zip | From |
|---|---|
| `SKSE/Plugins/Phantasm.dll` | the build directory |
| `SKSE/Plugins/Phantasm.ini` | `data/` |
| `SKSE/Plugins/Phantasm/VERSION.txt` | written by `package.ps1`: the version, the commit and the build time |
| `SKSE/Plugins/Phantasm/Licenses/*.txt` | `data/` (the third-party licences), and `LICENSE` as `Phantasm-MIT.txt` |
| `Interface/phantasm.swf` | `data/`: the blank movie the carrier menus load |

`package.ps1` ships every file git sees under `data/`, so a file added there ships with no change to the script. Never
in the zip: `Phantasm.pdb` (the second zip), `PhantasmProbe.dll` and `testclient/` (a development client), and `api/`
(a client copies the header from this repository).

SeverActions carries its own copy of Phantasm, whose `VERSION.txt` ends `- bundled with SeverActions <version>`. The two
installs share their files (SeverActions' copy has no `Phantasm.ini`), and for each one the install the mod manager puts
last wins, so the newer Phantasm goes last.

## Why there is no FOMOD

Phantasm is one DLL for SE, AE and VR with no options, so an installer would have nothing to ask. The zip is laid out
like `Data`: a mod manager installs it as it is, and a manual install extracts it into `Data`.

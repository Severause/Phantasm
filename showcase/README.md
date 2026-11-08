# Phantasm Showcase

Five pages that show what [Phantasm](../README.md) draws, live, in Skyrim SE and AE:

- **Lineup**: up to four people side by side, each with their own idle and expression, under one light.
- **Armory**: four weapons or pieces of armour on turntables, armour drawn as worn by a man or a woman.
- **Animations**: any loose animation file under `meshes/actors`, played on anyone, with next and previous.
- **Studio**: one person full screen, posed and lit, saved as a picture or a transparent cutout.
- **Wardrobe**: an NPC before and after, side by side, while you try clothes on them.

Press **H** on any page to hide every control, for recording.

It is an SKSE plugin with a [Magelight UI](https://github.com/Severause/MagelightUI) page. It has no plugin file and no
Papyrus scripts.

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x / 1.7.x, with SKSE and Address Library
- Magelight UI 1.5.0 or newer, or PrismaUI (flat only, needs Phantasm 1.0.0 when Magelight is not installed; see below)
- Phantasm 0.2.0 or newer; 0.4.0 draws four views at once (one before), and 1.0.0 holds the wardrobe's "before" as a
  live view (older ones show a turntable of pictures instead)

VR loads it, and Phantasm draws two views at once there, so the Lineup and Armory show two panels.

## Use

Press **F10**. `My Games/.../SKSE/PhantasmShowcase/settings.json` changes the key (`menuKey`, a DirectInput scan code),
the page's size (`uiScale`, 0.7 to 1.6; it already fits your screen's height), the long side of a saved picture
(`shotSize`, 512 to 4096) and the animation folders the browser hides (`hiddenAnimFolders`: folder names, or paths
under `meshes`), whether each view shows its frame rate (`showFps`, default on; also the FPS switch in each page's
dock) and whether the game stops rendering its world behind the menu (`freezeWorld`, default off; also the Freeze world
switch). The views draw once per frame the game renders, so freezing the world behind the opaque menu can give them
more frames on a machine the world render holds back. Frame generation shows more frames than the game renders, and
the views draw only the rendered ones: the readout's `game` number is the rendered rate. Freezing needs Magelight 0.31.0 or later on flat Skyrim and is off when Magelight.json says
`"freezeWorld": false`. Pictures are saved to `My Games/.../SKSE/PhantasmShowcase/Shots`. With Phantasm 1.0.0 or later, a slow
machine can lower how much of each frame the views take in Phantasm's own `Data/SKSE/Plugins/Phantasm.ini`
(`[LiveViews] Budget = light`).

The animation browser lists loose `.hkx` files only: animations packed in a BSA are not listed.

### In an ImGui menu

With [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) 3 installed, its Mod Control
Panel gets a **Phantasm Showcase > Live view** section: a live view of anyone nearby in an ImGui window, with the
lighting, expression, floor, weapons and pose choices of the pages, drag to turn and the wheel to zoom. **Pop out**
moves the view into a window of its own you can drag and resize. It needs neither Magelight nor PrismaUI, but without
Magelight it needs Phantasm 1.0.0 or newer.

### On PrismaUI

Without Magelight the menu runs on PrismaUI instead (`uiHost` in settings.json: `auto`, the default, picks Magelight
when it is installed, else PrismaUI; `prisma` forces PrismaUI on an install with both; `magelight` never uses
PrismaUI, even when Magelight is missing). PrismaUI cannot show a picture from another mod, so the showcase draws the backdrop and the live views into
the game's frame and PrismaUI draws the page over them. The menu key opens and closes it, Escape closes it. Phantasm
1.0.0 or newer is needed when Magelight is not installed (older ones refuse views without it). Not on VR.

The wardrobe never dresses you or a child. A piece the NPC does not own is lent for the fitting, and Revert (or closing
the menu) takes back exactly the copies it lent and puts their own clothes back on.

## Building

- Plugin: `native\build.ps1 -Tests` (Visual Studio 2022+ with C++, CMake, Ninja, vcpkg from `VCPKG_ROOT` or
  `C:\vcpkg`). It builds against Phantasm's own header, `../api/Phantasm_API.h`.
- Page: `cd ui`, `npm ci`, `npm run build` (writes `data/Magelight/PhantasmShowcase/views/main`). `npm run dev` previews
  the page in a browser with a mock backend.
- Release zip: `package.ps1` in this folder, after both builds (see `../docs/RELEASING.md`).

## Licence

MIT. See `LICENSE`. The page ships Source Sans 3 (Adobe) under the SIL Open Font License 1.1, and is built with React,
React DOM and Scheduler (Meta, MIT) and the Magelight SDK (MIT); their notices ship beside the page in `licenses/`. The plugin is built
with SKSE Menu Framework 3's API header (MIT, Thiago Kaique; `native/extern/skse-menu-framework/LICENSE.txt`).

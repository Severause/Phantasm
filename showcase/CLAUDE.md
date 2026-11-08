# Phantasm Showcase - project instructions

Phantasm Showcase is a set of pages that show off Phantasm, the live 3D renderer: an SKSE plugin with a Magelight UI
page (or PrismaUI) and an ImGui panel, no plugin file and no Papyrus. It lives in Phantasm's repository as `showcase/`,
an optional download beside Phantasm, and is the complete example of a Phantasm client. It exists to film and screenshot Phantasm, so every page must look good with its
controls hidden (clean mode). Read `docs/DESIGN.md` first; its principles (section 3) are the rules a change is held to.

## Rules

1. **Comments say what the code cannot**: why, a contract other code relies on (thread, lock, order), a warning
   against a known regression, units and sentinels. No history, no restating the code. A comment that is wrong about
   the code beside it is a bug: fix it in the same change.
2. **Engine-free rules live in `native/src/core`** and each has a test in `native/tests`. Run `native\build.ps1 -Tests`
   after touching one.
3. **`RE::` state on the game thread only.** A Magelight JS listener runs on the render thread inside Present: copy its
   argument and post with Magelight's `PostGameTask`, never SKSE's `AddTask`. A task never re-queues itself.
4. **Equips, adds and removes go through `Lane::Run`** (SKSE's UI-task queue), never from inside the SKSE task drain,
   and an NPC's 3D is rebuilt only there, through Phantasm's `RebuildActor3D`.
5. **Never remove an item by its base form alone.** The wardrobe takes back only the plain copies it lent (no
   enchantment, temper, name or soul), never more than it lent (DESIGN.md section 5).
6. **Phantasm views are slots, not actors.** A page names one of `ViewCore::kSlots`; every view closes when its page,
   its tab or the menu goes away. Events are matched by view id, never by slot.
7. **The page and the DLL change together.** A field added to `docs/PROTOCOL.md` is added to `native/src/Bridge.cpp`,
   `ui/src/api/protocol.ts` and the mock (`ui/src/api/mock.ts`) in the same change.
8. **Every size in the stylesheet is `calc(Npx * var(--ui))`** (hairlines excepted): the page scales with the screen.

## Build

`native\build.ps1 -Tests` (build dir `C:\b\phantasmshowcase`, `PS_BUILD_DIR` overrides; vcpkg from `VCPKG_ROOT`,
else `C:\vcpkg`). The CommonLib port is a byte copy of Phantasm's, so the two share vcpkg's binary cache: keep it
byte-identical. The release zip: `package.ps1` in this folder (`docs/RELEASING.md` at the repository's root).

Page: `cd ui; npm.cmd ci; npm.cmd run build`. `npm.cmd run dev` serves the page with the mock backend on port 5183;
`?old` in the URL plays a Phantasm without frozen views, `?slowfreeze` one whose frozen view holds only after 12 s.

## Testing

- The log is `PhantasmShowcase.log` in the SKSE log folder.
- DLL copies fail while the game runs.

## Layout

- `native/src` the plugin; `native/src/core` engine-free rules; `native/tests` their tests
- `native/extern` Magelight's, PrismaUI's and SKSE Menu Framework's API headers, copied unchanged; Phantasm's is
  `../api/Phantasm_API.h`
- `ui/` the page (React, Vite); built into `data/`
- `data/` the files installed under `Data`
- `docs/DESIGN.md` the design; `docs/PROTOCOL.md` the page protocol

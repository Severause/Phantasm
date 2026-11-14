# Phantasm - contributor rules

Phantasm is a shared actor renderer for Skyrim SE, AE and VR: an SKSE plugin with a frozen C API
(`api/Phantasm_API.h`) that hands other mods a live picture of an actor or an item, as a Magelight texture image or a
D3D11 SRV, and stills. Read `docs/DESIGN.md` first; `docs/API.md` is the client's guide.

## Rules

1. **The API is frozen.** Never change or remove a function, field, enum value or reason token of `Interface1`. A new
   capability is appended to the end of `Interface1`'s table or of a struct, behind its `size` (an appended field
   starts at or past the older struct's sizeof), with a "since" note. Freeze outcomes, never pose, light or camera
   settings: those are hints (unfrozen) or the renderer's own choice.
2. **Comments say what the code cannot**: why, a contract other code relies on (thread, lock, order), a warning
   against a known regression, units and sentinels. No history. A comment that is wrong about the code beside it is a
   bug: fix it in the same change.
3. **Engine-free rules live in `native/src/core`** and each has a test in `native/tests`. Run `native\build.ps1 -Tests`.
4. **Threads.** `RE::` state on the game thread only. Never call SKSE's `AddTask` from inside a render phase
   (Magelight's Present, a carrier's `PostDisplay`): post through `PresentHandoff` (an API call always through
   `PumpTask`). A task never re-queues itself. 3D rebuilds go through `MutationLane`, never inside the `AddTask` drain.
   Events reach clients on the game thread, outside any render phase, with no lock held.
5. **Every engine menu subclass carries the VR IMenu tail (0x40 bytes)** (`Carriers.h`).
6. **Compare ids that cross a Papyrus or UI boundary case-insensitively**; client and slot names are lower case by rule.
7. **Third-party code**: MIT or similar only, with its licence file under `data/SKSE/Plugins/Phantasm/Licenses`.
   Read GPL mods (HDT-SMP) for ideas, never copy their code.
8. **Line endings and encoding**: keep each file's as it is. Most files are CRLF in the working copy; edit with a tool
   that keeps them (never `sed -i`), and never add or remove a UTF-8 BOM.
9. **Docs follow the code**: a change a client can see updates the header, `docs/DESIGN.md`, the feature table in
   `docs/API.md` and `CHANGELOG.md`.

## Build and test

- `native\build.ps1 -Tests` builds `Phantasm.dll` and the probe and runs the engine-free self-tests; a failing check
  fails the build. The script's header lists what it needs; `PHANTASM_BUILD_DIR` sets the build folder. Packaging a
  release: `docs/RELEASING.md`.
- The log is `Phantasm.log` in the SKSE log folder (`Documents/My Games/<game>/SKSE`).
- The probe (`testclient/`) is a development client that never ships. F7 opens a HUD panel with a live view of the
  crosshair actor or the player; Ctrl+F7 toggles the captured pose, Alt+F7 the size, Shift+F7 / Ctrl+Shift+F7 step
  through animation stages (its `clips.txt`, else a few vanilla idles), Ctrl+Alt+F7 brings a partner in and
  Alt+Shift+F7 toggles the `aim` hint between face and figure. The
  numpad, or the arrows, Page Up / Down and Home, drive the camera. F8 and its modifiers open more panels and ask for
  stills; the top of `testclient/src/Probe.cpp` lists them.

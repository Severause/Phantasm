# Phantasm

Phantasm is an SKSE plugin that draws live 3D pictures of actors and items for other mods: an NPC or the player as
they look now, in their pose or an idle, or an item's model. It does nothing on its own. A mod that wants such a
picture asks Phantasm for it through Phantasm's API and shows it on its own page; which mods use Phantasm is up to
them.

A mod can get:

- a live view shown as a Magelight UI image, which its page shows like any other image;
- a live view it draws itself from the D3D11 texture Phantasm hands it, with no UI host at all;
- a still: one picture handed to the mod as pixels.

Phantasm has no ESP and writes nothing into your save.

## Requirements

- Skyrim Special Edition 1.5.97, Anniversary Edition 1.6.x, or Skyrim VR.
- SKSE: SKSE64 2.0 or later, or SKSEVR on Skyrim VR.
- Address Library for SKSE Plugins, or VR Address Library for SKSEVR on Skyrim VR.
- Magelight UI, only for views a mod shows as Magelight images and for Phantasm's own settings page (Magelight UI
  1.5.0 or newer). Phantasm uses Magelight's GPU texture images (its API version 2, on its D3D11 renderer); a client
  mod names the Magelight version it needs. Stills, and views a mod draws itself, need no UI host.
- SKSE Menu Framework 3, only for Phantasm's settings panel in its Mod Control Panel.

Live views and stills work on Skyrim VR too, but VR has been tested less than SE and AE.

## Install

The zip, and the optional Showcase, are on [Phantasm's Nexus Mods page](https://www.nexusmods.com/skyrimspecialedition/mods/197470).
Install it with Mod Organizer 2 or Vortex like any other SKSE plugin, or copy the archive's contents into the game's
`Data` folder. Its files there are `SKSE/Plugins/Phantasm.dll`, `SKSE/Plugins/Phantasm.ini`, `Interface/phantasm.swf`
(a blank movie the plugin needs), `SKSE/Plugins/Phantasm/PhantasmNRInProc.dll` (neural rendering's library, loaded
only when neural rendering is turned on), `Magelight/Phantasm/views/settings/index.html` (the settings page Magelight
UI shows) and the third-party licences in `SKSE/Plugins/Phantasm/Licenses`.

### Phantasm Showcase (optional)

An optional download beside Phantasm, to see what it can do: five pages (four people side by side, items on
turntables, any animation file on anyone, a studio that saves pictures, a wardrobe that shows before and after) and a
live view in SKSE Menu Framework's Mod Control Panel. It needs Magelight UI 1.5.0 or newer (or PrismaUI on flat
Skyrim), and SKSE Menu Framework 3 for its ImGui panel. Press F10 to open it. Its source is [showcase/](showcase/).

### With SeverActions

SeverActions 4.x ships its own copy of Phantasm, usually an older one, as a required file. Install this Phantasm as well
and let it win the file conflict:

- MO2: place Phantasm below SeverActions in the left pane.
- Vortex: when it reports the conflict, set Phantasm to load after SeverActions.

Every Phantasm 1.x keeps everything 0.4.0 offered its clients, so SeverActions works with it.

## Uninstall

You can remove Phantasm at any time: it has no ESP and writes nothing into the save, so a save made with it loads
without it. A mod that uses Phantasm has no picture to show while it is gone; that mod's own page says whether it needs
Phantasm.

## Settings

Phantasm's settings can be changed in the game, on either of two pages, and a change holds at once:

- **SKSE Menu Framework** (optional): its Mod Control Panel, Phantasm > Settings.
- **Magelight UI** (optional): the page **Scroll Lock** opens. Either page changes that key (Change, then press the
  new one), and so does `[SettingsPage] Key` in `Phantasm.ini` (a DirectInput scan code; 0 for none).

Both say whether neural rendering is running, and why not, and both write the changes to
`Data/SKSE/Plugins/Phantasm.ini`, whose comments stay. Without either page, edit that file: it is read when the game
loads its data. A change of where neural rendering's library comes from, made after it has found one, waits for the
next start of the game.

The file's `[LiveViews]` section sets how much of each frame the live views may take, in CPU time. Actor views and
item views each get this budget.

- `Budget = smooth` (the default): about 14 ms a frame (4 on VR) while the game is paused, so every view of a typical
  page draws every frame; while the game runs, balanced's budget.
- `Budget = balanced`: 6 ms (3 on VR). The newest view draws every frame and the others take turns. This was
  Phantasm 0.4.0's budget.
- `Budget = light`: 3 ms (1.5 on VR), for a machine where the views cost the game's frame rate.
- `BudgetMs`: a number of milliseconds (0.5 to 50) that replaces the mode's budget; 0 keeps the mode's.

`[NeuralRendering]` is the experimental neural rendering of character pictures (it needs an `nvngx_dlssnr.dll` from a
neural rendering mod). `Enabled = 1`, or the switch on a settings page, turns it on: the file ships with `Folder = auto`
and its live views on, so that one switch is all it takes. The file explains the rest.

An update of Phantasm overwrites `Phantasm.ini`. Note your changes and make them again, or, in MO2, keep your own copy
in a small mod placed below Phantasm.

## Log

Phantasm writes `Documents/My Games/<game>/SKSE/Phantasm.log`, where `<game>` is the game's folder there, such as
`Skyrim Special Edition` or `Skyrim VR`. It says which Phantasm loaded, whether Magelight's texture images are
available, which budget is in force, and why a view could not draw. Include it when you report a problem.

## For mod authors

Phantasm's API is one C++ header, `api/Phantasm_API.h` (MIT): copy it into your plugin unchanged.
[docs/API.md](docs/API.md) shows how to use it, and [docs/DESIGN.md](docs/DESIGN.md) explains the rules behind it.
[CHANGELOG.md](CHANGELOG.md) lists what each version added. Building and packaging Phantasm:
[docs/RELEASING.md](docs/RELEASING.md).

[showcase/](showcase/) is a complete client: an SKSE plugin with a React page that shows Phantasm's views three ways (as
Magelight images, drawn under a PrismaUI page from the SRV, and in an ImGui window), with item views, stills, hints and
queries. Its `native/src/Views.cpp` is the place to start.

## Credits

- **ChunkyChud** shared Isolated NPC Preview, an experimental preview renderer, with its source. Phantasm took ideas
  from it, not code: decoding model-space normal maps the engine's way, hiding dismembered partitions, skinning each
  mesh as captured, following the live pose, playing an actor's idle clips while the game is paused, springs for hair
  and cloth bones, honouring a material's vertex colours, mood expressions from the face's `.tri` files, and rendered
  portraits.
- **CBPC** (CBPCSSE): the CBPC bone physics step is ported from its `Thing::update` (MIT; licence in
  `data/SKSE/Plugins/Phantasm/Licenses/CBPCSSE-MIT.txt`).
- **RaceMenu** (expired6978): early development builds read overlay tint and alpha through RaceMenu's override
  interface (`IOverrideInterface::GetNodeProperty`), using a subset of its modder header
  (`ModderResource/IPluginInterface.h`). RaceMenu's source showed where it keeps those values, which Phantasm now reads
  from the overlay's own material, and how a dynamic face shape's partitions index its vertices.
- Behaviour checked against, no code taken: FSMP / HDT-SMP (bone renaming and physics XML), Community Shaders
  (lighting, hair tint and TruePBR rules), Open Animation Replacer (which replacement clips an actor plays), nif.xml
  from NifTools (the particle block layouts).
- Libraries: CommonLibSSE-NG, DirectXMath, DirectXTK, DirectXTex, fmt, nlohmann/json, OpenVR, rapidcsv, spdlog and
  xbyak, SKSE Menu Framework's API header (the settings panel) with the Dear ImGui declarations it carries in
  cimgui's wording, and Khronos's Vulkan headers, which `PhantasmNRInProc.dll` is compiled against; their licences
  ship in `data/SKSE/Plugins/Phantasm/Licenses`.

## Licence

Phantasm is MIT-licensed (`LICENSE`), the API header included. Magelight UI's API headers in `native/extern/magelight`
are MIT too (their `LICENSE.txt`). The libraries above keep their own licences: MIT (the Vulkan headers' choice of
Apache-2.0 or MIT taken as MIT), except OpenVR, rapidcsv and xbyak (BSD 3-clause).

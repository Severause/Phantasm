# Phantasm Showcase - design

## 1. What it is for

Phantasm draws actors and items live into Magelight pages. The showcase is a place to film that: five pages, each one
thing Phantasm does well, laid out so a recording needs no editing. It is a client like any other (client name
`showcase`), so it also proves the API covers what a mod would want.

## 2. Pages

| Page | Views | What it shows |
|------|-------|---------------|
| Lineup | `lineup0`-`lineup3` | up to four actor views, `pose` captured and `motion` on; per panel the actor, idle (`QueryHint("idles")`) and expression; shared `lighting`, `floor`, `weapons` |
| Armory | `armory0`-`armory3` | four item views (`OpenItemView`), `worn` male / female / off; a turntable sends one `viewOrbit` for every drawing panel each 33 ms |
| Animations | `anim` | one actor view playing `idle=file:<path>` from the loose files under `meshes/actors`, or one of their own idles |
| Studio | `studio` | one actor view full height; Save writes a Figure still (what the view shows) or a hinted Cutout (`turn`, `pitch`, `lighting`, `weapons`) as a PNG |
| Wardrobe | `before`, `wardrobe` | before and after: see section 5 |

Outside the page, a panel in SKSE Menu Framework's ImGui menu shows one live view (section 10).

The number of panels follows `maxDrawing`: 4 with Phantasm 0.4.0, 2 on VR, 1 before 0.4.0 (an older Phantasm draws
only the newest view, so more panels would wait).

## 3. Principles

- **Filmable first.** Every page reads well with its controls hidden (H, the clean mode): the pictures fill the space
  they had, nothing else is drawn but name plates.
- **One slot per place on a page.** Phantasm keeps a slot's image for the life of the process, so the slots are a fixed
  set (`ViewCore::kSlots`), re-targeted as the page changes, and closed when the page, its tab or the menu goes away.
- **Phantasm decides how things look.** The showcase only sets hints the API documents; an unknown one is ignored, so a
  newer Phantasm never breaks a page.
- **Nothing the player owns is touched.** Only the wardrobe changes inventory, and only by its own lent copies.
- **Sizes scale with the screen.** The page is drawn for 1080 pixels high; every size is `calc(Npx * var(--ui))`.

## 4. The Phantasm client (`Views`)

`Views` holds one record per slot: the view id, what it shows, its size and the hints it was last told. `viewOpen`
opens the slot, re-targets it (the view keeps its id) or resizes it, and brings its hints to what the page sent; a
re-target tells every hint again. Events are matched by view id (a slot can be reopened under a new id before an old
`Ended` arrives) and by the target's FormID (a re-target's late event names the old one). A `Reply` is pushed with its
slot, key and FormID. Stills (`RequestStillHinted`) answer on the game thread; their pixels are copied and handed to
`Shots`, which encodes PNGs through WIC on one worker thread (a Cutout is un-premultiplied in linear light first).

## 5. The wardrobe

Phantasm draws one live look per actor, so "before" needs care:

1. **Open.** `tryBegin` starts a session on the lane and records the stacks the NPC wears. The player and children are
   refused (`TryOnCore::Refusal`): a paused rebuild of the player's 3D is not safe, and a child is never dressed.
2. **Before.** The left view opens with `freeze=on` and the page asks `QueryHint("freeze")` until it answers
   `held:true` (Phantasm 1.0.0: the view holds the look it drew). Only then does try-on unlock, because a piece put on
   before the look is held would show on both sides. A Phantasm that answers `known:true` is waited for however long
   the hold takes (the view stays live and smooth to turn; Phantasm freezes a look only once it is complete), with a
   "Use pictures" button in the side panel past 8 s. A Phantasm that answers `known:false` or nothing within 8 s, a view that
   could not open or ended, a view that waits where only one draws, or that button gets the fallback: the left view
   closes and 24 Cutout stills are taken round the NPC (`TurnCore`), shown as a turntable the player drags; try-on
   unlocks when every frame is in. Either way the compare uses two views, so a frozen view is
   never pushed out (a view that waits loses its frozen look).
3. **Try on.** `tryWear` equips the piece at once. Outfit pieces only: a shield would put down what the left hand held
   (a two-handed weapon, a spell, a torch), which the session does not record, so it is refused (`handItem`) and the
   picker's filter is `outfit`. `tryBegin` and `tryWear` are refused (`closed`) while the menu is closed, so a request
   sent just before the close cannot start a fitting that lasts through play. When the NPC holds no copy of it (`TryOnCore::Lends`) one plain copy
   is added and counted in the session's ledger. The NPC's 3D is rebuilt through `RebuildActor3D` on the lane.
4. **Revert.** Every armour stack worn now that was not worn at the start is unequipped, the recorded stacks are put
   back on (by their own list when it is still in the inventory), and each lent item is taken back: plain copies only,
   never more than were lent (`TryOnCore::TakeBack`), copies in no list first so the engine never chooses a stack.
   Closing the page, changing NPC or closing the menu reverts too; on the next open the page starts a new session for
   the same NPC.

## 6. Animations

One walk of `Data/meshes/actors` runs on a worker thread started at kDataLoaded (`Anims::Prewarm`, MO2's virtual
folder included), so the list is ready before the menu first opens; an `anims` request before it ends answers
`scanning` and the page gets an `anims` push when it is ready.
A file counts when it is a `.hkx` under a folder named `animations` and not under `behaviors`, `characters` or
`character assets` (`AnimPathCore::IsAnimation`); a folder the player listed in `hiddenAnimFolders` is left out. The
list is kept sorted by its folded path, so a folder listing is one range and a search one pass. Files in BSAs are not
seen: the walk reads the disk, not the game's archives. A path the page sends back is played only if it is a relative
`.hkx` with no `..` (`AnimPathCore::SafeToPlay`).
A picked file plays through the view's `idle` hint (`file:<path>`). The page then asks `QueryHint("idles")` again
every 400 ms (25 times at most) until the answer names that choice and is not `pending`. A view that cannot play it
holds the last pose, so an answer with `plays: false` puts its `reason` over the stage; one still `pending` when the
asks run out shows its `reason` (a try that failed before the choice) as not playing yet. An older Phantasm never
reports a refused file, so nothing shows for one.

## 7. Settings

Per install, `My Games/<game>/SKSE/PhantasmShowcase/settings.json`: `menuKey` (default F10, 0x44), `uiScale` (0.7 to
1.6), `shotSize` (a saved picture's long side, 512 to 4096, default 2160), `hiddenAnimFolders` (default empty),
`showFps` (default true: each live view asks Phantasm for `QueryHint("stats")` once a second and shows the rate it kept
up at in its bottom-right corner, `held` when it had nothing new to draw; hidden in clean mode; an older Phantasm
answers `known: false` and nothing shows; the docks' FPS switch writes it), `freezeWorld` (below) and `uiHost` (`auto` / `magelight` /
`prisma`, section 9). Beside the rate the pill shows `game N`:
every frame the game renders gives each view one turn (drawn, unchanged, stalled or waiting), so the sum of those
rates is the game's rendered frame rate, and the dot's colour is the share of it the view kept (green 90% or more,
amber 60%, red below). `freezeWorld` (default false, as Magelight's own freeze has not been run in game yet): the view
asks Magelight to freeze the world behind it (`SetViewFreezeWorld`, Magelight 0.31.0+, flat only; `freezeAvailable` in
`hello` says whether it can; never on PrismaUI, which has no such call). Magelight sets the engine's freeze-frame flag
on its own focus menu only while the view holds UI mode with the game paused and takes it off itself on close, a load,
an unpause or another view taking UI mode, so the game skips its 3D world render and the frames it saves go to the
views. Phantasm draws in its carriers' PostDisplay, which the UI pass still runs; the page is opaque, so the frozen
frame never shows through. The docks' Freeze world switch writes it.

## 8. Threads

- The page's requests arrive on Magelight's render thread, are copied and posted with `PostGameTask`, and are handled
  on the game thread; replies and pushes go back with `InteropCall` from any thread. On PrismaUI they arrive on
  PrismaUI's thread and are posted with SKSE's `AddTask`.
- The underlay draws on the main thread during the engine's UI render (`PostDisplay`), holding its mutex only to copy.
- Phantasm's events and still answers arrive on the game thread.
- The ImGui panel draws on SKSE Menu Framework's render thread, inside the frame's Present (section 10).
- Inventory changes and 3D rebuilds run on the lane (`AddUITask`).
- The animation walk and the PNG encoder run on their own threads and touch no `RE::` state.

## 9. PrismaUI

`HostCore::Choose` picks the host at kDataLoaded: `uiHost` `prisma` takes PrismaUI when it is installed, otherwise
Magelight; `magelight` takes Magelight or nothing; `auto` takes Magelight when it is installed, else PrismaUI; never
PrismaUI on VR, whose pages are overlays the frame does not reach. On PrismaUI every view is opened with the view
hint `hostless=on` (`Views::WithHost`): Phantasm draws a view without Magelight only for a client that draws the SRV
itself, and refuses every other one `no-image` as before. The same
page serves both (`ui/src/lib/host.ts`): the build copies it into `Data/PrismaUI/views/PhantasmShowcase`.

- **No outside pictures in a PrismaUI page.** PrismaUI 1.5.1 renders pages on the CPU (Ultralight) and copies each into
  its own texture; its API has no way to place another mod's texture. So the page draws its backdrop and its stages
  see-through (`html.host-prisma`) and reports each stage's rect, the part of it it shows and its corner radius
  (`stageRects`, `lib/underlay.ts`, measured every animation frame and sent when they change), and the DLL draws the
  backdrop, each stage's gradient and Phantasm's SRV for its slot into the frame (`Underlay.cpp`).
- **The draw order.** The underlay is an engine menu (depth 5) drawing from its `PostDisplay`, during the engine's UI
  render inside the frame. PrismaUI draws its pages from its hook on the frame's Present (its log and strings name
  `D3DPresent` and "Installing D3D Present hook"), which is after every menu's draw, so the page lands on top whatever
  the load order; Phantasm's carriers (depth 0) draw the views' frames before the underlay in the same frame.
- **The SRV.** Kept (AddRef'd) from a view's `Shown` until its next `Shown`, `Waiting` or `Ended`, or until the slot
  is closed or refused (its `Ended` then finds no slot, so `Views::Close`, `CloseAll` and a refused `Open` clear it).
  A `Reply` keeps it, against the header's "release at the next event of any kind": the readout queries every view
  once a second and Phantasm sends no `Shown` after a `Reply`, so following the header would blank every stage. The
  reference keeps the texture alive, so the worst case is a stale picture, never a freed one; Phantasm sends a new
  `Shown` whenever the offscreen is rebuilt. The header should say a `Reply` leaves the SRV valid.
- **The target.** The underlay draws into the render target the UI render left bound and scales the page's boxes to
  it; a bound target other than the frame buffer is logged once with both sizes.
- **Opening and closing.** PrismaUI binds no keys: an input sink takes the menu key. It opens only in play: no game
  menu open, nothing pausing the game, and no other PrismaUI view focused (`MayOpen`). Open = Show + Focus (paused) + the
  underlay menu + a `uiMode` push; Escape in the page sends `close`; a game load closes it. A `hello` while the page is
  hidden (PrismaUI loads it at the main menu) answers `open: false` and opens no view.
- **Costs.** The underlay logs its CPU time every 600 frames ("underlay: ... CPU x ms a frame").

## 10. ImGui (SKSE Menu Framework)

A Phantasm view is a D3D11 texture, so it draws in an ImGui window as well as in a page: Shown carries the SRV, which
`ImGui::Image` (or an `ImDrawList::AddImage`) takes as its texture ID on the engine's device. The showcase proves it in
SKSE Menu Framework 3's Mod Control Panel (`ImGuiPanel.cpp`, header `native/extern/skse-menu-framework`, MIT): a
section "Phantasm Showcase > Live view" and a window it pops out, which share one view in their own slot
(`ViewCore::kPanelSlot`, never a page slot, so the page can neither open nor close it). Nothing is registered without
the framework, or with one that lacks an export the panel calls (the header's ImGui wrappers call them unchecked) or
runs another ImGui than 1.90 (the header's structs are that version's). ImGuiIO's mouse fields are read only once its
`Ctx`, just before them, is the current context; else the wheel does not zoom and the window opens at a fixed size.

- **What it shows.** Whom to show (the people list, the crosshair's actor or the player first, `PanelCore::DefaultPick`),
  the lighting and expression choices of the pages, Floor, Weapons, Classic pose and Motion; drag to turn and the
  wheel to zoom, as the pages turn (`PanelCore::Orbit`, `Zoom`). The picture is a 3:4 box as large as the space allows
  (`PanelCore::Fit`), on the pages' stage gradient; the view follows a new size only once it has held still for a
  quarter of a second (`PanelCore::ResizeGate`), as each resize rebuilds Phantasm's offscreen.
- **Hostless.** The panel's view always carries `hostless=on`, so it draws without Magelight (Phantasm 1.0.0); with
  Magelight installed every Shown carries the SRV anyway.
- **Premultiplied alpha.** Phantasm's offscreen is premultiplied and ImGui's dx11 renderer blends straight alpha, which
  would darken every soft edge. Two draw-list callbacks set a premultiplied blend state around the picture and put the
  renderer's back.
- **The SRV's life.** Kept from Shown until the view's next Shown, Waiting or Ended; every one handed to ImGui in a
  frame is kept until the panel's first draw of a later ImGui frame (`igGetFrameCount`), as ImGui renders the frame's
  lists after the section and the window have both returned, or until the pump's idle close a second after the last
  draw. Nothing else lets them go: a game load closes the view and leaves them to the pump.
- **Threads.** The framework calls the panel on its render thread inside Present. Phantasm may be called there; the
  people list (`RE::` state) is built in an SKSE task that a pump thread posts, never posted from inside Present.
  Views' events reach the panel on the game thread under Views' lock, so the panel calls Views only with its own data
  lock free. The draw holds a render lock that the pump takes too.
- **Closing.** A panel not drawn for a second (the menu closed, another section chosen, the window closed) has its view
  closed by the pump; a game load closes it, and so does a picture box too small to draw for a quarter of a second.
  A view that ends for a reason of the actor's (unloaded, dead, unrenderable) is not opened again for them while the
  panel stays up, until Try again or another choice; a panel shown again later starts afresh. One ended by a game
  load, the first two `no-image` ends in a row (Magelight before its first in-game frame), and a refused open are
  tried again after two seconds. What the panel shows is about the view it last opened (the id `Views::OpenPanel`
  returns, so a new view starts it afresh) and the actor it last chose (an event still on its way for the one before a
  re-target, which keeps the view's id, is dropped). The stage's notes are the page's words (`PanelCore::Note`).

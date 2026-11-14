# Phantasm - design

Phantasm is a shared actor renderer for Skyrim SE, AE and VR: one SKSE plugin that draws a live picture of an actor (an
NPC or the player) or an item and hands it to any mod that asks, as a Magelight texture image or a raw D3D11 shader
resource view, or as a still.

It is SeverActions' `MannequinRenderer` (the Outfits mirror and the party cutouts) taken out of SA, so that several
mods share one renderer instead of each carrying a copy. [API.md](API.md) is the client's guide; this file is the
reasoning behind the API and the renderer's rules.

## 1. Goals and limits

- **One renderer, many clients.** A client registers once, opens views of actors and shows them on its own page.
- **Freeze outcomes, never settings.** The API promises "a picture of this actor"; pose, light, camera and every other
  look decision stay the renderer's, free to change in any release. A client that needs a say uses hints (section 5),
  which are best-effort and unfrozen.
- **No SA dependency.** Phantasm knows nothing of SA's menus, stores or settings. What SA needs from the renderer comes
  in through the API like any other client's.
- **Requirements:** SE, AE or VR, SKSE, Address Library. Neither settings page is required: the panel needs SKSE
  Menu Framework 3, the page Magelight UI 1.5.0 or newer (section 13). A view is published as a Magelight texture image when
  Magelight with GPU texture images is installed (Phantasm asks for Magelight's API version 2, which carries them).
  With no Magelight at all, or one too old for texture images (which counts as none), since 1.0.0 a view whose client
  set its `hostless` hint draws with no image: its `Shown` carries the SRV alone, imageName and imageUrl empty, for the
  client's own D3D11 draw (`ImageHostProbeCore::DrawsHostless`; another UI host's page, an overlay). Every other view
  there ends `no-image` as before 1.0.0, so a client that did not ask never gets a picture it cannot show. A Magelight
  that cannot take images (its CPU renderer, or before its first in-game frame) ends views `no-image`, hostless or
  not. A hostless view still skips the frames whose picture cannot have changed; with no image to invalidate, a
  client drawing the SRV every frame draws the last picture.
- **Not in this version:** a head-and-shoulders live view (`Framing::Portrait` is a still only).


## 2. The API (`api/Phantasm_API.h`)

The header is the contract; this section is the reasoning behind it.

- **Entry:** `GetProcAddress(GetModuleHandleW(L"Phantasm.dll"), "RequestPluginAPI")(kInterface1)` returns the
  `Interface1` table or null (the PrismaUI / TrueHUD / SKSE Menu Framework pattern). Absent or too old = no picture;
  a client never fails hard.
- **Frozen forever:** every function, struct field, enum value and reason token in `Interface1`. `Interface1` grows
  only by appending: functions to the end of the table and fields to the end of a struct (each struct carries its
  `size`, and an appended field starts at or past the older struct's sizeof), so `RequestPluginAPI(1)` keeps answering
  with the same table and a client checks `size` before using anything added after `Interface1`'s first build
  ([API.md](API.md), Compatibility).
- **Clients** have a name (1-24 of `a-z 0-9 - _`), unique per process, and one event callback.
- **Views** live in a client's named slot. A view's image is `phantasm.<client>.<slot>` for the life of the process
  (Magelight's rule: an image a page may still reference is never unregistered), so a page can name it before the first
  frame and reopening a slot reuses the registration. `OpenView` on a held slot re-targets the view (same id): this is
  SA's mirror switching NPCs. Because a registration is never dropped, slots are a small fixed set a client reuses,
  one per place on its page, never a per-actor id (a party sheet has slots `face-1`..`face-4`, not one per NPC).
- **Item views** (`OpenItemView`, appended to the `Interface1` table: a client checks `size` first) show an item's base
  form under the same slot, image, size and event rules, drawn by the item renderer (`ItemRenderer.h`) on its dark card.
  `Orbit`, `Zoom`, `ResizeView` and `CloseView` act on them; `Refresh` and stills do not. Of the hints they take `worn`
  and `hostless`, and of the queries `stats`; any other query answers `{"known":false}`. A slot re-targeted from an
  actor to an item (or back) becomes that kind of view. The item's events carry its FormID in `actorFormId`. Only the
  base form is drawn: an instance's enchantment or tempering is not an input. While an item view draws the game refuses
  to save (section 4).
- **The image's size:** Magelight fixes an image's natural size at its registration, which is the slot's first
  offscreen (supersampled, about twice the view) and never changes after, through a resize or a reopen at another size.
  So a page gives the element a CSS width and height from the view's own size (OpenView's / ResizeView's size over
  `devicePixelRatio`) with `object-fit: fill` (the default), never `height: auto` or `contain` / `cover`, and never
  `Shown`'s width and height (the offscreen's).
- **Outcomes, as events:** `Shown` (drawing: image name, URL, SRV, size; repeated when the offscreen is rebuilt;
  up to about 5 s after OpenView while the actor's 3D settles, with nothing sent meanwhile), `Waiting` (open, not
  drawing: `busy`), `Ended` (gone, with a reason token: `closed`, `game-load`, `missing`, `dead`,
  `unrenderable`, `not-loaded`, `collect-failed`, `vr` (reserved, not sent), `no-image`, `gpu-unavailable`,
  `image-register-failed`, `faulted`), `Reply` (a hint query's answer). Every refusal of an accepted view is an `Ended`: the client opens
  again to retry. A call that returns anything but `Ok` changes nothing, and no event follows it.
- **Closed before Ended:** `CloseView`, a game load and a refusal take the view out of the registry at once and tell
  the client from a later task, so a call in between answers `UnknownView` before the `Ended` arrives. An `Ended` can
  also arrive after the client has reopened the same slot under a new id, so a client matches events by view id.
- **The SRV** is alive for the event call only; a client that keeps it AddRefs it and releases it at the view's next
  `Shown` (a resize rebuilds the offscreen, and every rebuild or device change is followed by a `Shown` with the new
  one), `Waiting` (a view that waits releases it) or `Ended`, or at `ReleaseClient`, after which no event comes. A
  `Reply` (a hint query, `stats` once a second) never rebuilds or releases the offscreen, so the SRV stays across it.
- **Interaction:** `ResizeView`, `Orbit` (a drag), `Zoom` (a wheel step toward a point, or toward the face with the
  `aim` hint at face), `Refresh` (the look changed).
  A NaN or infinite `Orbit` / `Zoom` number is `BadArgument`: it would stay in the camera until the view restarts. A
  size of 1-3 px is accepted and drawn at 4 (the renderer draws nothing smaller); only zero is refused.
- **`RebuildActor3D(actor, who)`**: the one synchronous call, and the one with a thread rule: the main thread
  outside SKSE's AddTask drain and outside any render phase (SKSE's UI-task queue drains on the main thread during
  play; Phantasm logs the thread of the first call on and off it, which shows where it drains on a given install);
  `BadThread` off the main thread, inside Phantasm's render phase, and from inside a Phantasm event handler, which runs
  in the AddTask drain. A UI task drained during a loading screen can run off the main thread: it gets `BadThread`, and
  the client posts the rebuild again after the load. A view holds raw pointers into the actor's 3D; a client
  that rebuilds an actor's 3D itself (Update3DModel) leaves a view drawing torn geometry until its SEH net and debounce
  catch up. Through Phantasm the view lets go first and recollects at once, with no blank frame; a view showing the
  actor's last look (`lastSeen`) keeps it until a collect succeeds. Clients' outfit and try-on code rebuilds 3D, which
  is why it is frozen rather than a hint.
- **Reserved:** `Framing::Portrait` for a live view (`OpenView` with it returns `Unsupported`), `NotOwner`.
- **`ReleaseClient`** frees the name and ends the client's views without events (release any SRV kept from them). On
  the game thread nothing reaches the client after it returns; from another thread one event already in delivery may
  still arrive (it does not wait).

## 3. Threads

The rules SA learned the hard way, kept:

- **Any thread may call the API, Magelight's Present included, and no call blocks** (`RebuildActor3D` excepted, see
  section 2). The registry (clients, views, hints: `ViewRegistryCore`) sits under one mutex, so a call answers
  `UnknownView` and friends at once. The renderer work
  is posted to the game thread through PresentHandoff's pump (`PumpTask`): a call never posts to SKSE from the caller's
  thread, because an SKSE `AddTask` from inside Present can hang Skyrim VR. View and hint changes apply in the
  order they were made; a camera move applies to the view drawing when it runs.
- **Events reach clients on the game thread, outside any render phase**, from Phantasm's own tasks, with no lock held,
  so a handler may read `RE::` state and call the API again. A renderer status raised inside the carrier's render phase
  is attributed to the view drawing at that moment and posted through PresentHandoff (pumped inside the scope).
- **The carriers' `PostDisplay` holds `PresentHandoff::Scope`** (the engine's render phase on the main thread); the
  draws run under SEH nets (`RunGuarded`) and inside a D3D state backup that puts every slot back.
- **`RE::` only on the game thread.** 3D rebuilds and the player's world settle go through `MutationLane`
  (SKSE's UI-task queue: RaceMenu VR's armor hook deadlocks inside the `AddTask` drain).
- **A task never re-queues itself** (SKSE drains until empty: a self-requeue runs its whole chain in one frame).
- **SKSE runs AddTask tasks on game job threads as well as on the main thread**, while the carriers' `PostDisplay`
  draws on the main thread. The renderer's live state is under one lock (`LiveLock`): every renderer entry point and
  every task the renderer posts hold it, never around a client callback, and whatever is posted under it goes through
  PresentHandoff's pump. The carriers' frames only try it: a task holding it may be inside an engine load, so on
  contention the frame is skipped and the image keeps its last picture. Host's own state is touched only by its
  tasks, which SKSE runs one at a time; a renderer status names the start it belongs to (the view and openedSeq
  `LiveStart` was given), so Host reads none of its state from the render phase. The equip-event sink and the
  player's world settle take no lock: they leave their news in atomics the frame reads.

## 4. Carriers and when drawing happens

The engine calls a menu's `PostDisplay` once per frame in its UI phase; that is where every live frame is drawn. There
are two invisible carrier menus (`Carriers.h`), each with the VR IMenu tail (0x40 bytes) and a blank movie
(`Interface/phantasm.swf`):

- **`PhantasmActorCarrier`** is shown while an actor view draws (a client's or a shot's), during play as in a menu,
  and hides itself when none does. It is **not** an item menu (during play the item-menu count would change how the
  game treats open menus) and it allows saving. Its gate is "a view draws", never a client's menu.
- **`PhantasmItemCarrier`** carries the item-menu flag the item view's borrowed Inventory3DManager needs. It is shown
  while an item view draws (a client's `OpenItemView` or an item still) and hides itself when none does. It does not
  allow saving, so the game refuses to save while it is open.

**The client ends its view before its page goes away.** Drawing into the UI pipeline while the host tears the page
down has crashed in the driver. Phantasm cannot see a client's page, so this is the client's job: close the view (or
hide the page only after `CloseView`). A view nobody closes keeps drawing.

## 5. Hints (unfrozen)

`SetHint(view, key, value)`; an empty value removes a hint (the default applies), and a value is kept to its first
2048 bytes. Applied when the view draws; keys
and values are case-insensitive; an on/off value is on for `on`, `true`, `yes` or `1` and off for anything else.
This build knows:

| key | values | default | what |
|-----|--------|---------|------|
| `pose` | `classic` / `captured` | classic | the doll at rest (the skeleton's rest pose, its bone scales kept), or every piece skinned onto the actor's current bones |
| `motion` | `on` / `off` | off | a captured view follows the actor's live bones and, while the game does not animate them, their idle clip |
| `weapons` | `on` / `off` | on | weapons, shield and quiver where the game has them, and a readied spell's casting art |
| `floor` | `on` / `off` | on | a floor that catches the shadow |
| `lighting` | `studio`, `daylight`, `hearth`, `torchlight`, `moonlight`, `here` (anything else: hearth) | hearth | the light preset |
| `aim` | `face` / `figure` (anything else: figure) | figure | since 1.1.0, actor views: `face` points the camera at the actor's head from in front of their face, its crown up in the picture, at a Portrait's head-and-shoulders distance. The front is where the drawn head looks (the head bone's turn read against the race skeleton's head at rest, `FaceAimCore::HeadFrame`), not the actor's heading, so an actor an animation turns around or lays down (a paired animation's partner) is still seen face on. The camera follows the head (the collect's, then the follow's live bones or the clip's each frame) with a short lag, and jumps on a turn past 120 degrees; `Orbit` turns and raises it from the face, `Zoom` closes on the face (no pan toward the pointer), and a shown `scene` widens the far plane and shadows but not its distance. The preset's figure and camera lights, the rim included, turn and rise about the face's up, so it is lit as if it stood. Without a head bone it frames the figure's front over the chest anchor, and without the race skeleton to read the head against, it takes the front from the heading. `figure`: the chest-anchored orbit from the heading |
| `idle` | a clip name from `QueryHint("idles")`, `still` in any case (hold the pose), `file:<path>` (an animation file under Data; `meshes\` may be left off), or none (the idle cycle) | idle cycle | per actor, for the session; plays only with `pose` captured and `motion` on (else it waits). A clip name holds for the skeleton it first played on: a werewolf or Vampire Lord runs the idle cycle until they are back. A file loops from its start whether the game runs or not, its tracks mapped through its own binding onto the actor's skeleton; only Skyrim SE packfiles (`hk_2010.2.0-r1`, 64-bit) of a spline-compressed or uncompressed animation play (a resource container some exporters write beside the animation container is left alone), a file that cannot play keeps the choice (`QueryHint("idles")` and the log say why; it is not read again while Phantasm keeps it), and up to 64 MB of files are kept, the least recently played going first (a let-go file is read again when it plays next). A file holds only while a view that set it is open: once none is, the actor goes back to the idle cycle |
| `scale` | a positive number, held to 0.5 to 2 (none, or anything that is not a positive number: 1) | 1 | since 1.1.0, actor views: the view's actor drawn at this multiple of the size the game gives them, about the spot they stand on, while the idle the view chose plays them (an `idle` hint, with `pose` captured and `motion` on). Until that idle plays (its file is still being read, or was refused), with `idle` `still` or none (the idle cycle), in a view following their live bones and in the classic doll they are drawn as they are. For a client that shows an animation made for another size than the actor's own (a paired animation authored for figures of scale 1). It is meant for a few percent either way: the camera, the floor and the shadows frame the actor's own size, and a spring or soft-body bone keeps its own reach. A `Figure` still shows it; a Cutout and a Portrait draw the actor as they are, and a look posed at another size is not kept as the actor's last look |
| `scene` | `<FormID hex>|<idle>|<x,y,z,r>|<scale>` entries separated by `;` (the offset and the scale may be left off), up to 3, or none | the actor alone | the view's other actors: each plays its idle (a clip name or `file:<path>`, as the `idle` hint; an entry with none, or `still`, is left out) at the view's actor's place, moved x along their facing, y to their right, z up and turned r degrees, at `scale` times their size (since 1.1.0; as the `scale` hint, 1 when left off); an actor is drawn only while that idle plays, and the camera, far plane and shadows widen by 1.5 while one is. Set on each start of the view (none when it names none); the others go back to the idle cycle when the scene drops them or the view ends, unless another view's scene names them. An extra's look is taken when it joins: an equip shows when the view starts again or after `RebuildActor3D` of that actor |
| `expression` | `neutral`, `happy`, `sad`, `angry`, `afraid`, `surprised`, `puzzled`, `disgusted`, or none (as in game) | as in game | per actor, for the session |
| `props` | `<FormID hex>|<model>` or `<FormID hex>|<model>|<node>` on an actor, `place|<model>|<x,y,z,r>` standing in the scene, entries separated by `;`, up to 16, or none | none | models the view draws in its scene (an animation's objects, its furniture). `<model>` is a .nif under Data (`meshes\` may be left off). One on an actor (the view's own, or one of the `scene` hint's) hangs from `<node>`, else the node its model's `Prn` string names (where the game attaches an animation object), else the actor's root, and is drawn while that actor plays a clip (`pose` captured, `motion` on), on the clip's pose of that node; one standing in the scene is at the view's actor's place moved and turned as a `scene` entry, and widens the camera as one does. Pieces are drawn rigid, and the models' effects and particles are not run. Set on each start of the view; they go when the view ends |
| `keepLook` | `on` / `off` | on | `off`: what the actor wears is a try-on, not their look, so no view keeps it as their last look; holds while any open view of the actor says `off` |
| `hostless` | `on` / `off` | off | since 1.0.0, read when the view starts: `on` says the client draws the `Shown` SRV itself, so with no Magelight (or one too old for texture images) the view draws with no image (imageName and imageUrl empty) instead of ending `no-image`. Set it before the view first draws (with or right after `OpenView`/`OpenItemView`); a change takes effect at the view's next start. Actor and item views |
| `worn` | `male` / `female` / `off` (or `ground`) | off | since 0.3.0, item views only: an armor drawn as worn by a man or a woman, its addons' models without the body skin they carry; `off` draws its ground model. Changing it loads the item again. `RequestItemStill` takes the same key as a still hint (section 10) |
| `freeze` | `on` / `off` | off | since 1.0.0, `on`: the view keeps drawing the look it holds (meshes, textures, face and pose as drawn, the classic pose's rest included) and collects nothing, so the actor can be dressed otherwise while another view of them draws live (a try-on beside how they looked). It freezes when it is set if the look it draws is complete, else at the first drawn frame that is (no texture still owed, no piece skipped for buffers not yet uploaded), and before a `RebuildActor3D` of the actor drops the look; a look kept while the actor was away freezes as it is. A readied spell's casting art and the props hung on the actor (drawn only while a clip plays) are not part of the frozen look, as they are not part of a kept one. Camera moves, the size, `floor` and `lighting` still act on it; `pose`, `motion`, `weapons`, `idle`, `scale` and `expression` changes wait until it thaws. Its pieces and buffers are its own (a still copy, no live bone, anchor or effect handles), so it stays on screen through equips and `RebuildActor3D`; it is never kept as the actor's last look and never upgraded to a live collect, `lastSeen` is false while it holds, and stills of the actor are unaffected (a `Figure` still takes a live view of them first). `off` goes back to live at once (a collect in place, the frozen look standing in until it succeeds). The frozen look is the view's while it draws: a view that waits (`busy`), is re-targeted or ends lets it go, and freezes again on what the actor wears when it next draws. A `scene`'s other actors are not frozen. Actor views only |

`idle` and `expression` are kept by the renderer per actor for the session: a view that starts drawing applies them
only when it sets them (another client's choice for the same actor stands otherwise), and removing one while the view
draws puts the default back.

Queries (`QueryHint`, answered by a `Reply` whose `json` is):

- `idles`: `{"formId": n, "choice": "<the idle in force, or empty>", "available": bool, "plays": bool, "reason":
  "<display text, not a token>", "idles": [{"id": "...", "label": "...", "loop": bool}, ...]}`. A view not drawing its
  actor answers `available: false`. `plays` says whether a chosen idle plays in this view now: false while the view is
  classic, motion is off, a fault stopped the follow, or the chosen clip waits for the NPC's other form, and false when
  the choice itself is refused (a file that cannot be read or placed, or whose tracks drive none of the actor's nodes, a
  clip no longer loaded), with the `reason`. `"pending": true` while the choice has not been tried yet, its file is
  being read, or its last try failed before it (retried; that try's `reason` comes with it): `plays` stays true until a
  try refuses it, so ask again, and a client that stops asking can show the `reason`.
  The list still comes, because a choice
  stands for the session and plays once the view follows. The list holds the idles of the NPC's skeleton, sex and
  scale that the game's own animations or the replacer set (OAR, DAR) the NPC plays from offer, never another set's
  conditional idles; a replacer's idle whose label matches another's names its set in brackets.
- `expression`: `{"formId": n, "expression": "<an id above, or empty for as in game>"}`.
- `freeze` (since 1.0.0; an actor view): `{"known": true, "freeze": bool, "held": bool}`: `freeze` is the hint as set,
  `held` whether the view draws a frozen look now (false until it freezes, and while it waits). A client re-dresses the
  actor once `held` is true. A build without the hint answers `{"known":false}`, which is how a client tells.
- `stats` (since 1.0.0; actor and item views): `{"known": true, "drawing": bool, "valid": bool, "fps": n,
  "drawnFps": n, "skippedFps": n, "waitedFps": n, "stalledFps": n, "cpuMs": x, "budgetMode": "smooth|balanced|light",
  "budgetApplied": "...", "budgetMs": x}` over the last second (`ViewStatsCore`): drawn = frames with a new picture,
  skipped = turns with nothing to redraw (a held pose, a frozen view), stalled = turns that could not draw (a 3D
  rebuild, a start still loading, the GPU bundle not ready), waited = frames without a turn (the budget, section 6);
  `fps` = drawn + skipped, so a held pose does not read as slow and a stalled view does; `cpuMs` = the average CPU time
  of a drawn frame. `budgetMode` is the setting, `budgetApplied` and `budgetMs` what is in force now (smooth runs
  balanced while the game runs). `drawing` is false for a view that is not active in the renderer (ended, not started
  yet, or waiting `busy`), and `valid` false until a full second has been counted since the view started. Each
  drawing view also logs these every 10 s.
- anything else, the other keys of the table above included: `{"known":false}`.

## 6. Views, drawing order and budget

- **Several views draw at once:** the newest open views, those whose `OpenView` or `OpenItemView` came last, up to 4
  (2 on VR; `ViewRegistryCore::kMaxDrawing`). Every other open view gets `Waiting("busy")` once per wait and comes
  back with `Shown` when a newer view closes. Camera moves of a waiting view are dropped (its camera starts fresh
  when it draws again). Each drawing view has its own state in the renderer (its actor or item, camera, display
  hints, scene and props) and its own offscreen and depth targets, which are its image; the shadow maps are shared
  scratch, drawn again for each view. A look the renderer keeps for an actor is kept per `pose` and `weapons` choice,
  so views that draw the actor differently keep looks of their own. `idle` and `expression` stay per actor: two views
  of one actor play the same idle (a shot's view excepted: its standing idle and neutral face are its own).
- **The frame budget (`LiveViewBudgetCore`).** A carrier draws its views one after another on the main thread inside
  the game's frame. The newest draws every frame; the others are offered a turn longest-waited first (round robin
  among equals) while the carrier's draws stay under the budget, and a view that has waited the mode's maximum draws
  whatever the budget says. A view whose picture cannot have changed skips its frame, which costs next to nothing.
  The budget is per carrier: the actor views and the item views each get it, so with both kinds open the views can
  take twice it. It counts CPU time only. The player picks it in `Data/SKSE/Plugins/Phantasm.ini` (beside the
  game's executable), `[LiveViews]`, read at kDataLoaded (a missing file or unknown value is smooth):

  | `Budget` | CPU ms a frame (VR) | a view waits at most | for |
  |----------|---------------------|----------------------|-----|
  | `smooth` (default) | 14 (4) while the game is paused, balanced's while it runs | 2 frames paused | every view of a typical page drawing every frame while a menu holds the game, when the frame has room; while the game runs (a page shown during play, a HUD popup) the views share the frame with play, so it takes balanced's budget |
  | `balanced` | 6 (3) | 6 frames | 0.4.0's budget: the newest every frame, the others in turns |
  | `light` | 3 (1.5) | 10 frames | a machine where the views cost the game's frame rate |

  `BudgetMs` above 0 replaces the mode's budget (clamped to 0.5-50 ms). The log says which budget is in force, and
  `QueryHint("stats")` (section 5) shows what each view gets.
- **Shots draw beside the views.** A Cutout, Portrait or item still draws on a view of its own (the renderer's view
  0, which no client names) in the same carrier turns as the clients' views, and only that view takes the shot's
  settings (`InPortrait`). A view starting or drawing no longer cancels or refuses a shot, and `view-drawing` is no
  longer sent. One shot still runs at a time.
- **A GPU refusal ends one view, not the queue.** When the device refuses a live GPU object the renderer refuses every
  start for `kGpuRetryDelay` (10 s), and the view it was building that object for ends `gpu-unavailable`. A view that
  already holds the bundle and its own targets goes on drawing through the window (it asks the device for nothing
  new: `LiveViewLifecycleCore::DecideGpuFrame`); one that needs a new object in it (a resize) ends too. Host starts
  nothing in that window: the views that would start stay `Waiting("busy")`, and a timer thread posts one
  `Reconcile` when the window has passed (never a task that posts itself again). Host learns the window from the refusal event, so its window ends no
  earlier than the renderer's. A shader compile failure is also reported as `gpu-unavailable` and never clears, so
  Host opens no window for it: the waiting views end at once, each start refused in turn. A compile whose constant
  buffers or particle vertices the bytecode lays out differently from the C++ records counts as failed
  (`ShaderContractCore`, checked by reflection at every compile and by the self-tests on the renderer's own text).
- **A skin the engine has not bound is first bound by Phantasm** (`FillUnboundPalette`,
  `ActorCollectCore::SelfBindsUnbound`): the engine fills a skin's palette only when it draws the shape, so an actor
  it has not drawn yet (hidden, or placed while the game is paused) has none, but its bone list is already linked to
  the skeleton. Each bone's current world gives the palette the engine would build, and the clip player and the
  physics take the skin like a bound one. It is refused, and the skin handled as below, when a bone is missing, off
  the scene the skin's root parent is in or cannot be placed, or the skin has no bone list or bone data or more bones
  than a palette holds (the log names the bone and the test); it is never tried for a skin the game would not draw
  either (`UnboundPlace::kHidden`). A skin bound this way does not count as bound for `ClassicForUnbound`, so an actor
  with one skin refused still takes the classic pose whole.
- **A palette holds 256 bones** (`ShaderContractCore::kMaxBones`, the size of `cbBones` on both sides), every bone
  one shape can address: Phantasm draws from the engine's own vertex buffer, whose four bone indices per vertex are
  UINT8, so a larger palette (Bone Limit Extender's) would never be indexed. A skin with more bones is not skinned,
  captured or at rest, and keeps its bind placement (the log says `more bones than the palette holds`).
- **Skins the engine has not bound yet are left out of a captured live view** (`ActorCollectCore::UnboundPlaceOf`,
  `LeavesOutUnbound`): drawn, such a skin would stay rigid at its rest node while a pose or clip moves the body (hair
  floating away after a rebuild). A skin the game would not draw either (no shader, dropped by the hair-hide, body
  skin on an armour piece of an actor the engine has drawn: on one it never drew, an armour's arms are as unbound as
  its hidden body) is hidden and never waited for, and an armour's hair-shader piece (a wig's hairline) rides
  the live head from its bind frame. The view rechecks, scene actors included, every second for `kUnboundFastTries`,
  then every 5 s up to `kUnboundMaxTries` rechecks (on VR every second, with no limit); a recheck reads the left-out
  skins in the 3D and collects again only when one is bound (a start, re-target or game load counts afresh). For the
  first `kUnboundSettleTries` rechecks the look counts as incomplete: it is not kept as the actor's last look and a
  `freeze` waits; a skin still unbound after that no longer holds either back. A frozen view does not recheck its
  scene's actors.
- **Hair strands draw both faces, and a one-sided strand's back face is lit as a front face**
  (`RendererMaterialCore::CullsBackFaces`, `StrandBackFaceReverses`, `ShadesNormalReversed`). The game culls the back
  faces of hair whose material is not two-sided; the views draw them, since a thin card's back half is part of the
  hair as seen. Hair made two-sided by geometry (a reversed copy of each triangle in the same place, its normals
  pointing in, as many Sims 4 conversions are) would show the copy, lit from the inside, wherever it wins the depth
  tie: dark flakes with triangle edges on the lit side, light ones on the dark side. So on such a back face the vertex
  normal is reversed where it points to the far side of its own triangle (the triangle's facing, not the view
  direction, which a bent vertex normal near the silhouette would fool). A two-sided strand, whose back face the game
  draws too, a card wound the other way whose normals still point out, and every front face keep their normals.
- **Next (no API change):** views of the same actor drawn with the same `pose` and `weapons` sharing one collect.

## 7. Game load

The SKSE revert callback (a save loads or a new game starts) ends every view with `game-load` and makes the renderer
forget the session (kept looks, idles, expressions, try-on marks): a view holds NiPointers and FormIDs that must not
cross saves. Clients open their views again after `kPostLoadGame` / `kNewGame`. Phantasm writes nothing into the save.

The teardown runs as a task behind the API work posted before the load, so the old views' `Ended("game-load")` can
arrive after `kPostLoadGame` and after the client has reopened the same slot under a new id: a client matches events
by view id, never by slot or image name. The teardown ends with a `Reconcile`, so a view opened after the load starts
cleanly even when a `Reconcile` posted before the load ran first and started it.

## 8. Platform

Universal SE/AE/VR build on CommonLibSSE-NG. Live views and stills work on Skyrim VR too, but VR has been tested less
than SE and AE. Views draw there as on SE and AE (through Magelight's GPU images, or hostless), and the base draw is
proven; untested on VR are the clip player, whose Havok reads are vtable-checked so that a wrong offset refuses the
clip and holds the pose (each actor's pick and each refusal are logged as `MannequinRenderer/vr`), the effects,
particles, physics, scene actors and props, and the item view's effects through VR's inventory manager (below). VR's
NiStream ctor and dtor are its address library's offsets, which set and free the same fields as AE's. SKSE
VR drains UI tasks on job threads, so `RebuildActor3D` is not thread-checked there; the draw skips its frames while an
engine rebuild runs (`s_rebuildsRunning`). Version-specific engine knowledge the renderer relies on, which
every client now depends on through Phantasm: the NiStream ctor/dtor ids, `NiSwitchNode::index`, the NiSkinData bone
stride, the BSDynamicTriShape size probe, unreflected Havok offsets for the clip player, the carriers' VR tail, and
Inventory3DManager's loadedModels on VR (0x48-byte entries, the count at +0x258; CommonLib reads VR with the SE
layout, which finds no model, and the item view then draws the model cache's clone with its effects held still).

## 9. Known clients

Which mods use Phantasm is up to them; Phantasm knows none of them. SeverActions 4.x is one (client `sa`): its Outfits
mirror (slot `mirror`), its Inventory page's item inspect view (slot `item`) and its party cutouts (`Framing::Cutout`
stills) are drawn by Phantasm, and it ships a copy of Phantasm.

A HUD face (a party sheet) is a view in a small slot, which shows the figure, or a `Framing::Portrait` still. A
PrismaUI page cannot show a texture image (its API has no image call). Its client can show a still there, which it
encodes itself (`RequestStill` hands back RGBA pixels; the client writes them as a PNG file or a data URL), use
Magelight for the view, or draw a `hostless` view's SRV itself under or over the page.

## 10. Stills

`RequestStill(client, actor, w, h, Framing::Figure, done)` in this build is a picture of what the drawing view of that
actor shows (any client's view: the one drawing), for a client that sends the view on, as to a vision model:

- **When:** the view's next frame drawn with its scene settled for two frames (the actor and its chosen clip, every
  other actor of its `scene` shown, every prop collected, no textures loading), or the frame after 4 seconds however
  it is (logged). The view keeps drawing frames until then; one that draws none for 8 seconds answers `timed-out`.
- **What:** that frame cut to its drawn area and scaled down to fit `w` x `h` (never up, and off the render
  thread), RGBA8 sRGB on the view's backdrop (opaque: the alpha is 255 where the backdrop shows), handed to `done` once
  on the game thread, outside any render phase. The read-back is one GPU stall of the frame. Nothing is drawn for it alone: the camera, light, pose and scene are the view's.
- **No picture** (`ok` false, `reason`): `not-drawing` (no view of that actor is drawing when the request is taken),
  `view-ended` (it ended first), `timed-out`, `read-failed`. `done` is not called once the client is released.
- **Refused:** a null `done`, a zero actor, a side of 0 or over 4096 (`BadArgument`), an unknown client
  (`UnknownClient`).

`Framing::Cutout` is a still of an actor drawn on their own (SA's party cutouts), from the portrait job
(`PortraitStart`):

- **What:** the whole figure on the shot's own view (beside any client views), their standing idle (their live pose
  where the clip player cannot run), a neutral face without blinks, the Hearth light, no weapons (unless the
  `weapons` hint asks; a readied spell's casting art never) or floor, on a
  transparent ground: RGBA8 premultiplied by alpha in linear light, then sRGB-encoded, scaled to fit `w` x `h`, with
  `standingPx` the figure's standing height in its pixels (the client crops and unpremultiplies it). An actor the
  engine has not bound (`ActorCollectCore::ClassicForUnbound`: no skin bound, or an unbound head part) is shot in the
  classic pose, standing at its bind placement with the head re-hung on it, whatever the live pose, and the still
  says so (`classicPose`, since 0.4.0): the engine usually binds them later (in the field, after the player had
  been near them a while), so a later shot can show their live look; a head part the game hides can stay unbound.
- **No picture:** `busy` (32 shots already wait), a live start's refusal (`no-image`, `unrenderable`,
  `gpu-unavailable`), `missing`, `dead`, `not-loaded`, `not-drawn` (it never drew settled in 6 s), `read-failed`,
  `cancelled` (`CancelStill`, or the shot's view ended). Before 0.4.0 also `view-drawing`: a shot waited for the
  views and a view's start cancelled it. One shot (a Cutout, a Portrait or an item still) runs at a time; Host queues the rest in order
  (`busy` only past 32 waiting), answers a cancelled one `cancelled`, drops a released client's, and answers every
  waiting one `cancelled` on a game load.
- **Shots need no UI host.** A shot publishes no image, so it needs no Magelight; neither does a `hostless` live view
  since 1.0.0 (section 1, Requirements).
- **Shots of one actor in a row share their collect.** After a shot its view lingers
  `StillShotCore::kShotLingerMs`, drawing nothing; the next shot of the same actor with the
  same `weapons` choice only changes the camera and light (and the view's size between Cutout and Portrait), so a
  client asking for several angles at once pays one collect. Another actor or another `weapons` choice ends it.
  An item shot does the same on its item view (`StillShotCore::kItemShotLingerMs`, short: the item menu
  it runs in refuses saving while open), keyed on the item and its `worn` choice. When a shot ends, the queue takes the
  next shot of the same subject ahead of older ones (`StillShotCore::NextShotIndex`, at most
  `kMaxSameSubjectRun` in a row), so clients asking at once do not interleave and reload each other's subjects.
- **Worn armor** (item hint `worn`). An armor's item shot draws, instead of its ground model, the models of the addons
  a Nord wears (the armor's own race's, else its first) for that sex, each a clone of BSModelDB's copy whose skeleton
  nodes stand in the bind pose, so the skinned pieces keep the shape they have on a body; the body skin the files
  carry (`kFaceGenRGBTint`, `kFaceGen`) is left out, and the effects are not run (the clones share their materials).
  `turn` is then measured from the wearer's front.
- **Weapons and pitch** (hints `weapons`, `pitch`). A shot draws no weapons unless asked; `pitch` raises or lowers
  the camera. A Portrait aims at a creature's head (one standing forward of its feet) rather than above the chest
  (`StillShotCore::HeadShotTarget`).

`Framing::Portrait` is the same shot of the actor's head and shoulders: a square view, the camera on a point
`StillShotCore::kHeadTargetLift` over the chest anchor at `kHeadDistance` (both in figure units, so a child or a giant
frames the same), about 44 units of height shown. `standingPx` is the figure's standing height at that scale.

`RequestStillHinted` is `RequestStill` with still hints (`StillShotCore::Parse`: `key=value` pairs separated by `;`,
unfrozen like view hints, an unknown key or value ignored). For a Cutout or a Portrait, `turn` turns the camera from
the figure's front (degrees, toward their left for a positive turn; 14 when unset) and `lighting` picks a lighting
preset (Hearth when unset). Since 1.1.0 `aim=face` makes a Portrait face the drawn head as the view hint does, with no
lag, `turn` and `pitch` measured from where the face looks: for the shot's live pose when its standing idle cannot
play (otherwise the idle stands the actor along their heading anyway). A Cutout ignores it. Since 1.1.0 `zoom` (0.5
to 3, 1 when unset) brings a Portrait's camera closer or further, with either aim: the frame's top edge stays where it
was, over the crown, and the frame closes from below, so about 2 shows the face alone
(`StillShotCore::HeadBelowBone`); a Cutout ignores it. A head shot's aim follows the head as the pose moves it after
the collect, with either aim. A Figure still takes the drawing view's camera and light (its `aim` included) and
ignores them.

`RequestItemStill` is a shot of an item's model on an item view of its own (`ItemShotStart`): the model as
`OpenItemView` draws it, its effects and particles run `kItemSettleMs` (or until `kItemTimeoutMs`), on a transparent
ground with the item view's neutral light, read back like a Cutout (`standingPx` 0). `turn` turns it from the item
view's opening side. Its reasons are the Cutout's less the actor ones, plus `no-model` (the item drew nothing). While
it draws the item carrier is open, an item menu that refuses saving, for about half a second.

**Neural stills** (`Phantasm.ini [NeuralRendering]`, `NeuralStills.h`). When `Enabled` and `Folder` turn it on, a Cutout's or a
Portrait's shot goes through NVIDIA DLSS NR before it is fitted and answered. The library is the player's own
`nvngx_dlssnr.dll` (`NeuralSource.h`, rules in `NeuralSourceCore`): the folder or library file `Folder` names, or
with `Folder = auto` the copy a neural rendering mod installed, which is the one a mod has loaded in the game, else
the first found under `Data/SKSE/Plugins`, `Data/Shaders` or the game's folder (read as the game sees them, so a mod
manager's virtual folders count); up to `kMaxTries` found copies are tried. A mod may keep its copies under names of
its own (`nvngx_dlssnr*.dll`: Theo's Render Pipeline ships `nvngx_dlssnr_int8.dll` and NVIDIA's builds under their
version). Those are found too, and within a folder the order is the file the mod is set to run from (Theo's
`NeuralRenderingRuntimePath`), on RTX 50 and later the versioned NVIDIA builds, newest first, then
`nvngx_dlssnr.dll`, then the rest; a file Theo's own rules keep off the game's GPU is left out (`Suits`: its INT8
build on RTX 40, a versioned NVIDIA build before RTX 50, where Theo's patches it in memory from a kernel pack, which
Phantasm does not do). NVIDIA's loader finds a library by its own file name only, so a copy under another name is
loaded and called directly, signed or not. The work runs inside the game, from `PhantasmNRInProc.dll` beside Phantasm
in `SKSE/Plugins/Phantasm/` (`NeuralInProc.h`, `nr/PhantasmNR.h`): loaded only when NR is turned on, and called on two
threads, the NR worker's and, for live frames, the render thread's. NR is a Vulkan feature, so it keeps a Vulkan
device of its own and talks to NVIDIA's loader, the NGX core of the graphics driver (`_nvngx.dll`, found through the
driver's registry entries, else the Windows system folder). The library holds no code of NVIDIA's and is built without
NVIDIA's SDK: `nr/Ngx.h` is Phantasm's own statement of the layouts and entry points the core and a feature library
export, and each is reached by its export name. Two things that SDK's library did for its callers are NR's own: the
core lists a library's Vulkan extensions only for one it will run itself, so a library run directly is given the list
the core gives for the builds it takes; and the core's shutdown writes through a second argument, which it is handed.
What running inside the game has to answer is the Windows loader lock. NR's start loads some seventy libraries, each
starts inside that lock, held by the thread that loads it, and another mod's code on the game's presenting thread asks
for the lock after every library load (the Skyrim Upscaler takes a module list in the frame's present): whatever one
of those libraries takes to start is how long the game stands still. Three things keep that short.
`PhantasmNR_FlagNoLayers` makes NR's Vulkan instance with no implicit layer (the Vulkan loader's
`VK_LOADER_LAYERS_DISABLE=~implicit~`, set for the length of the start and put back; a loader older than 1.3.234 does
not read it and NR then refuses to start: `NeuralLayerCore`), so what loads is Windows' and the graphics driver's. A
Vulkan instance otherwise brings in every implicit layer registered on the machine: overlays, capture hooks, a global
ReShade, which started inside the game on NR's thread, hooked the game's input from there and held the lock for twenty
seconds. NR runs from a private copy of the library (`NeuralSource::PrivateCopy`, under `%LOCALAPPDATA%\Phantasm\NR`,
one per version of the file): a module apart from the one the player's mod has loaded. And the worker starts NR at
kDataLoaded, while the game is still loading to its main menu, where a wait for the loader lock is loading time, and
nothing is loaded after the start (measured outside the game: the start loads 68 libraries, the slowest of them holds
the lock 36 ms, and stills of any size load none). A settings page that turns NR on in play starts it then, outside
that window: the player asked for it, and the page says it is starting. What is left: a library of the driver's that is slow to start still
holds the game for that long (the start watches the lock and the log names the library, at once when a hold passes
half a second); a fault in NVIDIA's code is the game's crash; NVIDIA's NGX core is one module, shared with the
player's mod; and NR's memory is the game's. NVIDIA's loader takes only an NVIDIA-signed library built for the GPU. A
signed copy it refuses is not run at all; an unsigned one (a community build for an older GPU, a pre-release one) is,
unless `Unsigned = 0`, loaded and called through the library's own Vulkan entry points, the ones the loader calls. The
library asks Windows which module calls it and takes only NVIDIA's loader, so the private copy's own imports of
`GetModuleFileName` are pointed at an answer naming the loader; its start and its feature's creation run behind a
fault guard, so a copy built for another GPU is a refusal. One worker thread starts NR (when the mod it follows had
its own NR off at kDataLoaded: as soon as that mod turns NR on, or with `Prestart = 0` at the first shot or view after
that: a shot is then answered late by the seconds NR takes to start, a view shown as drawn meanwhile) and runs every
shot, so the shot queue does not wait for it; the answer is posted back to the game thread. NR's switch, style and strengths are the ones the player
set in the mod the library belongs to (`Follow`, `NeuralSettingsCore`: Skyrim Upscaler's `SkyrimUpscaler.ini`, Theo's
Render Pipeline's ini under its user ini, Open Shaders' `SettingsUser.json`, told apart by the folder the library was
found in), laid over `Phantasm.ini`'s, which stand for what the mod does not set; the worker reads the files again
when they change, so a change in the mod's menu reaches the next shot and the next live frame, and a mod that turns
its NR off turns Phantasm's off (NR that has not started is not started until it is on again). The shot is laid over a
dark ground (NR takes an opaque picture) and the ground taken off its answer (`NeuralStillCore`): a pixel's colour
never exceeds its coverage, so the edge stays clean, and the figure is given a flat depth (the shot keeps none of its
own). A shot NR cannot take is answered as drawn, and NR that cannot start (its own library missing, no copy of
NVIDIA's it can run, no GPU for it) is off for the session, logged once. NR's memory goes back to the driver after
`kIdleTrim` without a shot. Item stills are never changed.

**Neural live views** (`[NeuralRendering] LiveViews`, `NeuralLive.h`) run the same NR over an actor view's frames on
the GPU, one view at a time (the first to draw while none holds it, until it ends or has not drawn for a second; the
next view to draw then takes over, with the same objects when it is the same size). After the
frame is drawn and before it is published, the offscreen is scaled (`LiveScale` of its supersampled size) into a float
texture NR's Vulkan device imports by NT handle, with the frame's depth; a shared D3D11 fence is signalled and
flushed; NR is queued to wait for that value and signal the next; only once it is queued does the game's context wait for
the next value (on the GPU) and scale NR's answer back into the offscreen, so the game never waits for a signal that
will not come. A frame NR cannot queue is published as drawn and turns live NR off for the session. NR keeps its
history with no motion vectors (measured: the answer barely moves with the figure) and starts it again when the
actor or the offscreen changes; a size change or a view of another size attaches again on the worker, and nothing is
attached after `kIdleDetachMs` without a frame. While NR is being set up for a view its frames are published as drawn
and `Process` asks for the next one, so a view whose picture does not change still gets one frame through NR. A frame still (`StillOfView`) is taken before NR, as drawn.

## 11. Effects and known gaps

What the actor view draws of an actor's effects:

- **Effect pieces authored in what they wear and hold** (a staff's orb glow, rays and swirls, glowing armour trim)
  draw with their material as the game animates it: each frame the view reads back the scrolled UVs, colour, palette
  row and alpha the engine's controllers left in the piece's effect-shader material. It never runs a controller on the
  live 3D. While the game is paused the engine advances none, so the view evaluates them itself: at the collect it
  copies each free-running shader-property controller's timing and keys (checked against the value the engine last
  left), and evaluates them on its own clock from the engine's last time. A controller a NiControllerManager sequence
  drives is not copied, so its piece holds still while paused.
- **Pieces under a billboard node** turn to the view's camera, not the game's: mode 5 about the node's own up axis
  (nif.xml's BSRotateAboutUp), mode 1 assumed to do the same (unverified; the item view's particle frames keep world
  up for mode 1).
- **The engine's runtime attachments are left out**: spell art (cloaks, hit art, an enchanted weapon's EnchantArt),
  the addon shells of effect shaders, an idle's props (AnimObject nodes) and blood shells. They are not the actor's
  look, would draw frozen at the collect's moment and outlive the spell, and a kept look would store them. Nothing
  recollects when such an effect starts or ends. An effect whose 3D holds what the actor wears (a skin, worn armour,
  a weapon attach node) is never left out.
- **A readied spell's casting art is drawn** while the view draws weapons (the `weapons` hint; never in a portrait):
  the art the engine attaches under a hand's magic node for each hand caster with its art attached
  (`ActorMagicCaster`: `castingArt`, `kCastingArtAttached`, the clone in `castingArtData.attachedArt`, else the magic
  node's child named as the model's root node). The engine clears `castingArt` once the art is attached, so the art is
  then the held spell's costliest effect's casting art. Its effect pieces draw and animate as the pieces above (and hold still
  while the game is paused where a controller manager's sequence drives them); its particle systems run on the view's
  own clock as the item view's do, whether the game runs or not, at the art's nodes read live each tick and placed as
  the art is drawn: by its magic node as the view draws what hangs on it (the follow's, the clip player's, or the
  collect's, moved onto the rest-pose hand in the classic pose). A local-space system's particles are carried with its
  node. A spell readied, swapped or put away while a view is open recollects it (a signature of the hand casters and
  the mirror's weapons hint, compared each frame); a recollect of the same art keeps its particles running. Its
  pieces are transient: a kept look never holds them.
- **An engine-hidden root**: when the engine hides the actor's root and most shapes below it still show (the player's
  third-person root in first person), the hidden shapes below it stay out (blood shells, a hidden duplicate body, a
  helmet a toggle hid). When most shapes below are hidden too, the flags are read as a fade's and nothing is left out
  for them. Which nodes a distance fade flags is not verified; the collect logs both counts.
- **A world another mod left shrunk**: a first-person body mod (Improved Camera) hides the head, the upper arms and
  the weapons on the back of the player's third-person body for the game's own picture by shrinking the node to a
  thousandth, updating the worlds below it and putting the local scale back. The worlds stay shrunk while the locals
  say the part shows, so a pose drawn from the bones' worlds puts every vertex of the part on one point (a neck that
  ends in a cone). A captured collect, a follow tick and a clip rig therefore take such a node's world, and those of
  the nodes below it, from their locals on the nearest ancestor whose world is sound
  (`ActorCollectCore::TakesChain`, `PosedWorld`), not from the shrunk world scaled back up: positions under a world
  shrunk a thousandfold are rounded away. A node whose own local scale hides it stays hidden, so a part the mod keeps
  shrunk through its local scale (its first-person view on a mount, as a werewolf or in a scripted scene) stays out.
  Below a shrunk node, a world written since at a scale of its own is kept; a physics bone whose world still carries
  the shrunk scale is drawn at rest. The first such pass of an actor logs the nodes. Not covered: the classic pose,
  whose skins do not read those worlds but whose head re-parent does, and a readied spell's art, whose magic node and
  particles keep the game's worlds.

Known gaps:

| gap | what it costs | why not yet |
|-----|---------------|-------------|
| Effect-shader membranes (`BSShaderProperty::effectData`) and enchantment art | an enchanted weapon's glow (its membrane and its EnchantArt, which drew frozen before and is now left out), Oak/Stone/Ebonyflesh, illusion and ghost shaders are not drawn; "effect pieces" above means effect shaders authored in a mesh only | a new membrane pass (fill and rim colours, edge falloff, projected UVs, palette, the effect's own blend) fed by a per-tick snapshot of the animated `effectData`, off for portraits and cutouts: a feature of its own |
| Actor fade (`SetAlpha`) | ghosts such as Katria draw solid | whether a partial `SetAlpha` reaches the opaque pieces' material alpha needs checking in game first; then such pieces move to the blend pass behind a depth pre-pass (never `BSFadeNode::currentFade`, the distance fade) |
| An actor the game's camera does not see | an NPC the engine culls (behind the player, out of view) may not have its effect controllers advanced, so its effects hold still as when paused | the view reads what the engine animated and runs no controller itself |
| Loops played together | while the graph plays several loops at once (a spell readied in each hand), the clip player plays them together, each bone from the one loop whose pose matched it when the view took over, so a bone the graph blends between two loops takes one of them; a loop of a walk, run or turn blend, or one that would drive fewer than six bones, is not played as a layer, and one the graph runs at the base loop's phase keeps that phase (until the graph's phases drift apart, when it takes its own clock). A one-shot the graph plays over a stance loop (a look-around over the shield-block idle) is played the same way, the stance keeping the bones it still holds (the shield arm), and when the one-shot ends the stance plays on alone as the base loop, from where it was, keeping only the takes of its own idle family (the pool is not loaded for that family, so a stance the view saw only under a one-shot loops without one-shots of its own). While they play the idle cycle holds (no one-shot breaks in, no other variant follows). A loop that starts while the view already plays them (the game running with the actor out of view, or after a recollect) gets its bones only at the next bake from a live pose | the view reads which clips play, not the graph's blend weights or sync; frozen bones or a recollect's rest pose cannot say which bones a new loop drives; a one-shot the cycle breaks into itself (paused) plays alone, so a stance's own one-shots replace the whole pose |
| Node animation of effect pieces | swirls a node controller turns move only while the view follows the actor (pose captured, motion on) | the classic pose and a still view keep the collect's node worlds |
| Particles on what the actor wears | a torch's or staff's particle fire is not simulated in the actor view | the actor view runs only a readied spell's particle systems; a worn piece's would need the same per-piece rig |
| A readied spell's charge states | the nodes the engine shows or hides as a cast goes from ready to charging to release (its sequences) keep the visibility they had at the collect; its particles run the art's idle or looping sequence's rate, particle size, life, speed and gravity | the view plays no sequence and reads visibility only at the collect; following it needs a per-tick read of the art's node flags |
| A readied spell's hand light | the light the caster carries (`ActorMagicCaster::light`) does not light the figure | the view's lights are the lighting preset's; a hand light would be one more point light in the rig, placed at the magic node as the art is drawn |
| Mesh-emitter and strip particles | a system emitting from a mesh's surface (an ice whip's motes, a fireball's charge fire) and a strip system (the Phoenix Glave's streaks) are not drawn in either view; the log names each and why | a mesh emitter needs the emitter mesh's vertices (a skinned one's from its skin partition) in the simulation, and a strip joins its particles into ribbons, not quads |
| Item view: a model drawn from the model cache | when the item's model comes from a clone of BSModelDB's cached copy (an engine item menu is open, or Inventory3DManager built nothing), its effects and particles hold still | the clone's properties keep sharing their materials with the cached copy and the world's copies of the item (reference-counted, cached materials), and its controllers would write into them. The Inventory3DManager-built model (the engine's own inventory preview model) does run its effects; whether its materials are its own is not verified (the exe on disk cannot be read statically) |
| Particle shaders | if the particle shaders fail to compile, or the device refuses them, both views draw without particles (logged once) | they are a compile stage of their own, so their failure never touches the other shaders or the bundle |
| Item view: sequence-driven effects | an effect a NiControllerManager sequence drives (the bound bow's flames) keeps the look it was collected with; a sequence-driven emitter runs its idle or looping sequence's rate, particle size, life, speed and gravity | no sequence plays in the view: playing one needs the manager's blending, and which sequence the game would show an item in depends on events the view does not have |
| Art attached node by node | spell art whose attach technique hangs parts on several bones may still be collected | only each effect's own art root, the effect shaders' addon roots and the named attachment nodes are known (a readied spell's hand art is found through its caster); the log lists each effect piece kept with its parents |

## 12. Layout and build

- `api/Phantasm_API.h` the public header (MIT), the one clients copy
- `native/src` the plugin; `native/src/core` engine-free rules; `native/tests` their tests
- `native/extern/magelight` Magelight's API headers (MIT), copied unchanged
- `native/extern/skse-menu-framework` SKSE Menu Framework's API header (MIT), copied unchanged
- `data/` the files installed under `Data` (the blank carrier movie, `SKSE/Plugins/Phantasm.ini`, the Magelight
  settings page under `Magelight/Phantasm/views/settings`, the licences)
- `testclient/` PhantasmProbe, a development client (never shipped)

`native\build.ps1 -Tests` builds the plugin and runs the self-tests (its header lists what it needs;
`PHANTASM_BUILD_DIR` sets the build folder). `native/ports/commonlibsse-ng` is an overlay port that pins
CommonLibSSE-NG to MinLL/CommonLibVR, the MIT continuation, with a VR fix. Packaging a release:
[RELEASING.md](RELEASING.md).

## 13. Settings while the game runs

`Settings.h` holds `Phantasm.ini` in force. `Settings::Load` reads it at kDataLoaded (`SettingsCore::Parse`, engine-free
and tested); after that two optional pages read and change the values, and `Settings::Apply` puts a change in force and
writes it back into the file's own text (`SettingsCore::Rewrite`: a changed key's line keeps its place and its
comments, a key the file lacks is added only when its value is not the default the file's silence means).

- **What holds at once**: the live views' budget (`SetLiveBudget`, atomics), the Magelight page's key
  (`SettingsPage::Rebind`: the host lets go of the old key and takes the new one; one it refuses, held by another mod,
  leaves the old one bound and `Status::keyError` says why), and for neural rendering its switch, its live views and
  their size, Follow, Prestart, the style, the strengths and the passes. `NeuralStills::Reconfigure` hands the change
  to the NR worker, which takes it up ahead of its jobs: the worker owns the configuration, other threads read its
  atomics. Turning NR on starts it there, as a followed mod turning its own NR on does. A `Folder` that holds no
  library is not settled: the status says so, and a changed `Folder` is looked at again.
- **What waits for the next start of the game**: where NR's library comes from (`Folder`, `Unsigned`) once NR has
  found it. `Status::restartNeeded` says so; the file has the new value.
- **The pages** call only `Settings` (`Get`, `Apply`, `ApplyJson`, `TakePageKey`, `GetStatus`, `ToJson`), which is
  safe on any thread and touches no game state, so neither page needs the game thread. A page catches the key for
  the Magelight page as a Windows virtual-key code and `TakePageKey` turns it into the DirectInput scan code the
  host binds (`SettingsCore::KeyFromScan`, `KeyUsable`: not Escape, Enter or a modifier alone).
  - `SettingsPanel` (SKSE Menu Framework, when `SKSEMenuFramework.dll` is loaded and has every export the panel calls):
    Mod Control Panel, Phantasm > Settings. It draws on the framework's render thread, and reads the keyboard with
    `GetAsyncKeyState` while it catches a key.
  - `SettingsPage` (Magelight UI, when the host and `Data/Magelight/Phantasm/views/settings/index.html` are installed;
    a copy bundled in another mod ships no page, and no view is made for it): one hand-written page, opened by
    `[SettingsPage] Key` (Scroll Lock). The page sends `{"op":"get"}`, `{"op":"set","values":{...}}` or
    `{"op":"key","vk":N}` (0 forgets the last refusal) to the listener `phantasmSettings` and is answered through
    `phantasmSettingsEvent` with `{"values":{...},"status":{...}}` (`Settings::ToJson`).

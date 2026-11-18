# Changelog

Phantasm follows semantic versioning from 1.0.0 ([docs/API.md](docs/API.md), Compatibility). 0.2.0, 0.3.0 and 0.4.0
reached players inside SeverActions; 0.5.0 was never released and became 1.0.0. 1.1.0 was only ever a test build and
became 1.2.0: `docs/API.md` and the header still give the `aim`, `zoom` and `scale` hints as added in 1.1.0 and gate
them on `pluginVersion >= 10100`, which 1.2.0 passes. A client tells versions apart by `Interface1::pluginVersion`
(MAJOR*10000 + MINOR*100 + PATCH).

## 1.3.0 - 2026-11-17

For players:

- An NPC whose record gives a very dark skin tone (a Dremora, for one) has a body as dark as the game draws it.
  Phantasm used to ignore a tone that dark and drew the plain skin texture, so the body came out lighter than in
  game and no longer matched the face.
- Skin no longer looks like plastic when its texture keeps the shine in the normal map, as most body and skin texture
  mods with tangent-space normal maps do. Phantasm gave such skin the same gloss everywhere, or read a separate
  specular map (`_s`) the game itself ignores on that kind of skin; it now reads where the texture says the skin
  shines, as the game does. Skin with a model-space normal map and its `_s` map (the vanilla body's) is drawn as
  before.
- A head-and-shoulders picture of a creature whose skeleton names its head its own way (a dog's `Canine_Head`, for
  one) now shows its head, with either `aim`. Phantasm looked only for a person's head bone, so it framed where a
  person's head would be: the empty air above a dog, which a client such as SeverActions then refused to keep.
- A body mod's bone sizes (The New Gentleman, SOS, RaceMenu's bone scales) now show in Phantasm's classic pose too,
  as in the live pose. The classic pose read each skeleton bone from a copy the skeleton keeps, which those mods do
  not change, so a client that asks for that pose (OmniSight, for one) drew such a body at its default sizes.
- With neural rendering on for live views, a wide view (OmniSight's capture, for one) is no longer blurry. NR worked
  at half of the view's supersampled picture, which a wide view supersamples less, so it worked well below the size
  the view is shown at and its answer was stretched back up. `LiveScale` (Working size) now means the same on every
  view: 0.5 is the size shown, 1 up to twice that (at most the view's supersampled picture). NR's GPU time grows
  with the size a view is shown at, so a large wide view costs about three times what it did; a tall mirror's is
  unchanged.
- A ghost looks like one, in a live view and in a still: its ghostly shader's blue rim and glow are drawn over it, and
  its own body and clothes faint under them, so what is behind it shows through. Phantasm drew ghosts as ordinary
  people before. The look is read from the records, so a copy the game never draws (a preview spawn) has it too. Not
  drawn yet: the shader's drifting particles, a ghost whose look is a script's fade (Katria), another character's
  lasting shader, and a spell's passing one (Oakflesh, for one).
- In a scene a view draws with several actors (a scene preview, for one), a penis is fitted into the opening it is
  aimed at, a vagina, an anus or a mouth, instead of passing beside it or through the body: the scene's animations
  are made for reference bodies, which the actors drawn are not. The opening spreads by how deep it goes. It needs a
  skeleton with the genital bones (XPMSE with a body mod's), and a mouth's place is an estimate still being tuned.

For mod authors:

- `Phantasm.log` names, once per view and pose, each genital bone of a character's skinned pieces with the scale the
  game gives it and the scale the view draws it at (`MannequinRenderer/genitals:`), to find which pose drops a body
  mod's size (The New Gentleman, SOS).

## 1.2.0 - 2026-11-13

For players:

- With a first-person body mod (Improved Camera), a view of the player taken in first person no longer shows a neck
  that ends in a point, missing arms or missing weapons on the back. Such a mod shrinks those parts of the player's
  body for the game's own picture; Phantasm now draws them from the body's own pose.
- A body with more than 128 bones (a soft-body UBE body used with Bone Limit Extender, for one) is posed with the
  rest of the character, instead of standing in its modelled pose while the head, hands and feet follow the
  skeleton. Phantasm now skins a piece with up to 256 bones, the most one piece can use.
- Hair built from doubled triangles (each one in the mesh twice, facing out and facing in, as in many Sims 4
  conversions) no longer shows dark, hard-edged flakes on its lit side and light ones on its dark side in a live view
  or a still: the copy that faces in is now lit like the one that faces out.
- Phantasm's settings can be changed in the game, and a change holds at once: in SKSE Menu Framework's Mod Control
  Panel (Phantasm > Settings), or, with Magelight UI, on the page Scroll Lock opens (either page changes that key).
  Both say whether neural rendering is running, and why not, and both write the changes back to
  `Phantasm.ini`, whose comments stay. Both are optional: without either, the file is edited as before.
- Experimental: `Phantasm.ini [NeuralRendering] Enabled = 1`, or the switch on a settings page, runs NVIDIA DLSS NR
  (the neural rendering of DLSS 5) on the pictures mods ask Phantasm to take of a character (a Cutout or a Portrait
  still), from the `nvngx_dlssnr.dll` you already have. `Folder = auto`, as the file ships, finds the copy your
  neural rendering mod installed (the one it has loaded, else the
  first under `Data/SKSE/Plugins`, `Data/Shaders` or the game's folder, a copy under the mod's own file name
  included, such as Theo's Render Pipeline's `nvngx_dlssnr_int8.dll`); a path names the folder or the file to use. It
  runs inside the game, from Phantasm's own `PhantasmNRInProc.dll`, which holds no code of NVIDIA's and needs
  nothing installed beyond your graphics driver. It starts while the game loads to its main menu, works from a copy
  of the library of its own (150 to 300 MB under `%LOCALAPPDATA%\Phantasm\NR`), so your mod's own use of the library
  is left alone, and keeps other programs' Vulkan overlays and capture hooks out of the game. A library that is slow
  to start then holds the game for that long, and `Phantasm.log` names it. It takes the settings you chose in that
  mod (its NR switch, style and strengths, for Skyrim Upscaler, Theo's Render Pipeline and Open Shaders;
  `Follow = 0` uses `Phantasm.ini`'s). An NVIDIA-signed copy runs through NVIDIA's loader on the GPUs it was made
  for; an unsigned copy (a community build for an older GPU) is loaded directly unless `Unsigned = 0`. `Model`,
  `Intensity`, `Tone`, `Structure`, `Skin` and `Passes` tune it. `LiveViews`, on as the file ships, runs it on a
  character's live view too (one view at a time), at `LiveScale` of its picture's size: a few milliseconds of GPU
  time each frame it draws. Item pictures are unchanged. It keeps several hundred MB of the game's memory while it
  runs.

For mod authors:

- The `aim` hint for actor views: `face` keeps the camera on the actor's face, taking the front from where their head
  looks rather than from their heading, so an actor an animation turns around or lays down (one partner of a paired
  animation) is still seen face on, crown up. It follows the head as the animation moves it; `Orbit` turns the camera
  from the face and `Zoom` closes on it. `RequestStillHinted` takes `aim=face` for a Portrait.
- The `zoom` still hint for a Portrait (0.5 to 3): about 2 frames the face alone, for a badge or a round avatar, with
  the crown kept in the picture.
- The `scale` hint for actor views, and a fourth field of a `scene` entry: an actor drawn at a multiple of their own
  size (0.5 to 2) while a clip plays them, about the spot they stand on. A paired animation is made for figures of
  one size, and a mod that plays it in the game resizes its actors to that size; a client can now show them the same
  way, so a tall and a short actor meet where the animation has them meet.
- A Portrait of an actor whose pose is still moving (the start of an animation) aims at their head where it is when
  the picture is taken, not where it was when their look was read.
- The head bone a view anchors the face and hair on is the skeleton's own, also when a worn piece (a wig with its own
  physics tree) carries a node of the same name.

## 1.0.0 - 2026-11-08

The first standalone release. The API table and structs are unchanged from 0.4.0: everything new is a hint, a query
or a setting, so every 0.4.0 client works as before.

For players:

- Hair no longer floats away from the head after an NPC's 3D is rebuilt: a live view leaves out a skin the game has
  not bound to the skeleton yet and draws it once the game binds it. A wig's unbound hairline rides the head instead
  of showing as a tuft beside it, and a piece the game does not draw either (a body under armour) stays hidden.
- An NPC the game has not drawn yet (placed hidden, or while the game is paused) draws in their own pose in a live
  view, face, hair and arms in place, instead of the classic pose with the face's pieces apart: Phantasm binds such a
  skin to the skeleton itself, as the game would.
- Animation files that some exporters write with an extra (empty) resource container now play.
- Particle systems in Skyrim LE meshes, which some mods still ship, are read and drawn.
- An empty piece of a mesh no longer keeps an actor's look from counting as complete, which held back a frozen view and
  the look Phantasm keeps of an actor.
- Skin decals (eye shadow, scars) no longer show through an eyelid or a nose seen from the side, and an alpha-tested
  decal on the head's own shader is left out (it would take the face's tint).
- On VR, an item view runs its item's effects (a torch's flame): Phantasm reads the game's inventory preview models
  with VR's own layout.
- New `Data/SKSE/Plugins/Phantasm.ini`: `[LiveViews] Budget` sets how much of each frame the live views may take:
  `smooth` (the default), `balanced` (0.4.0's budget) or `light`; `BudgetMs` sets a number of milliseconds instead.
  The views behind the newest take turns longest-waited first, so they draw at a more even rate.

For mod authors:

- The `hostless` hint: a view whose client draws the `Shown` SRV itself draws with no Magelight installed, its
  `imageName` and `imageUrl` empty. Without the hint such a view still ends `no-image`.
- The `freeze` hint and its `QueryHint("freeze")` answer: a view keeps drawing the look it holds while the actor is
  dressed otherwise, for a before-and-after beside a live view.
- `QueryHint("stats")`: a view's frame rates over the last second (drawn, skipped, waited, stalled), the CPU time of a
  drawn frame and the budget in force.
- The `idles` answer says why a chosen clip cannot play (`plays: false` with its `reason`), and `pending: true` while
  the choice has not been tried yet.
- The header says when a kept SRV must be released: at the view's next `Shown`, `Waiting` or `Ended`; a `Reply` leaves
  it valid.

## 0.4.0 - 2026-11-07

For players:

- Several views draw at once: the newest four (two on VR), the others waiting their turn, within a frame budget.
- Portraits and cutouts are taken while other views draw, instead of waiting for them or being cancelled by them.
- An actor whose skins the game has not bound yet is shot whole in the classic pose, instead of with a missing head,
  hair below a bald crown, or eyes and mouth off the face.
- Item view: a weapon's hidden blood shells are no longer drawn, dark metals reflect a sheen rather than the colours
  of their environment map, and an additive flame no longer draws as squares around the fire.

For mod authors:

- Up to four views draw at once (two on VR); the others get `Waiting("busy")` and come back with `Shown`.
- Shots draw beside the views: `view-drawing` is no longer sent, and a view starting no longer cancels a shot.
- `Still::classicPose` (after `Still::reserved0`; check `size`): the Cutout or Portrait was drawn in the classic pose,
  so a later shot may show the actor as the game does.
- A GPU refusal ends only the view that needed a new GPU object; views already drawing go on.
- The look Phantasm keeps of an actor is kept per `pose` and `weapons` choice, so changing a hint no longer drops it.

## 0.3.0 - 2026-10-29

- The `worn` hint, on item views and item stills: an armor drawn as worn by a man or a woman, its addons' models
  without the body skin they carry.
- Worn armor is framed from its bind pose, so a helmet whose offset lives in its bone data is no longer drawn off
  screen; a shield shows its ground model, and a worn piece with an inventory marker is no longer laid on its back.
- Shots of one item (and one `worn` choice) in a row share its load, and the queue takes the next shot of the item just
  shot ahead of older ones.
- Metal in the item view has its own reflection look.

## 0.2.0 - 2026-10-29

The first version that reached players, inside SeverActions. It is SeverActions' actor renderer as a plugin of its own,
with the C API `Interface1` (`api/Phantasm_API.h`) that any mod can use.

- Live views of actors, shown as Magelight images or drawn by the client from the SRV, with camera control (`Orbit`,
  `Zoom`, `ResizeView`), and `RebuildActor3D` so a view shows no torn frame when a client rebuilds an actor.
- Hints: `pose` (classic or captured), `motion` (follow the live bones and idles), `weapons`, `floor`, `lighting`
  presets, `expression`, `idle` (a clip from the `idles` query, `still`, or an animation file), `scene` (up to three
  other actors playing their own clips), `props` (animation objects and furniture) and `keepLook`.
- Item views (`OpenItemView`) of an item's model with its effects and particles.
- Stills: a `Figure` still of what a view shows, `Cutout` and `Portrait` shots of an actor on their own, item stills,
  still hints (`turn`, `lighting`, `pitch`, `weapons`) and `CancelStill`. Shots queue (`busy` only past 32 waiting),
  shots of one actor in a row share one load, and shots need no UI host.
- Rendering: SMP and CBPC physics on hair and cloth, RaceMenu overlays, skin decals, eyes, effect pieces that animate
  while the game is paused, a readied spell's casting art, and Portrait shots aimed at a creature's head.
- Live views and portrait shots on Skyrim VR.

# Changelog

Phantasm follows semantic versioning from 1.0.0 ([docs/API.md](docs/API.md), Compatibility). 0.2.0, 0.3.0 and 0.4.0
reached players inside SeverActions; 0.5.0 was never released and became 1.0.0. A client tells versions apart by
`Interface1::pluginVersion` (MAJOR*10000 + MINOR*100 + PATCH).

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

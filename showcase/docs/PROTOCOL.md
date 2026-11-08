# Phantasm Showcase - page protocol

The page sends JSON on the `showcase` channel and listens on `showcaseEvent`. A request is
`{ id, op, ...args }`; its answer is `{ id, ok: true, data }` or `{ id, ok: false, error, detail }` with `error` one of
`badRequest`, `notFound`, `refused`, `busy`. `hello` always has id 0. A push is `{ event, ... }`.

FormIDs are 8 hex digits. Slots are `lineup0`-`lineup3`, `armory0`-`armory3`, `anim`, `studio`, `wardrobe`, `before`.

## Requests

| op | args | data |
|----|------|------|
| `hello` | | `{ version, runtime, menuKey, uiScale, showFps, freezeWorld, freezeAvailable, renderer, phantasmVersion, maxDrawing, shotsFolder, playerFemale?, target?, host, open }`; `renderer` is `""` or `absent` / `old` / `refused`; `host` `magelight` / `prisma`; `open` false for a hello while the PrismaUI page is hidden |
| `people` | | `{ people: [{ formId, name, group: player\|follower\|nearby, female, child, distance }] }` |
| `items` | `text?, filter?, source?: all\|held, actor?, offset?, limit?` | `{ total, items: [{ formId, name, plugin, kind, enchanted, value, count?, worn? }] }`; `filter`: `weapons`, `armor`, a kind word, or anything else for all; `held` lists what `actor` carries |
| `viewOpen` | `slot, kind: actor\|item, formId, width, height, scale, hints` | `{ state: opening\|unavailable, reason? }`; width / height in CSS pixels, scale the devicePixelRatio; `hints` a key to string map, `""` removes one |
| `viewHint` | `slot, key, value` | `{}` |
| `viewQuery` | `slot, key` | `{}`; the answer is a `reply` push (`stats`: the view's frame rates, Phantasm 1.0.0+) |
| `setShowFps` | `on` | `{}`; keeps the FPS readout switch and writes `settings.json` |
| `setFreezeWorld` | `on` | `{}`; keeps the Freeze world switch, writes `settings.json` and gives it to Magelight |
| `viewOrbit` | `slot` or `slots: []`, `dx, dy` | `{}` |
| `viewZoom` | `slot, dz, u, v` | `{}` |
| `viewRefresh` | `slot` | `{}` |
| `viewClose` | `slot` | `{}` |
| `viewCloseAll` | | `{}` |
| `anims` | `folder` | `{ state: scanning\|ready, total, folder, folders: [{ name, count }], files: [{ name, path }] }` |
| `animSearch` | `text, limit?` | `{ state, total, files }` |
| `shot` | `formId, framing: figure\|cutout, turn?, pitch?, lighting?, weapons?` | answered later: `{ file, width, height }` |
| `turntable` | `formId, frames, height, lighting` | `{ session, frames }`; each picture is a `frame` push |
| `turntableCancel` | | `{}` |
| `tryBegin` | `formId` | answered later: `{ refusal }` (`player`, `child`, `dead`, `notLoaded`) or `{ worn: [items] }` |
| `tryWear` | `formId, item` | answered later: `{ lent, worn }`; refused `handItem` for a shield, `closed` with the menu closed (`tryBegin` too) |
| `tryRevert` | `formId` | answered later: `{ taken, worn }` |
| `close` | | `{}`; PrismaUI: closes the menu (Escape) |
| `stageRects` | `pageW, pageH, boxes: [{ slot, x, y, w, h, cx, cy, cw, ch, r }]` | `{}`; PrismaUI: each stage's rect, the part shown and its corner radius, in the page's device pixels (DESIGN.md section 9) |

An `idle` hint of `file:<path>` must be a relative `.hkx` path (`badRequest` otherwise).

## Pushes

| event | fields |
|-------|--------|
| `view` | `slot, view: { state: shown, formId, imageUrl, lastSeen } \| { state: waiting\|ended, formId, reason }`; a view the page closed sends no `ended` |
| `reply` | `slot, key, formId, json`: Phantasm's answer to a `viewQuery` |
| `frame` | `session, index, ok, url?, reason?`: one turntable picture as a PNG data URL; it may come before the `turntable` answer |
| `anims` | `state: ready`: the animation walk finished |
| `uiMode` | `on`: PrismaUI only, the menu opened or closed (Magelight says so itself) |

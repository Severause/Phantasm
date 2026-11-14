# Using Phantasm from a mod

Phantasm's API is one header, `api/Phantasm_API.h` (MIT). Copy it into your SKSE plugin unchanged; it needs only the
standard library, and you link nothing. The header is the contract: every function's comment there says what it does
and refuses. This page is the short tour; [DESIGN.md](DESIGN.md) has the reasoning and the details.

## Getting the interface

At SKSE's `kDataLoaded` or later, find `RequestPluginAPI` in `Phantasm.dll` and ask it for `kInterface1`. It returns
the `Interface1` table, or null when Phantasm is not installed. Treat null as "no picture" and carry on: a client never
fails hard because Phantasm is missing.

`Interface1` starts with its `size`, its `interfaceVersion` and Phantasm's `pluginVersion` (MAJOR*10000 + MINOR*100 +
PATCH, so 1.0.0 is 10000). See the feature table below for what to check before using what.

## Clients and views

- **Register once** with `RegisterClient(name, onEvent, user, &client)`. The name is 1-24 characters of `a-z`, `0-9`,
  `-` and `_`, one registration per name per process. `ReleaseClient` ends every view of the client and frees the name.
- **Open a view** with `OpenView(client, slot, actorFormId, width, height, Framing::Figure, &view)`, or an item with
  `OpenItemView`. A slot follows the client name's rule; the view's Magelight image is `phantasm.<client>.<slot>` for
  the life of the process. Slots are a small fixed set you reuse, one per place on your page, never one per actor:
  opening a slot that already holds a view points that view at the new actor. `width` and `height` are the size you
  show it at, in device pixels. A client holds at most 16 views.
- **Move the camera** with `Orbit` and `Zoom`, resize with `ResizeView`, and call `Refresh` after the actor's look
  changes. Call `RebuildActor3D` instead of the game's own 3D rebuild of an actor a view may show; it has its own
  thread rule (the header, and [DESIGN.md section 2](DESIGN.md#2-the-api-apiphantasm_apih)).
- **Hints** (`SetHint`) set a view's pose, light, idle and so on; `QueryHint` asks for an answer that comes as a
  `Reply`. Hints are best-effort: an unknown key is ignored, and the look may change between releases.
  [DESIGN.md section 5](DESIGN.md#5-hints-unfrozen) lists them.

A call that returns `Ok` has only been accepted: its outcome comes later as an event. A call that returns anything
else changed nothing, and no event follows it. Any thread may call (`RebuildActor3D` excepted), and no call blocks.

## Events

Your `onEvent` gets every event of your views, on the game thread, outside any render phase. Match events by view id,
never by slot or image name: an `Ended` can arrive after you have opened the same slot again under a new id.

| Event | Meaning |
|-------|---------|
| `Shown` | The view draws. `imageName`, `imageUrl`, `srv`, `width` and `height` are set (the image fields empty for a hostless view). Sent at its first frame (up to about 5 s after `OpenView`) and again whenever any of them changes. |
| `Waiting` | The view is open but does not draw now (`reason` is `busy`: more views are open than draw at once, or the renderer waits out a GPU refusal). It comes back with `Shown` by itself. |
| `Ended` | The view is gone for good, with a `reason` token (`closed`, `game-load`, `missing`, `no-image`...). Open it again to retry. Treat an unknown token as "no picture". |
| `Reply` | The answer to a `QueryHint`: `key` and its `json`. |

Every string in an event lives only for the call: copy what you keep. Read only the fields within the event's `size`.

**The SRV's lifetime.** `Shown`'s `srv` is valid for the call only. To draw it later, `AddRef` it in the `Shown`
handler and `Release` it at the view's next `Shown` (which carries the new one), `Waiting` or `Ended`, or when you stop
drawing it, at the latest at `ReleaseClient` (no event comes after that). A `Reply` leaves it valid. Never write to it.

**Image size.** Give the `<img>` (or CSS `url()` box) a CSS width and height from the view's own size (`OpenView`'s or
`ResizeView`'s width and height over `devicePixelRatio`), never from `Shown`'s `width` and `height` and never
`height: auto`: the image is supersampled and its natural size is fixed at the slot's first offscreen.

**Game load.** A save loading or a new game ends every view with `game-load`. Open your views again after SKSE's
`kPostLoadGame` or `kNewGame` (SKSEVR sends no `kNewGame`: on VR, wait for the player to be in a cell).

## Close views before your page goes

A view nobody closes keeps drawing, during play too. Call `CloseView` before the page or panel that shows it goes
away, or hide the page only after `CloseView`: drawing into a page the UI host is tearing down has crashed in the
driver. Phantasm cannot see your page, so this is your job
([DESIGN.md section 4](DESIGN.md#4-carriers-and-when-drawing-happens)).

While an item view draws, the game refuses to save: the item view runs in an item menu.

## Drawing the SRV yourself (hostless)

A view is normally published as a Magelight texture image, and `Shown` carries both the image URL and the SRV. A
client that draws the SRV itself (another UI host, its own overlay, ImGui) can work without Magelight: set the view's
`hostless` hint to `on` with or right after `OpenView` / `OpenItemView`. Then, on an install with no Magelight (or one
too old for texture images), the view draws and `Shown` carries the SRV with `imageName` and `imageUrl` empty. Without
the hint, such an install ends the view `no-image`. A Magelight that is installed but cannot take images (its CPU
renderer, or before its first in-game frame) ends views `no-image` either way. Draw the SRV on the game's own D3D11
device; Phantasm skips frames whose picture cannot have changed, so the SRV may hold the same picture for a while.

## Stills

A still is one picture handed to your `done` callback once, on the game thread, as RGBA8 pixels (`width * height * 4`
bytes, valid for the call only). A Cutout, Portrait or item still needs no UI host. To show a still on a page, encode
it yourself (a PNG file, a data URL).

- `RequestStill(..., Framing::Figure, ...)`: what the drawing view of that actor shows, once its scene has settled.
- `RequestStill` with `Framing::Cutout` or `Framing::Portrait`: the actor drawn on their own (whole figure, or head and
  shoulders) in their standing idle, on a transparent ground (premultiplied alpha). `standingPx` is the figure's
  standing height in pixels.
- `RequestStillHinted`: the same with still hints (`turn`, `lighting`, `pitch`, `weapons`, and since 1.1.0 `aim` and
  `zoom` for a Portrait).
- `RequestItemStill`: an item's model on its own, with hints (`turn`, `pitch`, `worn`).
- `CancelStill` ends a Cutout, Portrait or item still that has not answered yet.

Shots run one at a time and queue (`busy` only past 32 waiting); shots of one actor or item in a row share one load,
so ask for every angle at once. A shot that could not be taken answers `ok` false with a `reason` token.
[DESIGN.md section 10](DESIGN.md#10-stills) has the details.

## What each version offers

`size` tells whether a function or field is there: a function or field added after `Interface1`'s first build exists
only when `size >= offsetof(<struct>, <name>) + sizeof(<name>)`. `pluginVersion` tells behaviour apart. A query a build
does not answer replies `{"known":false}`.

| Feature | Since | How to detect |
|---------|-------|---------------|
| All 16 `Interface1` functions, `RegisterClient` to `RequestItemStill` | 0.2.0 (every release) | `Interface1::size` covers the function (cheap; every release passes) |
| The `idles` and `expression` queries | 0.2.0 | always answered for an actor view |
| Shots queue (`busy` only past 32), share one load per actor, need no UI host; `pitch` and `weapons` still hints | 0.2.0 | `pluginVersion >= 200` |
| The `worn` hint (item views and item stills) | 0.3.0 | `pluginVersion >= 300` |
| Several views draw at once (four, two on VR) | 0.4.0 | `pluginVersion >= 400` |
| Shots draw beside views (`view-drawing` no longer sent) | 0.4.0 | `pluginVersion >= 400` |
| `Still::classicPose` | 0.4.0 | `still->size >= offsetof(Still, classicPose) + sizeof(bool)` |
| The `hostless` hint | 1.0.0 | `pluginVersion >= 10000` (an older build ignores the hint) |
| The `freeze` hint and query (actor views) | 1.0.0 | `QueryHint(view, "freeze")` answers `"known": true`; an older build answers `{"known":false}` |
| The `stats` query | 1.0.0 | `QueryHint(view, "stats")` answers `"known": true`; an older build answers `{"known":false}` |
| The `idles` answer's refusal `reason` and `pending` | 1.0.0 | `pluginVersion >= 10000` |
| The `aim` hint (actor views) and `aim` still hint (Portrait) | 1.1.0 | `pluginVersion >= 10100` (an older build ignores the hint) |
| The `zoom` still hint (Portrait) | 1.1.0 | `pluginVersion >= 10100` (an older build ignores the hint) |
| The `scale` hint (actor views) and a `scene` entry's scale | 1.1.0 | `pluginVersion >= 10100` (an older build ignores both) |

1.1.0 was a test build only: 1.2.0 is the first release with what this table gives as 1.1.0. Gate on the number shown.

## Compatibility

From 1.0.0 Phantasm follows semantic versioning:

- **Within 1.x nothing in `Interface1` changes or goes away.** Every function, struct field, enum value and reason
  token keeps its meaning. New functions and fields are appended to the end of `Interface1`'s table or of a struct,
  behind `size`, so `RequestPluginAPI(kInterface1)` keeps working and a client written for 1.0.0 runs on every 1.x.
  A minor release may add reason tokens: treat an unknown one as "no picture".
- **Not frozen:** the hint keys and values, the query keys and their answers, and the look (pose, light, camera,
  framing, the frame budget). These may change in a minor release. A client must keep working when a hint is ignored
  or a query answers `{"known":false}`.
- A change that cannot be made by appending would be a new major version.

## A minimal client

For a complete one, see [showcase/](../showcase/): it opens actor and item views in several slots, draws them as
Magelight images, under a PrismaUI page and in an ImGui window, and takes stills (`native/src/Views.cpp`).

```cpp
#include "Phantasm_API.h"
#include <d3d11.h>

namespace
{
    const PHANTASM_API::Interface1* g_api  = nullptr;
    PHANTASM_API::ClientId          g_me   = 0;
    PHANTASM_API::ViewId            g_view = 0;
    ID3D11ShaderResourceView*       g_srv  = nullptr;   // kept only if you draw it yourself; game thread only here

    void OnPhantasmEvent(const PHANTASM_API::Event* e, void*)
    {
        using K = PHANTASM_API::EventKind;
        if (e->view != g_view || e->kind == K::Reply) return;   // a Reply leaves the SRV valid
        if (g_srv) { g_srv->Release(); g_srv = nullptr; }      // Shown, Waiting and Ended end the kept one
        if (e->kind == K::Shown && e->srv) { g_srv = e->srv; g_srv->AddRef(); }
        if (e->kind == K::Shown) { /* show e->imageUrl on the page; it is empty for a hostless view */ }
        if (e->kind == K::Ended) g_view = 0;
    }
}

void PhantasmStart()   // at kDataLoaded or later
{
    auto* get = reinterpret_cast<PHANTASM_API::RequestPluginAPIFn>(
        GetProcAddress(GetModuleHandleW(L"Phantasm.dll"), "RequestPluginAPI"));
    g_api = get ? static_cast<const PHANTASM_API::Interface1*>(get(PHANTASM_API::kInterface1)) : nullptr;
    if (g_api && g_api->RegisterClient("mymod", &OnPhantasmEvent, nullptr, &g_me) != PHANTASM_API::Result::Ok) {
        g_api = nullptr;
    }
}

void ShowActor(std::uint32_t formId)   // the page opened
{
    if (!g_api) return;   // no Phantasm: no picture
    g_api->OpenView(g_me, "face", formId, 256, 320, PHANTASM_API::Framing::Figure, &g_view);
    g_api->SetHint(g_view, "hostless", "on");   // only if you draw g_srv yourself
}

void HideActor()   // before the page goes
{
    if (g_view) g_api->CloseView(g_view);
    g_view = 0;
    if (g_srv) { g_srv->Release(); g_srv = nullptr; }   // no event of the closed view is acted on
}
```

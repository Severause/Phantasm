import { useEffect, useRef, useState, type PointerEvent } from 'react';
import { client } from '../api/client';
import type { ItemCard } from '../api/protocol';
import { ActorPicker, Dropdown, LIGHT_OPTIONS } from '../components/Controls';
import { ItemList } from '../components/ItemList';
import { LiveView, type TroubleKind } from '../components/LiveView';
import { KIND_LABEL } from '../lib/labels';
import { useApp } from '../state/app';

// Outfit pieces only: a shield would put down what the left hand held, which a revert does not give back.
const FILTERS = [
  { value: 'outfit', label: 'All' },
  { value: 'body', label: 'Body' },
  { value: 'head', label: 'Head' },
  { value: 'hands', label: 'Hands' },
  { value: 'feet', label: 'Feet' },
  { value: 'jewelry', label: 'Jewellery' },
];
const FRAMES = 24;
const PX_PER_FRAME = 14;
// How long Phantasm may take to answer the freeze key at all before the stills take over. One that knows the key is
// waited for however long the hold takes (the stills turn in steps, the live view does not); past this the side panel
// offers the stills instead.
const FREEZE_WAIT_MS = 8000;
const FREEZE_POLL_MS = 400;

const REFUSAL_TEXT: Record<string, string> = {
  player: "You can't try clothes on yourself here: pick an NPC.",
  child: "Children can't be dressed.",
  dead: 'They are dead.',
  notLoaded: "They aren't close enough.",
};

/**
 * Before: how they look now, held while the right side changes. With a Phantasm that has the `freeze` hint the left
 * side is a second live view holding its look in the classic pose (a still doll: quick to hold, nothing animating to
 * copy), live until it holds; with an older one, or a view Phantasm would not draw, it is a turntable of cutout
 * stills taken first.
 */
type Before =
  | { mode: 'none' }
  | { mode: 'freezing' }
  | { mode: 'frozen' }
  | { mode: 'stills'; session: number; frames: (string | null)[] };

export function Wardrobe() {
  const { people, hello, openCount, clean, maxDrawing, toast, fail } = useApp();
  const [actor, setActor] = useState('');
  const [refusal, setRefusal] = useState('');
  const [worn, setWorn] = useState<ItemCard[]>([]);
  const [before, setBefore] = useState<Before>({ mode: 'none' });
  const [busy, setBusy] = useState(false);
  const [changed, setChanged] = useState(false);
  const [light, setLight] = useState('studio');
  const [frame, setFrame] = useState(0);
  const actorRef = useRef('');
  // Set once the left side is decided for this NPC: several freeze answers can be on their way at once.
  const decided = useRef(false);
  // Phantasm answered the freeze key as known for this NPC's view: the wait for `held` no longer times out.
  const freezeKnown = useRef(false);
  // Known, but not held after FREEZE_WAIT_MS (their look still owes a piece, or the view waits its turn): try-ons wait,
  // and the side panel says why and offers the stills.
  const [slowHold, setSlowHold] = useState(false);
  // The before picture is up: the hold note shows only then, since LiveView's own note (opening, waiting) sits in the
  // same place.
  const [beforeShown, setBeforeShown] = useState(false);

  const ready = before.mode === 'frozen' || (before.mode === 'stills' && before.frames.every((f) => f !== null));

  // Open: start the session (it records what they wear), then hold the "before" look. Again on each open of the menu:
  // its close reverted and ended the session.
  useEffect(() => {
    actorRef.current = actor;
    decided.current = false;
    freezeKnown.current = false;
    setSlowHold(false);
    setRefusal('');
    setChanged(false);
    setBefore({ mode: 'none' });
    if (!actor) return;
    let live = true;
    client.request('tryBegin', { formId: actor }).then((r) => {
      if (!live) return;
      if (r.refusal) {
        setRefusal(r.refusal);
        return;
      }
      setWorn(r.worn ?? []);
      setBefore({ mode: 'freezing' });
    }, fail);
    return () => {
      live = false;
      client.fire('turntableCancel', {});
      client.fire('tryRevert', { formId: actor });
    };
  }, [actor, openCount, fail]);

  // A frozen view says it holds its look (QueryHint "freeze": held); an older Phantasm does not know the key. The
  // fallback's clock starts with the session, so a view that never answers still falls back. A view whose Phantasm
  // knows the key stays live until it holds: it freezes once its look is complete, which can take a while (a texture
  // or piece still loading), and the drag stays smooth meanwhile. A new session (NPC, menu open) restarts the poll:
  // the old one's cleanup then runs before the session effect clears `decided` and `freezeKnown`, so no tick of the
  // old poll sees the new session's state.
  useEffect(() => {
    if (before.mode !== 'freezing') return;
    const started = Date.now();
    const ask = window.setInterval(() => {
      if (decided.current) return;
      if (Date.now() - started > FREEZE_WAIT_MS) {
        if (!freezeKnown.current) {
          window.clearInterval(ask);
          startStills();
          return;
        }
        setSlowHold(true);
      }
      client.fire('viewQuery', { slot: 'before', key: 'freeze' });
    }, FREEZE_POLL_MS);
    const off = client.onPush((p) => {
      if (p.event !== 'reply' || p.slot !== 'before' || p.key !== 'freeze' || p.formId !== actorRef.current) return;
      const j = p.json as { known?: boolean; held?: boolean };
      if (decided.current) return;
      if (j.known === false) {
        window.clearInterval(ask);
        startStills();
      } else if (j.held) {
        window.clearInterval(ask);
        decided.current = true;
        setSlowHold(false);
        setBefore({ mode: 'frozen' });
      } else if (j.known) {
        freezeKnown.current = true;
      }
    });
    client.fire('viewQuery', { slot: 'before', key: 'freeze' });
    return () => {
      window.clearInterval(ask);
      off();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [before.mode, actor, openCount]);

  // A view Phantasm would not draw (refused, ended) leaves the stills as the only "before". A wait ("busy") passes by
  // itself on a Phantasm that draws several views; where only one draws, the before view, opened first, waits for good.
  const onBeforeTrouble = (_note: string, kind: TroubleKind) => {
    if (before.mode !== 'freezing') return;
    if (kind === 'ended' || maxDrawing <= 1) startStills();
  };

  const startStills = () => {
    if (decided.current) return;
    decided.current = true;
    const formId = actorRef.current;
    setBefore({ mode: 'stills', session: -1, frames: Array(FRAMES).fill(null) });
    setFrame(0);
    client.request('turntable', { formId, frames: FRAMES, height: 960, lighting: light === 'here' ? 'hearth' : light }).then(
      (r) => setBefore((b) => (b.mode === 'stills' && (b.session === -1 || b.session === r.session) ? { ...b, session: r.session } : b)),
      fail,
    );
  };

  useEffect(
    () =>
      client.onPush((p) => {
        if (p.event !== 'frame') return;
        setBefore((b) => {
          // A frame can come before the turntable's own answer: the first one names the session (a cancelled one sends
          // no more frames).
          if (b.mode !== 'stills' || (b.session !== -1 && b.session !== p.session) || p.index >= b.frames.length) return b;
          const frames = b.frames.slice();
          frames[p.index] = p.ok && p.url ? p.url : '';
          return { ...b, session: p.session, frames };
        });
      }),
    [],
  );

  const wear = (item: ItemCard) => {
    if (!ready || busy) return;
    setBusy(true);
    client
      .request('tryWear', { formId: actor, item: item.formId })
      .then((r) => {
        setWorn(r.worn);
        setChanged(true);
        if (r.lent) toast(`${item.name} lent for the fitting; it goes back when you revert.`);
        client.fire('viewRefresh', { slot: 'wardrobe' });
      }, fail)
      .finally(() => setBusy(false));
  };

  const revert = () => {
    setBusy(true);
    client
      .request('tryRevert', { formId: actor })
      .then((r) => {
        setWorn(r.worn ?? []);
        setChanged(false);
        // The session ended: start a new one so the next piece is recorded against the same "before".
        return client.request('tryBegin', { formId: actor });
      })
      .then(() => client.fire('viewRefresh', { slot: 'wardrobe' }), fail)
      .finally(() => setBusy(false));
  };

  // The stills turntable: a drag steps through the frames.
  const drag = useRef<{ x: number; start: number } | null>(null);
  const onDown = (e: PointerEvent) => {
    drag.current = { x: e.clientX, start: frame };
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
  };
  const onMove = (e: PointerEvent) => {
    if (!drag.current || before.mode !== 'stills') return;
    const n = before.frames.length;
    const step = Math.floor((e.clientX - drag.current.x) / PX_PER_FRAME);
    setFrame((((drag.current.start + step) % n) + n) % n);
  };

  const name = people.find((p) => p.formId === actor)?.name ?? '';
  const target = actor && !refusal ? { kind: 'actor' as const, formId: actor } : null;
  const live = { pose: 'captured', motion: 'on', lighting: light, floor: 'on', weapons: 'off' };
  const loaded = before.mode === 'stills' ? before.frames.filter((f) => f !== null).length : 0;

  return (
    <div className="page wardrobe">
      <div className="compare">
        <div className="compare-side">
          {before.mode === 'stills' ? (
            <div className="panel live turntable" onPointerDown={onDown} onPointerMove={onMove} onPointerUp={() => (drag.current = null)}>
              {before.frames[frame] ? <img className="tt-img" src={before.frames[frame]!} alt="" draggable={false} /> : null}
              {loaded < before.frames.length && <div className="live-note"><span className="spinner" />Taking the before pictures… {loaded}/{before.frames.length}</div>}
            </div>
          ) : (
            <LiveView slot="before" className="panel" target={before.mode === 'none' ? null : target}
              onTrouble={onBeforeTrouble} onShown={setBeforeShown}
              hints={{ ...live, pose: 'classic', motion: 'off', freeze: 'on' }} empty={<span>{refusal ? REFUSAL_TEXT[refusal] ?? refusal : 'Choose someone to dress'}</span>}>
              {before.mode === 'freezing' && beforeShown && (
                <div className="live-note"><span className="spinner" />{slowHold ? 'Holding their look once it has fully loaded…' : 'Holding their look…'}</div>
              )}
            </LiveView>
          )}
          <div className="compare-tag">Before</div>
        </div>
        <div className="compare-side">
          {/* Opens at once; only a try-on waits for the "before" side to hold its look. */}
          <LiveView slot="wardrobe" className="panel" target={before.mode === 'none' ? null : target} hints={{ ...live, keepLook: 'off' }}
            empty={<span>{refusal ? '' : actor ? 'Getting ready…' : ''}</span>}>
            {name && <div className="nameplate">{name}</div>}
          </LiveView>
          <div className="compare-tag accent">{changed ? 'After' : 'Now'}</div>
        </div>
      </div>
      {!clean && (
        <aside className="side right">
          <ActorPicker value={actor} onChange={setActor} placeholder="Choose an NPC" filter={(p) => p.group !== 'player' && !p.child} />
          {refusal && <div className="panel-warn">{REFUSAL_TEXT[refusal] ?? refusal}</div>}
          <div className="worn">
            <div className="side-title">Wearing</div>
            {worn.length === 0 && <div className="fine">Nothing.</div>}
            {worn.map((w) => (
              <div key={w.formId} className="worn-row"><span className={`item-dot k-${w.kind}${w.enchanted ? ' is-ench' : ''}`} />{w.name}<span className="item-kind">{KIND_LABEL[w.kind]}</span></div>
            ))}
          </div>
          <div className="row">
            <button type="button" className="btn primary" disabled={!changed || busy} onClick={revert}>Revert</button>
            <Dropdown value={light} options={LIGHT_OPTIONS} onChange={setLight} disabled={before.mode !== 'none' && before.mode !== 'frozen'} />
          </div>
          {before.mode === 'freezing' && slowHold && (
            <div className="row">
              <span className="fine">"Before" holds once their look has fully loaded; try-ons wait for it.</span>
              <button type="button" className="btn ghost" onClick={startStills}>Use pictures</button>
            </div>
          )}
          <div className="side-title">Try on</div>
          <ItemList filters={FILTERS} defaultFilter="outfit" holder={actor || undefined} holderLabel="Their things" onPick={wear}
            picked={new Set(worn.map((w) => w.formId))} disabled={!ready || busy || !!refusal} />
          <p className="fine">
            {hello && hello.phantasmVersion >= 500 ? '' : 'This Phantasm has no frozen views, so "before" is a turntable of pictures. '}
            A piece they don't own is lent for the fitting and taken back when you revert or close the menu.
          </p>
        </aside>
      )}
    </div>
  );
}

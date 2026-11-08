import { useCallback, useEffect, useLayoutEffect, useRef, useState, type PointerEvent, type ReactNode, type RefObject, type WheelEvent } from 'react';
import { client } from '../api/client';
import type { Hints, Slot, ViewState } from '../api/protocol';
import { prismaLayout } from '../lib/host';
import { viewNote } from '../lib/labels';
import { registerStage } from '../lib/underlay';
import { useApp } from '../state/app';

// A drag turns the subject half a degree per CSS pixel; pitch goes to Phantasm as pixels, which the API takes.
const YAW_PER_PX = 0.5;
// The box's size settles this long before the view is resized: one layout change reports many sizes.
const SIZE_SETTLE_MS = 150;
const MIN_SIDE = 32;
const WHEEL_LINE_PX = 40;
// How often a drawing view's frame rate is asked for (Phantasm counts over a second).
const STATS_POLL_MS = 1000;

/** Phantasm's QueryHint("stats") answer (1.0.0+); an older Phantasm answers known: false. */
interface ViewStats {
  known?: boolean;
  drawing?: boolean;
  valid?: boolean;
  fps?: number;
  drawnFps?: number;
  skippedFps?: number;
  stalledFps?: number;
  waitedFps?: number;
  cpuMs?: number;
  budgetMode?: string;
}

/** The corner readout: the rate the view kept up at, `held` while it had nothing new to draw, and the game's own
 * rate beside it. Every frame the game renders gives the view one turn (drawn, unchanged, stalled or waiting), so
 * the turns a second are the game's real frame rate (frame generation's extra frames are not rendered by the game,
 * and a view cannot draw in them). The colour says how much of the game's rate the view keeps. */
function FpsBadge({ stats }: { stats: ViewStats }) {
  const drawn = stats.drawnFps ?? 0;
  const skipped = stats.skippedFps ?? 0;
  const stalled = stats.stalledFps ?? 0;
  const waited = stats.waitedFps ?? 0;
  const fps = stats.fps ?? 0;
  const game = drawn + skipped + stalled + waited;
  const held = drawn < 1 && skipped > 0;
  const kept = game > 0 ? fps / game : 0;
  const tone = held ? 'held' : kept >= 0.9 ? 'good' : kept >= 0.6 ? 'fair' : 'poor';
  const title = `drawn ${drawn.toFixed(0)}, unchanged ${skipped.toFixed(0)}, stalled ${stalled.toFixed(0)}, waited ${waited.toFixed(0)} a second; ${(stats.cpuMs ?? 0).toFixed(2)} ms a drawn frame (${stats.budgetMode ?? '?'} budget)`;
  return (
    <div className={`fps-badge fps-${tone} no-drag`} title={title}>
      <span className="fps-dot" aria-hidden />
      {held ? 'held' : `${Math.round(fps)} fps`}
      {game > 0 && <span className="fps-game">game {Math.round(game)}</span>}
    </div>
  );
}

interface Css {
  w: number;
  h: number;
}

function useSettledSize(ref: RefObject<HTMLDivElement | null>): Css | null {
  const [size, setSize] = useState<Css | null>(null);
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    let timer = 0;
    const measure = () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(() => {
        const w = Math.floor(el.clientWidth);
        const h = Math.floor(el.clientHeight);
        setSize((old) => (w < MIN_SIDE || h < MIN_SIDE ? null : old && old.w === w && old.h === h ? old : { w, h }));
      }, SIZE_SETTLE_MS);
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => {
      ro.disconnect();
      window.clearTimeout(timer);
    };
  }, [ref]);
  return size;
}

export interface Target {
  kind: 'actor' | 'item';
  formId: string;
}

/** waiting: Phantasm says the view cannot draw now and comes back by itself; ended: it could not open or ended. */
export type TroubleKind = 'waiting' | 'ended';

type Phase =
  | { kind: 'idle'; note?: string }
  | { kind: 'shown'; url: string; at: Css; lastSeen: boolean }
  | { kind: 'waiting'; note: string };

/**
 * One Phantasm view drawn into a box: opened when the box has a size and a target, re-targeted or resized as they
 * change, told the hints, and closed on unmount, when the target clears and when the menu closes. Drag turns, the
 * wheel zooms. The picture is drawn at exactly the CSS size the view was opened at (never its natural size).
 */
export function LiveView({ slot, target, hints, className, empty, children, onShown, onTrouble }: {
  slot: Slot;
  target: Target | null;
  hints: Hints;
  className?: string;
  /** shown while nothing is drawn and there is no target */
  empty?: ReactNode;
  /** drawn over the picture (labels, controls) */
  children?: ReactNode;
  onShown?: (shown: boolean) => void;
  /** the view waits, ended or could not open (with Phantasm's reason as page text) */
  onTrouble?: (note: string, kind: TroubleKind) => void;
}) {
  const { hello, openCount, showFps, clean } = useApp();
  const [stats, setStats] = useState<ViewStats | null>(null);
  const boxRef = useRef<HTMLDivElement>(null);
  const size = useSettledSize(boxRef);
  const renderer = hello?.renderer ?? '';
  const [phase, setPhase] = useState<Phase>({ kind: 'idle' });
  const [dragging, setDragging] = useState(false);
  const formRef = useRef(target?.formId);
  formRef.current = target?.formId;
  const openedAt = useRef<Css | null>(null);
  const shown = phase.kind === 'shown';
  const hintsKey = JSON.stringify(hints);

  useEffect(() => {
    setPhase({ kind: 'idle' });
  }, [target?.formId, target?.kind]);

  useEffect(() => {
    onShown?.(shown);
  }, [shown, onShown]);

  // On PrismaUI the DLL draws the picture under this box (lib/underlay.ts); the page draws only what lies over it.
  useEffect(() => {
    const el = boxRef.current;
    return el ? registerStage(slot, el) : undefined;
  }, [slot]);
  const troubleNote = phase.kind === 'waiting' || (phase.kind === 'idle' && phase.note) ? phase.note ?? '' : null;
  const troubleKind: TroubleKind = phase.kind === 'waiting' ? 'waiting' : 'ended';
  useEffect(() => {
    if (troubleNote !== null) onTrouble?.(troubleNote, troubleKind);
  }, [troubleNote, troubleKind, onTrouble]);

  useEffect(() => {
    if (renderer || !size || !target || !hello) return;
    let live = true;
    const at = size;
    client
      .request('viewOpen', { slot, kind: target.kind, formId: target.formId, width: at.w, height: at.h, scale: window.devicePixelRatio || 1, hints: JSON.parse(hintsKey) })
      .then(
        (r) => {
          if (!live) return;
          if (r.state === 'opening') openedAt.current = at;
          else setPhase({ kind: 'idle', note: viewNote(r.reason) });
        },
        (e) => live && setPhase({ kind: 'idle', note: (e as Error).message }),
      );
    return () => {
      live = false;
    };
  }, [renderer, slot, target?.kind, target?.formId, size, openCount, hintsKey, hello]);

  // Closed when the target clears, on unmount and when the menu closes; the next open opens it again.
  const hasTarget = !!target;
  useEffect(() => {
    if (renderer || !hasTarget) return;
    const offClose = client.onClose(() => client.fire('viewClose', { slot }));
    return () => {
      offClose();
      client.fire('viewClose', { slot });
    };
  }, [renderer, slot, hasTarget]);

  useEffect(
    () =>
      client.onPush((p) => {
        if (p.event !== 'view' || p.slot !== slot) return;
        const v: ViewState = p.view;
        if (v.formId !== formRef.current) return;
        if (v.state === 'shown') setPhase({ kind: 'shown', url: v.imageUrl, at: openedAt.current ?? { w: 0, h: 0 }, lastSeen: !!v.lastSeen });
        else if (v.state === 'waiting') setPhase({ kind: 'waiting', note: viewNote(v.reason) });
        else setPhase({ kind: 'idle', note: viewNote(v.reason) });
      }),
    [slot],
  );

  // The frame rate, asked for once a second while the view draws and the readout is on.
  const polling = shown && showFps && !clean && !renderer;
  useEffect(() => {
    if (!polling) {
      setStats(null);
      return;
    }
    const off = client.onPush((p) => {
      if (p.event !== 'reply' || p.slot !== slot || p.key !== 'stats' || p.formId !== formRef.current) return;
      setStats(p.json as ViewStats);
    });
    const ask = () => client.fire('viewQuery', { slot, key: 'stats' });
    ask();
    const t = window.setInterval(ask, STATS_POLL_MS);
    return () => {
      off();
      window.clearInterval(t);
    };
  }, [polling, slot]);

  // Camera moves are summed and sent at most once a frame.
  const pending = useRef({ dx: 0, dy: 0, dz: 0, u: 0.5, v: 0.5, frame: 0 });
  const flush = useCallback(() => {
    const p = pending.current;
    p.frame = 0;
    if (p.dx || p.dy) client.fire('viewOrbit', { slot, dx: p.dx, dy: p.dy });
    if (p.dz) client.fire('viewZoom', { slot, dz: p.dz, u: p.u, v: p.v });
    p.dx = p.dy = p.dz = 0;
  }, [slot]);
  const schedule = useCallback(() => {
    if (!pending.current.frame) pending.current.frame = window.setTimeout(flush, 16);
  }, [flush]);

  const last = useRef<{ x: number; y: number } | null>(null);
  const onPointerDown = (e: PointerEvent) => {
    if (!shown || (e.target as HTMLElement).closest('.no-drag')) return;
    last.current = { x: e.clientX, y: e.clientY };
    setDragging(true);
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
  };
  const onPointerMove = (e: PointerEvent) => {
    if (!last.current) return;
    pending.current.dx += (e.clientX - last.current.x) * YAW_PER_PX;
    pending.current.dy += e.clientY - last.current.y;
    last.current = { x: e.clientX, y: e.clientY };
    schedule();
  };
  const endDrag = () => {
    last.current = null;
    setDragging(false);
  };
  const onWheel = (e: WheelEvent) => {
    if (!shown || (e.target as HTMLElement).closest('.no-drag')) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const p = pending.current;
    p.dz += e.deltaMode === 1 ? e.deltaY * WHEEL_LINE_PX : e.deltaY;
    p.u = rect.width ? (e.clientX - rect.left) / rect.width : 0.5;
    p.v = rect.height ? (e.clientY - rect.top) / rect.height : 0.5;
    schedule();
  };

  const note = renderer
    ? viewNote(renderer)
    : !target
      ? undefined
      : phase.kind === 'idle'
        ? phase.note ?? 'Opening the view…'
        : phase.kind === 'waiting'
          ? phase.note
          : undefined;
  return (
    <div
      ref={boxRef}
      className={`live${dragging ? ' is-dragging' : ''}${shown ? ' is-live' : ''}${className ? ' ' + className : ''}`}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={endDrag}
      onPointerCancel={endDrag}
      onWheel={onWheel}
    >
      {!prismaLayout && shown && phase.kind === 'shown' && (
        <img className="live-img" src={phase.url} alt="" draggable={false}
          style={phase.at.w ? { width: `${phase.at.w}px`, height: `${phase.at.h}px` } : undefined} />
      )}
      {!target && empty && <div className="live-empty">{empty}</div>}
      {note && <div className="live-note"><span className="spinner" aria-hidden />{note}</div>}
      {polling && stats?.known && stats.drawing && stats.valid && <FpsBadge stats={stats} />}
      {children}
    </div>
  );
}

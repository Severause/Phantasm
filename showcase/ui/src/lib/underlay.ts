// On PrismaUI the DLL draws each view's picture under the page (native/src/Underlay.cpp), so the page tells it where
// its stages are: every registered stage's rect, the part of it the page shows, and its corner radius, in the page's
// device pixels, sent whenever any of them changes while the menu is open. Off PrismaUI nothing here runs.
//
// ?prisma=1 in a plain browser draws a stand-in underlay behind the page instead (the same boxes, the mock's pictures),
// to check the layout leaves the stages see-through.

import { client } from '../api/client';
import type { Slot, StageBox } from '../api/protocol';
import { inPrisma, previewPrisma } from './host';

const enabled = inPrisma || previewPrisma;
const stages = new Map<Slot, HTMLElement>();
let open = false;
let frame = 0;
let lastSent = '';

/** The part of el the page shows: its rect cut by every ancestor that clips (overflow other than visible). */
function shownRect(el: HTMLElement): DOMRect {
  const r = el.getBoundingClientRect();
  let x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
  for (let p = el.parentElement; p && p !== document.body; p = p.parentElement) {
    const cs = getComputedStyle(p);
    if (cs.overflowX === 'visible' && cs.overflowY === 'visible') continue;
    const pr = p.getBoundingClientRect();
    x0 = Math.max(x0, pr.left);
    y0 = Math.max(y0, pr.top);
    x1 = Math.min(x1, pr.right);
    y1 = Math.min(y1, pr.bottom);
  }
  x0 = Math.max(x0, 0);
  y0 = Math.max(y0, 0);
  x1 = Math.min(x1, window.innerWidth);
  y1 = Math.min(y1, window.innerHeight);
  return new DOMRect(x0, y0, Math.max(0, x1 - x0), Math.max(0, y1 - y0));
}

function measure(): { pageW: number; pageH: number; boxes: StageBox[] } {
  const dpr = window.devicePixelRatio || 1;
  const boxes: StageBox[] = [];
  for (const [slot, el] of stages) {
    if (!el.isConnected) continue;
    const r = el.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) continue;
    const c = shownRect(el);
    if (c.width < 1 || c.height < 1) continue;
    const radius = parseFloat(getComputedStyle(el).borderTopLeftRadius) || 0;
    const px = (v: number) => Math.round(v * dpr * 10) / 10;
    boxes.push({ slot, x: px(r.left), y: px(r.top), w: px(r.width), h: px(r.height), cx: px(c.left), cy: px(c.top), cw: px(c.width), ch: px(c.height), r: px(radius) });
  }
  return { pageW: Math.round(window.innerWidth * dpr), pageH: Math.round(window.innerHeight * dpr), boxes };
}

function tick() {
  frame = 0;
  if (!open) return;
  const m = measure();
  const key = JSON.stringify(m);
  if (key !== lastSent) {
    lastSent = key;
    if (inPrisma) client.fire('stageRects', m);
    else drawPreview(m.boxes);
  }
  frame = window.requestAnimationFrame(tick);
}

function setOpen(on: boolean) {
  open = on;
  lastSent = '';
  if (on && !frame) frame = window.requestAnimationFrame(tick);
  if (!on) {
    if (frame) window.cancelAnimationFrame(frame);
    frame = 0;
    if (previewPrisma) drawPreview([]);
  }
}

/** A stage the DLL draws a slot's picture into. Returns the unregister. */
export function registerStage(slot: Slot, el: HTMLElement): () => void {
  if (!enabled) return () => {};
  stages.set(slot, el);
  return () => {
    if (stages.get(slot) === el) stages.delete(slot);
  };
}

if (enabled) {
  client.onHello((h) => setOpen(h.open !== false));
  client.onClose(() => setOpen(false));
}

// ── The browser preview's stand-in for the DLL's draw ────────────────────────────────────────────────────────────────

const pictures = new Map<Slot, string>();
if (previewPrisma) {
  client.onPush((p) => {
    if (p.event !== 'view') return;
    if (p.view.state === 'shown') pictures.set(p.slot, p.view.imageUrl);
    else pictures.delete(p.slot);
    lastSent = '';
  });
}

function drawPreview(boxes: StageBox[]) {
  let root = document.getElementById('underlay-preview');
  if (!root) {
    root = document.createElement('div');
    root.id = 'underlay-preview';
    document.body.prepend(root);
  }
  const dpr = window.devicePixelRatio || 1;
  root.replaceChildren(
    ...boxes.map((b) => {
      const d = document.createElement('div');
      d.className = 'underlay-preview-stage';
      Object.assign(d.style, {
        left: `${b.x / dpr}px`,
        top: `${b.y / dpr}px`,
        width: `${b.w / dpr}px`,
        height: `${b.h / dpr}px`,
        borderRadius: `${b.r / dpr}px`,
        clipPath: `inset(${(b.cy - b.y) / dpr}px ${(b.x + b.w - b.cx - b.cw) / dpr}px ${(b.y + b.h - b.cy - b.ch) / dpr}px ${(b.cx - b.x) / dpr}px round ${b.r / dpr}px)`,
      });
      const url = pictures.get(b.slot);
      if (url) {
        const img = document.createElement('img');
        img.src = url;
        d.appendChild(img);
      }
      return d;
    }),
  );
}

// The browser stand-in for the DLL: answers every op of PROTOCOL.md from in-memory state and draws placeholder
// pictures, so the page can be built and clicked through outside the game. Never used when a Magelight host is present.
import type { Transport } from './client';
import type { AnimFile, ErrorCode, HelloData, ItemCard, ItemKind, Person, Push, Request, Slot } from './protocol';
import { WEARABLE } from './protocol';

type Answer = { ok: true; data: unknown } | { ok: false; error: ErrorCode; detail?: string };
const ok = (data: unknown): Answer => ({ ok: true, data });
const fail = (error: ErrorCode, detail?: string): Answer => ({ ok: false, error, detail });

const PEOPLE: Person[] = [
  { formId: '00000014', name: 'Aela', group: 'player', female: true, child: false, distance: 0 },
  { formId: '000A2C94', name: 'Lydia', group: 'follower', female: true, child: false, distance: 180 },
  { formId: '0002BA8E', name: 'Marcurio', group: 'follower', female: false, child: false, distance: 240 },
  { formId: '00019DF7', name: 'Ysolda', group: 'nearby', female: true, child: false, distance: 610 },
  { formId: '00013BA3', name: 'Belethor', group: 'nearby', female: false, child: false, distance: 820 },
  { formId: '00013BAB', name: 'Hulda', group: 'nearby', female: true, child: false, distance: 1250 },
  { formId: '00035533', name: 'Braith', group: 'nearby', female: true, child: true, distance: 1400 },
];

const ITEM_NAMES: [string, ItemKind, boolean, number][] = [
  ['Ancient Nord Sword', 'weapon', false, 8], ['Daedric Battleaxe', 'weapon', false, 2750], ['Dragonbone Bow', 'bow', false, 2725],
  ['Ebony Blade', 'weapon', true, 2000], ['Glass Dagger', 'weapon', false, 165], ['Staff of Magnus', 'staff', true, 1470],
  ['Steel Mace of Burning', 'weapon', true, 245], ['Elven Shield', 'shield', false, 115], ['Dragonplate Armor', 'body', false, 2125],
  ['Daedric Helmet', 'head', false, 1600], ['Nightingale Boots', 'feet', true, 1200], ['Glass Gauntlets', 'hands', false, 190],
  ['Fine Clothes', 'body', false, 70], ['Archmage Robes', 'body', true, 2000], ['Gold Diamond Necklace', 'jewelry', false, 1200],
  ['Hide Boots', 'feet', false, 10], ['Iron Helmet', 'head', false, 60], ['Ebony Mail', 'body', true, 5000],
  ['Blades Shield', 'shield', false, 280], ['Nordic Carved Armor', 'body', false, 1250], ['Fur Hood', 'head', false, 12],
  ['Stalhrim Gauntlets', 'hands', false, 270], ['Imperial Bow', 'bow', false, 90], ['Orcish Warhammer', 'weapon', false, 465],
];
const ITEMS: ItemCard[] = ITEM_NAMES.map(([name, kind, enchanted, value], i) => ({
  formId: (0x00012e40 + i * 7).toString(16).toUpperCase().padStart(8, '0'),
  name, kind, enchanted, value, plugin: i % 5 === 4 ? 'Dragonborn.esm' : 'Skyrim.esm',
}));

const ANIMS: string[] = [];
for (const pack of ['Dance Pack', 'Idle Collection', 'Combat Stances']) {
  for (let i = 1; i <= 14; i++) ANIMS.push(`actors\\character\\animations\\${pack}\\${pack.split(' ')[0].toLowerCase()}_${String(i).padStart(2, '0')}.hkx`);
}
ANIMS.push('actors\\character\\animations\\mt_idle.hkx', 'actors\\character\\animations\\mt_walkforward.hkx',
  'actors\\dragon\\animations\\fly_hover.hkx', 'actors\\horse\\animations\\idle.hkx',
  // A file Phantasm refuses, and one that never gets past the NPC's own clip (the Animations page shows why).
  'actors\\character\\animations\\broken_export.hkx', 'actors\\character\\animations\\stuck_pending.hkx');
ANIMS.sort((a, b) => a.toLowerCase().localeCompare(b.toLowerCase()));

const HUES: Record<string, number> = {};
const hueOf = (key: string) => (HUES[key] ??= [...key].reduce((h, c) => (h * 31 + c.charCodeAt(0)) % 360, 7));

/** A placeholder picture: a lit figure on a floor, turned `turn` degrees; an item draws as a slim blade or a plate. */
function picture(key: string, label: string, opts: { item?: ItemCard; turn?: number; cutout?: boolean; pose?: string; frozen?: boolean } = {}) {
  const hue = hueOf(key + (opts.item?.formId ?? ''));
  const t = ((opts.turn ?? 0) * Math.PI) / 180;
  const sx = Math.max(0.35, Math.abs(Math.cos(t)));
  const bg = opts.cutout ? '' : `<rect width="300" height="400" fill="url(#bg)"/><ellipse cx="150" cy="352" rx="92" ry="16" fill="#000" opacity=".45"/>`;
  const figure = opts.item
    ? WEARABLE.has(opts.item.kind)
      ? `<path d="M100 120 Q150 95 200 120 L190 280 Q150 300 110 280 Z" fill="hsl(${hue} 30% 45%)" stroke="hsl(${hue} 50% 70%)" stroke-width="3"/>`
      : `<rect x="140" y="70" width="20" height="230" rx="4" fill="hsl(${hue} 15% 70%)"/><rect x="112" y="250" width="76" height="12" rx="4" fill="hsl(${hue} 40% 40%)"/>`
    : `<g transform="translate(150 0) scale(${sx.toFixed(3)} 1) translate(-150 0)">
        <circle cx="150" cy="92" r="26" fill="hsl(${hue} 25% 70%)"/>
        <path d="M112 128 Q150 116 188 128 L196 240 L176 244 L170 340 L150 340 L150 250 L150 340 L130 340 L124 244 L104 240 Z" fill="hsl(${hue} 35% 42%)"/>
      </g>`;
  const glow = opts.item?.enchanted ? `<circle cx="150" cy="190" r="110" fill="hsl(265 90% 70%)" opacity=".12"/>` : '';
  const tag = opts.frozen ? `<text x="150" y="388" font-size="11" text-anchor="middle" fill="#9fd" font-family="sans-serif">frozen</text>` : '';
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="300" height="400" viewBox="0 0 300 400">
    <defs><radialGradient id="bg" cx="50%" cy="38%" r="75%"><stop offset="0" stop-color="hsl(${hue} 14% 22%)"/><stop offset="1" stop-color="#0b0c0e"/></radialGradient></defs>
    ${bg}${glow}${figure}
    <text x="150" y="30" font-size="13" text-anchor="middle" fill="#cfd3da" font-family="sans-serif">${label}</text>
    ${opts.pose ? `<text x="150" y="372" font-size="11" text-anchor="middle" fill="#8b90a0" font-family="sans-serif">${opts.pose}</text>` : ''}${tag}
  </svg>`;
  return `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`;
}

// ?old in the URL plays an older Phantasm: no frozen views, so the wardrobe takes its turntable of stills.
const OLD = typeof location !== 'undefined' && location.search.includes('old');
// ?slowfreeze: a frozen view holds only after SLOW_HOLD_MS (a piece still loading); the wardrobe stays live meanwhile.
const SLOW_FREEZE = typeof location !== 'undefined' && location.search.includes('slowfreeze');
const SLOW_HOLD_MS = 12000;

export function createMockBackend(): Transport {
  let listener: (msg: unknown) => void = () => {};
  const push = (p: Push, delay = 0) => window.setTimeout(() => listener(p), delay);
  const views = new Map<Slot, { kind: string; formId: string; hints: Record<string, string>; turn: number; openedAt: number; idleAt: number }>();
  const idleOf: Record<string, string> = {};
  let turnSession = 0;
  // The mock NPC wardrobe: what each wears, and pieces they hold.
  const worn = new Map<string, string[]>();
  const wornOf = (id: string) => worn.get(id) ?? (worn.set(id, [ITEMS[8].formId, ITEMS[16].formId]), worn.get(id)!);
  const original = new Map<string, string[]>();

  const show = (slot: Slot) => {
    const v = views.get(slot);
    if (!v) return;
    const item = v.kind === 'item' ? ITEMS.find((i) => i.formId === v.formId) : undefined;
    const person = PEOPLE.find((p) => p.formId === v.formId);
    const label = item ? item.name : person?.name ?? v.formId;
    const pose = item ? (v.hints.worn && v.hints.worn !== 'off' ? `worn (${v.hints.worn})` : '') : (idleOf[v.formId] ?? 'idle cycle');
    const outfit = !item ? (wornOf(v.formId).map((f) => ITEMS.find((i) => i.formId === f)?.name ?? '').join(', ')) : '';
    const frozen = v.hints.freeze === 'on';
    push({ event: 'view', slot, view: { state: 'shown', formId: v.formId, imageUrl: picture(slot + label + (frozen ? '' : outfit), label, { item, turn: v.turn, pose, frozen }) } }, 250);
  };

  const answer = (r: Request): Answer | null => {
    switch (r.op) {
      case 'hello': {
        const h: HelloData = { version: '1.0.0', runtime: 'SE', menuKey: 'F10', uiScale: 1, freezeWorld: false, freezeAvailable: true, renderer: '', phantasmVersion: OLD ? 400 : 10000, maxDrawing: 4,
          shotsFolder: 'C:\\Users\\you\\Documents\\My Games\\Skyrim Special Edition\\SKSE\\PhantasmShowcase\\Shots', playerFemale: true, target: '00019DF7' };
        return ok(h);
      }
      case 'people': return ok({ people: PEOPLE });
      case 'items': {
        const words = String(r.text ?? '').toLowerCase().split(/\s+/).filter(Boolean);
        const filter = String(r.filter ?? 'all');
        const inFilter = (i: ItemCard) => filter === 'weapons' ? !WEARABLE.has(i.kind) : filter === 'armor' ? WEARABLE.has(i.kind) : filter === 'outfit' ? WEARABLE.has(i.kind) && i.kind !== 'shield' : filter === 'all' || filter === '' ? true : i.kind === filter;
        let list = ITEMS.filter((i) => inFilter(i) && words.every((w) => i.name.toLowerCase().includes(w)));
        if (r.source === 'held') {
          const w = wornOf(String(r.actor));
          list = list.filter((_, n) => n % 3 === 0 || w.includes(_.formId)).map((i) => ({ ...i, count: 1, worn: w.includes(i.formId) }));
        }
        const offset = Number(r.offset ?? 0), limit = Number(r.limit ?? 80);
        return ok({ total: list.length, items: list.slice(offset, offset + limit) });
      }
      case 'viewOpen': {
        const slot = r.slot as Slot;
        const old = views.get(slot);
        const same = old && old.formId === r.formId && old.kind === r.kind;
        const hints = { ...(same ? old!.hints : {}), ...(r.hints as Record<string, string>) };
        views.set(slot, { kind: String(r.kind), formId: String(r.formId), hints, turn: same ? old!.turn : 0, openedAt: same ? old!.openedAt : Date.now(),
          idleAt: same && old!.hints.idle === hints.idle ? old!.idleAt : Date.now() });
        if (hints.idle) idleOf[String(r.formId)] = hints.idle.replace(/^file:.*\\/, '');
        show(slot);
        return ok({ state: 'opening' });
      }
      case 'viewHint': {
        const v = views.get(r.slot as Slot);
        if (v) {
          v.hints[String(r.key)] = String(r.value);
          if (r.key === 'idle') {
            idleOf[v.formId] = String(r.value).replace(/^file:.*\\/, '') || 'idle cycle';
            v.idleAt = Date.now();
          }
          show(r.slot as Slot);
        }
        return ok({});
      }
      case 'viewQuery': {
        const slot = r.slot as Slot;
        const v = views.get(slot);
        if (!v) return ok({});
        if (r.key === 'idles') {
          // A file is read for a moment (pending), then plays, or is refused with Phantasm's reason.
          const choice = v.hints.idle ?? '';
          const stuck = !OLD && /stuck_pending/i.test(choice);
          const reading = !OLD && choice.startsWith('file:') && (stuck || Date.now() - v.idleAt < 700);
          const broken = !OLD && !reading && /broken_export/i.test(choice);
          push({ event: 'reply', slot, key: 'idles', formId: v.formId, json: { formId: 1, choice, available: true,
            plays: v.hints.pose === 'captured' && v.hints.motion === 'on' && !broken,
            reason: broken ? `'meshes\\${choice.slice(5)}' cannot play: it is not a Havok packfile` : stuck ? 'no plain playing clip (a paired scene)' : '',
            ...(reading ? { pending: true } : {}),
            idles: [{ id: 'IdleStudyStart', label: 'Study', loop: true }, { id: 'IdleWarmHandsCrouched', label: 'Warm hands', loop: true },
              { id: 'IdleSalute', label: 'Salute', loop: false }, { id: 'IdleLeanTable', label: 'Lean', loop: true }] } }, 120);
        } else if (r.key === 'freeze') {
          push({ event: 'reply', slot, key: 'freeze', formId: v.formId, json: OLD ? { known: false } : { known: true, freeze: v.hints.freeze === 'on',
            held: v.hints.freeze === 'on' && (!SLOW_FREEZE || Date.now() - v.openedAt > SLOW_HOLD_MS) } }, 300);
        } else if (r.key === 'stats') {
          // A spread like a 4-panel lineup's on a game at 60 real frames: the newest steady, one held, one stalling
          // now and then, one waiting for turns.
          const seed = [...String(slot)].reduce((a, c) => a + c.charCodeAt(0), 0) % 4;
          const drawn = [60, 55, 41, 0][seed], skipped = seed === 3 ? 60 : 0, stalled = seed === 1 ? 5 : 0;
          push({ event: 'reply', slot, key: 'stats', formId: v.formId, json: OLD ? { known: false } : { known: true, drawing: true, valid: true, fps: drawn + skipped,
            drawnFps: drawn, skippedFps: skipped, stalledFps: stalled, waitedFps: 60 - drawn - skipped - stalled, cpuMs: 2.4, budgetMode: 'smooth' } }, 60);
        } else push({ event: 'reply', slot, key: String(r.key), formId: v.formId, json: { known: false } }, 120);
        return ok({});
      }
      case 'viewOrbit': {
        const slots = (r.slots as Slot[] | undefined) ?? [r.slot as Slot];
        for (const s of slots) {
          const v = views.get(s);
          if (v) {
            v.turn += Number(r.dx ?? 0);
            show(s);
          }
        }
        return ok({});
      }
      case 'viewZoom': case 'viewRefresh': return ok({});
      case 'viewClose': views.delete(r.slot as Slot); return ok({});
      case 'setShowFps': return ok({});
      case 'setFreezeWorld': return ok({});
      case 'viewCloseAll': views.clear(); return ok({});
      case 'anims': case 'animSearch': {
        const file = (p: string): AnimFile => ({ name: p.slice(p.lastIndexOf('\\') + 1).replace(/\.hkx$/i, ''), path: p });
        if (r.op === 'animSearch') {
          const words = String(r.text ?? '').toLowerCase().split(/\s+/).filter(Boolean);
          return ok({ state: 'ready', total: ANIMS.length, files: words.length ? ANIMS.filter((p) => words.every((w) => p.toLowerCase().includes(w))).map(file) : [] });
        }
        const folder = String(r.folder ?? '').toLowerCase();
        const prefix = folder ? folder + '\\' : '';
        const folders = new Map<string, number>();
        const files: AnimFile[] = [];
        for (const p of ANIMS) {
          const lp = p.toLowerCase();
          if (!lp.startsWith(prefix)) continue;
          const rest = lp.slice(prefix.length);
          const at = rest.indexOf('\\');
          if (at < 0) files.push(file(p));
          else folders.set(rest.slice(0, at), (folders.get(rest.slice(0, at)) ?? 0) + 1);
        }
        return ok({ state: 'ready', total: ANIMS.length, folder, folders: [...folders].map(([name, count]) => ({ name, count })), files });
      }
      case 'shot': return ok({ file: `C:\\...\\Shots\\${r.framing}_mock.png`, width: 1440, height: 2160 });
      case 'turntable': {
        const session = ++turnSession;
        const frames = Number(r.frames ?? 24);
        const person = PEOPLE.find((p) => p.formId === r.formId);
        for (let i = 0; i < frames; i++) {
          push({ event: 'frame', session, index: i, ok: true, url: picture('tt' + r.formId, person?.name ?? '', { turn: (360 * i) / frames, cutout: true }) }, 80 + i * 40);
        }
        return ok({ session, frames });
      }
      case 'turntableCancel': turnSession++; return ok({});
      case 'tryBegin': {
        const p = PEOPLE.find((x) => x.formId === r.formId);
        if (!p) return fail('refused', 'notLoaded');
        if (p.group === 'player') return ok({ refusal: 'player' });
        if (p.child) return ok({ refusal: 'child' });
        if (!original.has(p.formId)) original.set(p.formId, [...wornOf(p.formId)]);
        return ok({ worn: wornOf(p.formId).map((f) => ITEMS.find((i) => i.formId === f)!) });
      }
      case 'tryWear': {
        const item = ITEMS.find((i) => i.formId === r.item);
        if (!item || !WEARABLE.has(item.kind)) return fail('refused', 'notArmor');
        const w = wornOf(String(r.formId)).filter((f) => ITEMS.find((i) => i.formId === f)?.kind !== item.kind);
        w.push(item.formId);
        worn.set(String(r.formId), w);
        for (const [slot, v] of views) if (v.formId === r.formId && v.kind === 'actor') show(slot);
        return ok({ lent: true, worn: w.map((f) => ITEMS.find((i) => i.formId === f)!) });
      }
      case 'tryRevert': {
        const o = original.get(String(r.formId));
        if (o) worn.set(String(r.formId), [...o]);
        original.delete(String(r.formId));
        for (const [slot, v] of views) if (v.formId === r.formId && v.kind === 'actor') show(slot);
        return ok({ taken: 1, worn: (o ?? []).map((f) => ITEMS.find((i) => i.formId === f)!) });
      }
      default: return fail('badRequest', 'unknown op ' + r.op);
    }
  };

  return {
    send: (req) => {
      const a = answer(req);
      if (a) window.setTimeout(() => listener({ id: req.id, ...a }), 30 + Math.random() * 60);
    },
    listen: (fn) => { listener = fn; },
    onUIMode: () => {},
  };
}

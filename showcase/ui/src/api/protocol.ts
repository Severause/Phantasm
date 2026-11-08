// The page protocol (docs/PROTOCOL.md). Every request is { id, op, ...args }; every reply { id, ok, data } or
// { id, ok: false, error, detail }; pushes carry { event }.

export type ErrorCode = 'badRequest' | 'notFound' | 'refused' | 'busy';

export type Slot =
  | 'lineup0' | 'lineup1' | 'lineup2' | 'lineup3'
  | 'armory0' | 'armory1' | 'armory2' | 'armory3'
  | 'anim' | 'studio' | 'wardrobe' | 'before';

export interface HelloData {
  version: string;
  runtime: 'SE' | 'AE' | 'VR';
  menuKey: string;
  uiScale?: number;
  /** settings.json showFps: each view's frame rate in its corner */
  showFps?: boolean;
  /** settings.json freezeWorld: the game skips its 3D world render behind the open menu */
  freezeWorld?: boolean;
  /** Magelight can freeze the world (0.31.0+, flat, not turned off in Magelight.json) */
  freezeAvailable?: boolean;
  /** '' when views can open; else absent | old | refused */
  renderer: string;
  phantasmVersion: number;
  /** how many views Phantasm draws at once: 4 (2 on VR) from 0.4.0, 1 before */
  maxDrawing: number;
  shotsFolder: string;
  playerFemale?: boolean;
  /** the NPC under the crosshair when the menu opened */
  target?: string;
  /** false when the hello came while the menu is closed (PrismaUI loads the page hidden) */
  open?: boolean;
  /** the UI host the page runs on: on prisma the views are drawn under the page (lib/underlay.ts) */
  host?: 'magelight' | 'prisma' | '';
}

export type Group = 'player' | 'follower' | 'nearby';

export interface Person {
  formId: string;
  name: string;
  group: Group;
  female: boolean;
  child: boolean;
  distance: number;
}

export type ItemKind = 'weapon' | 'bow' | 'staff' | 'head' | 'body' | 'hands' | 'feet' | 'shield' | 'jewelry' | 'other';
export const WEARABLE: ReadonlySet<ItemKind> = new Set(['head', 'body', 'hands', 'feet', 'shield', 'jewelry', 'other']);

export interface ItemCard {
  formId: string;
  name: string;
  plugin: string;
  kind: ItemKind;
  enchanted: boolean;
  value: number;
  count?: number;
  worn?: boolean;
}

export interface ItemPage {
  total: number;
  items: ItemCard[];
}

export interface AnimFile {
  name: string;
  path: string;
}

export interface AnimListing {
  state: 'scanning' | 'ready';
  total?: number;
  folder?: string;
  folders?: { name: string; count: number }[];
  files?: AnimFile[];
}

export type Hints = Record<string, string>;

export type ViewState =
  | { state: 'shown'; formId: string; imageUrl: string; lastSeen?: boolean }
  | { state: 'waiting'; formId: string; reason: string }
  | { state: 'ended'; formId: string; reason: string };

export interface IdleChoice {
  id: string;
  label: string;
  loop: boolean;
}

export interface Ops {
  hello: { args: Record<string, never>; data: HelloData };
  people: { args: Record<string, never>; data: { people: Person[] } };
  items: {
    args: { text?: string; filter?: string; source?: 'all' | 'held'; actor?: string; offset?: number; limit?: number };
    data: ItemPage;
  };
  viewOpen: {
    args: { slot: Slot; kind: 'actor' | 'item'; formId: string; width: number; height: number; scale: number; hints: Hints };
    data: { state: 'opening' | 'unavailable'; reason?: string };
  };
  viewHint: { args: { slot: Slot; key: string; value: string }; data: Record<string, never> };
  viewQuery: { args: { slot: Slot; key: string }; data: Record<string, never> };
  setShowFps: { args: { on: boolean }; data: Record<string, never> };
  setFreezeWorld: { args: { on: boolean }; data: Record<string, never> };
  viewOrbit: { args: { slot?: Slot; slots?: Slot[]; dx: number; dy: number }; data: Record<string, never> };
  viewZoom: { args: { slot: Slot; dz: number; u: number; v: number }; data: Record<string, never> };
  viewRefresh: { args: { slot: Slot }; data: Record<string, never> };
  viewClose: { args: { slot: Slot }; data: Record<string, never> };
  viewCloseAll: { args: Record<string, never>; data: Record<string, never> };
  anims: { args: { folder: string }; data: AnimListing };
  animSearch: { args: { text: string; limit?: number }; data: AnimListing };
  shot: {
    args: { formId: string; framing: 'figure' | 'cutout'; turn?: number; pitch?: number; lighting?: string; weapons?: 'on' | 'off' };
    data: { file: string; width: number; height: number };
  };
  turntable: { args: { formId: string; frames: number; height: number; lighting: string }; data: { session: number; frames: number } };
  turntableCancel: { args: Record<string, never>; data: Record<string, never> };
  tryBegin: { args: { formId: string }; data: { refusal?: string; worn?: ItemCard[] } };
  tryWear: { args: { formId: string; item: string }; data: { lent: boolean; worn: ItemCard[] } };
  tryRevert: { args: { formId: string }; data: { taken: number; worn: ItemCard[] } };
  /** PrismaUI only: close the menu (Escape) */
  close: { args: Record<string, never>; data: Record<string, never> };
  /** PrismaUI only: where the stages are, in the page's device pixels (lib/underlay.ts) */
  stageRects: { args: { pageW: number; pageH: number; boxes: StageBox[] }; data: Record<string, never> };
}

export interface StageBox {
  slot: Slot;
  /** the element's whole rect */
  x: number;
  y: number;
  w: number;
  h: number;
  /** the part of it the page shows */
  cx: number;
  cy: number;
  cw: number;
  ch: number;
  /** corner radius */
  r: number;
}

export type Op = keyof Ops;

export interface Request {
  id: number;
  op: string;
  [key: string]: unknown;
}

export type Reply = { id: number; ok: true; data: unknown } | { id: number; ok: false; error: ErrorCode; detail?: string };

export type Push =
  | { event: 'view'; slot: Slot; view: ViewState }
  | { event: 'reply'; slot: Slot; key: string; formId: string; json: Record<string, unknown> }
  | { event: 'frame'; session: number; index: number; ok: boolean; url?: string; reason?: string }
  | { event: 'anims'; state: 'ready' };

export const LIGHTING = ['studio', 'daylight', 'hearth', 'torchlight', 'moonlight', 'here'] as const;
export const EXPRESSIONS = ['', 'neutral', 'happy', 'sad', 'angry', 'afraid', 'surprised', 'puzzled', 'disgusted'] as const;

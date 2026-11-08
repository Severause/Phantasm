import { bridge, host } from '@magelight/sdk';
import type { ErrorCode, HelloData, Op, Ops, Push, Reply, Request } from './protocol';
import { createMockBackend } from './mock';
import { inPrisma } from '../lib/host';

// Two channels: a DLL listener on a name makes the host define window.<name>, and InteropCall on that name would call
// it, so a reply sent on the request channel would go back to the DLL instead of reaching the page.
const CHANNEL_OUT = 'showcase';
const CHANNEL_IN = 'showcaseEvent';
// Two hellos this close together are one open of the menu (the mount and the replayed UI-mode signal).
const HELLO_DEDUPE_MS = 500;
const TIMEOUT_MS = 15000;
// PROTOCOL.md fixes hello's id at 0; every other request takes 1 and up.
const HELLO_ID = 0;

export type ClientErrorCode = ErrorCode | 'timeout' | 'transport';

const DETAIL_TEXT: Record<string, string> = {
  player: "You can't try clothes on yourself here. Pick an NPC.",
  child: "Children can't be dressed.",
  dead: 'They are dead.',
  notLoaded: "They aren't close enough anymore.",
  noSession: 'Open the wardrobe for them again.',
  notArmor: 'Only armour and clothing can be tried on.',
  stillRefused: 'Phantasm refused the picture. Try again in a moment.',
  'not-drawing': 'The picture needs the live view drawing first.',
  'timed-out': 'The picture took too long.',
  encode: "The picture couldn't be encoded.",
  write: "The picture couldn't be written to disk.",
  loading: 'A game load started.',
};

export function describeError(code: ClientErrorCode, detail?: string): string {
  if (detail && DETAIL_TEXT[detail]) return DETAIL_TEXT[detail];
  const base: Record<ClientErrorCode, string> = {
    badRequest: "The showcase didn't understand that request.",
    notFound: "That isn't in your load order anymore.",
    refused: 'The game refused that.',
    busy: 'The game is busy. Try again in a moment.',
    timeout: "The game didn't answer in time.",
    transport: 'The connection to the game failed.',
  };
  return detail ? `${base[code]} (${detail})` : base[code];
}

export class RequestError extends Error {
  readonly code: ClientErrorCode;
  readonly op: string;
  readonly detail?: string;
  constructor(op: string, code: ClientErrorCode, detail?: string) {
    super(describeError(code, detail));
    this.op = op;
    this.code = code;
    this.detail = detail;
  }
}

export interface Transport {
  send(req: Request): void;
  listen(fn: (msg: unknown) => void): void;
  /** true when the menu gains UI mode (opens), false when it loses it */
  onUIMode(fn: (on: boolean) => void): void;
}

function hostTransport(): Transport {
  return {
    send: (req) => bridge.send(CHANNEL_OUT, req),
    listen: (fn) => void bridge.on<unknown>(CHANNEL_IN, fn),
    onUIMode: (fn) => void bridge.onUIMode(fn),
  };
}

// PrismaUI: the DLL's RegisterJSListener defines window.showcase (page -> DLL, one text argument) and its InteropCall
// calls window.showcaseEvent (DLL -> page). PrismaUI has no UI-mode signal of its own, so the DLL pushes
// { event: 'uiMode', on } when it opens and closes the menu.
interface PrismaGlobals {
  showcase?: (text: string) => void;
  showcaseEvent?: (text: string) => void;
}
const SEND_RETRY_MS = 50;
const SEND_GIVE_UP_MS = 10000;

function prismaTransport(): Transport {
  const g = window as unknown as PrismaGlobals;
  const uiModeFns: ((on: boolean) => void)[] = [];
  let listener: (msg: unknown) => void = () => {};
  // Requests made before the DLL's listener exists (the page loads before it registers) wait for it.
  let queue: string[] = [];
  let since = 0;
  let timer = 0;
  const flush = () => {
    timer = 0;
    if (typeof g.showcase === 'function') {
      const out = queue;
      queue = [];
      for (const text of out) g.showcase(text);
      return;
    }
    if (Date.now() - since > SEND_GIVE_UP_MS) {
      console.error('[showcase] the DLL never listened; dropping', queue.length, 'request(s)');
      queue = [];
      return;
    }
    timer = window.setTimeout(flush, SEND_RETRY_MS);
  };
  g.showcaseEvent = (raw: string) => {
    let msg: unknown;
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    const m = msg as { event?: string; on?: boolean };
    if (m && m.event === 'uiMode') {
      for (const fn of uiModeFns) fn(!!m.on);
      return;
    }
    listener(msg);
  };
  return {
    send: (req) => {
      const text = JSON.stringify(req);
      if (typeof g.showcase === 'function' && queue.length === 0) {
        g.showcase(text);
        return;
      }
      if (queue.length === 0) since = Date.now();
      queue.push(text);
      if (!timer) timer = window.setTimeout(flush, SEND_RETRY_MS);
    },
    listen: (fn) => {
      listener = fn;
    },
    onUIMode: (fn) => {
      uiModeFns.push(fn);
    },
  };
}

type Pending = { op: string; resolve: (d: unknown) => void; reject: (e: RequestError) => void; timer: number };

export class Client {
  readonly mock: boolean;
  private readonly transport: Transport;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();
  private readonly helloWaiters: Pending[] = [];
  private readonly pushSubs = new Set<(p: Push) => void>();
  private readonly helloSubs = new Set<(h: HelloData) => void>();
  private readonly closeSubs = new Set<() => void>();
  private lastHelloSentAt = 0;

  constructor(transport: Transport, mock: boolean) {
    this.transport = transport;
    this.mock = mock;
    transport.listen((msg) => this.receive(msg));
    transport.onUIMode((on) => {
      if (on) this.hello().catch(() => {});
      else for (const fn of this.closeSubs) fn();
    });
  }

  request<O extends Op>(op: O, args: Ops[O]['args']): Promise<Ops[O]['data']> {
    const id = this.nextId++;
    return new Promise<Ops[O]['data']>((resolve, reject) => {
      const timer = window.setTimeout(() => this.settle(id, new RequestError(op, 'timeout')), TIMEOUT_MS);
      this.pending.set(id, { op, resolve: resolve as (d: unknown) => void, reject, timer });
      this.post({ ...args, id, op }, id);
    });
  }

  /** A request whose answer nobody waits for (a camera move): failures are dropped. */
  fire<O extends Op>(op: O, args: Ops[O]['args']): void {
    this.request(op, args).catch(() => {});
  }

  hello(): Promise<HelloData> {
    return new Promise<HelloData>((resolve, reject) => {
      const waiter: Pending = {
        op: 'hello',
        resolve: resolve as (d: unknown) => void,
        reject,
        timer: window.setTimeout(() => {
          const i = this.helloWaiters.indexOf(waiter);
          if (i >= 0) this.helloWaiters.splice(i, 1);
          reject(new RequestError('hello', 'timeout'));
        }, TIMEOUT_MS),
      };
      const sendNow = this.helloWaiters.length === 0 || Date.now() - this.lastHelloSentAt > HELLO_DEDUPE_MS;
      this.helloWaiters.push(waiter);
      if (sendNow) {
        this.lastHelloSentAt = Date.now();
        this.post({ id: HELLO_ID, op: 'hello' }, HELLO_ID);
      }
    });
  }

  onPush(fn: (p: Push) => void): () => void {
    this.pushSubs.add(fn);
    return () => this.pushSubs.delete(fn);
  }

  /** Every hello reply, including the ones sent because the menu reopened. */
  onHello(fn: (h: HelloData) => void): () => void {
    this.helloSubs.add(fn);
    return () => this.helloSubs.delete(fn);
  }

  /** The menu lost UI mode (it closed). */
  onClose(fn: () => void): () => void {
    this.closeSubs.add(fn);
    return () => this.closeSubs.delete(fn);
  }

  private post(req: Request, id: number) {
    try {
      this.transport.send(req);
    } catch (e) {
      const err = new RequestError(req.op, 'transport', String(e));
      if (id === HELLO_ID) this.failHello(err);
      else this.settle(id, err);
    }
  }

  private receive(msg: unknown) {
    if (!msg || typeof msg !== 'object') return;
    const m = msg as Partial<Reply> & Partial<Push>;
    if (typeof m.event === 'string') {
      for (const fn of this.pushSubs) {
        try {
          fn(m as Push);
        } catch (e) {
          console.error('[showcase] push handler threw', e);
        }
      }
      return;
    }
    if (typeof m.id !== 'number') return;
    const reply = m as Reply;
    if (reply.id === HELLO_ID) {
      if (reply.ok) {
        const data = reply.data as HelloData;
        for (const w of this.helloWaiters.splice(0)) {
          clearTimeout(w.timer);
          w.resolve(data);
        }
        for (const fn of this.helloSubs) fn(data);
      } else {
        this.failHello(new RequestError('hello', reply.error, reply.detail));
      }
      return;
    }
    this.settle(
      reply.id,
      reply.ok ? undefined : new RequestError(this.pending.get(reply.id)?.op ?? '?', reply.error, reply.detail),
      reply.ok ? reply.data : undefined,
    );
  }

  private failHello(err: RequestError) {
    for (const w of this.helloWaiters.splice(0)) {
      clearTimeout(w.timer);
      w.reject(err);
    }
  }

  private settle(id: number, err: RequestError | undefined, data?: unknown) {
    const p = this.pending.get(id);
    if (!p) return; // a late reply after its timeout
    this.pending.delete(id);
    clearTimeout(p.timer);
    if (err) p.reject(err);
    else p.resolve(data);
  }
}

// Decided before anything touches the SDK's bridge: in a plain browser the SDK installs its own mock host, after
// which host.present reads true.
const inGame = host.present;

export const client = new Client(
  inGame ? hostTransport() : inPrisma ? prismaTransport() : createMockBackend(),
  !inGame && !inPrisma,
);

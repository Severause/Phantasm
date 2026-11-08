import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import { client, type RequestError } from '../api/client';
import type { HelloData, Person } from '../api/protocol';
import { inPrisma } from '../lib/host';
import { setUserScale } from '../lib/scale';

export type Tab = 'lineup' | 'armory' | 'anims' | 'studio' | 'wardrobe';

interface Toast {
  id: number;
  text: string;
  tone: 'info' | 'error';
}

interface AppState {
  hello: HelloData | null;
  /** bumped on every open of the menu: views open again */
  openCount: number;
  people: Person[];
  refreshPeople: () => void;
  tab: Tab;
  setTab: (t: Tab) => void;
  clean: boolean;
  setClean: (on: boolean) => void;
  /** each view's frame rate in its corner (settings.json showFps; the docks' FPS switch) */
  showFps: boolean;
  setShowFps: (on: boolean) => void;
  /** the game skips its 3D world render behind the menu (settings.json freezeWorld; the docks' switch) */
  freezeWorld: boolean;
  setFreezeWorld: (on: boolean) => void;
  /** how many views draw at once: what the panels lay out for */
  maxDrawing: number;
  toast: (text: string, tone?: Toast['tone']) => void;
  toasts: Toast[];
  fail: (e: unknown) => void;
}

const Ctx = createContext<AppState | null>(null);

export function useApp(): AppState {
  const v = useContext(Ctx);
  if (!v) throw new Error('useApp outside AppProvider');
  return v;
}

let nextToast = 1;

export function AppProvider({ children }: { children: ReactNode }) {
  const [hello, setHello] = useState<HelloData | null>(null);
  const [openCount, setOpenCount] = useState(0);
  const [people, setPeople] = useState<Person[]>([]);
  const [tab, setTab] = useState<Tab>('lineup');
  const [clean, setClean] = useState(false);
  const [showFps, setShowFpsState] = useState(true);
  const setShowFps = useCallback((on: boolean) => {
    setShowFpsState(on);
    client.fire('setShowFps', { on });
  }, []);
  const [freezeWorld, setFreezeWorldState] = useState(false);
  const setFreezeWorld = useCallback((on: boolean) => {
    setFreezeWorldState(on);
    client.fire('setFreezeWorld', { on });
  }, []);
  const [toasts, setToasts] = useState<Toast[]>([]);

  const toast = useCallback((text: string, tone: Toast['tone'] = 'info') => {
    const id = nextToast++;
    setToasts((t) => [...t.slice(-3), { id, text, tone }]);
    window.setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 4200);
  }, []);
  const fail = useCallback((e: unknown) => toast((e as RequestError)?.message ?? String(e), 'error'), [toast]);

  const refreshPeople = useCallback(() => {
    client.request('people', {}).then((r) => setPeople(r.people), () => {});
  }, []);

  useEffect(() => {
    const off = client.onHello((h) => {
      setHello(h);
      setUserScale(h.uiScale);
      setShowFpsState(h.showFps ?? true);
      setFreezeWorldState(h.freezeWorld ?? false);
      setOpenCount((n) => n + 1);
      refreshPeople();
    });
    client.hello().catch(() => {});
    const offClose = client.onClose(() => setClean(false));
    return () => {
      off();
      offClose();
    };
  }, [refreshPeople]);

  // H hides every control for recording; a second H brings them back. Typing in a field never toggles it.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      // Magelight closes the menu on Escape itself; PrismaUI leaves it to the page, from any field.
      if (inPrisma && (e.key === 'Escape' || e.keyCode === 27)) {
        client.fire('close', {});
        return;
      }
      const el = e.target as HTMLElement | null;
      if (el && (el.tagName === 'INPUT' || el.tagName === 'SELECT' || el.tagName === 'TEXTAREA')) return;
      if (e.key === 'h' || e.key === 'H' || e.keyCode === 72) setClean((c) => !c);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const maxDrawing = Math.max(1, hello?.maxDrawing ?? 4);
  const value = useMemo<AppState>(
    () => ({ hello, openCount, people, refreshPeople, tab, setTab, clean, setClean, showFps, setShowFps, freezeWorld, setFreezeWorld, maxDrawing, toast, toasts, fail }),
    [hello, openCount, people, refreshPeople, tab, clean, showFps, setShowFps, freezeWorld, setFreezeWorld, maxDrawing, toast, toasts, fail],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

import { BrandMark } from './components/BrandMark';
import { Animations } from './pages/Animations';
import { Armory } from './pages/Armory';
import { Lineup } from './pages/Lineup';
import { Studio } from './pages/Studio';
import { Wardrobe } from './pages/Wardrobe';
import { AppProvider, useApp, type Tab } from './state/app';
import { viewNote } from './lib/labels';

const TABS: { id: Tab; label: string }[] = [
  { id: 'lineup', label: 'Lineup' },
  { id: 'armory', label: 'Armory' },
  { id: 'anims', label: 'Animations' },
  { id: 'studio', label: 'Studio' },
  { id: 'wardrobe', label: 'Wardrobe' },
];

function Shell() {
  const { hello, tab, setTab, clean, setClean, toasts, maxDrawing } = useApp();
  const version = hello?.phantasmVersion ?? 0;
  const pv = version ? `Phantasm ${Math.floor(version / 10000)}.${Math.floor(version / 100) % 100}.${version % 100}` : '';
  return (
    <div className={`app${clean ? ' is-clean' : ''}`}>
      {!clean && (
        <header className="top">
          <div className="brand">
            <BrandMark className="brand-mark" />
            <span className="brand-name">Phantasm<b>Showcase</b></span>
          </div>
          <nav className="tabs" role="tablist">
            {TABS.map((t) => (
              <button key={t.id} type="button" role="tab" aria-selected={tab === t.id} className={`tab${tab === t.id ? ' is-on' : ''}`} onClick={() => setTab(t.id)}>
                {t.label}
              </button>
            ))}
          </nav>
          <div className="top-right">
            {pv && <span className="chip">{pv} · {maxDrawing} live</span>}
            <button type="button" className="btn ghost" onClick={() => setClean(true)} title="Hide every control (H)">Clean view <kbd>H</kbd></button>
          </div>
        </header>
      )}
      {hello?.renderer && !clean && <div className="banner">{viewNote(hello.renderer)}</div>}
      <main className="body">
        {tab === 'lineup' && <Lineup />}
        {tab === 'armory' && <Armory />}
        {tab === 'anims' && <Animations />}
        {tab === 'studio' && <Studio />}
        {tab === 'wardrobe' && <Wardrobe />}
      </main>
      {clean && <div className="clean-hint">H shows the controls</div>}
      <div className="toasts">
        {toasts.map((t) => <div key={t.id} className={`toast ${t.tone}`}>{t.text}</div>)}
      </div>
    </div>
  );
}

export function App() {
  return (
    <AppProvider>
      <Shell />
    </AppProvider>
  );
}

import { useCallback, useEffect, useMemo, useState } from 'react';
import { client } from '../api/client';
import type { AnimFile, AnimListing } from '../api/protocol';
import { FpsToggle, ActorPicker, Dropdown, LIGHT_OPTIONS, Segmented, Toggle, idleOptions, useIdles } from '../components/Controls';
import { LiveView } from '../components/LiveView';
import { useApp } from '../state/app';

// Where the browser opens: the character animations, which is where animation packs install.
const START_FOLDER = 'actors\\character\\animations';
// After a pick, the idles answer is asked again until Phantasm has tried it (it reads a file on its next frames).
const RECHECK_MS = 400;
const RECHECK_TRIES = 25;

type Source = 'files' | 'idles';

export function Animations() {
  const { people, hello, clean } = useApp();
  const [actor, setActor] = useState('');
  const [source, setSource] = useState<Source>('files');
  const [folder, setFolder] = useState('');
  const [listing, setListing] = useState<AnimListing | null>(null);
  const [search, setSearch] = useState('');
  const [found, setFound] = useState<AnimFile[] | null>(null);
  const [playing, setPlaying] = useState<AnimFile | null>(null);
  const [idle, setIdle] = useState('cycle');
  const [light, setLight] = useState('studio');
  const [floor, setFloor] = useState(true);
  const [shown, setShown] = useState(false);
  const { idles, answer, ask } = useIdles('anim', actor || undefined, shown);

  useEffect(() => {
    if (!actor && people.length) setActor(people.find((p) => p.formId === hello?.target)?.formId ?? people[0].formId);
  }, [people, actor, hello?.target]);

  // Opens a folder, passing through any that hold no animation of their own and a single folder (meshes > actors >
  // character > animations), and falls back to the top when the start folder does not exist.
  const load = useCallback((f: string, descend = true, fallback = false) => {
    setFolder(f);
    client.request('anims', { folder: f }).then((r) => {
      if (r.state === 'ready' && fallback && !r.files?.length && !r.folders?.length) {
        load('', true, false);
        return;
      }
      setListing(r);
      if (r.state !== 'ready') return;
      const at = r.folder ?? f;
      setFolder(at);
      if (descend && !r.files?.length && r.folders?.length === 1) load(at ? `${at}\\${r.folders[0].name}` : r.folders[0].name, true, false);
    }, () => {});
  }, []);

  useEffect(() => load(START_FOLDER, true, true), [load]);
  // The first list comes while the folders are still being walked; the DLL says when they are ready.
  useEffect(() => client.onPush((p) => p.event === 'anims' && load(folder || START_FOLDER, true, true)), [load, folder]);

  useEffect(() => {
    const words = search.trim();
    if (!words) {
      setFound(null);
      return;
    }
    const t = window.setTimeout(() => client.request('animSearch', { text: words, limit: 300 }).then((r) => setFound(r.files ?? []), () => {}), 160);
    return () => window.clearTimeout(t);
  }, [search]);

  const files = found ?? listing?.files ?? [];
  const step = (by: number) => {
    if (!files.length) return;
    const at = playing ? files.findIndex((f) => f.path === playing.path) : -1;
    setPlaying(files[(at + by + files.length) % files.length]);
  };

  const idleHint = source === 'files' ? (playing ? `file:${playing.path}` : '') : idle === 'cycle' ? '' : idle;
  const crumbs = useMemo(() => (folder ? folder.split('\\') : []), [folder]);

  // Phantasm's word on the pick: its answer names the choice in force, so one about an earlier pick, or a pending one
  // (the file still being read, a try that failed before it), is asked again. A view that draws but cannot play the
  // pick holds the last pose, so the reason is shown over it; one still pending when the asks run out shows its why.
  const [gaveUp, setGaveUp] = useState('');
  const pick = `${actor}|${idleHint}`;
  const sameChoice = !!answer && !!idleHint && answer.choice.toLowerCase() === idleHint.toLowerCase();
  const answered = sameChoice && !answer!.pending;
  useEffect(() => {
    if (!shown || !idleHint || answered) return;
    let tries = 0;
    const t = window.setInterval(() => {
      if (++tries <= RECHECK_TRIES) {
        ask();
        return;
      }
      window.clearInterval(t);
      setGaveUp(pick);
    }, RECHECK_MS);
    return () => window.clearInterval(t);
  }, [shown, idleHint, pick, answered, ask]);
  // A file's reason starts with its path, which the nameplate shows already.
  const refusal =
    shown && answered && !answer!.plays ? (answer!.reason || 'Phantasm cannot play this one.').replace(/^'.*' cannot play: /, "Can't play: ")
    : shown && sameChoice && answer!.pending && answer!.reason && gaveUp === pick ? `Not playing yet: ${answer!.reason}`
    : '';

  return (
    <div className="page anims">
      {!clean && (
        <aside className="side">
          <ActorPicker value={actor} onChange={(a) => setActor(a)} />
          <Segmented value={source} onChange={setSource} options={[{ value: 'files', label: 'Animation files' }, { value: 'idles', label: 'Their idles' }]} />
          {source === 'files' ? (
            <>
              <div className="search">
                <svg viewBox="0 0 16 16" className="search-icon" aria-hidden><circle cx="7" cy="7" r="5" fill="none" stroke="currentColor" strokeWidth="1.6" /><path d="M11 11l3.5 3.5" stroke="currentColor" strokeWidth="1.6" /></svg>
                <input value={search} placeholder="Search every animation" onChange={(e) => setSearch(e.target.value)} spellCheck={false} />
              </div>
              {!found && (
                <div className="crumbs">
                  <button type="button" onClick={() => load('', false)}>meshes</button>
                  {crumbs.map((c, i) => (
                    <button type="button" key={i} onClick={() => load(crumbs.slice(0, i + 1).join('\\'), false)}>{c}</button>
                  ))}
                </div>
              )}
              <div className="anim-list">
                {listing?.state === 'scanning' && <div className="items-empty"><span className="spinner" />Finding animations…</div>}
                {!found && listing?.folders?.map((f) => (
                  <button type="button" key={f.name} className="anim-row folder" onClick={() => load(folder ? `${folder}\\${f.name}` : f.name)}>
                    <span className="anim-icon">▸</span><span className="item-name">{f.name}</span><span className="item-kind">{f.count}</span>
                  </button>
                ))}
                {files.map((f) => (
                  <button type="button" key={f.path} className={`anim-row${playing?.path === f.path ? ' is-on' : ''}`} onClick={() => setPlaying(f)} title={f.path}>
                    <span className="anim-icon">♪</span><span className="item-name">{f.name}</span>
                  </button>
                ))}
                {listing?.state === 'ready' && files.length === 0 && !listing.folders?.length && <div className="items-empty">No animations here.</div>}
              </div>
              <p className="fine">Lists loose .hkx files under Data/meshes/actors. Animations packed in a BSA are not listed. {listing?.total ? `${listing.total.toLocaleString('en-US')} found.` : ''}</p>
            </>
          ) : (
            <div className="anim-list">
              {idleOptions(idles).map((o) => (
                <button type="button" key={o.value} className={`anim-row${idle === o.value ? ' is-on' : ''}`} onClick={() => setIdle(o.value)}>
                  <span className="anim-icon">♪</span><span className="item-name">{o.label}</span>{o.hint && <span className="item-kind">{o.hint}</span>}
                </button>
              ))}
            </div>
          )}
        </aside>
      )}
      <div className="stage-wrap">
        <LiveView slot="anim" className="panel stage" target={actor ? { kind: 'actor', formId: actor } : null} onShown={setShown}
          hints={{ pose: 'captured', motion: 'on', idle: idleHint, lighting: light, floor: floor ? 'on' : 'off', weapons: 'off' }}
          empty={<span>Choose someone</span>}>
          {source === 'files' && playing && <div className="nameplate">{playing.name}<small>{playing.path}</small></div>}
          {refusal && <div className="live-note at-top">{refusal}</div>}
        </LiveView>
        {!clean && (
          <div className="dock">
            <button type="button" className="btn" onClick={() => step(-1)} disabled={!files.length}>◀ Previous</button>
            <button type="button" className="btn" onClick={() => step(1)} disabled={!files.length}>Next ▶</button>
            <button type="button" className="btn ghost" onClick={() => setPlaying(null)} disabled={!playing}>Stop</button>
            <div className="dock-group">
              <span className="dock-label">Light</span>
              <Dropdown value={light} options={LIGHT_OPTIONS} onChange={setLight} />
            </div>
            <Toggle on={floor} onChange={setFloor} label="Floor" />
            <FpsToggle />
            <span className="dock-note">A file plays from its start and loops.</span>
          </div>
        )}
      </div>
    </div>
  );
}

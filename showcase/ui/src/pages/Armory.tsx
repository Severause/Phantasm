import { useEffect, useRef, useState } from 'react';
import { client } from '../api/client';
import { WEARABLE, type ItemCard, type Slot } from '../api/protocol';
import { FpsToggle, Segmented, Toggle } from '../components/Controls';
import { ItemList } from '../components/ItemList';
import { LiveView } from '../components/LiveView';
import { KIND_LABEL } from '../lib/labels';
import { useApp } from '../state/app';

const SLOTS: Slot[] = ['armory0', 'armory1', 'armory2', 'armory3'];
// The turntable steps every 33 ms; a speed is degrees a second.
const TURN_STEP_MS = 33;

const FILTERS = [
  { value: 'all', label: 'All' },
  { value: 'weapons', label: 'Weapons' },
  { value: 'armor', label: 'Armour' },
  { value: 'shield', label: 'Shields' },
  { value: 'jewelry', label: 'Jewellery' },
];

export function Armory() {
  const { hello, maxDrawing, clean } = useApp();
  const count = Math.min(4, maxDrawing);
  const slots = SLOTS.slice(0, count);
  const [items, setItems] = useState<(ItemCard | null)[]>([null, null, null, null]);
  const [selected, setSelected] = useState(0);
  const [worn, setWorn] = useState<'male' | 'female' | 'off'>(hello?.playerFemale ? 'female' : 'male');
  const [spin, setSpin] = useState(true);
  const [speed, setSpeed] = useState('30');
  const [shown, setShown] = useState<boolean[]>([false, false, false, false]);
  const shownRef = useRef(shown);
  shownRef.current = shown;

  // One orbit request a step for every drawing panel: cheap, and the panels turn together.
  useEffect(() => {
    if (!spin) return;
    const dx = (Number(speed) * TURN_STEP_MS) / 1000;
    const t = window.setInterval(() => {
      const live = slots.filter((_, i) => shownRef.current[i]);
      if (live.length) client.fire('viewOrbit', { slots: live, dx, dy: 0 });
    }, TURN_STEP_MS);
    return () => window.clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [spin, speed, count]);

  const pick = (item: ItemCard) => {
    setItems((old) => old.map((x, i) => (i === selected ? item : x)));
    setSelected((s) => {
      const next = items.findIndex((x, i) => i > s && !x);
      return next >= 0 && next < count ? next : s;
    });
  };

  return (
    <div className="page armory">
      {!clean && (
        <aside className="side">
          <div className="side-title">Pick for panel {selected + 1}</div>
          <ItemList filters={FILTERS} defaultFilter="all" holder="00000014" holderLabel="Your pack" onPick={pick}
            picked={new Set(items.filter(Boolean).map((i) => i!.formId))} />
        </aside>
      )}
      <div className="armory-main">
        <div className={`armory-grid n${count}`}>
          {slots.map((slot, i) => {
            const item = items[i];
            const wearable = item && WEARABLE.has(item.kind);
            return (
              <div key={slot} className={`armory-cell${selected === i && !clean ? ' is-selected' : ''}`} onMouseDown={() => setSelected(i)}>
                <LiveView slot={slot} className="panel" target={item ? { kind: 'item', formId: item.formId } : null}
                  hints={{ worn: wearable ? worn : 'off' }}
                  onShown={(s) => setShown((old) => (old[i] === s ? old : old.map((x, j) => (j === i ? s : x))))}
                  empty={<span>{clean ? '' : 'Pick an item from the list'}</span>}>
                  {item && (
                    <div className="nameplate">
                      {item.name}
                      <small>{KIND_LABEL[item.kind]}{item.enchanted ? ' · enchanted' : ''}</small>
                    </div>
                  )}
                </LiveView>
              </div>
            );
          })}
        </div>
        {!clean && (
          <div className="dock">
            <Toggle on={spin} onChange={setSpin} label="Turntable" />
            <FpsToggle />
            <div className="dock-group">
              <span className="dock-label">Speed</span>
              <Segmented value={speed} onChange={setSpeed} options={[{ value: '15', label: 'Slow' }, { value: '30', label: 'Medium' }, { value: '60', label: 'Fast' }]} />
            </div>
            <div className="dock-group">
              <span className="dock-label">Armour as</span>
              <Segmented value={worn} onChange={setWorn} options={[{ value: 'male', label: 'Worn (man)' }, { value: 'female', label: 'Worn (woman)' }, { value: 'off', label: 'On the ground' }]} />
            </div>
            <button type="button" className="btn ghost" onClick={() => setItems([null, null, null, null])}>Clear</button>
          </div>
        )}
      </div>
    </div>
  );
}

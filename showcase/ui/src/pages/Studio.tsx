import { useEffect, useState } from 'react';
import { client } from '../api/client';
import { FpsToggle, ActorPicker, Dropdown, EXPRESSION_OPTIONS, Field, LIGHT_OPTIONS, Segmented, Toggle, idleOptions, useIdles } from '../components/Controls';
import { LiveView } from '../components/LiveView';
import { useApp } from '../state/app';

const TURNS = [
  { value: '0', label: 'Front' },
  { value: '30', label: '¾ left' },
  { value: '-30', label: '¾ right' },
  { value: '90', label: 'Side' },
  { value: '180', label: 'Back' },
];

export function Studio() {
  const { people, hello, clean, toast, fail } = useApp();
  const [actor, setActor] = useState('');
  const [idle, setIdle] = useState('cycle');
  const [expression, setExpression] = useState('game');
  const [light, setLight] = useState('studio');
  const [floor, setFloor] = useState(true);
  const [weapons, setWeapons] = useState(true);
  const [turn, setTurn] = useState('30');
  const [pitch, setPitch] = useState('5');
  const [saving, setSaving] = useState(false);
  const [shown, setShown] = useState(false);
  const { idles } = useIdles('studio', actor || undefined, shown);

  useEffect(() => {
    if (!actor && people.length) setActor(people.find((p) => p.formId === hello?.target)?.formId ?? people[0].formId);
  }, [people, actor, hello?.target]);

  const save = (framing: 'figure' | 'cutout') => {
    if (!actor) return;
    setSaving(true);
    client
      .request('shot', { formId: actor, framing, turn: Number(turn), pitch: Number(pitch), lighting: light === 'here' ? 'hearth' : light, weapons: weapons ? 'on' : 'off' })
      .then((r) => toast(`Saved ${r.width}×${r.height}: ${r.file}`), fail)
      .finally(() => setSaving(false));
  };

  return (
    <div className="page studio">
      <LiveView slot="studio" className="panel stage" target={actor ? { kind: 'actor', formId: actor } : null} onShown={setShown}
        hints={{ pose: 'captured', motion: 'on', idle: idle === 'cycle' ? '' : idle, expression: expression === 'game' ? '' : expression, lighting: light, floor: floor ? 'on' : 'off', weapons: weapons ? 'on' : 'off' }}
        empty={<span>Choose someone</span>} />
      {!clean && (
        <aside className="inspector">
          <div className="side-title">Studio</div>
          <Field label="Subject"><ActorPicker value={actor} onChange={(a) => { setActor(a); setIdle('cycle'); }} /></Field>
          <Field label="Pose"><Dropdown value={idle} options={idleOptions(idles)} onChange={setIdle} /></Field>
          <Field label="Expression"><Dropdown value={expression} options={EXPRESSION_OPTIONS} onChange={setExpression} /></Field>
          <Field label="Light"><Dropdown value={light} options={LIGHT_OPTIONS} onChange={setLight} /></Field>
          <div className="toggles">
            <Toggle on={floor} onChange={setFloor} label="Floor" />
            <Toggle on={weapons} onChange={setWeapons} label="Weapons" />
            <FpsToggle />
          </div>
          <p className="fine">Drag the picture to turn the camera, scroll to zoom.</p>
          <div className="side-title">Save a picture</div>
          <button type="button" className="btn primary" disabled={!shown || saving} onClick={() => save('figure')}>Save what you see</button>
          <Field label="Cutout angle"><Segmented className="seg-wrap" value={turn} onChange={setTurn} options={TURNS} /></Field>
          <Field label="Camera height"><Segmented value={pitch} onChange={setPitch} options={[{ value: '-10', label: 'Low' }, { value: '5', label: 'Level' }, { value: '25', label: 'High' }]} /></Field>
          <button type="button" className="btn" disabled={!actor || saving} onClick={() => save('cutout')}>Save a transparent cutout</button>
          <p className="fine">Pictures go to {hello?.shotsFolder ?? 'SKSE/PhantasmShowcase/Shots'}.</p>
        </aside>
      )}
    </div>
  );
}

import { useEffect, useState } from 'react';
import type { Slot } from '../api/protocol';
import { FpsToggle, ActorPicker, Dropdown, EXPRESSION_OPTIONS, LIGHT_OPTIONS, Segmented, Toggle, idleOptions, useIdles } from '../components/Controls';
import { LiveView } from '../components/LiveView';
import { useApp } from '../state/app';

const SLOTS: Slot[] = ['lineup0', 'lineup1', 'lineup2', 'lineup3'];

interface PanelState {
  formId: string;
  idle: string;
  expression: string;
}

function Panel({ slot, panel, onChange, light, floor, weapons }: {
  slot: Slot;
  panel: PanelState;
  onChange: (p: PanelState) => void;
  light: string;
  floor: boolean;
  weapons: boolean;
}) {
  const { people, clean } = useApp();
  const [shown, setShown] = useState(false);
  const { idles, plays, answer } = useIdles(slot, panel.formId || undefined, shown);
  const name = people.find((p) => p.formId === panel.formId)?.name;
  return (
    <LiveView
      slot={slot}
      className="panel"
      target={panel.formId ? { kind: 'actor', formId: panel.formId } : null}
      onShown={setShown}
      hints={{
        pose: 'captured',
        motion: 'on',
        floor: floor ? 'on' : 'off',
        lighting: light,
        weapons: weapons ? 'on' : 'off',
        idle: panel.idle === 'cycle' ? '' : panel.idle,
        expression: panel.expression === 'game' ? '' : panel.expression,
      }}
      empty={<span>Choose someone to stand here</span>}
    >
      {!clean && (
        <div className="panel-tools">
          <ActorPicker value={panel.formId} onChange={(formId) => onChange({ ...panel, formId, idle: 'cycle' })} />
          {panel.formId && (
            <div className="panel-row">
              <Dropdown value={panel.idle} options={idleOptions(idles)} onChange={(idle) => onChange({ ...panel, idle })} />
              <Dropdown value={panel.expression} options={EXPRESSION_OPTIONS} onChange={(expression) => onChange({ ...panel, expression })} />
            </div>
          )}
          {panel.formId && !plays && (
            <div className="panel-warn">{answer?.reason ? `Their idle does not play: ${answer.reason}.` : 'Their idle plays once the view follows them.'}</div>
          )}
        </div>
      )}
      {name && <div className="nameplate">{name}</div>}
    </LiveView>
  );
}

export function Lineup() {
  const { hello, people, maxDrawing, clean } = useApp();
  const [count, setCount] = useState(Math.min(4, maxDrawing));
  const [light, setLight] = useState('studio');
  const [floor, setFloor] = useState(true);
  const [weapons, setWeapons] = useState(false);
  const [panels, setPanels] = useState<PanelState[]>(SLOTS.map(() => ({ formId: '', idle: 'cycle', expression: 'game' })));

  useEffect(() => setCount((c) => Math.min(c, maxDrawing)), [maxDrawing]);

  // First fill: the player, the followers, then whoever was under the crosshair.
  useEffect(() => {
    if (!people.length || panels.some((p) => p.formId)) return;
    const order = [...people.filter((p) => p.group !== 'nearby'), ...people.filter((p) => p.formId === hello?.target)];
    setPanels((old) => old.map((p, i) => (order[i] ? { ...p, formId: order[i].formId } : p)));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [people, hello?.target]);

  const counts = [1, 2, 3, 4].filter((n) => n <= maxDrawing).map((n) => ({ value: String(n), label: String(n) }));
  return (
    <div className="page lineup">
      <div className={`lineup-grid n${count}`}>
        {SLOTS.slice(0, count).map((slot, i) => (
          <Panel key={slot} slot={slot} panel={panels[i]} light={light} floor={floor} weapons={weapons}
            onChange={(p) => setPanels((old) => old.map((x, j) => (j === i ? p : x)))} />
        ))}
      </div>
      {!clean && (
        <div className="dock">
          <div className="dock-group">
            <span className="dock-label">Panels</span>
            <Segmented value={String(count)} options={counts} onChange={(v) => setCount(Number(v))} />
          </div>
          <div className="dock-group">
            <span className="dock-label">Light</span>
            <Segmented value={light} options={LIGHT_OPTIONS} onChange={setLight} />
          </div>
          <Toggle on={floor} onChange={setFloor} label="Floor" />
          <Toggle on={weapons} onChange={setWeapons} label="Weapons" />
          <FpsToggle />
          {maxDrawing < 4 && (
            <span className="dock-note">
              {maxDrawing === 1 ? 'This Phantasm draws one view at a time: 0.4.0 draws four.' : `Phantasm draws ${maxDrawing} views at once here.`}
            </span>
          )}
        </div>
      )}
    </div>
  );
}

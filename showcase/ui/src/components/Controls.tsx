import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import { client } from '../api/client';
import type { IdleChoice, Person, Slot } from '../api/protocol';
import { EXPRESSIONS, LIGHTING } from '../api/protocol';
import { EXPRESSION_LABEL, LIGHT_LABEL } from '../lib/labels';
import { useApp } from '../state/app';

export interface Option<T extends string> {
  value: T;
  label: string;
  group?: string;
  hint?: string;
}

/** A drop-down list drawn by the page (the host's native select is not reliable in every UI host). */
export function Dropdown<T extends string>({ value, options, onChange, placeholder, className, disabled }: {
  value: T | '';
  options: Option<T>[];
  onChange: (v: T) => void;
  placeholder?: string;
  className?: string;
  disabled?: boolean;
}) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false);
    };
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [open]);
  const current = options.find((o) => o.value === value);
  let lastGroup: string | undefined;
  return (
    <div ref={ref} className={`dd no-drag${open ? ' is-open' : ''}${className ? ' ' + className : ''}`}>
      <button type="button" className="dd-btn" disabled={disabled} onClick={() => setOpen((o) => !o)}>
        <span className="dd-text">{current?.label ?? placeholder ?? 'Choose'}</span>
        <svg className="dd-chev" viewBox="0 0 10 6" aria-hidden><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" strokeWidth="1.6" /></svg>
      </button>
      {open && (
        <div className="dd-panel" role="listbox">
          {options.map((o) => {
            const head = o.group && o.group !== lastGroup ? <div className="dd-group" key={'g' + o.group}>{o.group}</div> : null;
            lastGroup = o.group;
            return [
              head,
              <button type="button" key={o.value} role="option" aria-selected={o.value === value}
                className={`dd-opt${o.value === value ? ' is-on' : ''}`}
                onClick={() => {
                  onChange(o.value);
                  setOpen(false);
                }}>
                <span>{o.label}</span>
                {o.hint && <span className="dd-hint">{o.hint}</span>}
              </button>,
            ];
          })}
          {options.length === 0 && <div className="dd-empty">Nothing to choose</div>}
        </div>
      )}
    </div>
  );
}

export function Segmented<T extends string>({ value, options, onChange, className }: {
  value: T;
  options: Option<T>[];
  onChange: (v: T) => void;
  className?: string;
}) {
  return (
    <div className={`seg no-drag${className ? ' ' + className : ''}`} role="radiogroup">
      {options.map((o) => (
        <button key={o.value} type="button" role="radio" aria-checked={o.value === value}
          className={`seg-opt${o.value === value ? ' is-on' : ''}`} onClick={() => onChange(o.value)}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Toggle({ on, onChange, label }: { on: boolean; onChange: (on: boolean) => void; label: string }) {
  return (
    <button type="button" className={`toggle no-drag${on ? ' is-on' : ''}`} role="switch" aria-checked={on} onClick={() => onChange(!on)}>
      <span className="toggle-track"><span className="toggle-knob" /></span>
      <span>{label}</span>
    </button>
  );
}

const GROUP_LABEL: Record<string, string> = { player: 'You', follower: 'Followers', nearby: 'Nearby' };

export function personOptions(people: Person[], filter?: (p: Person) => boolean): Option<string>[] {
  return people
    .filter((p) => !filter || filter(p))
    .map((p) => ({ value: p.formId, label: p.name, group: GROUP_LABEL[p.group], hint: p.group === 'nearby' ? `${Math.round(p.distance / 70)} m` : undefined }));
}

export function ActorPicker({ value, onChange, filter, placeholder }: {
  value: string;
  onChange: (formId: string) => void;
  filter?: (p: Person) => boolean;
  placeholder?: string;
}) {
  const { people, refreshPeople } = useApp();
  return (
    <div className="actor-picker no-drag" onMouseDown={() => refreshPeople()}>
      <Dropdown value={value} options={personOptions(people, filter)} onChange={onChange} placeholder={placeholder ?? 'Choose someone'} />
    </div>
  );
}

export const LIGHT_OPTIONS: Option<string>[] = LIGHTING.map((l) => ({ value: l, label: LIGHT_LABEL[l] }));
export const EXPRESSION_OPTIONS: Option<string>[] = EXPRESSIONS.map((e) => ({ value: e || 'game', label: EXPRESSION_LABEL[e] }));

/** The docks' FPS switch (each view's frame rate in its corner) and, where Magelight can freeze the world, the Freeze
 * world switch (the game stops rendering its 3D world behind the menu, so the views get more of its frames). Both are
 * kept in settings.json. */
export function FpsToggle() {
  const { hello, showFps, setShowFps, freezeWorld, setFreezeWorld } = useApp();
  return (
    <>
      <Toggle on={showFps} onChange={setShowFps} label="FPS" />
      {hello?.freezeAvailable && <Toggle on={freezeWorld} onChange={setFreezeWorld} label="Freeze world" />}
    </>
  );
}

/** What Phantasm's QueryHint("idles") answer says of the choice in force. `pending`: not tried yet, its file is being
 * read, or its last try failed before it (ask again; `reason` says why when a try failed); `plays` false with `reason`
 * when it cannot play (an older Phantasm never reports a refused file). */
export interface IdleAnswer {
  choice: string;
  plays: boolean;
  reason: string;
  pending: boolean;
}

/** The idle the slot's actor plays: Phantasm's list for their skeleton (QueryHint "idles"), asked once the view draws;
 * `ask` asks again. */
export function useIdles(slot: Slot, formId: string | undefined, shown: boolean) {
  const [idles, setIdles] = useState<IdleChoice[]>([]);
  // Kept with the actor it answers for, so another actor's answer never shows for a render after a switch.
  const [answered, setAnswered] = useState<{ formId: string; answer: IdleAnswer } | null>(null);
  useEffect(() => {
    setIdles([]);
  }, [formId]);
  const ask = useCallback(() => client.fire('viewQuery', { slot, key: 'idles' }), [slot]);
  useEffect(() => {
    if (shown && formId) ask();
  }, [ask, formId, shown]);
  useEffect(
    () =>
      client.onPush((p) => {
        if (p.event !== 'reply' || p.slot !== slot || p.key !== 'idles' || p.formId !== formId) return;
        const j = p.json as { idles?: IdleChoice[]; choice?: string; plays?: boolean; reason?: string; pending?: boolean };
        setIdles(Array.isArray(j.idles) ? j.idles : []);
        setAnswered({ formId: p.formId, answer: { choice: j.choice ?? '', plays: j.plays !== false, reason: j.reason ?? '', pending: j.pending === true } });
      }),
    [slot, formId],
  );
  const answer = answered && answered.formId === formId ? answered.answer : null;
  return { idles, plays: answer?.plays ?? true, answer, ask };
}

export function idleOptions(idles: IdleChoice[]): Option<string>[] {
  return [{ value: 'cycle', label: 'Idle cycle' }, { value: 'still', label: 'Hold still' }, ...idles.map((i) => ({ value: i.id, label: i.label || i.id, hint: i.loop ? undefined : 'once' }))];
}

export function Field({ label, children }: { label: string; children: ReactNode }) {
  return (
    <label className="field no-drag">
      <span className="field-label">{label}</span>
      {children}
    </label>
  );
}

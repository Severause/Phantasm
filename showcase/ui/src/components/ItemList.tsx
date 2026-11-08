import { useEffect, useRef, useState } from 'react';
import { client } from '../api/client';
import type { ItemCard } from '../api/protocol';
import { KIND_LABEL } from '../lib/labels';
import { Segmented, type Option } from './Controls';

const PAGE = 80;

/**
 * A searchable list of weapons and armour: everything in the load order, or what one actor holds. Rows are picked
 * with a click; the parent decides what a pick does.
 */
export function ItemList({ filters, defaultFilter, holder, holderLabel, onPick, picked, disabled }: {
  filters: Option<string>[];
  defaultFilter: string;
  /** an actor whose inventory the "held" source lists */
  holder?: string;
  holderLabel?: string;
  onPick: (item: ItemCard) => void;
  picked?: ReadonlySet<string>;
  disabled?: boolean;
}) {
  const [text, setText] = useState('');
  const [filter, setFilter] = useState(defaultFilter);
  const [source, setSource] = useState<'all' | 'held'>(holder ? 'held' : 'all');
  const [items, setItems] = useState<ItemCard[]>([]);
  const [total, setTotal] = useState(0);
  const req = useRef(0);

  useEffect(() => {
    if (!holder && source === 'held') setSource('all');
  }, [holder, source]);

  const load = (offset: number) => {
    const n = ++req.current;
    const src = holder && source === 'held' ? 'held' : 'all';
    client
      .request('items', { text, filter, source: src, actor: src === 'held' ? holder : undefined, offset, limit: PAGE })
      .then((r) => {
        if (n !== req.current) return;
        setTotal(r.total);
        setItems((old) => (offset === 0 ? r.items : [...old, ...r.items]));
      }, () => {});
  };

  useEffect(() => {
    const t = window.setTimeout(() => load(0), 140);
    return () => window.clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [text, filter, source, holder]);

  return (
    <div className={`items no-drag${disabled ? ' is-disabled' : ''}`}>
      <div className="items-head">
        <div className="search">
          <svg viewBox="0 0 16 16" className="search-icon" aria-hidden><circle cx="7" cy="7" r="5" fill="none" stroke="currentColor" strokeWidth="1.6" /><path d="M11 11l3.5 3.5" stroke="currentColor" strokeWidth="1.6" /></svg>
          <input value={text} placeholder="Search by name" onChange={(e) => setText(e.target.value)} spellCheck={false} />
        </div>
        {holder && (
          <Segmented value={source} onChange={setSource}
            options={[{ value: 'held', label: holderLabel ?? 'Carried' }, { value: 'all', label: 'Everything' }]} />
        )}
        <Segmented className="seg-wrap" value={filter} onChange={setFilter} options={filters} />
      </div>
      <div className="items-list">
        {items.map((i) => (
          <button type="button" key={i.formId} className={`item-row${picked?.has(i.formId) ? ' is-on' : ''}`} onClick={() => onPick(i)} disabled={disabled}>
            <span className={`item-dot k-${i.kind}${i.enchanted ? ' is-ench' : ''}`} />
            <span className="item-name">{i.name}</span>
            {i.worn && <span className="item-tag">worn</span>}
            <span className="item-kind">{KIND_LABEL[i.kind]}</span>
          </button>
        ))}
        {items.length < total && (
          <button type="button" className="item-more" onClick={() => load(items.length)}>Show more ({total - items.length})</button>
        )}
        {items.length === 0 && <div className="items-empty">{source === 'held' ? 'They carry nothing that matches.' : 'Nothing matches.'}</div>}
      </div>
    </div>
  );
}

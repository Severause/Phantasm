import { useSyncExternalStore } from 'react';

// The page is drawn for a 1080-pixel-high screen and every size in the stylesheet is `calc(Npx * var(--ui))`, so a
// taller screen draws it larger at full sharpness (no transform or zoom, so pointer coordinates stay the page's own).
// The height is in CSS pixels: a host that already draws the view at a device scale has divided it out. The fit
// stays within 0.8 to 2 (no smaller below 864 pixels, no larger above 4K) and the fit times uiScale within MIN to MAX.
const BASE_HEIGHT = 1080;
const MIN = 0.7;
const MAX = 2.4;

let user = 1;
let current = 1;
const listeners = new Set<() => void>();

function compute(): number {
  const h = window.innerHeight || BASE_HEIGHT;
  const fit = Math.min(2, Math.max(0.8, h / BASE_HEIGHT));
  return Math.round(Math.min(MAX, Math.max(MIN, fit * user)) * 100) / 100;
}

function apply(): void {
  const next = compute();
  if (next === current && document.documentElement.style.getPropertyValue('--ui')) return;
  current = next;
  document.documentElement.style.setProperty('--ui', String(next));
  listeners.forEach((l) => l());
}

/** Starts the fit to the screen height; call once before the first render. */
export function initScale(): void {
  apply();
  let timer: number | undefined;
  window.addEventListener('resize', () => {
    window.clearTimeout(timer);
    timer = window.setTimeout(apply, 100);
  });
}

/** settings.json's uiScale, multiplied into the fit. */
export function setUserScale(value: number | undefined): void {
  user = typeof value === 'number' && Number.isFinite(value) ? Math.min(1.6, Math.max(0.7, value)) : 1;
  apply();
}

/** The scale in force, for sizes drawn in SVG user units that CSS cannot reach (a pattern's tile). */
export function useScale(): number {
  return useSyncExternalStore(
    (l) => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    () => current,
  );
}

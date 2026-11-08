// Words the page shows for the DLL's and Phantasm's tokens.

const VIEW_NOTES: Record<string, string> = {
  absent: 'Phantasm is not installed, so nothing can be drawn.',
  old: 'This Phantasm is too old for the showcase. Update Phantasm.',
  refused: 'Phantasm refused the view.',
  busy: 'Waiting: more views are open than Phantasm draws at once.',
  closed: 'The menu closed.',
  'game-load': 'A game load ended the view.',
  missing: "They aren't in the game anymore.",
  dead: 'They are dead.',
  unrenderable: "Phantasm can't draw them.",
  'not-loaded': 'Too far away to draw.',
  notLoaded: 'Too far away to draw.',
  'collect-failed': "Phantasm couldn't read their 3D.",
  'no-image': "Magelight can't show GPU pictures here (its CPU renderer?).",
  'gpu-unavailable': 'The graphics card refused. Try again in a moment.',
  faulted: 'The view stopped after an error.',
  tooMany: 'Too many views are open.',
  badSize: 'The panel is too small.',
};

export function viewNote(reason: string | undefined): string {
  if (!reason) return '';
  return VIEW_NOTES[reason] ?? `The picture isn't available (${reason}).`;
}

export const LIGHT_LABEL: Record<string, string> = {
  studio: 'Studio',
  daylight: 'Daylight',
  hearth: 'Hearth',
  torchlight: 'Torchlight',
  moonlight: 'Moonlight',
  here: 'As here',
};

export const EXPRESSION_LABEL: Record<string, string> = {
  '': 'As in game',
  neutral: 'Neutral',
  happy: 'Happy',
  sad: 'Sad',
  angry: 'Angry',
  afraid: 'Afraid',
  surprised: 'Surprised',
  puzzled: 'Puzzled',
  disgusted: 'Disgusted',
};

export const KIND_LABEL: Record<string, string> = {
  weapon: 'Weapon',
  bow: 'Bow',
  staff: 'Staff',
  head: 'Head',
  body: 'Body',
  hands: 'Hands',
  feet: 'Feet',
  shield: 'Shield',
  jewelry: 'Jewellery',
  other: 'Other',
};

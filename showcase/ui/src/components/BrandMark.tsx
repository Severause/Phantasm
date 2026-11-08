// Phantasm's mark for the top bar: the logo's arch, cowled figure and spark, redrawn for a small size (thicker lines,
// no mist wisps, particles or blur, which a few pixels cannot show).

export function BrandMark({ className }: { className?: string }) {
  return (
    <svg viewBox="92 38 416 500" className={className} aria-hidden>
      <defs>
        <linearGradient id="bm-frame" x1="0" y1="60" x2="0" y2="526" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#f2fffe" />
          <stop offset="0.6" stopColor="#8fe9e2" />
          <stop offset="1" stopColor="#3a9aa0" />
        </linearGradient>
        <linearGradient id="bm-cloak" x1="0" y1="124" x2="0" y2="530" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#e9fffc" />
          <stop offset="0.55" stopColor="#9ff1e9" />
          <stop offset="1" stopColor="#3fbfb6" stopOpacity="0.3" />
        </linearGradient>
        <radialGradient id="bm-mist" cx="300" cy="522" r="210" gradientUnits="userSpaceOnUse">
          <stop offset="0" stopColor="#8ff5ec" stopOpacity="0.55" />
          <stop offset="1" stopColor="#8ff5ec" stopOpacity="0" />
        </radialGradient>
      </defs>
      <path d="M139,522 V240 A161,161 0 0 1 461,240 V522 Z" fill="#0b2230" />
      <path
        d="M300,124 C338,124 364,152 370,198 C376,244 370,286 382,316 C400,346 432,360 442,396 C450,424 450,470 450,522 L150,522 C150,470 150,424 158,396 C168,360 200,346 218,316 C230,286 224,244 230,198 C236,152 262,124 300,124 Z"
        fill="url(#bm-cloak)"
      />
      <path d="M300,168 C330,168 347,194 347,236 C347,284 326,318 300,332 C274,318 253,284 253,236 C253,194 270,168 300,168 Z" fill="#04161d" stroke="#ffffff" strokeWidth="7" />
      <ellipse cx="284" cy="252" rx="14" ry="6.5" fill="#e9fffd" />
      <ellipse cx="316" cy="252" rx="14" ry="6.5" fill="#e9fffd" />
      <ellipse cx="300" cy="522" rx="210" ry="30" fill="url(#bm-mist)" />
      <path d="M130,522 V240 A170,170 0 0 1 470,240 V522 M108,526 H212 M388,526 H492" fill="none" stroke="url(#bm-frame)" strokeWidth="18" strokeLinecap="round" />
      <path d="M300,44 C302,61 309,68 326,70 C309,72 302,79 300,96 C298,79 291,72 274,70 C291,68 298,61 300,44 Z" fill="#ffffff" />
    </svg>
  );
}

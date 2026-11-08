// Which UI host the page runs on, decided once before anything touches the Magelight SDK.
//
// Magelight sets window.__MAGELIGHT__ before page scripts run; PrismaUI sets window.prismaUi. On PrismaUI the page
// cannot show Phantasm's pictures, so the DLL draws them under the page and the page leaves its backdrop and stages
// transparent (html.host-prisma in app.css, lib/underlay.ts). ?prisma=1 in a plain browser previews that layout with a
// stand-in underlay.

interface HostGlobals {
  __MAGELIGHT__?: unknown;
  prismaUi?: unknown;
}

const w = (typeof window !== 'undefined' ? window : {}) as HostGlobals & Window;

export const inMagelight = w.__MAGELIGHT__ !== undefined;
export const inPrisma = !inMagelight && w.prismaUi !== undefined;
/** a plain browser asked to preview the PrismaUI layout */
export const previewPrisma =
  !inMagelight && !inPrisma && typeof location !== 'undefined' && new URLSearchParams(location.search).has('prisma');
/** the page draws for PrismaUI: transparent backdrop and stages, the pictures under the page */
export const prismaLayout = inPrisma || previewPrisma;

if (prismaLayout && typeof document !== 'undefined') document.documentElement.classList.add('host-prisma');

// Copies the built page into the PrismaUI views folder too (Data/PrismaUI/views/PhantasmShowcase): PrismaUI loads
// pages from there and nowhere else. The same page serves both hosts (ui/src/lib/host.ts).
import { cpSync, existsSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', 'data');
const from = resolve(root, 'Magelight', 'PhantasmShowcase', 'views', 'main');
const to = resolve(root, 'PrismaUI', 'views', 'PhantasmShowcase');
if (!existsSync(resolve(from, 'index.html'))) {
  console.error(`copy-prisma: no built page in ${from}`);
  process.exit(1);
}
rmSync(to, { recursive: true, force: true });
cpSync(from, to, { recursive: true });
console.log(`copy-prisma: ${from} -> ${to}`);

import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { magelight } from '@magelight/vite-plugin';

// The built page ships in the repo: the plugin writes it straight into the data folder the DLL loads
// (Data/Magelight/PhantasmShowcase/views/main), replacing whatever was there.
export default defineConfig({
  plugins: [react(), magelight({ viewsDir: '../data/Magelight/PhantasmShowcase/views', single: 'main' })],
  // Stable file names: the built page is committed, and hashed names would churn every file on each build.
  build: { rollupOptions: { output: { entryFileNames: 'assets/[name].js', chunkFileNames: 'assets/[name].js', assetFileNames: 'assets/[name][extname]' } } },
  server: { port: 5183 },
});

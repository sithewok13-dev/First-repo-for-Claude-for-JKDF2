// Bundles the Mini App with esbuild into client/dist.

import { build } from 'esbuild';
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const root = new URL('../', import.meta.url).pathname;
const out = join(root, 'client/dist');
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

await build({
  entryPoints: { app: join(root, 'client/src/main.tsx') },
  bundle: true,
  format: 'esm',
  target: ['es2020', 'safari15'],
  outdir: out,
  minify: process.env.NODE_ENV !== 'development',
  sourcemap: true,
  jsx: 'automatic',
  jsxImportSource: 'preact',
  legalComments: 'linked',
  logLevel: 'info',
});
await build({
  entryPoints: { app: join(root, 'client/src/styles.css') },
  bundle: true,
  outdir: out,
  minify: true,
  logLevel: 'info',
});
cpSync(join(root, 'client/index.html'), join(out, 'index.html'));
cpSync(join(root, 'client/public'), out, { recursive: true });
// Licence notices of third-party code bundled into app.js (Preact is MIT).
writeFileSync(join(out, 'THIRD-PARTY-NOTICES.txt'), [
  'The arcade Mini App bundles the following third-party software.',
  '',
  '== Preact (https://preactjs.com) ==',
  readFileSync(join(root, 'node_modules/preact/LICENSE'), 'utf8'),
  'Emulator cores and their licences: /cores/SOURCES.txt, /cores/LICENSE-*.txt',
].join('\n'));
console.log('client built ->', out);

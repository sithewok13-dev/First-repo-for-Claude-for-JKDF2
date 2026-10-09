// Local development without Telegram: npm run dev
// Builds the Mini App if needed, then starts the server in DEV_MODE with a
// test group and four test members, and prints one sign-in link per member
// (open each in its own browser profile/tab to play together).
// Needs the cores in native/build (npm run build:cores, or copy them from the
// Docker image) and, to play, a lawful ROM uploaded from the arcade's shelf;
// native/testroms/build/atc-*.nes are made for this.

import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';

const root = new URL('../', import.meta.url).pathname;
process.env.DEV_MODE = '1';
process.env.DEV_GROUP ??= 'Dev Arcade';
process.env.DEV_MEMBERS ??= '1:creator,2,3,4';
process.env.PORT ??= '8080';
process.env.HOST ??= '127.0.0.1';
process.env.PUBLIC_URL ??= `http://localhost:${process.env.PORT}`;
process.env.DATA_DIR ??= join(root, 'data-dev');
if (!process.env.BOT_TOKEN) process.env.UPDATES_MODE = 'off';

if (!existsSync(join(root, 'native/build/fceumm.wasm'))) {
  console.error('No emulator cores in native/build. Run `npm run build:cores` (needs emsdk) or copy them from the Docker image.');
  process.exit(1);
}
if (!existsSync(join(root, 'client/dist/index.html'))) {
  execFileSync(process.execPath, [join(root, 'scripts/build-client.ts')], { stdio: 'inherit' });
}
await import('../server/main.ts');

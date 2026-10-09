// Validation child process (see ./validator.ts). One job per process:
//
//   inspect: sniff the file type, run the zip safety checks, identify an
//            arcade romset against the FBNeo catalogs, or validate an NES
//            header (extracting a lone .nes from a zip to nesOutPath);
//   boot:    load the right WebAssembly core with the files at the paths a
//            real session uses, run a few hundred frames and check that the
//            game produced a non-blank picture.
//
// The process starts with a minimal environment, a capped V8 heap and is
// killed by the host on timeout. It writes nothing except nesOutPath.

import { closeSync, openSync, readFileSync, statSync, writeSync } from 'node:fs';
import { extname, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Core, type FrameImage } from '../../shared/emu/core.ts';
import { SYSTEMS } from '../../shared/systems.ts';
import { RomCatalog } from './catalog.ts';
import { describeNes, isNesHeader, parseNes } from './nes.ts';
import type { ValidationFinding } from './types.ts';
import type { BootJob, BootResult, InspectJob, InspectResult, ValidateReply, ValidateRequest } from './validator.ts';
import { readEntry, safeLabel, validateZip, type ZipEntry } from './zip.ts';

function send(m: ValidateReply, done?: () => void): void {
  process.send!(m, undefined, {}, () => done?.());
}

// ---------------------------------------------------------------- inspect

type Sniffed = 'zip' | 'nes' | '7z' | 'rar' | 'gzip' | 'fds' | 'unif' | 'unknown';

function sniff(b: Uint8Array): Sniffed {
  const at = (i: number, ...bytes: number[]) => bytes.every((v, k) => b[i + k] === v);
  if (b.length >= 4 && at(0, 0x50, 0x4b) && ((b[2] === 3 && b[3] === 4) || (b[2] === 5 && b[3] === 6))) return 'zip';
  if (isNesHeader(b)) return 'nes';
  if (at(0, 0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c)) return '7z';
  if (at(0, 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07)) return 'rar';
  if (at(0, 0x1f, 0x8b)) return 'gzip';
  if (at(0, 0x46, 0x44, 0x53, 0x1a) || at(1, 0x2a, 0x4e, 0x49, 0x4e, 0x54, 0x45, 0x4e, 0x44, 0x4f)) return 'fds';
  if (at(0, 0x55, 0x4e, 0x49, 0x46)) return 'unif';
  return 'unknown';
}

const REJECT_TEXT: Record<Exclude<Sniffed, 'zip' | 'nes'>, string> = {
  '7z': '7z archives are not supported: re-pack the romset as a .zip (FBNeo romsets are zip files)',
  rar: 'RAR archives are not supported: re-pack the romset as a .zip',
  gzip: 'gzip/tar archives are not supported: upload the romset .zip or the .nes file itself',
  fds: 'Famicom Disk System images are not supported',
  unif: 'UNIF NES images are not supported: use an iNES (.nes) dump',
  unknown: 'unsupported file type: upload an arcade romset .zip (Capcom CPS-1/CPS-2, Neo Geo) or an NES .nes ROM',
};

// Extensions that identify other systems, so an unknown zip gets a precise message.
const OTHER_SYSTEMS: Record<string, string> = {
  sfc: 'Super Nintendo', smc: 'Super Nintendo', md: 'Mega Drive/Genesis', gen: 'Mega Drive/Genesis', smd: 'Mega Drive/Genesis',
  gb: 'Game Boy', gbc: 'Game Boy Color', gba: 'Game Boy Advance', n64: 'Nintendo 64', z64: 'Nintendo 64', v64: 'Nintendo 64',
  nds: 'Nintendo DS', sms: 'Master System', gg: 'Game Gear', pce: 'PC Engine', a26: 'Atari 2600', lnx: 'Atari Lynx',
  ws: 'WonderSwan', wsc: 'WonderSwan Color', ngp: 'Neo Geo Pocket', ngc: 'Neo Geo Pocket Color', '32x': 'Sega 32X',
  chd: 'CHD disc images', iso: 'disc images', cue: 'disc images', fds: 'Famicom Disk System', unf: 'UNIF NES images', unif: 'UNIF NES images',
};

function rejected(code: string, error: string, findings: ValidationFinding[] = []): InspectResult {
  if (!findings.some((f) => f.level === 'error')) findings.push({ level: 'error', code, message: error });
  return { ok: false, code, error, findings };
}

function inspectNes(data: Uint8Array, job: InspectJob, extractedFrom: string | null, extra: ValidationFinding[]): InspectResult {
  const { info, findings } = parseNes(data);
  const all = [...extra, ...findings];
  if (!info) {
    const first = findings.find((f) => f.level === 'error');
    return rejected(first?.code ?? 'nes_header', `invalid NES ROM: ${first?.message ?? 'bad header'}`, all);
  }
  let romPath = job.path;
  if (extractedFrom !== null) {
    const fd = openSync(job.nesOutPath, 'wx', 0o600);
    try {
      let off = 0;
      while (off < data.length) off += writeSync(fd, data, off, data.length - off);
    } finally {
      closeSync(fd);
    }
    romPath = job.nesOutPath;
    all.unshift({ level: 'info', code: 'nes_in_zip', message: `NES ROM "${safeLabel(extractedFrom)}" taken from the zip` });
  }
  return { ok: true, format: 'nes', nes: info, romPath, description: describeNes(info), findings: all };
}

async function inspect(job: InspectJob): Promise<InspectResult> {
  const size = statSync(job.path).size;
  if (size > job.maxBytes) return rejected('too_large', `the file is larger than the ${job.maxBytes}-byte upload limit`);
  if (size === 0) return rejected('empty_file', 'the file is empty');
  const data = readFileSync(job.path);
  const kind = sniff(data);
  if (kind === 'nes') return inspectNes(data, job, null, []);
  if (kind !== 'zip') return rejected(kind === 'unknown' ? 'unsupported_format' : `unsupported_${kind}`, REJECT_TEXT[kind]);

  const report = validateZip(data, job.limits);
  if (!report.ok) {
    const first = report.findings.find((f) => f.level === 'error');
    return rejected(first?.code ?? 'zip_malformed', `the zip was rejected: ${first?.message ?? 'malformed archive'}`, report.findings);
  }
  const findings = [...report.findings];

  // NES: a zip holding exactly one .nes file is that ROM.
  const nes = report.entries.filter((e) => e.name.toLowerCase().endsWith('.nes'));
  if (nes.length > 1) {
    return rejected('multiple_roms', `the zip holds ${nes.length} NES ROMs; upload one game per file`, findings);
  }
  if (nes.length === 1) {
    const others = report.entries.length - 1;
    if (others) findings.push({ level: 'info', code: 'extra_files', message: `${others} other file(s) in the zip are ignored` });
    return inspectNes(readEntry(data, nes[0]), job, nes[0].name, findings);
  }

  // Arcade: identify the romset by CRC-32 and size.
  const catalog = await RomCatalog.loadOrBuild(job.coreDir);
  const ident = catalog.identify(
    report.entries.map((e: ZipEntry) => ({ name: e.name, size: e.uncompressedSize, crc: e.crc32 })),
    job.fileName,
  );
  if (ident.result === 'unknown') {
    if (report.nested.length && report.nested.length >= report.entries.length / 2) {
      ident.findings.push({ level: 'error', code: 'nested_only', message: 'the zip mostly contains other archives; upload each romset zip directly, not a zip of zips' });
    }
    const systems = new Set<string>();
    for (const e of report.entries) {
      const s = OTHER_SYSTEMS[extname(e.name).slice(1).toLowerCase()];
      if (s) systems.add(s);
    }
    if (systems.size) {
      const msg = `this looks like ${[...systems].join(' / ')} content, which this arcade does not support`;
      ident.findings.push({ level: 'error', code: 'unsupported_system', message: msg });
      ident.message = msg;
    }
  }
  return {
    ok: true,
    format: 'zip',
    ident,
    entries: report.entries.length,
    totalUncompressed: report.totalUncompressed,
    findings: [...findings, ...ident.findings],
  };
}

// ---------------------------------------------------------------- boot

// Any two different pixels mean the game drew something.
function hasPicture(img: FrameImage): boolean {
  const px = new Uint32Array(img.rgba.buffer, img.rgba.byteOffset, img.width * img.height);
  const first = px[0];
  for (let i = 1; i < px.length; i++) if (px[i] !== first) return true;
  return false;
}

async function boot(job: BootJob): Promise<BootResult> {
  const sys = SYSTEMS[job.system];
  const mod = await import(pathToFileURL(join(job.coreDir, `${sys.core}.mjs`)).href);
  const core = await Core.create(mod.default, readFileSync(join(job.coreDir, `${sys.core}.wasm`)), { quiet: true });
  core.setLogLevel(2);
  core.setEpoch(1_700_000_000);
  for (const f of job.files) core.writeFile(f.path, readFileSync(f.source));
  for (const [k, v] of Object.entries(sys.options)) core.setOption(k, v);
  for (const [p, d] of Object.entries(sys.portDevices)) core.setPortDevice(Number(p), d);
  const tail = () => core.log.slice(-3).join(' | ').replace(/\s+/g, ' ').slice(0, 240);
  if (!core.loadGame(job.gamePath)) {
    return { tried: true, ok: false, frames: 0, detail: `the emulator could not load this game${tail() ? `: ${tail()}` : ''}` };
  }
  // The libretro FBNeo core reports success even when ROMs are missing or the
  // driver fails to start: it then shows its own "FBNeo Error" screen (which
  // would pass the picture check). A started driver is the real signal.
  if (job.system !== 'fceumm' && !core.driverInfo()?.name) {
    return { tried: true, ok: false, frames: 0, detail: `FBNeo could not start this romset${tail() ? `: ${tail()}` : ''}` };
  }
  // FBNeo registers per-game options during load; apply ours again.
  for (const [k, v] of Object.entries(sys.options)) core.setOption(k, v);
  let firstPicture = -1;
  let frames = 0;
  for (let f = 1; f <= job.maxFrames; f++) {
    const check = f % 30 === 0;
    core.runFrame(check, false);
    frames = f;
    if (check && firstPicture < 0) {
      const img = core.image();
      if (img && hasPicture(img)) firstPicture = f;
    }
    if (firstPicture >= 0 && f >= job.minFrames) break;
  }
  if (firstPicture < 0) {
    return { tried: true, ok: false, frames, detail: `the game ran ${frames} frames but showed only a blank screen (wrong or corrupt ROM data?)` };
  }
  return { tried: true, ok: true, frames, detail: `booted; first picture at frame ${firstPicture}, ran ${frames} frames without errors` };
}

// ---------------------------------------------------------------- main

let busy = false;
process.on('message', async (raw: unknown) => {
  if (busy) return;
  busy = true;
  const req = raw as ValidateRequest;
  // One job per process: exit once the reply has been flushed.
  const exit = () => process.exit(0);
  try {
    const result = req.t === 'inspect' ? await inspect(req.job) : await boot(req.job);
    send({ t: 'result', result }, exit);
  } catch (e) {
    send({ t: 'error', message: e instanceof Error ? e.message : String(e) }, exit);
  }
});

process.on('disconnect', () => process.exit(0));
send({ t: 'ready' });

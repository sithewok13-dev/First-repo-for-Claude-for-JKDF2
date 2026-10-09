// iNES / NES 2.0 header validation (nesdev.org/wiki/INES, /NES_2.0).
//
// The 16-byte header declares PRG/CHR sizes, an optional 512-byte trainer,
// the mapper and (NES 2.0) region and default expansion device. A file must
// be at least as long as its header says; trailing data is tolerated with a
// warning (NES 2.0 "miscellaneous ROMs" legitimately live there).

import type { ValidationFinding } from './types.ts';

export interface NesInfo {
  format: 'ines' | 'nes2';
  mapper: number;
  submapper: number | null;
  prgBytes: number;
  chrBytes: number;            // 0 = CHR RAM
  trainer: boolean;
  battery: boolean;
  mirroring: 'horizontal' | 'vertical' | 'four-screen';
  console: 'nes' | 'vs' | 'playchoice' | 'extended';
  region: 'ntsc' | 'pal' | 'multi' | 'dendy' | null;
  expansionDevice: number | null;  // NES 2.0 byte 15
  fourScore: boolean;              // header says the game expects four controllers
  trailingBytes: number;
}

const HEADER = 16;
const TRAINER = 512;
const PRG_UNIT = 16 * 1024;
const CHR_UNIT = 8 * 1024;
const MAX_ROM_BYTES = 8 * 1024 * 1024; // far above any licensed cartridge

export function isNesHeader(data: Uint8Array): boolean {
  return data.length >= 4 && data[0] === 0x4e && data[1] === 0x45 && data[2] === 0x53 && data[3] === 0x1a;
}

// NES 2.0 sizes: an MSB nibble of 0xF selects exponent-multiplier notation.
function nes2Size(lsb: number, msbNibble: number, unit: number): number {
  if (msbNibble === 0xf) {
    const exp = lsb >> 2;
    const mul = (lsb & 3) * 2 + 1;
    return exp > 30 ? Infinity : 2 ** exp * mul;
  }
  return ((msbNibble << 8) | lsb) * unit;
}

const kib = (n: number) => (n % 1024 === 0 ? `${n / 1024} KiB` : `${n} bytes`);

export function parseNes(data: Uint8Array): { info: NesInfo | null; findings: ValidationFinding[] } {
  const findings: ValidationFinding[] = [];
  const err = (code: string, message: string) => {
    findings.push({ level: 'error', code, message });
    return { info: null, findings };
  };
  if (data.length < HEADER || !isNesHeader(data)) return err('nes_header', 'not an iNES ROM (missing "NES" header)');

  const flags6 = data[6];
  const flags7 = data[7];
  const nes2 = (flags7 & 0x0c) === 0x08;
  let format: NesInfo['format'] = 'ines';
  let prg: number;
  let chr: number;
  let mapper: number;
  let submapper: number | null = null;
  let region: NesInfo['region'] = null;
  let expansion: number | null = null;
  let consoleType = flags7 & 0x03;

  if (nes2) {
    format = 'nes2';
    prg = nes2Size(data[4], data[9] & 0x0f, PRG_UNIT);
    chr = nes2Size(data[5], data[9] >> 4, CHR_UNIT);
    mapper = (flags6 >> 4) | (flags7 & 0xf0) | ((data[8] & 0x0f) << 8);
    submapper = data[8] >> 4;
    region = (['ntsc', 'pal', 'multi', 'dendy'] as const)[data[12] & 0x03];
    expansion = data[15] & 0x3f;
    findings.push({ level: 'info', code: 'nes2', message: 'NES 2.0 header' });
  } else {
    prg = data[4] * PRG_UNIT;
    chr = data[5] * CHR_UNIT;
    // Old dumping tools wrote signatures such as "DiskDude!" into bytes 7-15;
    // then byte 7 is garbage and only the low mapper nibble can be trusted.
    const junk = data[12] !== 0 || data[13] !== 0 || data[14] !== 0 || data[15] !== 0;
    if (junk) {
      mapper = flags6 >> 4;
      consoleType = 0;
      findings.push({ level: 'warn', code: 'nes_junk_header', message: 'the iNES header contains junk in bytes 7-15 (old dumping tool); the mapper may be misdetected' });
    } else {
      mapper = (flags6 >> 4) | (flags7 & 0xf0);
    }
  }

  if (!Number.isFinite(prg) || prg > MAX_ROM_BYTES || !Number.isFinite(chr) || chr > MAX_ROM_BYTES) {
    return err('nes_size', 'the header declares an impossible ROM size');
  }
  if (prg === 0) return err('nes_size', 'the header declares no program ROM');

  const trainer = (flags6 & 0x04) !== 0;
  const expected = HEADER + (trainer ? TRAINER : 0) + prg + chr;
  if (data.length < expected) {
    return err('nes_size', `the file is ${data.length} bytes but its header declares ${expected} (truncated or bad header)`);
  }
  const trailing = data.length - expected;
  if (trailing > 0) {
    findings.push({ level: 'warn', code: 'nes_size', message: `${trailing} bytes of extra data after the declared ROM (ignored by the emulator)` });
  }
  if (trainer) findings.push({ level: 'info', code: 'nes_trainer', message: 'contains a 512-byte trainer' });

  const consoleName = (['nes', 'vs', 'playchoice', 'extended'] as const)[consoleType];
  if (consoleName === 'vs') {
    findings.push({ level: 'warn', code: 'nes_vs', message: 'this is a VS. System arcade ROM; coin and DIP switch handling may not work' });
  } else if (consoleName === 'playchoice') {
    findings.push({ level: 'warn', code: 'nes_playchoice', message: 'this is a PlayChoice-10 ROM; it may not behave like the home version' });
  } else if (consoleName === 'extended') {
    findings.push({ level: 'warn', code: 'nes_console', message: 'the header targets a non-standard console type' });
  }
  if (region === 'pal' || region === 'dendy') {
    findings.push({ level: 'info', code: 'nes_region', message: `the header marks this as a ${region.toUpperCase()} game` });
  }

  const info: NesInfo = {
    format,
    mapper,
    submapper,
    prgBytes: prg,
    chrBytes: chr,
    trainer,
    battery: (flags6 & 0x02) !== 0,
    mirroring: flags6 & 0x08 ? 'four-screen' : flags6 & 0x01 ? 'vertical' : 'horizontal',
    console: consoleName,
    region,
    expansionDevice: expansion,
    // 0x02: NES Four Score / Satellite, 0x03: Famicom Four Players Adapter.
    fourScore: expansion === 0x02 || expansion === 0x03,
    trailingBytes: trailing,
  };
  return { info, findings };
}

export function describeNes(info: NesInfo): string {
  const chr = info.chrBytes ? `${kib(info.chrBytes)} CHR` : 'CHR RAM';
  return `NES ROM (${info.format === 'nes2' ? 'NES 2.0' : 'iNES'}, mapper ${info.mapper}, ${kib(info.prgBytes)} PRG, ${chr})`;
}

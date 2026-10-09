// Emulation systems the arcade can run, and the fairness rules for their input.
//
// Fairness: the server only forwards RetroPad bits that map one-to-one to a
// physical button of the emulated machine. Core-provided conveniences that
// press several buttons at once (FBNeo "3x Punch"/"ABC" macros, FCEUmm's
// A+B on L3), turbo buttons, and service/diagnostic inputs are masked out
// server-side, so no client can send them.

import { PAD } from './emu/core.ts';

export type SystemId = 'fbneo_cps12' | 'fbneo_neogeo' | 'fceumm';

export interface SystemDef {
  id: SystemId;
  label: string;
  core: string;                 // native/build/<core>.mjs|.wasm
  maxPorts: number;
  // RetroPad bit that inserts a coin, or null when the system has no coin slot.
  // Coins never come from client input; the server pulses this bit itself.
  coinBit: number | null;
  // Bits a seated player may send when the per-game descriptors are unknown.
  defaultAllowed: number;
  // Core options forced for determinism and fairness.
  options: Record<string, string>;
  // libretro device ids to set per port before loading (e.g. NES Four Score).
  portDevices: Record<number, number>;
  // Extra files a game needs that are never bundled (proprietary BIOS etc.).
  needsBiosFrom?: 'board';
  romExtensions: string[];
}

const bit = (...ids: number[]) => ids.reduce((m, id) => m | (1 << id), 0);

const DPAD = bit(PAD.UP, PAD.DOWN, PAD.LEFT, PAD.RIGHT);

const FBNEO_OPTIONS: Record<string, string> = {
  'fbneo-diagnostic-input': 'None',        // holding START must not open the service menu
  'fbneo-hiscores': 'disabled',            // cabinet hiscore files differ between devices
  'fbneo-allow-patched-romsets': 'disabled',
  'fbneo-frameskip-type': 'disabled',
  'fbneo-fixed-frameskip': '0',
  'fbneo-cpu-speed-adjust': '100%',
  'fbneo-samplerate': '48000',
  'fbneo-socd': '0',                       // opposing directions are cleaned server-side, statelessly
  'fbneo-lowpass-filter': 'disabled',
  'fbneo-force-60hz': 'disabled',          // on, it reads the host display's refresh rate
  'fbneo-fm-interpolation': '4-point 3rd order', // the default, pinned like every other sound option
  // Linear sample interpolation keeps less hidden resampler history (all of
  // which our FBNeo patch saves in states anyway).
  'fbneo-sample-interpolation': '2-point 1st order',
};

export const SYSTEMS: Record<SystemId, SystemDef> = {
  fbneo_cps12: {
    id: 'fbneo_cps12',
    label: 'Arcade (Capcom CPS-1 / CPS-2)',
    core: 'fbneo_cps12',
    maxPorts: 4,
    coinBit: PAD.SELECT,
    defaultAllowed: DPAD | bit(PAD.START, PAD.B, PAD.A, PAD.Y, PAD.X, PAD.L, PAD.R),
    options: FBNEO_OPTIONS,
    portDevices: {},
    romExtensions: ['zip'],
  },
  fbneo_neogeo: {
    id: 'fbneo_neogeo',
    label: 'Arcade (Neo Geo MVS)',
    core: 'fbneo_neogeo',
    maxPorts: 2,
    coinBit: PAD.SELECT,
    defaultAllowed: DPAD | bit(PAD.START, PAD.B, PAD.A, PAD.Y, PAD.X),
    options: { ...FBNEO_OPTIONS, 'fbneo-neogeo-mode': 'MVS_EUR' },
    portDevices: {},
    needsBiosFrom: 'board',
    romExtensions: ['zip'],
  },
  fceumm: {
    id: 'fceumm',
    label: 'NES / Famicom',
    core: 'fceumm',
    maxPorts: 4,
    coinBit: null,
    // B, A, SELECT, START + d-pad. X/Y are turbo and L3 is an A+B macro in FCEUmm.
    defaultAllowed: DPAD | bit(PAD.B, PAD.A, PAD.SELECT, PAD.START),
    options: {
      'fceumm_ramstate': 'fill $ff',
      'fceumm_up_down_allowed': 'disabled',
      'fceumm_turbo_enable': 'None',
      'fceumm_zapper_mode': 'disabled',
      'fceumm_overclocking': 'disabled',
    },
    // Ports 3/4 (Four Score) are enabled per game by portDevicesFor().
    portDevices: {},
    romExtensions: ['nes'],
  },
};

// Descriptor texts FBNeo uses for inputs that are not a single cabinet button.
const NOT_ONE_TO_ONE = /(3x|\bx3\b|macro|buttons [a-d]{2,}|diagnostic|service|\btest\b|reset|dip|volume)/i;

// Computes per-port allowed masks from the core's input descriptors. Falls
// back to the system default for ports without descriptors.
export function allowedMasks(sys: SystemDef, descriptors: { port: number; device: number; id: number; text: string }[], ports: number): number[] {
  const out: number[] = [];
  for (let p = 0; p < ports; p++) {
    const mine = descriptors.filter((d) => d.port === p && (d.device & 0xff) === 1 && d.id < 16);
    if (mine.length === 0) {
      out.push(sys.defaultAllowed);
      continue;
    }
    let m = 0;
    for (const d of mine) if (!NOT_ONE_TO_ONE.test(d.text)) m |= 1 << d.id;
    // Coins are server-controlled; START stays with the player.
    if (sys.coinBit !== null) m &= ~(1 << sys.coinBit);
    out.push(m & sys.defaultAllowed | (m & DPAD));
  }
  return out;
}

// Stateless SOCD cleaning: opposing directions cancel out. Applied by the
// server before a mask enters the authoritative record, so every replica sees
// the same cleaned input.
export function cleanSocd(mask: number): number {
  const lr = (1 << PAD.LEFT) | (1 << PAD.RIGHT);
  const ud = (1 << PAD.UP) | (1 << PAD.DOWN);
  if ((mask & lr) === lr) mask &= ~lr;
  if ((mask & ud) === ud) mask &= ~ud;
  return mask;
}

// FCEUmm's RETRO_DEVICE_GAMEPAD (RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)).
const FCEUMM_GAMEPAD = ((1 + 1) << 8) | 1;

// libretro device per port for a particular game. Four Score multitap
// emulation is switched on only for NES games set up for more than two
// players, because it changes what the game reads from the controller ports.
export function portDevicesFor(sys: SystemDef, ports: number): Record<number, number> {
  const out: Record<number, number> = { ...sys.portDevices };
  if (sys.id === 'fceumm' && ports > 2) {
    out[2] = FCEUMM_GAMEPAD;
    out[3] = FCEUMM_GAMEPAD;
  }
  return out;
}

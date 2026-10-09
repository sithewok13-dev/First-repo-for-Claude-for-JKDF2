// Adapter for the ATC test cabinet ROMs (native/testroms). Their RAM layout is
// ours and documented in native/testroms/atc-ram-map.md, so every capability
// here is verified by test/unit/atc-adapter.test.ts against the real ROM
// running in the real core.

import { PAD } from '../../shared/emu/core.ts';
import type { Adapter, AdapterEvent, AdapterInstance, AdapterMeta, GameStatus, PlayerStatus, RoomMode } from './types.ts';

const SIG = 0x300, GSTATE = 0x305, CREDITS = 0x308, ROUND = 0x309, WINNER = 0x30a, MATCHID = 0x30b, TURNP = 0x30c;
const PBASE = 0x310, CDOWN = 0x338;

// sha256 of native/testroms/build/atc-*.nes (pinned; rebuilt ROMs must match).
export const ATC_HASHES: Record<string, string> = {
  versus: '75e10859df5ec3895ae7e742bf9f11584d26ca87b2317a270e5aa5529eee3719',
  coop: '0e5e267b30e9e911d969b7763941334140b31d46a335e3e5c89102def55f4339',
  turns: '838cd2f865d3ea370c60f9e2c875d6f31af6599fc7ed1ed0b022ae1b5a44953c',
  solo: '4a0e19d5858f4d5aed270d0d47ba7e9bb2007906e3b775e94336f06baf995c25',
};

const MODES: { key: string; mode: RoomMode; players: number; title: string }[] = [
  { key: 'versus', mode: 'versus', players: 2, title: 'ATC Versus (test ROM)' },
  { key: 'coop', mode: 'coop', players: 4, title: 'ATC Co-op (test ROM)' },
  { key: 'turns', mode: 'turns-shared', players: 4, title: 'ATC Turns (test ROM)' },
  { key: 'solo', mode: 'single', players: 1, title: 'ATC Solo (test ROM)' },
];

function score(ram: Uint8Array, p: number): number {
  const o = PBASE + p * 8;
  return ram[o + 2] | (ram[o + 3] << 8) | (ram[o + 4] << 16);
}

function signatureOk(ram: Uint8Array): boolean {
  return ram[SIG] === 0x41 && ram[SIG + 1] === 0x54 && ram[SIG + 2] === 0x43 && ram[SIG + 3] === 0x31;
}

const PHASES: GameStatus['phase'][] = ['attract', 'playing', 'round-over', 'match-over', 'stage-clear'];

function makeInstance(players: number, modeKey: string): AdapterInstance {
  let prev: Uint8Array | null = null;
  return {
    step(frame, ram) {
      const events: AdapterEvent[] = [];
      if (!signatureOk(ram)) {
        prev = null;
        return events;
      }
      const cur = ram.slice(0x300, 0x350);
      if (prev) {
        const was = (a: number) => prev![a - 0x300];
        const now = (a: number) => cur[a - 0x300];
        const scores = Array.from({ length: players }, (_, p) => score(ram, p));
        for (let p = 0; p < players; p++) {
          const o = PBASE + p * 8;
          const a = was(o), b = now(o);
          if (a === b) continue;
          if (b === 1 && a === 0) events.push({ type: 'player_join', frame, port: p, scores });
          else if (b === 1 && a === 2) events.push({ type: 'player_continue', frame, port: p, scores });
          else if (b === 2 && a === 1) events.push({ type: 'player_game_over', frame, port: p, scores });
          else if (b === 0 && a === 2) events.push({ type: 'player_out', frame, port: p, scores });
        }
        if (was(MATCHID) !== now(MATCHID)) {
          const w = now(WINNER);
          events.push({ type: 'match_end', frame, winnerPort: w === 0xff || w === 0 ? null : w - 1, scores, data: { draw: w === 0xff } });
        }
        if (modeKey === 'versus' && was(GSTATE) === 1 && now(GSTATE) === 2) {
          events.push({ type: 'round_end', frame, scores, data: { round: now(ROUND) } });
        }
        if (was(GSTATE) !== 4 && now(GSTATE) === 4) events.push({ type: 'stage_clear', frame, scores, data: { stage: now(ROUND) } });
        if ((modeKey === 'coop' || modeKey === 'solo') && was(GSTATE) !== 3 && now(GSTATE) === 3) events.push({ type: 'game_over', frame, scores });
        if (modeKey === 'turns' && now(GSTATE) === 1 && (was(TURNP) !== now(TURNP) || was(GSTATE) !== 1)) {
          events.push({ type: 'turn_change', frame, port: now(TURNP), scores });
        }
      }
      prev = cur;
      return events;
    },
    status(frame, ram) {
      if (!signatureOk(ram)) return { frame, phase: 'unknown', credits: null, round: null, turnOwner: null, players: [] };
      const ps: PlayerStatus[] = [];
      for (let p = 0; p < players; p++) {
        const o = PBASE + p * 8;
        const st = ram[o];
        ps.push({
          phase: st === 1 ? 'playing' : st === 2 ? 'continue' : 'out',
          lives: modeKey === 'versus' || modeKey === 'turns' ? null : ram[o + 1],
          score: score(ram, p),
          continueSeconds: st === 2 ? ram[CDOWN + p] : null,
        });
      }
      const g = ram[GSTATE];
      let phase = PHASES[g] ?? 'unknown';
      if (g === 3 && (modeKey === 'coop' || modeKey === 'solo')) phase = 'game-over';
      return {
        frame,
        phase,
        credits: modeKey === 'turns' ? null : ram[CREDITS],
        round: ram[ROUND],
        turnOwner: modeKey === 'turns' && g === 1 ? ram[TURNP] : null,
        players: ps,
      };
    },
  };
}

export const ATC_ADAPTERS: Adapter[] = MODES.map((m) => {
  const meta: AdapterMeta = {
    id: `atc-${m.key}`,
    version: 1,
    title: m.title,
    system: 'fceumm',
    romSha256: [ATC_HASHES[m.key]],
    players: m.players,
    mode: m.mode,
    coinBit: m.key === 'turns' ? null : PAD.SELECT,
    startBit: PAD.START,
    creditModel: m.key === 'turns' ? 'free' : 'shared-pool',
    capabilities: {
      matchResult: m.key === 'versus' || m.key === 'turns',
      playerGameOver: m.key !== 'turns',
      stageBoundary: m.key === 'coop' || m.key === 'solo',
      turnOwner: m.key === 'turns',
      score: true,
      credits: m.key !== 'turns',
      admission: m.key === 'coop' || m.key === 'solo' || m.key === 'versus',
    },
    verification: 'test/unit/atc-adapter.test.ts',
  };
  return { meta, create: () => makeInstance(m.players, m.key) };
});


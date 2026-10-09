# ATC test cabinet — RAM map (adapter contract)

The ATC ("Arcade Test Cabinet") ROMs are tiny NES programs written for this
project (MIT licensed, see `LICENSE`). They exist so that the whole arcade —
lockstep emulation, seats, rotation rules, adapters, records — can be tested
end to end with lawful content. `server/adapters/atc.ts` reads these
addresses; `test/unit/atc-adapter.test.ts` verifies them against the real
ROMs in the real FCEUmm core. Any change to `atc.s` changes the ROM hashes
pinned in `server/adapters/atc.ts`, and the adapter must be re-verified.

| Address | Size | Meaning |
|---|---|---|
| `$0300` | 4 | Signature `ATC1` |
| `$0304` | 1 | Build mode: 0 versus, 1 co-op, 2 turns, 3 solo |
| `$0305` | 1 | Game state: 0 title/waiting, 1 playing, 2 round over, 3 match/game over, 4 stage clear |
| `$0306` | 2 | Frame counter (little endian) |
| `$0308` | 1 | Credits (shared pool, 0–9). SELECT inserts a coin (server-controlled) |
| `$0309` | 1 | Round (versus) / stage (co-op, solo) / turn round (turns) |
| `$030A` | 1 | Last match winner: 1–4 player, `$FF` draw |
| `$030B` | 1 | Match counter (increments when a match or turns game ends) |
| `$030C` | 1 | Turns: whose turn (0–3) |
| `$030D` | 1 | Turns: participants (2–4) |
| `$0310 + 8p` | 8 | Player p: +0 state (0 out, 1 playing, 2 continue countdown), +1 lives, +2..+4 score (24-bit LE), +5 round wins, +6 x, +7 y |
| `$0330 + p` | 1 | Game-over count |
| `$0334 + p` | 1 | Continues used |
| `$0338 + p` | 1 | Continue countdown seconds |
| `$0340 + p` | 1 | Versus hits this round |
| `$0344 + p` | 1 | Turns completed |

Rules per build:

* **Versus** (2P): coin + START joins. Each A press is a hit; first to 5 hits
  wins the round (simultaneous 5th hits = drawn round); first to 2 rounds wins
  the match; 5 rounds without a winner = drawn match. The loser (both on a
  draw) enters a 10 s continue countdown; the winner stays.
* **Co-op** (4P via Four Score): coin + START joins (also mid-game). A scores
  +10, B loses a life; 3 lives; at 0 lives the player enters a 10 s continue
  countdown (START with a credit continues, score kept), then is out. A stage
  boundary happens every 20 s of play. When nobody is in, game over.
* **Solo**: co-op rules with one player.
* **Turns** (one shared controller, pad 1 only): SELECT cycles 2–4
  participants on the title, START begins; A scores +10 for the current
  player, B ends the turn; 3 rounds; highest score wins (tie = draw).

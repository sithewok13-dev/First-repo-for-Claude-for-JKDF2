# Telegram Arcade — durable product specification

This file records the product owner's requirements so future sessions keep
them intact. Fixed decisions are marked **[fixed]** and must not be changed or
silently dropped. Implementation status lives in `PROGRESS.md`; the
architecture and its sources are in `ARCHITECTURE.md`.

## 1. Vision and fixed decisions

A Telegram bot + Mini App that turns a private Telegram group into a shared
virtual arcade cabinet for friends. Someone uploads a ROM to the group;
members open the arcade to play, spectate, text-chat and queue. The group
builds a persistent private game collection and votes on what to play next.
Pilot: the owner and friends; multiple independent groups later, never a
public gaming network.

- [fixed] iPhone, Android and PC.
- [fixed] Touchscreen, physical controller and desktop keyboard controls.
- [fixed] One active game per Telegram group.
- [fixed] Players share the SAME running game; spectators watch that same session.
- [fixed] Text chat only. No voice, microphone features or voice roadmap.
- [fixed] Persistent private game shelf per group.
- [fixed] Group voting controls game changes, with host/deputy override.
- [fixed] Server-authoritative player seats and waiting queue.
- [fixed] Fighters: winner stays, NO win-streak limit.
- [fixed] Co-op: rotate at the individual player's game over when someone is waiting; unlimited continues when nobody is waiting.
- [fixed] Single-player games with spectators and controller handoffs.
- [fixed] Native turn-based multiplayer and collaborative single-player pass-around.
- [fixed] Persistent scores and records within each group.
- [fixed] No global leaderboards, public matchmaking or cross-group social features.
- [fixed] Fair controls: no app-provided turbo, automated combos or one-button special moves.
- [fixed] Host can appoint deputies.
- [fixed] The room keeps running when the host or ROM uploader leaves.
- Initial focus: arcade fighters and side-scrolling beat 'em ups, plus a deliberately small set of console games.

## 2. Telegram presentation [fixed]

- The game opens in a Telegram Mini App panel launched from the group, expandable to full screen where supported. It is NOT a playable canvas embedded in the message stream.
- Flow: upload ROM to group → bot validates and posts a game card/status → members tap the room's launch link/button → shared arcade (play, watch, chat, vote, queue).
- Gameplay chat lives inside the Mini App, separate from the group thread. Only useful, rate-limited announcements go back to the group.
- Launch mechanisms must be verified for groups on current clients (not every Mini App button type works in groups).
- If an essential feature cannot work reliably in a Telegram client: document the verified limitation and offer an authenticated external-browser fallback. Never silently replace the requested experience.

## 3. Hosting and architecture

Delegated to engineering: evaluate cloud vs Mac mini vs hybrid; pick a pilot
default on responsiveness, reliability, cost, deployment effort, maintenance.
The uploader's/room creator's device must not be required to sustain the
game. Compare server-side streaming vs synchronized client-side emulation
(latency, compatibility, sync, spectators, handoff, persistence, cost,
licensing). Record sources; separate verified from assumed. Cost estimates
with explicit assumptions; no purchases or paid usage without authorization.

## 4. Prove gameplay first

Vertical slice before the full lobby: real supported game; two independent
players in the same game; a read-only spectator; simultaneous input and
consistent shared state; seat transfer without restart; room creator
disconnects without ending the session; touch and physical controller input
on available devices; measured responsiveness and spectator delay. Lawful
test content or owner-supplied ROMs only; never bundle commercial ROMs or
proprietary BIOS.

Initial compatibility set (incremental): a 2-player fighter, a 2-player beat
'em up, a 4-player beat 'em up, a single-player game for handoff, a
shared-controller turn-based game. Track separately: boot/playability, input
per platform, simultaneous multiplayer, spectating, seat handoff, disconnect
recovery, save-state compatibility, automatic match/turn/game-over detection,
score attribution. Set measurable latency/stability targets before testing;
report measurements with conditions and sample sizes; distinguish estimated
from measured. Never report unperformed tests as passed.

## 5. Identity and group access

Telegram identity verified on the server. Never trust client-supplied user
ids, group ids, roles, launch parameters or claimed membership. Bind access
to the correct room; forwarded links must not grant access; verify the
membership-check mechanism; do not assume launch data proves membership;
recheck on role/membership change; prevent cross-group access to games, chat,
records, sessions; handle expired sessions and duplicate updates idempotently;
handle bot removal, permission changes, group migrations; request only
necessary bot permissions (documented). Use stable Telegram user ids, never
display names.

## 6. Shelf and uploads

Entries: display name + editable metadata; platform/core and player count;
uploader and date; favorites and recently played; compatibility status
(untested / working / needs attention); useful validation details; lawful
artwork or a placeholder. Rules: uploading never interrupts the active game;
uploads become a shelf entry or pending-validation entry; existing entries can
be nominated without re-upload; deduplicate identical uploads without
revealing another group's collection; distinguish ROM revisions and
dependencies; preserve arcade archive structure; identify missing
BIOS/dependencies accurately; never silently download proprietary
dependencies; configurable upload/storage/extraction/processing limits;
isolate validation and emulation from the app server; protect against archive
bombs, path traversal, malformed files and emulator compromise; private ROM
storage, no public catalog or public download URLs; host/deputies can remove
entries; removing the active game must not crash a session (defer cleanup);
pin files needed by active sessions and compatible checkpoints. Verify
Telegram file limits; fallback upload path only if necessary and clearly
explained. Document retention, deletion, backup, quotas.

## 7. Seats and queue (server-authoritative)

One playing seat per person; at most one queue entry per person; a seated
player cannot also reserve a future seat; multiple tabs/devices must not
create duplicate seats or votes; spectators cannot send input; leave the
queue without leaving the room; joining the queue never interrupts players;
show players, queue order and who is next; offer the next seat with a
configurable acceptance countdown; skip unresponsive candidates without
stalling; fair re-entry for skipped candidates; atomic concurrent seat claims;
configurable disconnect grace period; revoke old input authority immediately
on seat change; clear held buttons on disconnect, focus loss or handoff;
reject delayed input from former owners; transfer the existing player slot
without resetting teammates' progress. Separate voluntary departure,
connectivity failure, game defeat and moderation removal; never record every
disconnect as a competitive loss.

## 8. Fighters

[fixed] Winner stays, no streak limit; loser to the back of the queue; with
nobody waiting the same players rematch indefinitely; rotate after a complete
match, not each round. Define draws/double KOs, disputed results, abandoned
matches, disconnects mid-match, character-select/rematch screens, no-show
challengers. Automatic results need verified game-specific support, else
clearly labelled manual reporting with host/deputy adjudication. A host must
not be able to remove a player merely for winning repeatedly.

## 9. Simultaneous co-op

[fixed] With someone waiting, a player rotates out at their individual game
over (not each life); surviving teammates keep playing; the replacement enters
the vacated slot and continues the run where supported; with nobody waiting,
continues are unlimited; a new queue entry must not eject someone mid-life.
Control credits/continues so nobody can bypass the queue; distinguish personal
credits from shared pools; validate per game; disclose limitations and use an
explicit supported transition when seamless replacement is not possible.

## 10. Single-player spectating and pass-around

Default single-player queue mode: one controller; spectators may queue;
rotate at complete game over; unlimited continues when nobody waits; "Pass
controller"; offer control to the next queued person; preserve state. Games
without clear game-over: voluntary handoff or optional room-approved timed
rotation. Collaborative mode: one controller, everyone discusses in chat,
control passes at agreed points, handoff rule shown, never conflicting inputs,
never labelled as one player's solo achievement.

## 11. Turn-based

Both separate-controller and shared-controller games. Maintain identities and
turn order (separate from the spectator queue); completing a turn does not
remove anyone; shared controller → input only for the current participant;
admit spectators only at a supported admission point or explicit replacement;
"End turn"/"Pass controller" when detection is unavailable; recipient must
accept; handle rejection, timeout, disconnect, duplicates; host/deputies
resolve stuck turns; never make irreversible decisions for absent players.
Automatic turn detection only with a verified adapter.

## 12. Adapters

Versioned adapters tied to ROM hashes, core version and configuration; may
expose verified match results, individual game over, stage boundaries, turn
ownership, scores, admission points, credit/continue behaviour. Idempotent
event processing. Manual controls + moderation where unsupported; honest
capability labels; console rules explicit (not arcade assumptions everywhere).

## 13. Voting and changing games

Nominate shelf games; one changeable vote per eligible member; "Keep
playing" option; merge duplicate nominations; show electorate, deadline and
rules; define presence, quorum, late arrivals, departures, ties, expiry;
separate preference from authorization; no small-plurality forcing; default
to keep when inconclusive; rate-limit nominations and switch attempts. After
approval: announce; finish the current fighting match; co-op at a verified
stage boundary or announced manual transition; single/turn at a handoff
point; never wait indefinitely; authorized cancel/override with visible
reason; validate the next game before terminating the current one; recover
on launch failure without losing room or queue. Ask who wants to play next;
preserve relative priority of waiting players; define where current players
enter the new queue.

## 14. Host, deputies, succession

Host is a moderation role (not an emulator host). Host appoints/removes
deputies (session-only or persistent; multiple; configurable succession).
Deputies may resolve stuck queues/handoffs, moderate, reset a frozen game,
override votes; may not remove the host, appoint deputies, jump the queue, or
evict for winning. Map Telegram owner/admin authority deliberately and verify
server-side. Host departure: everything continues; 60 s reconnection grace
before acting-host succession; deputies keep powers meanwhile; prefer a
designated deputy, then a present group admin, then optionally a willing
established participant with limited temporary powers; else automated rules
continue; returning host must not reset or disrupt. Temporary moderation ≠
persistent authority (no permanent ownership, unrestricted file access, or
deputy rewriting). Log role changes and moderation; resolve conflicting
commands deterministically.

## 15. Controls and fairness

Multi-touch joystick/D-pad and buttons; physical controllers where verified;
keyboard; per-user saved mappings and touch layouts; movable/resizable
controls; safe areas; per-game layouts; controller test screen; controller
connect/disconnect/reconnect; local volume/display preferences. No turbo,
combos, one-button specials or multi-action macros; one-to-one mappings by
default; no promise of detecting external hardware macros. Chat typing must
not control the game; clear stuck inputs on focus change, chat, app switch,
connectivity loss, seat transfer. Handle keyboard rollover, browser shortcuts,
mobile gesture conflicts, controller mappings. Don't obscure or needlessly
shrink the playfield.

## 16. Spectating and chat

Shared text chat for players and spectators; no voice; beside/below the game;
hide/mute and unread indicators; no messages over the action by default; show
players, queue, game state, pending votes; distinguish roles; ordinary
conversation stays in the Mini App; only useful rate-limited announcements to
the group; spam controls, safe rendering, access control, modest retention,
moderation; account for spectator delay so results are not spoiled; pilot
spectator limit and capacity policy that protects player responsiveness.

## 17. Records

Group-private records: arcade scores (player, game, score, date); fighting
wins, losses, longest streak; individual co-op scores when attributable;
team/shared-run records; single-player records. Separate verified vs manual.
Never merge across ROM revisions, relevant core changes, score-affecting
settings, fresh vs resumed runs, continue vs no-continue runs. Track control
ownership over time; replacements do not get the whole score; label shared
runs/seat scores when attribution is impossible. Keep the emulated cabinet's
own high-score table separate from app records. Deduplicate after
reconnect/recovery; keep adapter/config metadata; logged corrections;
display-name changes do not split history.

## 18. Lifecycle and recovery

Define players leaving with spectators remaining, everyone leaving, host
leaving, calls/backgrounding/screen lock/network change, emulator crash,
server restart, deployment interruption, corrupt/incompatible checkpoints,
full storage/resource exhaustion. When everyone leaves: pause, keep a
compatible checkpoint, release resources after an idle period, offer Resume
or Start fresh. Checkpoints tied to ROM/core/config; no universal portable
saves; resumed runs classified for records. Persisted authoritative state and
idempotent transitions; no duplicate emulator sessions, phantom seats,
duplicate records or stale input after recovery. Limits for rooms,
spectators, storage, idle sessions, compute; documented pilot capacity.

## 19–21. Interface, engineering, deliverables

Tasteful retro look with modern usability: cabinet (game, players, controls,
chat, queue), shelf, voting, records, personal controls/settings, compact
host/deputy controls. Fast entry, readable mobile layouts, landscape play,
usable portrait browsing/chat. Truthful loading/compatibility/failure states;
never fake connectivity or scores. Maintainable dependencies with licenses.
Group isolation, secrets, authenticated real-time connections, process
isolation, migrations, backups/restore, health checks, bounded logs, resource
limits; never log tokens, ROM contents or unnecessary personal data.

Order: A research/architecture → B real multiplayer slice → C identity +
shelf → D queue, adapters, voting, roles, chat → E single/turn modes, records,
recovery, limits → F packaging and docs. Don't polish the lobby before
gameplay works; stage and disclose remaining work.

Tests: concurrent seat claims and queue fairness; former-player input
rejection; group isolation and role authorization; vote outcomes and failed
transitions; host/deputy succession; match/turn/game-over dedup; shared-score
attribution; malicious uploads; disconnect/crash recovery.

Deliverables: source; reproducible local setup; deployment config; env
template; migrations; backup/restore; architecture decisions with sources;
compatibility matrix (verified vs untested); test evidence and limitations;
cost estimates; exact steps to start a private session; this spec and a
progress log.

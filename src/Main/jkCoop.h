// Added: entire file. Co-op campaign play (the host's "Episode Co-op": a
// single-player episode played by up to four, MULTIMODEFLAG_COOP), the parts
// the engine's own co-op lacks. Everything here does nothing outside co-op.
//
// - Between levels: the level's tally, and the Force powers screen, go on by
//   themselves after a while, so one player can't keep the others waiting to
//   join the next level (stars not spent stay to spend next time).
// - Gear between levels: each player keeps, on their own machine, what they
//   carried at the end of a level (weapons, ammo, items, Force powers and
//   stars; not keys, goals or what a level counts, which the game drops
//   anyway, nor what a cheat gave) into the next one, as one player does.
//   That kit, as the level began, is also what a player comes back with after
//   dying in co-op.
// - Starting spots: everyone starts (and comes back) where the one player
//   would; one already standing there, the next is put beside them (on the
//   floor, no wall between) rather than inside them.
// - The host's level: a guest whose campaign went another way (Jedi
//   Knight's light or dark path is each player's own choice) is told by the
//   host which level it is on when joining, and loads that one instead.

#ifndef _MAIN_JKCOOP_H
#define _MAIN_JKCOOP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QOL_IMPROVEMENTS

#define JKCOOP_TALLY_WAIT_MS (30000)
#define JKCOOP_FORCE_WAIT_MS (90000)

// In a co-op game now
int jkCoop_IsActive(void);

// The level has ended (its Force power given): the local player's gear kept
void jkCoop_LevelEnded(void);
// The multiplayer game left (sithMulti_Shutdown): nothing kept for another game
void jkCoop_SessionEnded(void);
// A multiplayer level has been opened
void jkCoop_LevelOpened(void);
// Once a frame in a level
void jkCoop_Tick(void);

// A player just put at their starting spot (sithPlayer_SpawnPlayerAt's
// placement): moved beside anyone already standing there, in co-op
void jkCoop_PlacePlayer(SithThing* pPlayer);

// A guest's join answered "not this level" by a co-op host that said which
// level it is on (sithMulti_ProcessJoinRequest): kept, 1 if so
int jkCoop_HostLevelFromJoining(SithMessage* pMsg);
// After that join gave up: the host's level made the one to load (the
// episode put there too), 1 if so; the caller then loads it
int jkCoop_FollowHostLevel(void);

// A menu about to be shown that, in co-op, goes on (as its OK) by itself after
// msWait; its own idle function, if any, still runs. Call jkCoop_MenuDone
// after it, which puts the menu back as it was.
void jkCoop_MenuAutoContinue(jkGuiMenu* pMenu, uint32_t msWait);
void jkCoop_MenuDone(jkGuiMenu* pMenu);

#endif // QOL_IMPROVEMENTS

#ifdef __cplusplus
}
#endif

#endif // _MAIN_JKCOOP_H

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
// A multiplayer level has been opened
void jkCoop_LevelOpened(void);
// Once a frame in a level
void jkCoop_Tick(void);

// A player just put at their starting spot (sithPlayer_SpawnPlayerAt's
// placement): moved beside anyone already standing there, in co-op
void jkCoop_PlacePlayer(SithThing* pPlayer);

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

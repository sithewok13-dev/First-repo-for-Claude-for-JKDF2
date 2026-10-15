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
//   dying in co-op. A level in which the player used cheats (when the host
//   allows them) adds nothing to it: they carry on what they began it with,
//   and the Force power the story gives at its end.
// - Characters: the host plays the story's own player (the level's: Kyle,
//   Mara), the guests the multiplayer characters they picked.
// - Player starts: a story level made for one player gets a start for each
//   of four (as its own), so everyone can join.
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
// Co-op, and this game the host: it plays the story's own player (the level's), not the multiplayer
// character picked (the guests play theirs)
int jkCoop_HostKeepsProtagonist(void);

// A player's gear, bin by bin (as carried between levels)
typedef struct jkCoopKitBin
{
    flex_t amount;
    int32_t state; // (SITHINVENTORY_ITEM_AVAILABLE, _FOUND)
} jkCoopKitBin;

// The kit this player carries (this level's start, or the last level's end), 0 if none
int jkCoop_GetKit(jkCoopKitBin aOut[SITHBIN_NUMBINS]);
// A kit from elsewhere (the co-op save, the host): carried as if from the last level, given at once if
// already in the level
void jkCoop_SetKit(const jkCoopKitBin aKit[SITHBIN_NUMBINS]);

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

// The co-op host may pick a level for everyone (cheats allowed, as sithewok's level pick)
int jkCoop_HostMayPickLevel(void);
// The co-op host picked the episode's entry: everyone goes there (the level ends for all; the host
// then loads that entry, and the guests follow the host's level); 1 if so
int jkCoop_HostPickLevel(int entryIdx);
// At the level's end (jkMain_EndLevel): the entry the host picked, -1 if none (taken once)
int jkCoop_TakePickedLevel(void);

// A level loading (sithPlayer_PlacePlayers, its player starts found: numStarts): in a co-op game, a
// level with fewer starts than JKCOOP_MAX_PLAYERS gets more, as its first (every game in the co-op
// does the same as it loads, so the new players are the same things everywhere); the number now
#define JKCOOP_MAX_PLAYERS 4
int jkCoop_AddPlayerStarts(SithWorld* pWorld, int numStarts);

// The game's flags as a co-op host sends them: its difficulty in them (MULTIMODEFLAG_COOPDIFF_MASK)
int jkCoop_HostGameFlags(int flags);
// A multiplayer level about to load: in co-op, a guest loads it at the host's difficulty (which
// decides what the level has: everyone's must be the same things); its own is put back when the game
// is left
void jkCoop_UseHostDifficulty(void);

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

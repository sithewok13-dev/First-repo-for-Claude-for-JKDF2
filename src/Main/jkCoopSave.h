// Added: entire file. Co-op campaign saves, at the start of each level: the
// host keeps where the campaign is (the episode, the level) and every
// player's gear as the level began, by each game's own random player id
// (made once, kept in its settings). Hosting co-op on an episode with a save
// carries it on (unless the host's "resume" setting is off): the saved level
// first, and everyone's gear as saved, a player who rejoins included.
//
// One file per episode, coop/<episode>.txt in the game's folder, nothing to
// do with single player's saves. Written to a new file and then put in place
// (the one before kept as .bak), so a save is never half written.

#ifndef _MAIN_JKCOOPSAVE_H
#define _MAIN_JKCOOPSAVE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QOL_IMPROVEMENTS

#define JKCOOPSAVE_DIR "coop"

void jkCoopSave_Startup(void);
// A level about to be loaded (jkMain_loadFile2): the episode noted; a co-op host resuming has the
// saved level loaded instead (*ppLevel changed, pointing at a name kept here)
void jkCoopSave_LevelLoading(const char* pEpisodeGob, char** ppLevel);
// A co-op level opened (after jkCoop's): a resumed host's own gear made its kit
void jkCoopSave_LevelOpened(void);
// This player's kit as the level began has been taken (jkCoop): sent to the host, or, the host's,
// saved. bCarried: the kit came from the last level (not the level's own start: a guest who joins
// with nothing gets theirs back from the host)
void jkCoopSave_KitTaken(int bCarried);
// Once a frame in a level
void jkCoopSave_Tick(void);
// Anything not yet written, written now (the level or the game ending)
void jkCoopSave_Flush(void);
// The resumed level isn't in the episode (jkMain_loadFile2): not resumed
void jkCoopSave_ResumeFailed(void);

#endif // QOL_IMPROVEMENTS

#ifdef __cplusplus
}
#endif

#endif // _MAIN_JKCOOPSAVE_H

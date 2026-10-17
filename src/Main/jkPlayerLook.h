// Added: entire file. In multiplayer, each player's saber as they chose it
// (the iOS overlay's SABER wheel: one or two blades or the crossguard, each blade's colour), so
// the others see it: each game sends its own player's every few seconds
// (jkNetExt, JKNETEXT_LOOK), and keeps the others' as they come. Colours go
// by their keys ("lightblue"), not their places in a list, so builds with
// other lists still agree; one this game doesn't have is the game's own.
//
// Every build with the enhanced gameplay (JK_ENHANCED: iOS, and the desktops
// by default) sends its player's choice, however it was made (the iOS
// wheel, the enh_saber* cvars). A build without it sends nothing, and the
// others draw its player's saber as the game does.

#ifndef _MAIN_JKPLAYERLOOK_H
#define _MAIN_JKPLAYERLOOK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QOL_IMPROVEMENTS

#define JKPLAYERLOOK_KEY_LEN 16
#define JKPLAYERLOOK_MAX_BLADES 2
#define JKPLAYERLOOK_SEND_MS (3000)  // (each game's own, this often)
#define JKPLAYERLOOK_KEEP_MS (10000) // (one not heard from this long: forgotten)

typedef struct jkPlayerLook
{
    int style;                                                   // (the wheel's, iosSaber's: 0 one blade, 1 two, 2 the crossguard;
                                                                 // one a game doesn't know it draws as one blade)
    char aColourKey[JKPLAYERLOOK_MAX_BLADES][JKPLAYERLOOK_KEY_LEN]; // ("" the game's own)
} jkPlayerLook;

void jkPlayerLook_Startup(void);
// Once a frame in a level
void jkPlayerLook_Tick(void);
// Another player's look (a player thing of someone else's game), NULL if none
// known: drawn as the game does
const jkPlayerLook* jkPlayerLook_ForThing(SithThing* pThing);

// This game's own player's look, from whoever offers one (iosSaber); 0 if none
int jkPlayerLook_GetLocal(jkPlayerLook* pOut);

#endif // QOL_IMPROVEMENTS

#ifdef __cplusplus
}
#endif

#endif // _MAIN_JKPLAYERLOOK_H

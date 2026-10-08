// Added: entire file. Game-side helpers for the iOS touch overlay.
//
// iosTouch.m can't include engine headers (types.h's BOOL clashes with
// Objective-C's), so whatever it needs to know about or do to the game goes
// through these plain C functions.

#ifndef _PLATFORM_IOS_IOSGAME_H
#define _PLATFORM_IOS_IOSGAME_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Short name of the player's currently selected force power ("SPEED",
// "LIGHTNING"...), or NULL if there is no player or no power selected yet.
const char* iosGame_GetForcePowerName(void);

// Loads quicksave.jks: in place if it was saved on the current level, through
// the normal level load (as the Load menu does) if it's from another level.
// Prints a message either way. Returns 1 if a load was started.
int iosGame_QuickLoad(void);

// Force powers the player can select right now, in the game's own order
// (JK: Jump ... Deadly Sight). Fills up to maxBins inventory bin numbers and
// returns how many.
int iosGame_GetForcePowers(int* aBins, int maxBins);

// Short name of a force power bin ("SPEED"...), or NULL if it isn't one.
const char* iosGame_GetPowerName(int bin);

// The selected force power's bin, or -1.
int iosGame_GetCurPower(void);

// Selects a force power, as the next/previous power keys do.
void iosGame_SelectPower(int bin);

// Whether the player has an item (bin) to use right now; also its count and
// whether it is switched on (field light, IR goggles).
int iosGame_GetItem(int bin, int* pAmount, int* pActive);

// Makes an item the selected one, so the use-item key uses it.
void iosGame_SelectItem(int bin);

// While set, gameplay holds still (single player only): the world is drawn
// but not updated. For the touch overlay's force wheel.
void iosGame_SetHold(int bHold);

// Called once per gameplay tick: returns 1 if this tick should not update the
// world (and keeps the game clock paused meanwhile).
int iosGame_HoldGameplay(void);

// Whether gameplay is being held right now (drawing still runs: what it
// animates on its own, like the weapon in view, should stay still too).
int iosGame_IsHolding(void);

// Opens the typing line (where cheats go), or closes it if it is open.
void iosGame_ToggleChat(void);

// How full the force meter is, 0..1 (*pbFull: as full as it gets right now),
// or -1 when there is no player.
float iosGame_GetForceMana(int* pbFull);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSGAME_H

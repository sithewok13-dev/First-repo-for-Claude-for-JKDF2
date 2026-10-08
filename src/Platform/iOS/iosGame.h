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

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSGAME_H

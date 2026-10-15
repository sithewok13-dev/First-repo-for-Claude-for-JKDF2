// Added: entire file. Saber Throw with two sabers (iosSaber's DUAL), in
// Mysteries of the Sith: each use of the power (FORCE, as the game's use-force
// key) throws the saber in hand, the right one first, then the left at least
// IOSSABERTHROW_STAGGER after it, on a slightly turned path. Each costs the
// power's own Force; Mara fights on with the one left in the meantime (drawn
// as one saber, in its own colour), and each comes back to its own hand.
//
// The throw itself is the game's (force_saberthrow.cog), done here for each
// saber: its rules (the saber in hand and not being drawn, the Force, not in
// a bubble, not while its own throw is under way), cost, disturbance, sound and
// thrown saber (+fsab_..., whose blow makes the returning saber,
// +fsaberreturns, which gives the saber back when caught); and, as its timer
// does, the saber back after a while however it went. Only with both thrown
// is the lightsaber out of the inventory, with Mara on her fists, as the
// game's own throw leaves her. One saber: the game's throw, unchanged.
//
// What is out and where is in memory only: a new level, a load or a new
// player puts both sabers back in hand (the inventory is the game's, saved).

#ifndef _PLATFORM_IOS_IOSSABERTHROW_H
#define _PLATFORM_IOS_IOSSABERTHROW_H

#ifdef TARGET_IOS

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOSSABERTHROW_STAGGER 0.25 // (seconds between the two sabers' throws, at least)

// The use-force key with Saber Throw selected (sithInventory's power keys),
// for the local player: 1 if it is this throw's (two sabers), done or
// refused; 0 for the game's own (one saber, or not Mysteries of the Sith)
int iosSaberThrow_Activate(SithThing* pPlayer);

// Once a frame: a second throw due, where each thrown saber is, and the
// sabers back after a while
void iosSaberThrow_Tick(void);

// The lightsaber given back (sithInventory_SetInventory, its bin, more than
// none): a saber caught, back in its own hand
void iosSaberThrow_SaberGiven(void);

// Which sabers are in hand: bit 0 the right (the first blade), bit 1 the left
// (3: both, as when none was thrown)
int iosSaberThrow_HandsInHand(void);

// Whether a saber is out (thrown, or coming back)
int iosSaberThrow_AnyOut(void);

// Everything back in hand, nothing out (a new level, a load, the cheat's
// saber taken away)
void iosSaberThrow_Reset(void);

// A thrown saber of the local player's, as it is drawn: the colour (the
// SABER wheel's id) it is thrown in, or -1 to leave it as the game made it
int iosSaberThrow_ThrownColour(SithThing* pThing);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSSABERTHROW_H

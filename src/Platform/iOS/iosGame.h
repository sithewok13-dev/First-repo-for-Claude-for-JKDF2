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
// Prints a message either way. Returns 1 if a load was started. A weapon
// picked on the touch overlay's wheel and still waiting is dropped.
int iosGame_QuickLoad(void);

// Whether Mysteries of the Sith is running (it has its own force powers).
int iosGame_IsMots(void);

// Whether the player can select this force power (bin) right now: learned,
// and allowed by rank and path. The test the next/previous power keys make.
int iosGame_IsPowerAvailable(int bin);

// A force power's level, 0-4: its bin's amount (the stars its Force screen
// shows), whole stars only. 0 if there is no player.
int iosGame_GetPowerLevel(int bin);

// Short name of a force power bin ("SPEED"...), or NULL if it isn't one.
const char* iosGame_GetPowerName(int bin);

// The selected force power's bin, or -1.
int iosGame_GetCurPower(void);

// Selects a force power, as the next/previous power keys do.
void iosGame_SelectPower(int bin);

// Short name of a weapon bin ("BRYAR PISTOL"...; Mysteries of the Sith's are
// bins 121-140), or NULL if it isn't one.
const char* iosGame_GetWeaponName(int bin);

// Whether the player has this weapon: its bin isn't empty (thermal
// detonators' and sequencer charges' bins are their count) and it is
// available. *pAmmo: what the HUD's ammo counter would show with it in hand
// (the same ammo bin, whole and at least 0), -1 for none (fists, lightsaber);
// *pbSelectable: whether selecting it would be taken -- the weapon cog's
// answer to the game's ammo question (as sithWeapon_SelectWeapon asks it) is
// not "no". All 0 / -1 if there is no player or it isn't a weapon.
int iosGame_GetWeapon(int bin, int* pAmmo, int* pbSelectable);

// The weapon in hand -- or the one it is changing to (a switch under way, or
// a pick waiting, see iosGame_SelectWeapon), or -1 if there is no player.
int iosGame_GetCurWeapon(void);

// Selects a weapon as its number key would, once the game would take that
// key: not while a weapon mounts or another switch is under way (the keys are
// ignored then), so the pick waits, trying again before every gameplay tick,
// for up to 3 s of game time. The weapon a switch under way is putting away
// can be picked too: once the switch is done, the game switches back. It is
// dropped if the weapon can't be selected any more, or the game clock goes
// back (a game loaded). Mysteries of the Sith: asks the level's PLAYERACTION
// cog first, as the key does, and is remembered as that key's weapon.
// Returns 1 if it was taken or is waiting.
int iosGame_SelectWeapon(int bin);

// Drops a pick still waiting (NEXT WPN tapped, the overlay reset, QUICK LOAD).
void iosGame_CancelWeaponPick(void);

// Whether the player has an item (bin) to use right now; also its count and
// whether it is switched on (field light, IR goggles).
int iosGame_GetItem(int bin, int* pAmount, int* pActive);

// Makes an item the selected one, so the use-item key uses it.
void iosGame_SelectItem(int bin);

// While set, gameplay holds still (single player only): the world is drawn
// but not updated. For the touch overlay's force and weapon wheels.
void iosGame_SetHold(int bHold);

// Called once per gameplay tick: returns 1 if this tick should not update the
// world (and keeps the game clock paused meanwhile). A tick that does update
// it first hands the game a weapon picked on the wheel, if it would take it.
int iosGame_HoldGameplay(void);

// Whether gameplay is being held right now (drawing still runs: what it
// animates on its own, like the weapon in view, should stay still too).
int iosGame_IsHolding(void);

// Opens the typing line (where cheats go), or closes it if it is open.
void iosGame_ToggleChat(void);

// How full the force meter is, 0..1 (*pbFull: as full as it gets right now),
// or -1 when there is no player.
float iosGame_GetForceMana(int* pbFull);

// Frames the game has drawn so far (it goes back to 0 when the video mode is
// set again). For the touch overlay's FPS readout.
unsigned int iosGame_GetFrameCount(void);

// Whether the game's Always Run option (Setup > Controls > Options) is on.
// Both games run while it is on, or while the run key (Shift) is held. It
// can change in the options menu, so the touch overlay reads it every frame.
int iosGame_IsAlwaysRun(void);

// How far one count of mouse movement turns the view (*pTurn: degrees right
// per count right) and tilts it (*pPitch: degrees down per count down), as
// the game's mouse look bindings have it (Setup > Controls > Mouse:
// sensitivity, reverse); 0 for an axis with nothing bound. Lets the touch
// overlay's gyro aiming turn the view by an exact angle. Read every frame:
// the bindings can change in the menu.
void iosGame_GetMouseLookDegrees(float* pTurn, float* pPitch);

// At launch, before the data folder is chosen: picks which game to run. With
// Jedi Knight in Documents/jk1 and Mysteries of the Sith in Documents/mots,
// it asks (the last choice first); with only one of them, that one; with
// neither, Jedi Knight (whose missing-files message then shows). Sets
// Main_bMotsCompat and openjkdf2_bOrigWasDF2 to match, and makes both
// folders, so both show in the Files app.
void iosGame_ChooseStartupGame(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSGAME_H

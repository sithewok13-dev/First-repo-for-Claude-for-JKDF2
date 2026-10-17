// Added: entire file. A hardware keyboard and a mouse or trackpad (an iPad's
// Magic Keyboard, a Bluetooth or USB mouse; on an iPhone, through
// AssistiveTouch).
//
// SDL already turns them into the game's own keys and mouse (GCKeyboard and
// GCMouse underneath): the keyboard keys and mouse buttons as Setup >
// Keyboard and Setup > Mouse bind them, the mouse turning the view while the
// game has the pointer (relative mouse mode: iOS hides the pointer and locks
// it, where it allows that, and the mouse's own movement still comes through
// where it doesn't). This adds what makes them play as well as touch and a
// pad do:
//   - Pointer: the game holds the pointer during play, and lets it go (shown
//     again, free to move) in the menus, while the app is in the background,
//     while a photo mode is up (a selfie, Action Freeze, the cinematic burst,
//     iOS asking for Photos access) and while MENU's tray or a game
//     controller's options are open -- where a click works the touch
//     controls as a tap would. Back in play, it takes it again.
//   - The touch controls hide while the keyboard or mouse is played, and
//     come back at a touch, as for a pad (iosTouch: the HUD hidden for an
//     outside controller); a click never counts as a touch there.
//   - The mouse wheel switches weapons (down: the next, up: the previous)
//     while nothing is bound to it in Setup > Mouse (the iOS defaults leave
//     it free; a player's own binding wins).
//   - The mouse's movement goes on in fractions (a trackpad's slow drift
//     turns the view too), and none of it reaches the game while a wheel,
//     a photo mode or a panel has it.
//   - Wheels, as the pad's: the key bound to the next or previous Force
//     power (E, Q) or weapon (G, ...), tapped, does what it is bound to
//     (once it is let go); held 0.3 s, it opens the Force or weapon wheel
//     (the game holds still). The mouse points at a slice (a stick here: the
//     way it last moved, held at the rim); letting go of the key picks it;
//     with nothing pointed at, the wheel stays open: a click picks, the
//     scroll wheel steps through it, a right click or Escape closes it.
//     L opens the saber wheel (once there is a saber): a click sets a
//     colour, 1 the next style (one saber, two, the crossguard), 2 which
//     blade, 3 takes the saber.
//   - Shortcuts for what is touch-only, where nothing else is bound to the
//     key (Setup > Keyboard can take any of them): H first or third person
//     (as VIEW; the game's own camera key F1 too), P a photo (as CAMERA;
//     held 0.5 s, the selfie), O Action Freeze on and off (P takes its
//     photos, Escape or O ends it), F10 held 0.3 s quick load (its ring
//     fills meanwhile; quick save is the game's own F9).
//   - Cmd held: no key reaches the game (Cmd-Tab and the other iPad
//     shortcuts don't open the map); Cmd-. is Escape, for keyboards
//     without one.
//   - Keys the game shouldn't see right now (a wheel or photo mode came up
//     while they were held) are ignored until let go.
// Everywhere else (menus, cutscenes, the typing line) keys and the mouse are
// passed on as they are.
//
// For finding out what an iPhone does with a mouse: with the FPS readout on
// (MENU's tray) and a keyboard or mouse connected, a line under it says
// what the app sees (iosKbMouseView): MOUSE / KEYS connected, whether the
// game wants the pointer locked and iOS has locked it, how much relative
// movement came in, and whether a click reached the touch controls as a
// pointer click or as a finger.

#ifndef _PLATFORM_IOS_IOSKBMOUSE_H
#define _PLATFORM_IOS_IOSKBMOUSE_H

// On in the iOS build, and with the enhanced input (ENH_INPUT: the desktops
// and Android: the same keys, wheels and shortcuts, the wheels drawn with
// the game's GL; see Platform/Input/inputUi.h). The iOS mouse bindings
// (IOSKBM_IOS_BINDS, below) are iOS's only: the desktops keep the game's
// own mouse defaults (the right button jumps, the wheel looks up and down;
// the wheel switches weapons there once nothing is bound to it). IOSKBM_RIG
// (the Linux test rig only) turns on the engine's hooks; the rig builds
// iosKbMouse.c itself with TARGET_IOS defined and stands in for the overlay
#if defined(TARGET_IOS) || defined(IOSKBM_RIG) || defined(ENH_INPUT)
#define IOSKBM_ENABLED 1
#endif
#if defined(TARGET_IOS) || defined(IOSKBM_RIG)
#define IOSKBM_IOS_BINDS 1
#endif

#ifdef IOSKBM_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

// SDL events as they are polled (Window_SdlUpdate). Returns 1 if the game is
// not to see this one (the mouse's movement while a wheel has it, Escape
// closing the wheel...). May change a motion event's movement (fractions
// carried over to the next).
int iosKbMouse_HandleEvent(void* pSdlEvent);

// Once a frame, after the frame's events are in: the keys' gestures, the
// wheels, the pointer
void iosKbMouse_Update(void);

// Whether the game should let go of the pointer during play right now (a
// photo mode, a panel)
int iosKbMouse_IsPointerFree(void);

// stdControl, as the game reads its controls: what it is to see of a key
// (SDL scancode, bDown: SDL has it down), and of a mouse button (0-4: left,
// right, middle, X1, X2)
int iosKbMouse_FilterKey(int scancode, int bDown);
int iosKbMouse_FilterMouseButton(int button, int bDown);

// The weapon steps the mouse wheel made since the game last looked, +1 the
// next weapon, -1 the previous (sithWeapon, as it reads those keys)
int iosKbMouse_TakeWheelSteps(int dir);

// The touch overlay: a finger went down (the touch controls are back); a
// touch began there (bPointer: a mouse or trackpad click, else a finger)
void iosKbMouse_NoteTouch(void);
void iosKbMouse_NoteTouchKind(int bPointer);

// What a wheel opened from the keyboard shows in its middle (as
// iosPad_WheelHint), or NULL for the touch wording
const char* iosKbMouse_WheelHint(int bHot, int bCancel);

// The diagnostic line's own part (counts since the readout came on)
void iosKbMouse_DiagText(char* pBuf, int size);
// Whether the last touch on the touch controls was a pointer click (the
// readout shows then too, even with no GCMouse behind it)
int iosKbMouse_SawPointerClick(void);

// (iosKbMouseView.m) The diagnostic line under the FPS readout, kept up to
// date (once a frame), and whether a mouse / a keyboard is connected
void iosKbMouseView_Tick(void);
int iosKbMouseView_HasMouse(void);
int iosKbMouseView_HasKeyboard(void);

// ------------------------------------------------------------ bindings
// The iOS mouse defaults (sithControl_RegisterMouseBindings: a new player,
// Setup > Mouse's Restore Defaults): left button fire, right button alt
// fire, middle button jump (the game's own had the right button jump and
// the middle one alt fire), the wheel left free for weapons (the game's own
// had it look up and down), as version 1.
#define IOSKBMBINDS_VERSION 1
#define IOSKBMBINDS_JSON_KEY "iosKbmBindsVersion" // (in the player's openjkdf2.json)
void iosKbMouse_MapMouseDefaults(void);
// After a player's controls are read: a player saved before version 1 with
// the game's own mouse defaults gets these (only what is still as the game
// made it changes). Returns 1 if the settings are to be saved again.
int iosKbMouse_MigrateBinds(int savedVersion);

#ifdef __cplusplus
}
#endif

#endif // IOSKBM_ENABLED

#endif // _PLATFORM_IOS_IOSKBMOUSE_H

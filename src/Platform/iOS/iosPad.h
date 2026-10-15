// Added: entire file. Game controllers (Xbox, PlayStation, Backbone-style and
// the other pads SDL knows; on iOS through Apple's GameController framework).
//
// Sits between SDL's gamepad and the game's own controls (stdControl), and
// makes a pad play the way the touch controls do:
//   - Sticks: a round dead zone (no square "notch" round the middle, no
//     creeping), the rest of the push rescaled so the stick still reaches
//     full; the move stick linear, the look stick through a response curve
//     (LINEAR, SMOOTH -- the default -- or PRECISE), with its own sensitivity
//     across (LOOK X) and up and down (LOOK Y), optionally inverted, and an
//     optional turn acceleration (off unless switched on). The game turns
//     the view by stick deflection times its frame time, so it turns as far
//     a second at any frame rate. Which stick looks is whichever one is
//     bound to turning or looking up and down (Setup > Controls > Joystick).
//   - The buttons go to the game through its own bindings (that screen
//     changes them), except for what a pad does on its own:
//       - the button bound to the next/previous Force power (LB by default):
//         a tap is the next power, as the binding says; held 0.3 s, or the
//         right stick pushed while it is down, opens the Force wheel. The
//         one bound to the next/previous weapon (RB) likewise, the weapon
//         wheel; D-pad left (while nothing else is bound to it) the saber
//         wheel, once there is a saber (as SABER on the touch overlay).
//         While a wheel is open the game holds still (single player), the
//         right stick points at a slice (the touch wheels' hysteresis; back
//         in the middle for a moment, or at the gap at the bottom, points at
//         nothing), and letting go of the button picks it; with nothing
//         pointed at, the wheel stays open to pick with A (D-pad left and
//         right step through it); B cancels. A tick and a short rumble for
//         every slice. On the saber wheel: A sets the colour (it stays
//         open), X one saber or two, Y which blade, R3 takes the saber.
//       - Start (Menu, Options, +): a tap opens the game menu as MENU does;
//         held 0.45 s, the controller options (iosPadPanel: LOOK X, LOOK Y,
//         CURVE, INVERT, ACCEL, VIBRATION, FPS, TYPE), the game held still.
//       - Back (View, Share, -): held 0.3 s quick saves -- a ring fills
//         meanwhile, and letting go sooner does nothing, as QUICK SAVE; with
//         D-pad down held too it quick loads instead. A quick tap is what it
//         is bound to (the automap by default).
//       - the button bound to running (L3 by default): a click toggles
//         running, until the move stick has rested for half a second.
//     A button held while the game starts listening (back from a menu, a
//     wheel closing) does nothing until it is let go.
//   - The touch controls hide while the pad is played and come back at a
//     touch (which does nothing else) or when the pad goes; the gyro stops
//     while they are hidden.
//   - Vibration (where the pad has it, and VIBRATION is on): the wheels'
//     ticks, a save or load going off, and the player being hurt.
// Everywhere else (menus, cutscenes) the pad is passed on as it is, its
// sticks through the round dead zone; the menus' own controller support
// (jkGuiRend_UpdateController) drives them.

#ifndef _PLATFORM_IOS_IOSPAD_H
#define _PLATFORM_IOS_IOSPAD_H

#if defined(TARGET_IOS) || defined(IOSPAD_RIG)
#define IOSPAD_ENABLED 1
#endif

#ifdef IOSPAD_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

// The wheels, as the touch overlay numbers them
#define IOSPAD_WHEEL_FORCE 0
#define IOSPAD_WHEEL_WEAPONS 1
#define IOSPAD_WHEEL_SABER 2

// The saber wheel's choices a pad can make (iosTouch_PadSaberChip)
#define IOSPAD_CHIP_STYLE 0  // one saber or two
#define IOSPAD_CHIP_TARGET 1 // with two: the next of LEFT, BOTH, RIGHT
#define IOSPAD_CHIP_EQUIP 2  // the saber in hand

// Hold rings (iosPadPanel_HoldRing)
#define IOSPAD_RING_NONE 0
#define IOSPAD_RING_SAVE 1
#define IOSPAD_RING_LOAD 2

// The controller options' rows (iosPadPanel)
enum {
    IOSPAD_ROW_SENSX = 0, IOSPAD_ROW_SENSY, IOSPAD_ROW_CURVE, IOSPAD_ROW_INVERT, IOSPAD_ROW_ACCEL,
    IOSPAD_ROW_RUMBLE, IOSPAD_ROW_FPS, IOSPAD_ROW_TYPE, IOSPAD_NUM_ROWS
};

// Once a frame, after the frame's events are in (Window_SdlUpdate): reads
// the pad, runs its gestures, opens and closes the wheels and the options.
void iosPad_Update(void);

// SDL events, as they are polled: hot-plugging
void iosPad_HandleEvent(const void* pSdlEvent);

// Whether Start and Back are the pad's own right now (gameplay), so the
// window shouldn't turn them into Escape itself
int iosPad_OwnsSystemButtons(void);

// stdControl, as the game reads its controls: pad idx's buttons and axes as
// the game is to see them. Returns 0 if stdControl should read it itself.
int iosPad_ReadGamepad(int idx, void* pSdlGamepad);

// stdControl's keyboard read: a key the pad presses (quick save's)
int iosPad_IsScancodeDown(int scancode);

// The touch overlay: a finger went down (the touch controls are back), and
// the pad-driven wheel pointed at another slice
void iosPad_NoteTouch(void);
void iosPad_OnWheelSlice(void);

// Whether the touch controls are hidden for the pad
int iosPad_IsHudHidden(void);

// What a wheel opened from the pad shows in its middle: what letting go or
// A would do ("release to pick"...), given what is pointed at (bHot: a slice
// that can be picked, bCancel: the gap), or NULL for the touch wording
const char* iosPad_WheelHint(int bHot, int bCancel);

// A pad button's name as this pad has it ("A", "Cross", "LB", "L1"...;
// bAscii: letters only, for the game's own fonts), by SDL_GamepadButton, or
// for the triggers SDL_GAMEPAD_BUTTON_COUNT + 0 (left) / + 1 (right)
const char* iosPad_ButtonName(int sdlButton, int bAscii);

// The name the game's control screens give a pad key (KEY_JOY1_*...) or a
// pad axis (AXIS_JOY1_X...: bAxis), or NULL to keep its own
const char* iosPad_KeyDisplayName(int dxKeyNum, int bAxis);

// The controller options (iosPadPanel draws them)
int iosPad_PanelIsOpen(void);
int iosPad_PanelFocus(void);
const char* iosPad_PanelTitle(void);
void iosPad_PanelRow(int row, char* pLabel, int labelSize, char* pValue, int valueSize);
const char* iosPad_PanelHint(void);
void iosPad_PanelTap(int row); // a finger on a row (-1: off the panel, which closes it)

// The look settings (also what the options show)
float iosPad_GetLookSensX(void);
float iosPad_GetLookSensY(void);

// ------------------------------------------------------------ bindings
// (iosPadBinds.c) The pad's default bindings, version 2:
//   left stick: move; right stick: look (sensitivity 1.5 across, 1.25 up
//   and down, as before); RT: fire; LT: alt fire; A: jump; B: duck;
//   X: use (doors, switches); Y: use the Force power; LB: next Force power /
//   Force wheel; RB: next weapon / weapon wheel; L3: run; R3: first or third
//   person; D-pad up: use the item, right: next item, down: previous item,
//   left: the saber wheel (left unbound for it); Back: automap (a tap; held,
//   quick save); Start: menu (not a binding).
// Version 1 (the builds before) had A: use last selected, Y: jump, X: use,
// LB/RB: previous/next weapon, the D-pad items and Force powers, L3/R3: use
// item / power, and no stick bindings at all for a player made with no pad
// connected.
#define IOSPADBINDS_VERSION 2
#define IOSPADBINDS_JSON_KEY "iosPadBindsVersion" // (in the player's openjkdf2.json)

// Adds the default pad bindings (sithControl_MapDefaultsJoystick: a new
// player, Setup > Joystick's Restore Defaults)
void iosPadBinds_MapDefaults(void);

// After a player's controls are read, with the bindings version their
// settings were last saved by: brings older pad bindings up to this
// version's, once. A player whose pad bindings are all version 1 defaults
// (or a part of them: no sticks, or some taken away) gets the new defaults;
// one who changed any keeps them as they are, getting only the default
// stick bindings for any stick function that has none. Returns 1 if the
// player's settings are to be saved again (with this version).
int iosPadBinds_Migrate(int savedVersion);

#ifdef __cplusplus
}
#endif

#endif // IOSPAD_ENABLED

#endif // _PLATFORM_IOS_IOSPAD_H

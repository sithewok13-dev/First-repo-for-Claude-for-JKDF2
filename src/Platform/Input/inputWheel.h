// Added: entire file. The Force, weapon and saber wheels, in plain C: what
// is on them, which slice a direction points into, what picking one does,
// and how they look -- drawn into the touch overlay's draw list
// (Platform/Touch/touchUiDraw.h), so the game's GL draws them
// (touchUiRender_gl.c) on the desktops and Android. No SDL, GL or platform
// code: the game through iosGame.c and iosSaber.c (src/Enhanced), a tick in
// the hand through platServices.
//
// It is the iOS touch overlay's wheel (iosTouch.m: the maps, the slices'
// hysteresis, what a pick does, the looks, the saber wheel's choices) and
// the Android touch overlay's (touchUi.c, which drew the first two in GL),
// made one, for every input that opens a wheel:
//   - a game controller and a keyboard and mouse (iosPad.c, iosKbMouse.c),
//     through inputOverlay.c (inputUi.h) everywhere but iOS;
//   - fingers (touchUi.c), which can take it over: the maps, the angles and
//     the pick from here, the finger's own gestures (slide, hold, tap) kept,
//     each finger's slice handed in with inputWheel_Point.
// One wheel is open at a time, and the game holds still while it is
// (iosGame_SetHold: single player).
//
// Every power has a fixed slice of the Force wheel, learned or not (those
// are dimmed); every weapon its slot on the weapon wheel, in the number key
// order, with its ammo count; every blade colour its slice on the saber
// wheel, with the choices beside it (one saber or two, which blade, take the
// saber). The gap at the bottom cancels. A learned power's level shows as
// four stars, filled one per level.
//
// Units: points (y down) for drawing; angles in degrees counter-clockwise
// from pointing right (90 is up), as the slices are.

#ifndef _PLATFORM_INPUT_INPUTWHEEL_H
#define _PLATFORM_INPUT_INPUTWHEEL_H

#include "Platform/Touch/touchUiDraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// The wheels (as iosPad.h's IOSPAD_WHEEL_* and the touch overlays number them)
enum { INPUTWHEEL_FORCE = 0, INPUTWHEEL_WEAPONS, INPUTWHEEL_SABER, INPUTWHEEL_NUM_KINDS };

// The "slice" of the gap at the bottom (pointing there cancels)
#define INPUTWHEEL_CANCEL (-2)
#define INPUTWHEEL_MAX 17
#define INPUTWHEEL_MAX_GROUPS 5

typedef struct {
    int bin;   // a Force power's or weapon's bin (SITHBIN_*), or a blade colour (iosSaber)
    int group; // its colour group
    float deg; // its middle
} inputWheelSlice;

typedef struct {
    const char* title; // shown beside the ring, or NULL
    float titleDeg;    // about where
    unsigned char r, g, b;
} inputWheelGroup;

typedef struct {
    const inputWheelSlice* aSlices;
    int numSlices;
    const inputWheelGroup* aGroups;
    int numGroups;
    float sliceDeg; // each slice's width
    float gapDeg;   // between two groups (the gap at the bottom is what is left)
} inputWheelMap;

// The saber wheel's choices beside the ring: one saber or two, which of two
// blades a colour goes to, and the saber in hand
enum { INPUTWHEEL_CHIP_SINGLE = 0, INPUTWHEEL_CHIP_DUAL, INPUTWHEEL_CHIP_LEFT, INPUTWHEEL_CHIP_BOTH, INPUTWHEEL_CHIP_RIGHT,
       INPUTWHEEL_CHIP_EQUIP, INPUTWHEEL_NUM_CHIPS };
// With two blades, the one a colour goes to
enum { INPUTWHEEL_SABER_RIGHT = 0, INPUTWHEEL_SABER_LEFT = 1, INPUTWHEEL_SABER_BOTH = 2 };

// ------------------------------------------------------------ the maps

// A wheel's map for this game (bMots: Mysteries of the Sith); the saber
// wheel's is made from iosSaber's colours as it is asked for
const inputWheelMap* inputWheel_GetMap(int kind, int bMots);

// a - b, as -180..180
float inputWheel_AngleDiff(float a, float b);

// The slice a direction points into, or INPUTWHEEL_CANCEL in the gap at the
// bottom. The slice already pointed at (cur) holds 4 degrees past its edges;
// a gap between two groups goes to the nearer slice.
int inputWheel_SliceForAngle(const inputWheelMap* m, float deg, int cur);

// ------------------------------------------------------------ the open wheel

// Opens a wheel: every power, weapon or colour in its place, read from the
// game now (learned, owned, ammo, levels), to point at and pick -- by
// letting go of what opened it, or (bTapMode) by a tap, a click or A. The
// game holds still. 0 if one is open already, or the saber wheel is asked
// for with no saber (iosGame_IsSaberButtonShown). (The opener closes the
// typing line first, where its keyboard would cover the wheel.)
int inputWheel_Open(int kind, int bTapMode);
int inputWheel_IsOpen(void);
int inputWheel_GetKind(void);
int inputWheel_IsTapMode(void);
void inputWheel_SetTapMode(int bTapMode);
const inputWheelMap* inputWheel_GetOpenMap(void);

// What is pointed at: a slice, INPUTWHEEL_CANCEL, or -1 (nothing: lifting or
// letting go changes nothing); bNeedle: a short needle at the middle's edge
// shows which way (needleDeg). Each new slice (or the gap) pointed at ticks
// in the hand (platServices_Haptic) and calls the slice hook.
void inputWheel_Point(int slice, int bNeedle, float needleDeg);
// ...from a direction (a stick, the mouse), held a little past a slice's
// edges; bAim 0: nothing
void inputWheel_Aim(int bAim, float deg);
// ...the next slice round (+1 clockwise, -1 the other way)
void inputWheel_Step(int dir);
int inputWheel_GetHot(void);
// Whether a slice can be picked: a learned power, a weapon the player has
// and could switch to, any colour
int inputWheel_IsPickable(int slice);

// Closes the wheel, picking slice if it can be picked (the power, the
// weapon -- the game takes it once it runs again --, the colour), else
// nothing. A colour is set and the saber wheel stays open, to pick more
// (the other blade, another colour): returns 1 while it is still open.
// -1 closes the saber wheel too.
int inputWheel_Close(int slice);

// The saber wheel's choices (INPUTWHEEL_CHIP_*): SINGLE / DUAL, the blade a
// colour goes to, EQUIP (the saber in hand: the wheel closes). Nothing for
// one that can't be chosen now.
void inputWheel_SaberChip(int chip);
// Whether EQUIP can take the saber now (the player has it and can switch to it)
int inputWheel_CanEquipSaber(void);
// The blade a colour goes to with two (INPUTWHEEL_SABER_*); remembered by
// the platform around this (it sets it at the start)
int inputWheel_GetSaberTarget(void);
void inputWheel_SetSaberTarget(int target);

// Called for each new slice pointed at, after the tick (a pad's rumble);
// NULL for none. Set by whoever opens the wheel, kept until it closes.
void inputWheel_SetSliceHook(void (*pfn)(void));
// What the middle says to do, in the opener's words, given what is pointed
// at (bPickable: a slice that can be picked; bCancel: the gap); NULL from it
// keeps the touch wording ("lift to select"). NULL for the touch wording.
void inputWheel_SetHintFn(const char* (*pfn)(int bPickable, int bCancel));

// ------------------------------------------------------------ drawing

typedef struct {
    float w, h;                   // the screen, in points
    float inL, inT, inR, inB;     // the safe area's insets
} inputWheelScreen;

// The open wheel, over the game dimmed: its slices (the one pointed at
// popped out), each name with its stars or ammo, the needle, the group
// names, the middle's name and hint, "cancel" in the gap; on the saber
// wheel its choices beside it. Nothing if no wheel is open.
void inputWheel_Draw(touchUiDrawList* pList, const inputWheelScreen* pScreen);

// Where it was last drawn (points), for the tests and for pointing with a
// finger or a pointer
void inputWheel_GetGeometry(float* pX, float* pY, float* pR0, float* pR1);
// The slice under a point on the ring (cur as for SliceForAngle),
// INPUTWHEEL_CANCEL in the gap, -1 off the ring
int inputWheel_SliceAt(float x, float y, int cur);
// The saber wheel's choice under a point, or -1
int inputWheel_ChipAt(float x, float y);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_INPUT_INPUTWHEEL_H

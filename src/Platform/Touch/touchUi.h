// Added: entire file. The touch overlay's core: on-screen touch controls in
// plain C, with no GL, SDL or platform code in it, so tests can drive it
// with scripted touches (tests/touchUi_test.c).
//
// It is the iOS overlay (src/Platform/iOS/iosTouch.m) rebuilt from the same
// layout, numbers and gestures -- the shared touch controls standard:
//   - a move stick where the left thumb lands (left 42% of the screen), with
//     the run marker above it while Always Run is off; a drag anywhere else
//     looks (1.6 / 1.3 game mouse units per point);
//   - FIRE, ALT, DUCK, ACT, JUMP and FORCE bottom right (a drag on them looks
//     too), NEXT WPN, FORCE WHEEL and the items top left, QUICK SAVE,
//     QUICK LOAD and MENU top right, CHEAT and VIEW between them;
//   - QUICK SAVE / QUICK LOAD: a 0.3 s hold with a ring, once per hold;
//     sliding off cancels; QUICK SAVE wins a tie;
//   - MENU: a tap is the game menu (Escape), a 0.45 s hold opens the tray
//     (GYRO, FPS, the camera switch, CHEAT, VIEW, JUMP style, CONTROLS, the
//     keyboard); holding GYRO there opens MODE and SENS;
//   - FORCE WHEEL and NEXT WPN: a tap is the next power / weapon, a 10 pt
//     slide opens the wheel at once to slide to a slice, a 0.3 s hold opens
//     it to tap one; the gap at the bottom cancels; the game holds still while
//     a wheel is open; a haptic tick per slice; 4 gold stars per power level;
//   - gyro aiming OFF / TOUCH / ALWAYS, SENS 1.0 / 1.5 / 2.0 / 3.0;
//   - the sithewok cheat's CHEAT (a 0.5 s hold), and JUMP / ACT / DUCK held
//     0.5 s while it is on (flight, noclip, level pick).
//   - CAMERA (switched on in the tray; on by default): a tap takes the photos
//     ticked in its options (CLEAN, HUD, HUD+BTNS), a slide is a burst for as
//     long as the finger stays down, a 0.5 s hold the selfie; with FREEZE on,
//     slid down Action Freeze (one finger turns the camera round, two move it
//     and pinch to zoom, CAMERA takes a photo, the X goes back) and slid up the
//     cinematic burst. Its options: the tray's camera switch held 0.45 s
//     (PHOTO, SELFIES 1/3/5, FREEZE ON/OFF); none of it in multiplayer (the
//     engine says no, as on iOS);
//   - SABER (once the saber is had, or with sithewok on): the saber wheel, every
//     blade colour, SINGLE or DUAL, which blade a colour goes to (LEFT, BOTH,
//     RIGHT) and EQUIP; a tap or a hold opens it to tap, a slide to slide.
//
// Units: "points" throughout -- on Android, density-independent pixels
// (dp), times a size factor for large tablets (touchUiSdl.c). Times are
// seconds, from the events' own clock.
//
// The platform side (touchUiSdl.c) feeds it touches, the screen and the
// gyro, reads back held keys, look movement and haptic requests, and hands
// the draw list (touchUiDraw.h) to the renderer (touchUiRender_gl.c).

#ifndef _PLATFORM_TOUCH_TOUCHUI_H
#define _PLATFORM_TOUCH_TOUCHUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Platform/Touch/touchUiDraw.h"

enum { TOUCHUI_PHASE_DOWN = 0, TOUCHUI_PHASE_MOVE, TOUCHUI_PHASE_UP, TOUCHUI_PHASE_CANCEL };

// What the overlay needs to know about, and do to, the game: iosGame.c's
// helpers in the engine (touchUiGame.c), fakes in the tests. Any may be NULL
// (then: nothing / 0).
typedef struct touchUiGame {
    int (*IsMots)(void);
    const char* (*GetForcePowerName)(void);
    int (*IsPowerAvailable)(int bin);
    int (*GetPowerLevel)(int bin);
    const char* (*GetPowerName)(int bin);
    int (*GetCurPower)(void);
    void (*SelectPower)(int bin);
    const char* (*GetWeaponName)(int bin);
    int (*GetWeapon)(int bin, int* pAmmo, int* pbSelectable);
    int (*GetCurWeapon)(void);
    int (*SelectWeapon)(int bin);
    void (*CancelWeaponPick)(void);
    int (*GetItem)(int bin, int* pAmount, int* pActive);
    void (*SelectItem)(int bin);
    int (*QuickLoad)(void);
    void (*SetHold)(int bHold);
    int (*IsHolding)(void);
    int (*IsChatOpen)(void);
    void (*ToggleChat)(void);
    float (*GetForceMana)(int* pbFull);
    unsigned int (*GetFrameCount)(void);
    int (*IsAlwaysRun)(void);
    void (*GetMouseLookDegrees)(float* pTurn, float* pPitch);
    int (*CanFly)(void);
    int (*ToggleFlight)(void);
    int (*IsFlying)(void);
    int (*CanNoclip)(void);
    int (*ToggleNoclip)(void);
    int (*NumPickableLevels)(void);
    int (*GoToLevel)(int level);
    int (*IsSithEwokOn)(void);
    int (*ToggleSithEwok)(void);
    int (*TakeSithEwokTypedOff)(void);
    void (*SetCheatShown)(int bShown);
    void (*SetThirdPerson)(int bThird);
    int (*IsThirdPerson)(void);
    void (*SetForceJumpStyle)(int style);
    int (*GetForceJumpRank)(void);
    void (*ShowMessage)(const char* pAscii);
    void (*ShowMessageInPlace)(const char* pAscii);
    // The HUD's corner gauge (bLeft: the health one, else ammo/force) in
    // points, or 0 while there is no HUD
    int (*GetGaugeRect)(int bLeft, float* pX0, float* pY0, float* pX1, float* pY1);

    // The saber (Enhanced/iosSaber.c, iosGame_IsSaberButtonShown): SABER
    // shown, the colours (ids 0..n-1, kept by their keys), the styles
    int (*IsSaberButtonShown)(void);
    int (*SaberNumColours)(void);
    const char* (*SaberColourName)(int id);
    const char* (*SaberColourKey)(int id);
    int (*SaberColourByKey)(const char* key);
    void (*SaberColourRGB)(int id, unsigned char* pR, unsigned char* pG, unsigned char* pB);
    int (*SaberStyleAvailable)(int style);
    const char* (*SaberStyleName)(int style);
    int (*SaberStyleNumBlades)(int style);
    int (*SaberGetStyle)(void);
    void (*SaberSetStyle)(int style);
    int (*SaberGetColour)(int style, int blade);
    void (*SaberSetColour)(int style, int blade, int id);

    // The camera (Enhanced/iosCapture, iosSelfieShots, iosFreeze, iosCinema
    // and platServices): their answers as theirs (TOUCHUI_CAPTURE_*,
    // TOUCHUI_PHOTOMODE_*)
    int (*CaptureRequest)(int kinds, int mode);
    void (*CaptureCancel)(void);
    void (*SetPhotoBatch)(int bOn);
    int (*SelfieStart)(int numShots);
    void (*SelfieStop)(void);
    int (*SelfieIsActive)(void);
    int (*FreezeBegin)(void);
    void (*FreezeEnd)(void);
    int (*FreezeIsActive)(void);
    void (*FreezeSetSmooth)(int bSmooth);
    int (*FreezeOrbit)(float yawDeg, float pitchDeg);
    void (*FreezePan)(float right, float down);
    void (*FreezeZoom)(float factor);
    int (*CinemaStart)(void);
    void (*CinemaStop)(void);
    int (*CinemaIsActive)(void);
} touchUiGame;

// The photos a capture takes, and what for (iosCapture.h's numbers)
enum { TOUCHUI_CAPTURE_CLEAN = 1, TOUCHUI_CAPTURE_HUD = 2, TOUCHUI_CAPTURE_HUDBTNS = 4 };
enum { TOUCHUI_CAPTURE_PHOTO = 0, TOUCHUI_CAPTURE_BURST = 1, TOUCHUI_CAPTURE_SELFIE = 2 };
// SelfieStart's and CinemaStart's answers (iosSelfieShots' and iosCinema's)
enum { TOUCHUI_PHOTOMODE_BUSY = -1, TOUCHUI_PHOTOMODE_CANT = 0, TOUCHUI_PHOTOMODE_STARTED = 1 };
// The photo modes that end by themselves (platServices_PhotoModeEnded's)
enum { TOUCHUI_MODE_SELFIE = 1, TOUCHUI_MODE_CINEMA = 2 };
// The saber's styles (iosSaber's)
enum { TOUCHUI_SABER_SINGLE = 0, TOUCHUI_SABER_DUAL = 1 };

// The buttons (for the tests, and the engine's queries)
enum {
    TOUCHUI_BTN_FIRE, TOUCHUI_BTN_ALT, TOUCHUI_BTN_DUCK, TOUCHUI_BTN_ACT, TOUCHUI_BTN_JUMP, TOUCHUI_BTN_FORCE,
    TOUCHUI_BTN_NEXTWPN, TOUCHUI_BTN_WHEEL, TOUCHUI_BTN_LIGHT, TOUCHUI_BTN_IR, TOUCHUI_BTN_BACTA,
    TOUCHUI_BTN_QUICKSAVE, TOUCHUI_BTN_QUICKLOAD, TOUCHUI_BTN_MENU,
    TOUCHUI_BTN_CAMERA, TOUCHUI_BTN_SABER, TOUCHUI_BTN_CHEAT, TOUCHUI_BTN_VIEW,
    TOUCHUI_BTN_TRAYGYRO, TOUCHUI_BTN_TRAYFPS, TOUCHUI_BTN_TRAYCAM, TOUCHUI_BTN_TRAYCHEAT, TOUCHUI_BTN_TRAYVIEW,
    TOUCHUI_BTN_TRAYJUMP, TOUCHUI_BTN_TRAYCTRL, TOUCHUI_BTN_TRAYKEYS,
    TOUCHUI_BTN_GYROOPT_MODE, TOUCHUI_BTN_GYROOPT_SENS,
    TOUCHUI_BTN_CAMROW_PHOTO, TOUCHUI_BTN_CAMROW_SELFIES, TOUCHUI_BTN_CAMROW_FREEZE,
    TOUCHUI_BTN_CAMOPT_CLEAN, TOUCHUI_BTN_CAMOPT_HUD, TOUCHUI_BTN_CAMOPT_BTNS,
    TOUCHUI_BTN_COUNT
};

enum { TOUCHUI_GYRO_OFF = 0, TOUCHUI_GYRO_TOUCH, TOUCHUI_GYRO_ALWAYS };
enum { TOUCHUI_HAPTIC_TICK = 1, TOUCHUI_HAPTIC_TAP = 2 };
enum { TOUCHUI_WHEEL_FORCE = 0, TOUCHUI_WHEEL_WEAPONS, TOUCHUI_WHEEL_SABER };
// The saber wheel's choices beside the ring
enum { TOUCHUI_CHIP_SINGLE = 0, TOUCHUI_CHIP_DUAL, TOUCHUI_CHIP_LEFT, TOUCHUI_CHIP_BOTH, TOUCHUI_CHIP_RIGHT, TOUCHUI_CHIP_EQUIP,
       TOUCHUI_NUM_CHIPS };

// Once, before anything else. The settings file (key=value lines) is read
// now, if there is one, and written whenever a remembered setting changes.
void touchUi_Init(const touchUiGame* pGame, const char* pSettingsPath);

// The screen in points, and the safe area's insets (the cutout, the system
// bars). Laid out again only when something changed (or touchUi_Relayout).
void touchUi_SetScreen(float w, float h, float insetLeft, float insetTop, float insetRight, float insetBottom);
void touchUi_Relayout(void);

// Whether the overlay is wanted: gameplay controls active, no cutscene, no
// menu. Shown or hidden, it starts over clean (every touch let go).
void touchUi_SetShown(int bShown);
int touchUi_IsShown(void);

// A game controller, keyboard or mouse in use: the controls hide (all but an
// open wheel and the FPS readout) until the next touch, which only brings
// them back.
void touchUi_SetOtherInput(int bInUse);
int touchUi_IsHiddenForOtherInput(void);

// A finger: down, moved, up, or cancelled (taken by the system: nothing fires).
void touchUi_Finger(long long id, int phase, float x, float y, double t);
// Every finger let go, nothing firing (the app went to the background)
void touchUi_Reset(void);

// Once a frame, after the frame's touches: the holds, the rings, what the
// buttons show. known: when this frame's events started being read (every
// touch that ended before it has been seen); now: the time now.
void touchUi_Tick(double known, double now);

// The drag's look movement since the last call, in whole game mouse counts
void touchUi_TakeLook(int* pDx, int* pDy);

// Whether the overlay holds this SDL scancode down, for one read of the
// keyboard (a one-off press reads down twice, then up once).
int touchUi_IsScancodeDown(int scancode);
// The same without taking a press (tests)
int touchUi_IsKeyHeld(int scancode);

// MENU's tap: whether Escape is to be held down this frame (it is held for
// two frames, then let go).
int touchUi_TakeEscape(void);

// Haptic requests since the last call (TOUCHUI_HAPTIC_* bits)
int touchUi_TakeHaptics(void);

// Gyro aiming. Available: the device has the sensors. Wanted: the sensors
// should be running now (shown, on, not hidden for another input).
void touchUi_SetGyroAvailable(int bAvailable);
// The saber wheel's blade choice (one of two) is the shared wheel's
// (Platform/Input/inputWheel.c: a pad and the keys set it too), remembered
// here: hands it the one remembered (at the start, once the platform's own
// settings are in, so this one wins); after that it is kept in step
void touchUi_ShareSaberTarget(void);
int touchUi_IsGyroWanted(void);
int touchUi_GetGyroMode(void);
float touchUi_GetGyroSens(void);
// The screen's landscape side (+1 / -1, 0: not landscape), each frame
void touchUi_GyroSetSide(int side, double now);
// One sample: the turn rate (rad/s) about the screen's right, up and
// toward-the-player axes, and which way up is in those axes (unit vector)
void touchUi_GyroSample(double rx, double ry, double rz, double ux, double uy, double uz, double t);
// Once a frame, after the touches: what the device turned since the last
// frame, in whole game mouse counts (0 if it isn't aiming now)
void touchUi_GyroFrame(double now, int* pDx, int* pDy);

// The FPS readout on or off
int touchUi_IsFpsShown(void);
// The FPS readout on or off from elsewhere (the controller options' FPS, enh_showFps), remembered
void touchUi_SetFpsShown(int bShow);

// The frame's drawing, in points
const touchUiDrawList* touchUi_Draw(double now);

// The camera's photos were just taken (platServices_SavePhotos, the frame
// they are of drawn): the flash and the tap (TOUCHUI_CAPTURE_* mode)
void touchUi_CameraFeedback(int mode);
// A selfie or the cinematic burst, run by the engine (enh_Tick), is over
// (platServices_PhotoModeEnded: TOUCHUI_MODE_*): everything put back
void touchUi_PhotoModeEnded(int mode);
// Whether a selfie, Action Freeze or the cinematic burst is up (the
// controls for other inputs don't hide the overlay meanwhile)
int touchUi_IsPhotoModeUp(void);

// Where the game's top message text goes, in points: under the top row,
// clear of the buttons. 0 until laid out.
int touchUi_GetTopTextRect(float* pTop, float* pLeft, float* pRight);

// For the tests: a button's place (0 if hidden), and the state
int touchUi_GetButton(int button, float* pX, float* pY, float* pR);
int touchUi_IsWheelOpen(void);
int touchUi_GetWheelKind(void);
int touchUi_IsWheelTapMode(void);
int touchUi_GetWheelHot(void); // the slice pointed at (-2: the gap, -1: none)
int touchUi_GetWheelSliceBin(int slice);
int touchUi_GetWheelSlice(int bin, float* pDeg); // the slice a bin is in, and its angle
void touchUi_GetWheelGeometry(float* pX, float* pY, float* pR0, float* pR1);
int touchUi_IsTrayOpen(void);
int touchUi_IsGyroRowOpen(void);
int touchUi_IsStickActive(void);
int touchUi_IsStickRunning(void);
float touchUi_GetRingProgress(int button); // the hold ring on a button (QUICK SAVE...)
int touchUi_IsCheatShown(void);
int touchUi_IsViewShown(void);
int touchUi_IsSwapped(void);
int touchUi_GetJumpStyle(void);
int touchUi_IsCameraShown(void);
int touchUi_IsSaberShown(void);
int touchUi_GetOptRow(void); // 0: none, 1: the gyro options, 2: the camera options
int touchUi_IsKindsRowOpen(void);
int touchUi_GetCameraKinds(void);
int touchUi_GetSelfieShots(void);
int touchUi_IsFreezeOn(void);
int touchUi_GetBurstShots(void); // -1: no burst going
int touchUi_IsSelfieUp(void);
int touchUi_IsFreezing(void);
int touchUi_IsCinemaUp(void);
int touchUi_GetSaberTarget(void); // TOUCHUI_CHIP_LEFT, _BOTH or _RIGHT
int touchUi_GetSaberChip(int chip, float* pX, float* pY); // its middle (0 if not shown)
void touchUi_GetFreezeClose(float* pX, float* pY);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_TOUCH_TOUCHUI_H

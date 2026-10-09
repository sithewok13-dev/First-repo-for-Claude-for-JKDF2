// Added: entire file. See iosTouch.h.
// Written to work with or without ARC (no weak refs; the overlay lives for
// the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreMotion/CoreMotion.h>
#include <SDL3/SDL.h>
#include <os/lock.h>
#include <math.h>
#include <string.h>

#include "Platform/iOS/iosTouch.h"
#include "Platform/iOS/iosGame.h"

extern SDL_Window* displayWindow;
extern int stdControl_bControlsActive;
extern int stdControl_bControllerEscapeKey;
extern int jkCutscene_isRendering;
extern int jkGuiRend_IsMenuActive(void);
extern int jkHud_IosGetRightGaugeRectPt(float* pX0, float* pY0, float* pX1, float* pY1);
extern int Window_lastXRel;
extern int Window_lastYRel;
extern int jkHud_bChatOpen;

// Look speed: game mouse units per point of finger travel.
#define IOSTOUCH_LOOK_SCALE_X 1.6f
#define IOSTOUCH_LOOK_SCALE_Y 1.3f
// Stick: radius in points (and its knob's), dead zone as a fraction of it.
// It appears wherever the left thumb lands in the left IOSTOUCH_STICK_ZONE of
// the screen, and only while that thumb is down. It walks, however far it is
// pushed.
#define IOSTOUCH_STICK_ZONE 0.42f
#define IOSTOUCH_STICK_RADIUS 60.0f
#define IOSTOUCH_STICK_KNOB 24.0
#define IOSTOUCH_STICK_DEADZONE 0.30f
// Running, as in Alien: Isolation: while the game's Always Run option is off,
// a run marker shows with the stick -- a small circle (radius IOSTOUCH_RUN_R)
// IOSTOUCH_RUN_DIST above the stick's centre, joined to its ring by a line.
// Where the top row or the top of the screen is in the way it comes down
// toward the stick, no nearer than IOSTOUCH_RUN_MIN_DIST (just clear of the
// ring, and past IOSTOUCH_RUN_ON, so a thumb on it always runs); where even
// that doesn't fit, there is no marker and the stick only walks. The thumb
// pushed up past the ring to it -- at least IOSTOUCH_RUN_ON from the stick's
// centre, pointing within IOSTOUCH_RUN_CONE degrees of the marker -- runs
// straight ahead (W + Shift, the run key), the knob on the marker; back under
// IOSTOUCH_RUN_OFF, or more than IOSTOUCH_RUN_CONE_OFF degrees off, it walks
// again.
#define IOSTOUCH_RUN_R 15.0
#define IOSTOUCH_RUN_DIST 100.0
#define IOSTOUCH_RUN_MIN_DIST 76.0
#define IOSTOUCH_RUN_ON 70.0
#define IOSTOUCH_RUN_OFF 60.0
#define IOSTOUCH_RUN_CONE 30.0
#define IOSTOUCH_RUN_CONE_OFF 40.0

// Buttons show at this opacity while untouched, so they hide less of the game;
// a touched one shows at full strength
#define IOSTOUCH_IDLE_ALPHA 0.65
// Edge-to-edge gap between the buttons around FIRE (the arc, ALT and FORCE)
#define IOSTOUCH_CLUSTER_GAP 30.0f
// A button takes touches up to this far outside its edge (buttonAt, isPoint)
#define IOSTOUCH_TOUCH_SLOP 6.0
// Force wheel (see the maps below): its outer radius, at most
// IOSTOUCH_WHEEL_R1_MAX points; the hole in the middle, a fraction of that;
// how far the slice pointed at pops out
#define IOSTOUCH_WHEEL_R1_MAX 168.0
#define IOSTOUCH_WHEEL_R0_FRAC 0.37
#define IOSTOUCH_WHEEL_POP 10.0
// Sliding to pick a power is measured from where the thumb went down on FORCE
// WHEEL: it points nowhere until IOSTOUCH_WHEEL_DEAD points away (keep this
// small: the slices pointing up only have ~30pt of screen above the button),
// the point it is measured from trails at most IOSTOUCH_WHEEL_LEASH behind
// the thumb (so turning back after a long slide is quick), and a slice stays
// picked until the thumb points IOSTOUCH_WHEEL_HYST degrees past its edge. On
// the wheel itself the slice under the thumb is picked. A lift without
// sliding, sooner than IOSTOUCH_WHEEL_TAP_TIME (by the touch's own clock),
// leaves the wheel open to tap a power instead.
#define IOSTOUCH_WHEEL_DEAD 14.0
#define IOSTOUCH_WHEEL_LEASH 60.0
#define IOSTOUCH_WHEEL_HYST 4.0
#define IOSTOUCH_WHEEL_TAP_TIME 0.35
#define IOSTOUCH_WHEEL_CANCEL (-2) // "slice" of the gap at the bottom
// A learned power's level (0-4) shows under its name as a row of
// IOSTOUCH_WHEEL_STARS star slots, the first that many filled: each in a cell
// IOSTOUCH_WHEEL_STAR_CELL points across, IOSTOUCH_WHEEL_STAR_STEP apart,
// IOSTOUCH_WHEEL_STAR_GAP below the name. The star's points reach
// IOSTOUCH_WHEEL_STAR_R from its middle, the notches between them a fraction
// IOSTOUCH_WHEEL_STAR_INNER of that. Empty stars are outlines
// IOSTOUCH_WHEEL_STAR_LINE wide; filled ones have an IOSTOUCH_WHEEL_STAR_EDGE
// dark edge. All but the gap scale down together on a wheel whose slices are
// too narrow for them.
#define IOSTOUCH_WHEEL_STARS 4
#define IOSTOUCH_WHEEL_STAR_CELL 8.0
#define IOSTOUCH_WHEEL_STAR_STEP 9.0
#define IOSTOUCH_WHEEL_STAR_GAP 2.0
#define IOSTOUCH_WHEEL_STAR_R 4.3
#define IOSTOUCH_WHEEL_STAR_INNER 0.45
#define IOSTOUCH_WHEEL_STAR_LINE 0.9
#define IOSTOUCH_WHEEL_STAR_EDGE 0.2
#define IOSTOUCH_WHEEL_STAR_MIN 0.6 // smallest scale (fitWheelLabels)
// QUICK SAVE and QUICK LOAD have to be held this long, so a stray tap can't
// save over the quicksave or throw away progress
#define IOSTOUCH_QUICKSAVE_HOLD 0.3
#define IOSTOUCH_QUICKLOAD_HOLD 0.3
// MENU held this long opens its tray (gyro, FPS, keyboard); a tap opens the
// menu as it lifts, holding Escape down for this many iosTouch_Update calls
#define IOSTOUCH_MENU_HOLD 0.45
#define IOSTOUCH_MENU_PULSE_UPDATES 2
// The tray's buttons are IOSTOUCH_TRAY_STEP apart in a row; where its backing
// would come nearer than IOSTOUCH_TRAY_CLEAR to the buttons round FIRE, the
// row moves left, as far as the screen allows
#define IOSTOUCH_TRAY_STEP 50.0
#define IOSTOUCH_TRAY_CLEAR 8.0
// The FPS readout counts frames over at least this many seconds; whether it
// shows is kept in the app's settings under this key
#define IOSTOUCH_FPS_PERIOD 0.5
#define IOSTOUCH_FPS_DEFAULTS_KEY @"iosTouchShowFps"
// Gyro aiming (see iosTouch_GyroUpdate). CoreMotion's device motion gives the
// rotation rate with the gyro's bias already taken out, IOSTOUCH_GYRO_HZ
// times a second. Turning left and right is measured about the way up
// (against gravity), so it works the same however far back the phone is
// tipped ("player space": what the screen's yaw and roll axes turn about up
// together -- made up by as much as IOSTOUCH_GYRO_YAW_RELAX times for a phone
// held rolled a little to one side, but never more than the two turn in
// all). Where up says nothing about which way the player's head is -- the
// screen facing down at a player lying under it, or rolled over on its side
// -- it is about the screen's own up axis instead ("local space"). Tilting
// is about the screen's own side-to-side axis. Under
// IOSTOUCH_GYRO_SMOOTH deg/s the motion is averaged over the last
// IOSTOUCH_GYRO_SMOOTH_N samples (all of it under half that), and under
// IOSTOUCH_GYRO_SOFT deg/s it is scaled down, to nothing at rest, so a phone
// held still doesn't creep. A gap between two samples longer than
// IOSTOUCH_GYRO_MAX_DT (a stall) is skipped, and so is what the phone did
// between two frames more than IOSTOUCH_GYRO_MAX_FRAME apart (the game was
// stopped: it would come all at once), or in the IOSTOUCH_GYRO_TURN_HOLD
// after the screen turned round to the other landscape side (the phone is
// still on its way round). The mode and the sensitivity (one of
// iosTouch_aGyroSens: at 1.0x the view turns as far as the phone does) are
// kept in the app's settings under these keys.
#define IOSTOUCH_GYRO_HZ 100.0
#define IOSTOUCH_GYRO_YAW_RELAX 1.41
#define IOSTOUCH_GYRO_SMOOTH 4.0
#define IOSTOUCH_GYRO_SMOOTH_N 12 // about 0.125 s
#define IOSTOUCH_GYRO_SOFT 1.5
#define IOSTOUCH_GYRO_MAX_DT 0.05
#define IOSTOUCH_GYRO_MAX_FRAME 0.5
#define IOSTOUCH_GYRO_TURN_HOLD 0.35
#define IOSTOUCH_GYRO_DEFAULTS_KEY @"iosTouchGyroMode"
#define IOSTOUCH_GYRO_SENS_DEFAULTS_KEY @"iosTouchGyroSens"
#define IOSTOUCH_GYRO_DEFAULT_SENS 1 // 1.5x
// A one-off key press is held for this many control reads, then released for one
#define IOSTOUCH_PULSE_READS 2

#define IOSTOUCH_MAX_TOUCHES 10
#define IOSTOUCH_NUM_SCANCODES 512

enum {
    ROLE_NONE = 0,
    ROLE_STICK,
    ROLE_LOOK,
    ROLE_BUTTON,
    ROLE_WHEEL,   // a touch on the open force wheel
    ROLE_IGNORED, // was down when the wheel opened, or only closed the MENU tray; ignored until it lifts
};

enum {
    KIND_KEY = 0,  // holds its key while touched
    KIND_MENU,     // Escape (via stdControl_bControllerEscapeKey) when the touch lifts on it; held, opens the tray
    KIND_HOLDSAVE, // hold IOSTOUCH_QUICKSAVE_HOLD seconds for one press of its key (quick save)
    KIND_HOLDLOAD, // hold IOSTOUCH_QUICKLOAD_HOLD seconds to quick load
    KIND_WHEEL,    // opens the force wheel
    KIND_ITEM,     // uses an inventory item when the touch lifts on it; only shown while the player has it
    KIND_CHAT,     // (MENU tray) opens or closes the typing line for cheats, when the touch lifts on it
    KIND_FPS,      // (MENU tray) shows or hides the FPS readout, when the touch lifts on it
    KIND_GYRO,     // (MENU tray) the next gyro aiming mode, when the touch lifts on it
    KIND_GYROSENS, // (MENU tray) the next gyro aiming sensitivity, when the touch lifts on it
};

// Keys are the game's default keyboard bindings (sithControl_RegisterKeyboardBindings).
// Inventory bins of the usable items (SITHBIN_* in types_enums.h, which can't
// be included here); KIND_ITEM buttons select one, then press use-item (Return).
#define SITHBIN_BACTATANK_IOS 40
#define SITHBIN_IRGOGGLES_IOS 41
#define SITHBIN_FIELDLIGHT_IOS 42
typedef struct {
    const char* label;
    int kind;
    int scancode;       // -1 if the button isn't a key
    float radius;       // points
    int bLookWhileHeld; // dragging on this button also turns the view
    int bin;            // KIND_ITEM: the inventory bin it uses
    float x, y;         // centre, set in layout
} iosTouchButton;

// Layout (see layoutSubviews). Bottom right, under the right thumb: FIRE, an
// arc of DUCK / ACT / JUMP around it, ALT above the ammo gauge and FORCE right
// of JUMP, above ALT (or, where the cutout or the top row is in the way, over
// JUMP) -- all IOSTOUCH_CLUSTER_GAP apart. FORCE uses the selected
// power for as long as it is held (Force Jump charges, Lightning keeps going).
// These all pass drags through to looking, so a thumb that lands on one while
// aiming keeps aiming. Top left: next weapon, the FORCE WHEEL that picks the
// power, and a button for each usable item while the player has it (field
// light, IR goggles, bacta), each always in its own place. Top right: quick
// save and quick load (short holds) and the menu. Holding MENU opens a tray
// just under it: gyro aiming's sensitivity and mode, FPS, which shows or hides
// a frame rate readout left of QUICK SAVE, and the keyboard, for the typing
// line (cheats). ACT is the door/switch key.
enum {
    BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_TRAYSENS, BTN_TRAYGYRO, BTN_TRAYFPS, BTN_TRAYKEYS, // MENU's tray (left to right), hidden unless it is open
    BTN_COUNT,
    BTN_TRAY_FIRST = BTN_TRAYSENS, BTN_TRAY_LAST = BTN_TRAYKEYS
};
static iosTouchButton iosTouch_aButtons[] = {
    [BTN_FIRE]      = { "FIRE",        KIND_KEY,      SDL_SCANCODE_LCTRL,  42.0f, 1 },
    [BTN_ALT]       = { "ALT",         KIND_KEY,      SDL_SCANCODE_Z,      28.0f, 1 },
    [BTN_DUCK]      = { "DUCK",        KIND_KEY,      SDL_SCANCODE_C,      29.0f, 1 },
    [BTN_ACT]       = { "ACT",         KIND_KEY,      SDL_SCANCODE_SPACE,  29.0f, 1 },
    [BTN_JUMP]      = { "JUMP",        KIND_KEY,      SDL_SCANCODE_X,      31.0f, 1 },
    [BTN_FORCE]     = { "FORCE",       KIND_KEY,      SDL_SCANCODE_F,      30.0f, 1 },
    [BTN_NEXTWPN]   = { "NEXT\nWPN",   KIND_KEY,      SDL_SCANCODE_G,      22.0f, 0 },
    [BTN_WHEEL]     = { "FORCE\nWHEEL", KIND_WHEEL,   -1,                  22.0f, 0 },
    [BTN_LIGHT]     = { "LIGHT",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_FIELDLIGHT_IOS },
    [BTN_IR]        = { "IR",          KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_IRGOGGLES_IOS },
    [BTN_BACTA]     = { "BACTA",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_BACTATANK_IOS },
    [BTN_QUICKSAVE] = { "QUICK\nSAVE", KIND_HOLDSAVE, SDL_SCANCODE_F9,     22.0f, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD", KIND_HOLDLOAD, -1,                  22.0f, 0 },
    [BTN_MENU]      = { "MENU",        KIND_MENU,     -1,                  22.0f, 0 },
    [BTN_TRAYSENS]  = { "SENS",        KIND_GYROSENS, -1,                  20.0f, 0 },
    [BTN_TRAYGYRO]  = { "GYRO",        KIND_GYRO,     -1,                  20.0f, 0 },
    [BTN_TRAYFPS]   = { "FPS",         KIND_FPS,      -1,                  20.0f, 0 },
    [BTN_TRAYKEYS]  = { "",            KIND_CHAT,     -1,                  20.0f, 0 },
};
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))
typedef char iosTouch_assertButtonCount[(IOSTOUCH_NUM_BUTTONS == BTN_COUNT) ? 1 : -1];

static int iosTouch_IsTrayButton(int button)
{
    return button >= BTN_TRAY_FIRST && button <= BTN_TRAY_LAST;
}

static int iosTouch_IsQuickHold(int button)
{
    return button == BTN_QUICKSAVE || button == BTN_QUICKLOAD;
}

// ------------------------------------------------------------ force wheel maps

// Every power has its own fixed slice of the wheel, whether or not the player
// has learned it yet (those are dimmed), so a power is always in the same
// place. The slices come in coloured groups, a small gap between groups and a
// wider one at the bottom, which cancels. Angles are the slice's middle, in
// degrees counter-clockwise from pointing right (90 is up).
#define IOSTOUCH_WHEEL_MAX 17
#define IOSTOUCH_WHEEL_MAX_GROUPS 4
typedef struct {
    int bin;   // SITHBIN_F_* (types_enums.h can't be included here)
    int group;
    float deg;
} iosTouchWheelSlice;
typedef struct {
    const char* title; // shown beside the ring, or NULL
    float titleDeg;    // about where (it keeps clear of the ring and the top row)
    unsigned char r, g, b;
} iosTouchWheelGroup;
typedef struct {
    const iosTouchWheelSlice* aSlices;
    int numSlices;
    const iosTouchWheelGroup* aGroups;
    int numGroups;
    float sliceDeg; // each slice's width
    float gapDeg;   // between two groups (the gap at the bottom is what is left)
} iosTouchWheelMap;

// Jedi Knight, as its Force screen lays the powers out: the light side down
// the left and the dark side down the right, each from its first power at the
// top to its capstone (Protection, Deadly Sight) by the gap, and the neutral
// powers across the top.
static const iosTouchWheelGroup iosTouch_aJkWheelGroups[] = {
    { "LIGHT\nSIDE", 150.0f, 120, 200, 255 },
    { NULL,           90.0f, 255, 214, 120 },
    { "DARK\nSIDE",   30.0f, 255, 105,  90 },
};
static const iosTouchWheelSlice iosTouch_aJkWheelSlices[] = {
    { 29, 0, 239.0f }, { 28, 0, 217.0f }, { 27, 0, 195.0f }, { 26, 0, 173.0f }, { 25, 0, 151.0f }, // Protection, Absorb, Blinding, Persuasion, Healing
    { 21, 1, 123.0f }, { 22, 1, 101.0f }, { 23, 1,  79.0f }, { 24, 1,  57.0f },                    // Jump, Speed, Seeing, Pull
    { 30, 2,  29.0f }, { 31, 2,   7.0f }, { 32, 2, -15.0f }, { 33, 2, -37.0f }, { 34, 2, -59.0f }, // Throw, Grip, Lightning, Destruction, Deadly Sight
};
// Mysteries of the Sith has no light and dark side. It sorts its powers into
// four tiers instead (jkPlayer_aMotsFpBins), opened at Jedi ranks 1, 2, 5 and
// 7 and shown as the four columns of its Force screen. They go round
// clockwise in that order from the bottom left: tier I reads its column top
// to bottom down the left, tier IV its column down the right (Deadly Sight by
// the gap, as in JK); tier II is in the game's own power order, and tier III
// puts its long names where the slices have room for them. Defense is
// multiplayer only and can't be selected, so it isn't on the wheel.
static const iosTouchWheelGroup iosTouch_aMotsWheelGroups[] = {
    { "TIER I",   207.0f, 255, 214, 120 },
    { "TIER II",  120.0f, 110, 220, 170 },
    { "TIER III",  42.0f, 120, 200, 255 },
    { "TIER IV",  -36.0f, 190, 150, 255 },
};
static const iosTouchWheelSlice iosTouch_aMotsWheelSlices[] = {
    { 38, 0, 243.0f }, { 22, 0, 225.0f }, { 23, 0, 207.0f }, { 36, 0, 189.0f }, { 21, 0, 171.0f }, // Push, Speed, Seeing, Projection, Jump
    { 24, 1, 147.0f }, { 31, 1, 129.0f }, { 35, 1, 111.0f }, { 37, 1,  93.0f },                    // Pull, Grip, Far Sight, Saber Throw
    { 39, 2,  69.0f }, { 25, 2,  51.0f }, { 27, 2,  33.0f }, { 26, 2,  15.0f },                    // Chain Lightning, Healing, Blinding, Persuasion
    { 28, 3,  -9.0f }, { 33, 3, -27.0f }, { 29, 3, -45.0f }, { 34, 3, -63.0f },                    // Absorb, Destruction, Protection, Deadly Sight
};
#define IOSTOUCH_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const iosTouchWheelMap iosTouch_jkWheel = {
    iosTouch_aJkWheelSlices, IOSTOUCH_COUNT(iosTouch_aJkWheelSlices),
    iosTouch_aJkWheelGroups, IOSTOUCH_COUNT(iosTouch_aJkWheelGroups), 22.0f, 6.0f
};
static const iosTouchWheelMap iosTouch_motsWheel = {
    iosTouch_aMotsWheelSlices, IOSTOUCH_COUNT(iosTouch_aMotsWheelSlices),
    iosTouch_aMotsWheelGroups, IOSTOUCH_COUNT(iosTouch_aMotsWheelGroups), 18.0f, 6.0f
};
typedef char iosTouch_assertWheelSize[(IOSTOUCH_COUNT(iosTouch_aJkWheelSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aMotsWheelSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aMotsWheelGroups) <= IOSTOUCH_WHEEL_MAX_GROUPS) ? 1 : -1];

// ------------------------------------------------------------ state

typedef struct {
    UITouch* touch; // not retained; only compared
    int role;
    int button;
    CGPoint origin;
    CGPoint last;
    CFTimeInterval tDown; // when the touch began reaching us (CACurrentMediaTime, as -tick counts)...
    NSTimeInterval tDownTouch; // ...and when it began, by the touch's own clock (UITouch.timestamp)
    int bFired;           // QUICK SAVE, QUICK LOAD: the hold is over (a save or load went off, from this finger or another one on either button; or it slid off, or typing). MENU: the hold is over (tray opened, or slid off)
    int bSeen;            // KIND_KEY: the game has read the key as held at least once
    int trayButton;       // MENU, after its tray opened: the tray button under the finger, or -1
    int wheelSlot;        // ROLE_WHEEL: the slice picked (index into the map), IOSTOUCH_WHEEL_CANCEL, or -1
    int bWheelOpener;     // ROLE_WHEEL: the touch on FORCE WHEEL that opened it...
    int bArmed;           // ...has slid out of the dead zone
    CGPoint wheelOrigin;  // ...where its slide is measured from
    CGFloat wheelAim;     // ...which way it points (degrees, as for slices)
} iosTouchSlot;

static iosTouchSlot iosTouch_aSlots[IOSTOUCH_MAX_TOUCHES];
static int iosTouch_aButtonHeld[IOSTOUCH_NUM_BUTTONS];
static unsigned char iosTouch_aKeyDown[IOSTOUCH_NUM_SCANCODES];
// One-off presses waiting to be read, and the one being read right now
static unsigned char iosTouch_aPulseQueue[IOSTOUCH_NUM_SCANCODES];
static unsigned char iosTouch_aPulseReads[IOSTOUCH_NUM_SCANCODES];
static unsigned char iosTouch_aPulseGap[IOSTOUCH_NUM_SCANCODES];
static float iosTouch_lookX = 0.0f, iosTouch_lookY = 0.0f;
static float iosTouch_stickX = 0.0f, iosTouch_stickY = 0.0f;
static int iosTouch_bStickActive = 0;
// The game's Always Run option as last read (-1: not yet); whether the stick
// has its run marker, where (its centre) and which way that is from the
// stick's centre (degrees, as for the wheel: 90 is up); whether the thumb is
// on it
static int iosTouch_bAlwaysRun = -1;
static int iosTouch_bRunMarker = 0;
static CGPoint iosTouch_runMarkerPos;
static CGFloat iosTouch_runDeg = 90.0;
static int iosTouch_bStickRunning = 0;
// The HUD gauge rectangle and cutout side the current layout was made for
static int iosTouch_bLayoutGauge = 0;
static float iosTouch_aLayoutGauge[4];
static int iosTouch_layoutCutoutRight = -1;
// The force wheel: open or not (and whether it was opened with a tap, so
// powers are tapped), the map it shows, which of the map's powers the player
// has learned, and where it is
static int iosTouch_bWheelOpen = 0;
static int iosTouch_bWheelTapMode = 0;
static const iosTouchWheelMap* iosTouch_pWheelMap = NULL;
static int iosTouch_aWheelEarned[IOSTOUCH_WHEEL_MAX];
static int iosTouch_aWheelLevel[IOSTOUCH_WHEEL_MAX]; // as the wheel opened (iosGame_GetPowerLevel)
static CGPoint iosTouch_wheelCentre;
static CGFloat iosTouch_wheelR0 = 0.0, iosTouch_wheelR1 = 0.0;
// MENU's tray; Escape held down for a MENU tap (iosTouch_Update calls left);
// the FPS readout
static int iosTouch_bTrayOpen = 0;
static int iosTouch_menuPulse = 0;
static int iosTouch_bShowFps = 0;
// Gyro aiming: its mode, its sensitivity (an index into iosTouch_aGyroSens),
// the motion manager (one for the app, as Apple asks) and the queue its
// samples are handled on, whether its updates are running, whether iOS
// refused them (no motion permission), and whether the last frame could have
// used them (and when it was); the mouse counts it has yet to hand over (the
// part short of a whole count); the landscape side the screen was last
// turned and when it turned round to it. Under iosTouch_gyroLock, as the
// samples come in on that queue: which way round the screen is (+1 landscape
// left, -1 landscape right, 0 neither, so no aiming), how far the view is to
// turn (degrees, + left) and tilt (+ up) for the samples since the last
// frame, what the samples keep (the last one's time, the smoothing's), and
// whether one came back refused.
enum { IOSTOUCH_GYRO_OFF = 0, IOSTOUCH_GYRO_TOUCH, IOSTOUCH_GYRO_ALWAYS, IOSTOUCH_GYRO_NUM_MODES };
static const float iosTouch_aGyroSens[] = { 1.0f, 1.5f, 2.0f, 3.0f };
static int iosTouch_gyroMode = IOSTOUCH_GYRO_OFF; // until it is switched on in MENU's tray
static int iosTouch_gyroSens = IOSTOUCH_GYRO_DEFAULT_SENS;
static CMMotionManager* iosTouch_pMotion = nil;
static NSOperationQueue* iosTouch_pMotionQueue = nil;
static int iosTouch_bGyroRunning = 0;
static int iosTouch_bGyroRefused = 0;
static int iosTouch_bGyroWasOk = 0;
static CFTimeInterval iosTouch_gyroLastFrame = 0.0;
static float iosTouch_gyroCountX = 0.0f, iosTouch_gyroCountY = 0.0f;
static int iosTouch_gyroLastSide = 0;
static CFTimeInterval iosTouch_gyroTurnedRound = -1.0e9;
static os_unfair_lock iosTouch_gyroLock = OS_UNFAIR_LOCK_INIT;
static int iosTouch_gyroSide = 0;
static double iosTouch_gyroYaw = 0.0, iosTouch_gyroPitch = 0.0;
static NSTimeInterval iosTouch_gyroLastT = 0.0;
static double iosTouch_aGyroSmooth[IOSTOUCH_GYRO_SMOOTH_N][2];
static int iosTouch_gyroSmoothNext = 0;
static int iosTouch_bGyroRefusedOnQueue = 0;

static void iosTouch_QueuePress(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return;
    if (iosTouch_aPulseQueue[scancode] < 255) iosTouch_aPulseQueue[scancode]++;
}

static void iosTouch_RecomputeKeys(void)
{
    memset(iosTouch_aKeyDown, 0, sizeof(iosTouch_aKeyDown));
    // MENU's Escape comes and goes on its own (see iosTouch_Update)
    stdControl_bControllerEscapeKey = iosTouch_menuPulse > 0;
    if (iosTouch_bWheelOpen) return; // the wheel takes every touch
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (!iosTouch_aButtonHeld[i]) continue;
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (b->kind == KIND_KEY && b->scancode >= 0) iosTouch_aKeyDown[b->scancode] = 1;
    }

    if (iosTouch_bStickActive && iosTouch_bStickRunning) {
        // On the run marker: straight ahead, holding the run key
        iosTouch_aKeyDown[SDL_SCANCODE_W] = 1;
        iosTouch_aKeyDown[SDL_SCANCODE_LSHIFT] = 1;
    }
    else if (iosTouch_bStickActive) {
        float x = iosTouch_stickX, y = iosTouch_stickY;
        if (y < -IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_W] = 1;
        if (y >  IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_S] = 1;
        if (x < -IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_A] = 1;
        if (x >  IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_D] = 1;
    }
}

// Distance from p to the segment (ax,ay)-(bx,by)
static CGFloat iosTouch_DistToSegment(CGPoint p, CGFloat ax, CGFloat ay, CGFloat bx, CGFloat by)
{
    CGFloat vx = bx - ax, vy = by - ay;
    CGFloat len2 = vx * vx + vy * vy;
    CGFloat t = len2 > 0 ? ((p.x - ax) * vx + (p.y - ay) * vy) / len2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    CGFloat dx = p.x - (ax + t * vx), dy = p.y - (ay + t * vy);
    return sqrt(dx * dx + dy * dy);
}

// Where MENU's tray goes (see layoutSubviews), with FORCE at *pForce: a row
// of its buttons, IOSTOUCH_TRAY_STEP apart, just under MENU, the keyboard (the
// right end) right below it, moved left (4 pt at a time, as long as its
// backing stays right of left, the safe area's edge) until its backing is
// IOSTOUCH_TRAY_CLEAR from every button round FIRE (FIRE, ALT, DUCK, ACT,
// JUMP and FORCE) -- or, if it never is, to wherever the nearest of them is
// furthest from it. Returns the keyboard button's centre; *pClear is how far
// the backing is from the nearest of them, *pForceClear how far from FORCE.
// With pForce NULL, FORCE is left out (FORCE's own search: how clear the tray
// can be without it).
static CGPoint iosTouch_TrayKeysAt(CGPoint menu, CGFloat left, const CGPoint* pForce, CGFloat* pClear, CGFloat* pForceClear)
{
    const CGFloat ty = menu.y + 52;
    const CGFloat len = IOSTOUCH_TRAY_STEP * (BTN_TRAY_LAST - BTN_TRAY_FIRST);
    CGFloat kx = menu.x, bestClear = -1e9, bestForce = 1e9;
    for (CGFloat x = menu.x; ; x -= 4) {
        CGFloat clear = 1e9, forceClear = 1e9;
        for (int i = BTN_FIRE; i <= BTN_FORCE; i++) {
            iosTouchButton* c = &iosTouch_aButtons[i];
            CGPoint p = CGPointMake(c->x, c->y);
            if (i == BTN_FORCE) {
                if (!pForce) continue;
                p = *pForce;
            }
            CGFloat d = iosTouch_DistToSegment(p, x - len, ty, x, ty) - 26 - c->radius;
            clear = MIN(clear, d);
            if (i == BTN_FORCE) forceClear = d;
        }
        if (clear > bestClear) {
            bestClear = clear;
            bestForce = forceClear;
            kx = x;
        }
        if (clear >= IOSTOUCH_TRAY_CLEAR) break;
        if (x - 4 - len - 26 < left) break; // the next spot would go off the left
    }
    if (pClear) *pClear = bestClear;
    if (pForceClear) *pForceClear = bestForce;
    return CGPointMake(kx, ty);
}

// Whether the camera cutout may be on the right of the screen. Landscape right
// has the bottom of the phone on the right, so the cutout (at the top) is on
// the left; landscape left puts it on the right. Not known yet: assume it can be.
static int iosTouch_CutoutMayBeRight(UIView* v)
{
    UIWindowScene* scene = v.window.windowScene;
    return !(scene && scene.interfaceOrientation == UIInterfaceOrientationLandscapeRight);
}

// a - b, in degrees, as -180..180
static CGFloat iosTouch_AngleDiff(CGFloat a, CGFloat b)
{
    CGFloat d = fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

// The slice of the open wheel a direction (degrees) points into, or
// IOSTOUCH_WHEEL_CANCEL in the gap at the bottom. The slice already picked
// (cur) holds on IOSTOUCH_WHEEL_HYST past its edges; a gap between two groups
// goes to the nearer slice.
static int iosTouch_WheelSliceForAngle(CGFloat deg, int cur)
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (!m) return IOSTOUCH_WHEEL_CANCEL;
    CGFloat half = m->sliceDeg * 0.5;
    if (cur >= 0 && cur < m->numSlices && fabs(iosTouch_AngleDiff(deg, m->aSlices[cur].deg)) <= half + IOSTOUCH_WHEEL_HYST)
        return cur;
    int best = IOSTOUCH_WHEEL_CANCEL;
    CGFloat bestDiff = 0.0;
    for (int i = 0; i < m->numSlices; i++) {
        CGFloat d = fabs(iosTouch_AngleDiff(deg, m->aSlices[i].deg));
        if (d <= half + m->gapDeg * 0.5 && (best < 0 || d < bestDiff)) {
            best = i;
            bestDiff = d;
        }
    }
    return best;
}

// The slice of the open wheel under p (cur as above), IOSTOUCH_WHEEL_CANCEL in
// the gap at the bottom, or -1 off the ring (the middle, or well outside it)
static int iosTouch_WheelSliceAt(CGPoint p, int cur)
{
    CGFloat dx = p.x - iosTouch_wheelCentre.x, dy = iosTouch_wheelCentre.y - p.y;
    CGFloat r = sqrt(dx * dx + dy * dy);
    if (r < iosTouch_wheelR0 - 4.0 || r > iosTouch_wheelR1 + IOSTOUCH_WHEEL_POP + 14.0) return -1;
    return iosTouch_WheelSliceForAngle(atan2(dy, dx) * 180.0 / M_PI, cur);
}

// Whether p is inside the open wheel's slice at deg (not popped out), at
// least pad points in from its edges
static int iosTouch_InWheelSlice(CGPoint p, CGFloat deg, CGFloat pad)
{
    CGFloat dx = p.x - iosTouch_wheelCentre.x, dy = iosTouch_wheelCentre.y - p.y;
    CGFloat r = sqrt(dx * dx + dy * dy);
    if (r < iosTouch_wheelR0 + pad || r > iosTouch_wheelR1 - pad) return 0;
    // the slices are drawn a degree in from their edges
    CGFloat edge = iosTouch_pWheelMap->sliceDeg * 0.5 - 1.0 - pad / r * 180.0 / M_PI;
    return fabs(iosTouch_AngleDiff(atan2(dy, dx) * 180.0 / M_PI, deg)) <= edge;
}

// Whether the stick's thumb at p (the stick centred on o) is on its run
// marker: pushed up to it past the ring, pointing its way. Once on, it stays
// on a little further back and further round (bRunning).
static int iosTouch_StickOnRunMarker(CGPoint p, CGPoint o, int bRunning)
{
    if (!iosTouch_bRunMarker) return 0;
    CGFloat dx = p.x - o.x, dy = o.y - p.y;
    CGFloat d = sqrt(dx * dx + dy * dy);
    if (d < (bRunning ? IOSTOUCH_RUN_OFF : IOSTOUCH_RUN_ON)) return 0;
    CGFloat off = fabs(iosTouch_AngleDiff(atan2(dy, dx) * 180.0 / M_PI, iosTouch_runDeg));
    return off <= (bRunning ? IOSTOUCH_RUN_CONE_OFF : IOSTOUCH_RUN_CONE);
}

// ---------------------------------------------------------------- gyro aiming

// One sample of device motion, on the samples' queue: the rotation rate w
// (rad/s) and gravity g (toward the ground) in the device's own axes (x
// right, y up, z out of the screen, as if held upright), taken at time t.
// Adds how far it turns and tilts the view.
static void iosTouch_GyroSample(double wx, double wy, double wz, double gx, double gy, double gz, NSTimeInterval t)
{
    os_unfair_lock_lock(&iosTouch_gyroLock);
    double dt = iosTouch_gyroLastT > 0.0 ? t - iosTouch_gyroLastT : 0.0;
    iosTouch_gyroLastT = t;
    double s = iosTouch_gyroSide;
    double gn = sqrt(gx * gx + gy * gy + gz * gz);
    if (s != 0.0 && dt > 0.0 && dt <= IOSTOUCH_GYRO_MAX_DT && gn > 0.1) {
        // The screen's axes as the player sees it: right, up, toward them.
        // How fast the phone turns about each (the right-hand way round: +
        // about up turns left), and how far each points up.
        double rx = s * wy, ry = -s * wx, rz = wz;
        double ux = -s * gy / gn, uy = s * gx / gn, uz = -gz / gn;
        // Not while held upside down for the way round the screen is turned
        // (rolled more than 120 degrees either way, unless within 30 of
        // flat): the phone is on its way round to the other landscape side
        double tilt = sqrt(ux * ux + uy * uy);
        if (tilt < 0.5 || uy > -0.5 * tilt) {
            double yaw = ry, pitch = rx;
            if (uz >= 0.0 && (tilt < 0.5 || uy > 0.5 * tilt)) {
                // Player space, while the screen faces up and is held the way
                // round it is turned (rolled less than 60 degrees either way)
                // or about flat: turning is about up, from the screen's up and
                // toward-the-player axes as far as each points up
                yaw = uy * ry + uz * rz;
                double mag = sqrt(ry * ry + rz * rz);
                yaw = copysign(fmin(fabs(yaw) * IOSTOUCH_GYRO_YAW_RELAX, mag), yaw);
            }
            // (Otherwise local space: facing down, or rolled further over --
            // which also leaves out the roll of a phone on its way round.)
            yaw *= 180.0 / M_PI;
            pitch *= 180.0 / M_PI;

            // Smoothed only where it is slow
            double m = sqrt(yaw * yaw + pitch * pitch);
            double direct = (m - IOSTOUCH_GYRO_SMOOTH * 0.5) / (IOSTOUCH_GYRO_SMOOTH * 0.5);
            direct = direct < 0.0 ? 0.0 : (direct > 1.0 ? 1.0 : direct);
            iosTouch_aGyroSmooth[iosTouch_gyroSmoothNext][0] = yaw * (1.0 - direct);
            iosTouch_aGyroSmooth[iosTouch_gyroSmoothNext][1] = pitch * (1.0 - direct);
            iosTouch_gyroSmoothNext = (iosTouch_gyroSmoothNext + 1) % IOSTOUCH_GYRO_SMOOTH_N;
            double sumYaw = 0.0, sumPitch = 0.0;
            for (int i = 0; i < IOSTOUCH_GYRO_SMOOTH_N; i++) {
                sumYaw += iosTouch_aGyroSmooth[i][0];
                sumPitch += iosTouch_aGyroSmooth[i][1];
            }
            yaw = yaw * direct + sumYaw / IOSTOUCH_GYRO_SMOOTH_N;
            pitch = pitch * direct + sumPitch / IOSTOUCH_GYRO_SMOOTH_N;

            // The soft dead zone
            m = sqrt(yaw * yaw + pitch * pitch);
            if (m < IOSTOUCH_GYRO_SOFT) {
                yaw *= m / IOSTOUCH_GYRO_SOFT;
                pitch *= m / IOSTOUCH_GYRO_SOFT;
            }
            iosTouch_gyroYaw += yaw * dt;
            iosTouch_gyroPitch += pitch * dt;
        }
    }
    os_unfair_lock_unlock(&iosTouch_gyroLock);
}

// Whether an error the motion updates hand back is iOS refusing the motion
// data (no permission: none is asked for today, but iOS could start to)
static int iosTouch_GyroIsRefusal(NSError* err)
{
    if (![err.domain isEqualToString:CMErrorDomain]) return 0;
    return err.code == CMErrorNotAuthorized || err.code == CMErrorNotEntitled
           || err.code == CMErrorMotionActivityNotAuthorized || err.code == CMErrorMotionActivityNotEntitled;
}

// Starts or stops the motion updates
static void iosTouch_GyroRun(int bRun)
{
    bRun = bRun != 0;
    if (bRun == iosTouch_bGyroRunning) return;
    iosTouch_bGyroRunning = bRun;
    if (!bRun) {
        [iosTouch_pMotion stopDeviceMotionUpdates];
        return;
    }
    // From scratch: nothing from before it stopped, the smoothing empty
    os_unfair_lock_lock(&iosTouch_gyroLock);
    iosTouch_gyroLastT = 0.0;
    iosTouch_gyroYaw = iosTouch_gyroPitch = 0.0;
    memset(iosTouch_aGyroSmooth, 0, sizeof(iosTouch_aGyroSmooth));
    os_unfair_lock_unlock(&iosTouch_gyroLock);
    iosTouch_pMotion.deviceMotionUpdateInterval = 1.0 / IOSTOUCH_GYRO_HZ;
    // (the reference frame that needs no compass: the attitude isn't used)
    [iosTouch_pMotion startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXArbitraryZVertical
                                                          toQueue:iosTouch_pMotionQueue
                                                      withHandler:^(CMDeviceMotion* dm, NSError* err) {
        if (err && iosTouch_GyroIsRefusal(err)) {
            os_unfair_lock_lock(&iosTouch_gyroLock);
            iosTouch_bGyroRefusedOnQueue = 1;
            os_unfair_lock_unlock(&iosTouch_gyroLock);
        }
        if (!dm || err) return;
        CMRotationRate w = dm.rotationRate;
        CMAcceleration g = dm.gravity;
        iosTouch_GyroSample(w.x, w.y, w.z, g.x, g.y, g.z, dm.timestamp);
    }];
}

// Once, as the overlay is made: the motion manager and the queue for its
// samples (nothing runs yet), and the settings -- OFF and 1.5x unless set
static void iosTouch_GyroSetup(void)
{
    if (iosTouch_pMotion) return;
    iosTouch_pMotion = [[CMMotionManager alloc] init];
    iosTouch_pMotionQueue = [[NSOperationQueue alloc] init];
    iosTouch_pMotionQueue.maxConcurrentOperationCount = 1; // in order, one at a time
    iosTouch_pMotionQueue.qualityOfService = NSQualityOfServiceUserInteractive;

    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    // (a mode that was never set, or isn't one, is off)
    if ([d objectForKey:IOSTOUCH_GYRO_DEFAULTS_KEY]) {
        NSInteger m = [d integerForKey:IOSTOUCH_GYRO_DEFAULTS_KEY];
        iosTouch_gyroMode = (m >= 0 && m < IOSTOUCH_GYRO_NUM_MODES) ? (int)m : IOSTOUCH_GYRO_OFF;
    }
    if ([d objectForKey:IOSTOUCH_GYRO_SENS_DEFAULTS_KEY]) {
        float k = [d floatForKey:IOSTOUCH_GYRO_SENS_DEFAULTS_KEY];
        for (int i = 0; i < IOSTOUCH_COUNT(iosTouch_aGyroSens); i++) {
            if (fabsf(iosTouch_aGyroSens[i] - k) < fabsf(iosTouch_aGyroSens[iosTouch_gyroSens] - k)) iosTouch_gyroSens = i;
        }
    }
}

// Whether this device has the gyro aiming needs (not the Simulator), and iOS
// hasn't refused it
static int iosTouch_GyroAvailable(void)
{
    return iosTouch_pMotion && iosTouch_pMotion.deviceMotionAvailable && !iosTouch_bGyroRefused;
}

// The next mode or sensitivity (MENU's tray), kept in the app's settings
static void iosTouch_GyroNextMode(void)
{
    iosTouch_gyroMode = (iosTouch_gyroMode + 1) % IOSTOUCH_GYRO_NUM_MODES;
    [[NSUserDefaults standardUserDefaults] setInteger:iosTouch_gyroMode forKey:IOSTOUCH_GYRO_DEFAULTS_KEY];
}
static void iosTouch_GyroNextSens(void)
{
    iosTouch_gyroSens = (iosTouch_gyroSens + 1) % IOSTOUCH_COUNT(iosTouch_aGyroSens);
    [[NSUserDefaults standardUserDefaults] setFloat:iosTouch_aGyroSens[iosTouch_gyroSens] forKey:IOSTOUCH_GYRO_SENS_DEFAULTS_KEY];
}

// Whether a thumb that aims is down, for TOUCH: the touches that drag the
// view -- on the look area, or on one of the buttons under the right thumb
// that pass a drag through to looking (FIRE, ALT, DUCK, ACT, JUMP, FORCE).
// Not the stick, the top row, the wheel, or a touch being ignored.
static int iosTouch_GyroThumbDown(void)
{
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_LOOK || (s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].bLookWhileHeld)) return 1;
    }
    return 0;
}

// Once per frame, from iosTouch_UpdateGyro, after the frame's touches came in
// (bShown: whether the overlay v is up). The motion updates run only while it
// is shown, the app is active, the gyro is on and the screen is turned either
// landscape way. What the phone turned since the last frame goes to the mouse
// axes, on top of the look drag's movement -- if it could aim both then and
// now, so a lifted thumb freezes the view where it is (the frame it lifted in
// counts for nothing) and nothing springs back, and touching again carries on
// from there, from however the phone is held by then. Never while the force
// wheel (or anything else that holds the game), the typing line or the tray
// is open, just after the screen turned round to the other landscape side,
// nor after a frame that took too long.
static void iosTouch_GyroUpdate(int bShown, UIView* v)
{
    UIWindowScene* scene = v.window.windowScene;
    UIInterfaceOrientation o = scene ? scene.interfaceOrientation : UIInterfaceOrientationUnknown;
    int side = (o == UIInterfaceOrientationLandscapeLeft) ? 1 : ((o == UIInterfaceOrientationLandscapeRight) ? -1 : 0);
    int bActive = [UIApplication sharedApplication].applicationState == UIApplicationStateActive;
    CFTimeInterval now = CACurrentMediaTime();
    if (side != 0 && iosTouch_gyroLastSide != 0 && side != iosTouch_gyroLastSide) iosTouch_gyroTurnedRound = now;
    if (side != 0) iosTouch_gyroLastSide = side;
    iosTouch_GyroRun(bShown && bActive && side != 0 && iosTouch_gyroMode != IOSTOUCH_GYRO_OFF && iosTouch_GyroAvailable());

    os_unfair_lock_lock(&iosTouch_gyroLock);
    iosTouch_gyroSide = side;
    double yaw = iosTouch_gyroYaw, pitch = iosTouch_gyroPitch;
    iosTouch_gyroYaw = iosTouch_gyroPitch = 0.0;
    iosTouch_bGyroRefused |= iosTouch_bGyroRefusedOnQueue; // (stops it next frame)
    os_unfair_lock_unlock(&iosTouch_gyroLock);

    int bOk = iosTouch_bGyroRunning
              && (iosTouch_gyroMode == IOSTOUCH_GYRO_ALWAYS || iosTouch_GyroThumbDown())
              && !iosTouch_bWheelOpen && !iosGame_IsHolding() && !jkHud_bChatOpen && !iosTouch_bTrayOpen
              && now - iosTouch_gyroTurnedRound >= IOSTOUCH_GYRO_TURN_HOLD;
    int bUse = bOk && iosTouch_bGyroWasOk && now - iosTouch_gyroLastFrame <= IOSTOUCH_GYRO_MAX_FRAME;
    iosTouch_bGyroWasOk = bOk;
    iosTouch_gyroLastFrame = now;
    if (!bUse) return;

    // Into mouse counts through the game's own mouse look settings (Setup >
    // Controls > Mouse), so the view turns by that angle whatever their
    // sensitivity, and the same way round whether or not they are reversed.
    // Handed on in whole counts; the rest waits for the next frame.
    float turn = 0.0f, tilt = 0.0f; // degrees a count turns the view right / tilts it down
    iosGame_GetMouseLookDegrees(&turn, &tilt);
    float k = iosTouch_aGyroSens[iosTouch_gyroSens];
    if (fabsf(turn) > 0.001f) iosTouch_gyroCountX -= (float)yaw * k / turn;
    if (fabsf(tilt) > 0.001f) iosTouch_gyroCountY -= (float)pitch * k / tilt;
    int dx = (int)iosTouch_gyroCountX;
    int dy = (int)iosTouch_gyroCountY;
    iosTouch_gyroCountX -= (float)dx;
    iosTouch_gyroCountY -= (float)dy;
    Window_lastXRel += dx;
    Window_lastYRel += dy;
}

// ---------------------------------------------------------------- overlay view

@interface IOSTouchOverlay : UIView {
    UIView* stickBase;
    UIView* stickKnob;
    UIView* runMarker;       // the stick's run marker (Always Run off)...
    CAShapeLayer* runChevrons; // ...its "^^"...
    CAShapeLayer* runLink;   // ...and the line joining it to the stick's ring
    int bRunLit;             // what runMarker shows (-1: not yet set)
    UISelectionFeedbackGenerator* runTick;
    UILabel* aButtonViews[IOSTOUCH_NUM_BUTTONS];
    CAShapeLayer* saveRing; // QUICK SAVE's hold progress
    CAShapeLayer* loadRing; // QUICK LOAD's
    CAShapeLayer* menuRing; // MENU's
    UIView* trayBack;       // behind MENU's tray
    UILabel* fpsLabel;      // the FPS readout...
    int bFpsBase;           // ...counting frames since fpsBaseFrames, at fpsBaseTime
    unsigned int fpsBaseFrames;
    CFTimeInterval fpsBaseTime;
    const char* forceLabelName;
    int bForceLabelSet;
    int aItemAmount[IOSTOUCH_NUM_BUTTONS]; // KIND_ITEM: what the label shows (-1: not yet set)
    int aItemActive[IOSTOUCH_NUM_BUTTONS];
    UIView* wheelView;                     // dims the game; holds the wheel
    CAShapeLayer* wheelDisc;               // behind the ring
    CAShapeLayer* aSliceLayers[IOSTOUCH_WHEEL_MAX];
    UILabel* aSliceLabels[IOSTOUCH_WHEEL_MAX];
    int aSlicePopped[IOSTOUCH_WHEEL_MAX];  // the shape it has now (-1: none yet)
    // Each label fitted to its slice twice: [0] alone, [1] with the star row under it
    CGFloat aSliceFont[IOSTOUCH_WHEEL_MAX][2];   // its size (0: the stars don't fit)...
    CGFloat aSliceLabelR[IOSTOUCH_WHEEL_MAX][2]; // ...how far out it sits (with the stars: their middle)...
    CGSize aSliceLabelSize[IOSTOUCH_WHEEL_MAX][2]; // ...and how big it is
    CAShapeLayer* aSliceStars[IOSTOUCH_WHEEL_MAX][2]; // a learned power's level: its filled stars, its empty ones
    CGFloat wheelStarScale;                // their size, as a fraction of full size (fitWheelLabels)
    const iosTouchWheelMap* fitMap;        // the map and size the labels were fitted for
    CGFloat fitR1;
    UILabel* aGroupLabels[IOSTOUCH_WHEEL_MAX_GROUPS];
    CAShapeLayer* wheelNeedle;             // in the middle: which way the sliding thumb points
    UILabel* wheelTitle;                   // in the middle: the power pointed at, or the selected one
    UILabel* wheelHint;                    // under it: what lifting does, or what to do
    UILabel* wheelCancelLabel;             // in the gap at the bottom
    int wheelLastHot;                      // the slice pointed at last (for the haptic tick)
    int bWheelLayingOut;                   // laying out: no animating into place
    UISelectionFeedbackGenerator* wheelTick;
    CAShapeLayer* forceRing;               // around FORCE: how full the force meter is
    float forceRingFrac;                   // what it shows (-1: not yet set)
    int bForceRingFull;
    CFTimeInterval lastTickTime;           // when -tick last ran (0: not yet)
}
- (void)resetAll;
- (void)tick;
- (void)refreshGyroLabels;
- (void)refreshButtonLooks;
@end

@implementation IOSTouchOverlay

static UIView* IOSTouch_MakeCircle(CGFloat radius, CGFloat alpha)
{
    UIView* v = [[UIView alloc] initWithFrame:CGRectMake(0, 0, radius * 2, radius * 2)];
    v.backgroundColor = [UIColor colorWithWhite:1.0 alpha:alpha];
    v.layer.cornerRadius = radius;
    v.layer.borderWidth = 1.5;
    v.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.45].CGColor;
    v.userInteractionEnabled = NO;
    return v;
}

static CGFloat IOSTouch_FontSize(const char* label, CGFloat radius)
{
    if (strchr(label, '\n')) return radius >= 28 ? 10 : 9; // two-line labels
    return radius >= 40 ? 16 : (radius >= 25 ? 13 : 10);
}

// A ring round a button of radius r that fills (strokeEnd) while it is held
static CAShapeLayer* IOSTouch_MakeHoldRing(CGFloat r)
{
    CAShapeLayer* ring = [CAShapeLayer layer];
    ring.frame = CGRectMake(0, 0, r * 2, r * 2);
    ring.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(r, r) radius:r - 2.5
                                           startAngle:-M_PI_2 endAngle:3 * M_PI_2 clockwise:YES].CGPath;
    ring.fillColor = [UIColor clearColor].CGColor;
    ring.strokeColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor;
    ring.lineWidth = 3.0;
    ring.strokeEnd = 0.0;
    return ring;
}

// The wheel's labels are in the condensed system font, so the long names fit
static UIFont* IOSTouch_WheelFont(CGFloat size)
{
    if (@available(iOS 16.0, *)) {
        return [UIFont systemFontOfSize:size weight:UIFontWeightBold width:UIFontWidthCondensed];
    }
    return [UIFont boldSystemFontOfSize:size];
}

// A wheel group's colour, mixed k of the way to it from the wheel's dark grey
static UIColor* IOSTouch_WheelColour(const iosTouchWheelGroup* g, CGFloat k)
{
    return [UIColor colorWithRed:(g->r * k + 32.0 * (1.0 - k)) / 255.0
                           green:(g->g * k + 32.0 * (1.0 - k)) / 255.0
                            blue:(g->b * k + 32.0 * (1.0 - k)) / 255.0
                           alpha:1.0];
}

// A slice's shape: from deg0 round to deg1 (counter-clockwise), radius r0 to r1
static UIBezierPath* IOSTouch_WedgePath(CGPoint c, CGFloat deg0, CGFloat deg1, CGFloat r0, CGFloat r1)
{
    // UIKit's angles go clockwise (y points down)
    CGFloat a0 = -deg1 * M_PI / 180.0, a1 = -deg0 * M_PI / 180.0;
    UIBezierPath* p = [UIBezierPath bezierPathWithArcCenter:c radius:r1 startAngle:a0 endAngle:a1 clockwise:YES];
    [p addArcWithCenter:c radius:r0 startAngle:a1 endAngle:a0 clockwise:NO];
    [p closePath];
    return p;
}

// Adds a five-pointed star to p, point up, centred in its cell at c, at a
// fraction k of full size
static void IOSTouch_AddStar(UIBezierPath* p, CGPoint c, CGFloat k)
{
    // the points reach R above the middle but only R cos 36 below it: nudged
    // down by half the difference, so the star sits in the middle of its cell
    CGFloat R = IOSTOUCH_WHEEL_STAR_R * k, y = c.y + R * (1.0 - cos(M_PI / 5.0)) * 0.5;
    for (int j = 0; j < 10; j++) {
        CGFloat r = (j & 1) ? R * IOSTOUCH_WHEEL_STAR_INNER : R;
        CGFloat a = (-90.0 + 36.0 * j) * M_PI / 180.0;
        CGPoint q = CGPointMake(c.x + r * cos(a), y + r * sin(a));
        if (j == 0) [p moveToPoint:q];
        else [p addLineToPoint:q];
    }
    [p closePath];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.multipleTouchEnabled = YES;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
            iosTouchButton* b = &iosTouch_aButtons[i];
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, b->radius * 2, b->radius * 2)];
            l.text = [NSString stringWithUTF8String:b->label];
            l.textAlignment = NSTextAlignmentCenter;
            l.textColor = [UIColor colorWithWhite:1.0 alpha:0.85];
            l.font = [UIFont boldSystemFontOfSize:IOSTouch_FontSize(b->label, b->radius)];
            l.numberOfLines = 2;
            l.adjustsFontSizeToFitWidth = YES;
            l.minimumScaleFactor = 0.6;
            l.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.22];
            l.layer.cornerRadius = b->radius;
            l.layer.borderWidth = 1.5;
            l.layer.borderColor = (i == BTN_FIRE) ? [UIColor colorWithRed:1.0 green:0.55 blue:0.45 alpha:0.6].CGColor
                                                  : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
            l.clipsToBounds = YES;
            l.userInteractionEnabled = NO;
            aButtonViews[i] = l;
            [self addSubview:l];
        }

        // Rings that fill while QUICK SAVE, QUICK LOAD and MENU are held. QUICK
        // SAVE's and QUICK LOAD's look the same.
        saveRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_QUICKSAVE].radius);
        [aButtonViews[BTN_QUICKSAVE].layer addSublayer:saveRing];
        loadRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_QUICKLOAD].radius);
        [aButtonViews[BTN_QUICKLOAD].layer addSublayer:loadRing];
        menuRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_MENU].radius);
        [aButtonViews[BTN_MENU].layer addSublayer:menuRing];

        // Around FORCE: the force meter, a ring that empties as the meter does
        // (QUICK LOAD's ring the other way round) and glows when full
        CGFloat fr = iosTouch_aButtons[BTN_FORCE].radius;
        forceRing = [CAShapeLayer layer];
        forceRing.frame = CGRectMake(0, 0, fr * 2, fr * 2);
        forceRing.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(fr, fr) radius:fr - 2.5
                                                    startAngle:-M_PI_2 endAngle:3 * M_PI_2 clockwise:YES].CGPath;
        forceRing.fillColor = [UIColor clearColor].CGColor;
        forceRing.strokeColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor;
        forceRing.lineWidth = 3.0;
        forceRing.lineCap = kCALineCapRound;
        forceRing.strokeEnd = 0.0;
        forceRing.shadowColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:1.0].CGColor;
        forceRing.shadowOffset = CGSizeZero;
        forceRing.shadowRadius = 7.0;
        forceRing.shadowOpacity = 0.0;
        forceRing.hidden = YES;
        [self.layer insertSublayer:forceRing above:aButtonViews[BTN_FORCE].layer];
        forceRingFrac = -1.0f;
        bForceRingFull = 0;

        // MENU's tray: its buttons on a dark backing, hidden until a hold on
        // MENU opens it. The gyro's two say what they are set to; the
        // keyboard button shows the keyboard symbol.
        trayBack = [[UIView alloc] initWithFrame:CGRectZero];
        trayBack.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
        trayBack.layer.cornerRadius = 26.0;
        trayBack.layer.borderWidth = 1.5;
        trayBack.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        trayBack.userInteractionEnabled = NO;
        trayBack.hidden = YES;
        [self insertSubview:trayBack belowSubview:aButtonViews[BTN_TRAY_FIRST]];
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) aButtonViews[i].hidden = YES;
        iosTouch_GyroSetup();
        // GYRO and SENS say what they are set to on a second line: at the size
        // of a two-line label, condensed so "ALWAYS" fits a circle this small
        for (int i = BTN_TRAYSENS; i <= BTN_TRAYGYRO; i++) {
            aButtonViews[i].font = IOSTouch_WheelFont(IOSTouch_FontSize("GYRO\nALWAYS", iosTouch_aButtons[i].radius));
        }
        [self refreshGyroLabels];
        UIImageSymbolConfiguration* cfg = [UIImageSymbolConfiguration configurationWithPointSize:14 weight:UIImageSymbolWeightSemibold];
        UIImage* kb = [UIImage systemImageNamed:@"keyboard" withConfiguration:cfg];
        if (kb) {
            NSTextAttachment* att = [[NSTextAttachment alloc] init];
            att.image = [kb imageWithTintColor:[UIColor colorWithWhite:1.0 alpha:0.85] renderingMode:UIImageRenderingModeAlwaysOriginal];
            aButtonViews[BTN_TRAYKEYS].attributedText = [NSAttributedString attributedStringWithAttachment:att];
        }
        else {
            aButtonViews[BTN_TRAYKEYS].text = @"TYPE";
        }

        // The FPS readout, shown if it was left on
        fpsLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 54, 18)];
        fpsLabel.textAlignment = NSTextAlignmentCenter;
        fpsLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.85];
        fpsLabel.font = [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightSemibold];
        fpsLabel.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.35];
        fpsLabel.layer.cornerRadius = 9.0;
        fpsLabel.clipsToBounds = YES;
        fpsLabel.userInteractionEnabled = NO;
        fpsLabel.text = @"-- FPS";
        iosTouch_bShowFps = [[NSUserDefaults standardUserDefaults] boolForKey:IOSTOUCH_FPS_DEFAULTS_KEY] ? 1 : 0;
        fpsLabel.hidden = !iosTouch_bShowFps;
        [self addSubview:fpsLabel];

        // Item buttons appear once the player has the item (see -tick)
        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
            aItemAmount[i] = -1;
            aItemActive[i] = 0;
            if (iosTouch_aButtons[i].kind == KIND_ITEM) aButtonViews[i].hidden = YES;
        }

        stickBase = IOSTouch_MakeCircle(IOSTOUCH_STICK_RADIUS, 0.08);
        stickKnob = IOSTouch_MakeCircle(IOSTOUCH_STICK_KNOB, 0.30);
        stickBase.hidden = YES;
        stickKnob.hidden = YES;
        [self addSubview:stickBase];
        [self addSubview:stickKnob];

        // The stick's run marker: a small circle in the buttons' style with
        // "^^" in it, above the knob (which sits on it while running), and
        // its line to the stick's ring, under the knob. Shown with the stick.
        runLink = [CAShapeLayer layer];
        runLink.lineWidth = 1.5;
        runLink.lineCap = kCALineCapRound;
        runLink.fillColor = [UIColor clearColor].CGColor;
        runLink.hidden = YES;
        [self.layer insertSublayer:runLink above:stickBase.layer];
        const CGFloat rr = IOSTOUCH_RUN_R;
        runMarker = [[UIView alloc] initWithFrame:CGRectMake(0, 0, rr * 2, rr * 2)];
        runMarker.layer.cornerRadius = rr;
        runMarker.layer.borderWidth = 1.5;
        runMarker.userInteractionEnabled = NO;
        runMarker.hidden = YES;
        runChevrons = [CAShapeLayer layer];
        runChevrons.frame = CGRectMake(0, 0, rr * 2, rr * 2);
        UIBezierPath* chev = [UIBezierPath bezierPath];
        for (int i = 0; i < 2; i++) {
            CGFloat tip = rr - 5.5 + 5.5 * i; // two chevrons, 12pt wide, 5.5pt apart, centred
            [chev moveToPoint:CGPointMake(rr - 6.0, tip + 5.0)];
            [chev addLineToPoint:CGPointMake(rr, tip)];
            [chev addLineToPoint:CGPointMake(rr + 6.0, tip + 5.0)];
        }
        runChevrons.path = chev.CGPath;
        runChevrons.lineWidth = 2.0;
        runChevrons.lineCap = kCALineCapRound;
        runChevrons.lineJoin = kCALineJoinRound;
        runChevrons.fillColor = [UIColor clearColor].CGColor;
        [runMarker.layer addSublayer:runChevrons];
        [self addSubview:runMarker];
        bRunLit = -1;
        [self setRunLit:0];
        runTick = [[UISelectionFeedbackGenerator alloc] init];

        // The force wheel, on top of everything, hidden until opened: a dark
        // disc, the slices on it, and the labels above them (higher up than a
        // popped-out slice, which never covers one)
        wheelView = [[UIView alloc] initWithFrame:self.bounds];
        wheelView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        wheelView.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
        wheelView.userInteractionEnabled = NO; // touches are handled here, by the overlay
        wheelView.hidden = YES;
        wheelDisc = [CAShapeLayer layer];
        wheelDisc.fillColor = [UIColor colorWithWhite:0.0 alpha:0.85].CGColor;
        [wheelView.layer addSublayer:wheelDisc];
        for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
            CAShapeLayer* sl = [CAShapeLayer layer];
            sl.lineJoin = kCALineJoinRound;
            sl.hidden = YES;
            aSliceLayers[i] = sl;
            aSlicePopped[i] = -1;
            [wheelView.layer addSublayer:sl];
        }
        wheelNeedle = [CAShapeLayer layer];
        wheelNeedle.lineWidth = 3.0;
        wheelNeedle.lineCap = kCALineCapRound;
        wheelNeedle.fillColor = [UIColor clearColor].CGColor;
        wheelNeedle.hidden = YES;
        [wheelView.layer addSublayer:wheelNeedle];
        for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
            for (int k = 0; k < 2; k++) {
                // up with the labels, above a popped-out slice (line widths: layoutWheel)
                CAShapeLayer* st = [CAShapeLayer layer];
                st.lineJoin = kCALineJoinRound;
                if (k) st.fillColor = [UIColor clearColor].CGColor;
                st.zPosition = 2.0;
                st.hidden = YES;
                aSliceStars[i][k] = st;
                [wheelView.layer addSublayer:st];
            }
        }
        for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectZero];
            l.textAlignment = NSTextAlignmentCenter;
            l.numberOfLines = 2;
            l.adjustsFontSizeToFitWidth = YES; // only in case the fit (fitWheelLabels) is a little off
            l.minimumScaleFactor = 0.8;
            l.layer.zPosition = 2.0;
            l.hidden = YES;
            aSliceLabels[i] = l;
            [wheelView addSubview:l];
        }
        for (int i = 0; i < IOSTOUCH_WHEEL_MAX_GROUPS; i++) {
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectZero];
            l.textAlignment = NSTextAlignmentCenter;
            l.font = [UIFont boldSystemFontOfSize:12];
            l.numberOfLines = 2;
            l.layer.zPosition = 2.0;
            l.hidden = YES;
            aGroupLabels[i] = l;
            [wheelView addSubview:l];
        }
        wheelTitle = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 100, 24)];
        wheelTitle.textAlignment = NSTextAlignmentCenter;
        wheelTitle.font = [UIFont boldSystemFontOfSize:17];
        wheelTitle.adjustsFontSizeToFitWidth = YES;
        wheelTitle.minimumScaleFactor = 0.6;
        wheelTitle.layer.zPosition = 2.0;
        [wheelView addSubview:wheelTitle];
        wheelHint = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 100, 14)];
        wheelHint.textAlignment = NSTextAlignmentCenter;
        wheelHint.font = [UIFont systemFontOfSize:10 weight:UIFontWeightSemibold];
        wheelHint.textColor = [UIColor colorWithWhite:0.8 alpha:1.0];
        wheelHint.adjustsFontSizeToFitWidth = YES;
        wheelHint.minimumScaleFactor = 0.8;
        wheelHint.layer.zPosition = 2.0;
        [wheelView addSubview:wheelHint];
        wheelCancelLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 80, 14)];
        wheelCancelLabel.textAlignment = NSTextAlignmentCenter;
        wheelCancelLabel.font = [UIFont systemFontOfSize:9 weight:UIFontWeightSemibold];
        wheelCancelLabel.textColor = [UIColor colorWithWhite:0.59 alpha:1.0];
        wheelCancelLabel.text = @"cancel";
        wheelCancelLabel.layer.zPosition = 2.0;
        [wheelView addSubview:wheelCancelLabel];
        [self addSubview:wheelView];
        wheelLastHot = -1;
        wheelTick = [[UISelectionFeedbackGenerator alloc] init];

        [self refreshButtonLooks];
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat W = b.size.width, H = b.size.height;
    UIEdgeInsets in = UIEdgeInsetsZero;
    if (@available(iOS 11.0, *)) {
        in = self.safeAreaInsets;
    }
    CGFloat left = MAX(in.left, 8.0);
    CGFloat right = W - MAX(in.right, 8.0);
    CGFloat top = MAX(in.top, 8.0);
    CGFloat bottom = H - MAX(in.bottom, 8.0);

    // Where the HUD's right (ammo/force) gauge is; iosTouch_Update re-runs
    // this layout whenever it moves
    float g[4] = {0, 0, 0, 0};
    iosTouch_bLayoutGauge = jkHud_IosGetRightGaugeRectPt(&g[0], &g[1], &g[2], &g[3]);
    memcpy(iosTouch_aLayoutGauge, g, sizeof(g));

    // FIRE in the corner, left of the gauge. right - 76 clears it on notched
    // iPhones; where it is wider in points (no side insets: Home-button
    // iPhones, iPads) FIRE moves left of it.
    const CGFloat FR = iosTouch_aButtons[BTN_FIRE].radius;
    CGFloat fireX = right - 76;
    if (iosTouch_bLayoutGauge && fireX + FR + 4 > g[0]) {
        fireX = g[0] - FR - 4;
    }
    CGPoint fire = CGPointMake(fireX, bottom - 58);
    iosTouch_aButtons[BTN_FIRE].x = fire.x;
    iosTouch_aButtons[BTN_FIRE].y = fire.y;

    // DUCK, ACT and JUMP on an arc around FIRE (angles counter-clockwise from
    // pointing right, so 90 is straight up), 45 degrees apart -- which at this
    // radius leaves about IOSTOUCH_CLUSTER_GAP between them
    const CGFloat arc = 115.0;
    const int aArcBtn[3] = { BTN_DUCK, BTN_ACT, BTN_JUMP };
    const CGFloat aArcDeg[3] = { 180.0f, 135.0f, 90.0f };
    for (int i = 0; i < 3; i++) {
        CGFloat rad = aArcDeg[i] * (CGFloat)M_PI / 180.0;
        iosTouch_aButtons[aArcBtn[i]].x = fire.x + arc * cos(rad);
        iosTouch_aButtons[aArcBtn[i]].y = fire.y - arc * sin(rad);
    }

    // The camera cutout, when it is on the right, as a capsule along the right
    // edge: the Dynamic Island (side inset ~59-62pt) is ~126x37pt, 11pt in
    // from the edge; a notch (side inset ~44-50pt) is up to ~210x33pt at the
    // edge. The landscape side insets are the same both ways round, so which
    // side it is on comes from the screen's orientation.
    iosTouch_layoutCutoutRight = iosTouch_CutoutMayBeRight(self);
    int bCutout = in.right >= 40.0 && iosTouch_layoutCutoutRight;
    CGFloat cutX = (in.right >= 55.0) ? W - 29.5 : W - 16.5;
    CGFloat cutR = (in.right >= 55.0) ? 18.5 : 16.5;
    CGFloat cutHalf = ((in.right >= 55.0) ? 63.0 : 105.0) - cutR;

    // ALT: up and to the right of FIRE, IOSTOUCH_CLUSTER_GAP from it, as low
    // as it can sit while staying above the ammo gauge, on screen, clear of
    // JUMP and clear of the cutout.
    {
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        const CGFloat AR = a->radius;
        const CGFloat D = FR + AR + IOSTOUCH_CLUSTER_GAP;
        // If nothing at that distance fits (a short screen with the cutout
        // between the gauge and JUMP, or an iPad's tall gauge), step outwards;
        // failing that, allow ALT closer to JUMP.
        CGPoint best = CGPointMake(fire.x + D * cos(M_PI * 35.0 / 180.0), fire.y - D * sin(M_PI * 35.0 / 180.0));
        int bFound = 0;
        for (int pass = 0; pass < 2 && !bFound; pass++) {
            const CGFloat jumpGap = pass ? 2.0 : 8.0;
            for (int extra = 0; extra <= 80 && !bFound; extra += 2) {
                for (int deg = 10; deg <= 85; deg++) {
                    CGFloat rad = deg * (CGFloat)M_PI / 180.0;
                    CGPoint p = CGPointMake(fire.x + (D + extra) * cos(rad), fire.y - (D + extra) * sin(rad));
                    if (p.x + AR > W - 4 || p.y - AR < top + 60) continue;                    // on screen, below the top row
                    if (iosTouch_bLayoutGauge && p.y + AR > g[1] - 4) continue;               // above the gauge
                    CGFloat dj = sqrt((p.x - j->x) * (p.x - j->x) + (p.y - j->y) * (p.y - j->y));
                    if (dj < AR + j->radius + jumpGap) continue;                               // clear of JUMP
                    if (bCutout && iosTouch_DistToSegment(p, cutX, H * 0.5 - cutHalf, cutX, H * 0.5 + cutHalf) < AR + cutR + 2)
                        continue;                                                              // clear of the cutout
                    best = p;
                    bFound = 1;
                    break;
                }
            }
        }
        // Last resort (e.g. a very large HUD scale on an iPad): never on the
        // gauge's numbers -- lift it above the gauge
        if (!bFound && iosTouch_bLayoutGauge && best.y + AR > g[1] - 4) {
            best.y = MAX(g[1] - 4 - AR, top + 60 + AR);
        }
        a->x = best.x;
        a->y = best.y;
    }

    // Top left: NEXT WPN | FORCE WHEEL | LIGHT, IR, BACTA -- each item keeps its
    // own place whether or not the ones before it are showing
    iosTouch_aButtons[BTN_NEXTWPN].x = left + 24;
    iosTouch_aButtons[BTN_WHEEL].x = left + 84;
    iosTouch_aButtons[BTN_LIGHT].x = left + 148;
    iosTouch_aButtons[BTN_IR].x = left + 204;
    iosTouch_aButtons[BTN_BACTA].x = left + 260;
    // Top right: QUICK SAVE, QUICK LOAD, MENU in the corner, 66 pt apart (10 pt
    // between where each takes touches): with both quick holds 0.3 s, that is
    // what keeps a press meant for one off the other (and FORCE, below, keeps
    // at least as far from every top row button)
    iosTouch_aButtons[BTN_QUICKSAVE].x = right - 156;
    iosTouch_aButtons[BTN_QUICKLOAD].x = right - 90;
    iosTouch_aButtons[BTN_MENU].x = right - 24;
    const int aTopRow[] = { BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
                            BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU };
    for (int i = 0; i < IOSTOUCH_COUNT(aTopRow); i++) {
        iosTouch_aButtons[aTopRow[i]].y = top + 26;
    }

    // FORCE: right of JUMP and above ALT, out of the way of aiming -- on the
    // circle IOSTOUCH_CLUSTER_GAP out from JUMP, as far round towards pointing
    // right as it fits on screen, below the top row and clear of ALT, ACT,
    // FIRE, the gauge and the cutout. Where the cutout (or, on smaller
    // screens, ALT) takes that spot it goes higher, over JUMP; failing that,
    // the gaps shrink, and with the smallest it may go on round, over ACT.
    // Wherever it goes, it keeps clear of the top row and of MENU's tray:
    // FORCE is held (Force Jump, Lightning) and QUICK SAVE and QUICK LOAD go
    // off after a 0.3 s hold, so it is never nearer any top row button than
    // QUICK SAVE is to QUICK LOAD (10 pt between where each takes touches),
    // and never where MENU's open tray would have to cover it, or come nearer
    // the other buttons round FIRE than it could with FORCE elsewhere
    // (iosTouch_TrayKeysAt).
    {
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* c = &iosTouch_aButtons[BTN_ACT];
        const CGFloat R = f->radius;
        // Pass by pass: the gap (edge to edge) from ALT, ACT, FIRE and JUMP,
        // and how far round from pointing right it looks (180: straight left)
        const CGFloat aGap[] = { IOSTOUCH_CLUSTER_GAP, 20.0, 12.0 };
        const int aMaxDeg[] = { 135, 135, 180 };
        // In every pass, edge to edge from each top row button
        const CGFloat topGap = IOSTOUCH_TOUCH_SLOP + IOSTOUCH_TOUCH_SLOP + 10.0;
        const CGPoint menu = CGPointMake(iosTouch_aButtons[BTN_MENU].x, iosTouch_aButtons[BTN_MENU].y);
        // ...and where MENU's tray can sit as clear of the other buttons round
        // FIRE as it could without FORCE (IOSTOUCH_TRAY_CLEAR, unless they
        // leave it less), and that clear of FORCE
        CGFloat trayFree;
        iosTouch_TrayKeysAt(menu, left, NULL, &trayFree, NULL);
        const CGFloat trayNeed = MIN((CGFloat)IOSTOUCH_TRAY_CLEAR, trayFree);
        // If nothing fits (a very short screen at a large HUD scale, e.g. with
        // Display Zoom): just outside the arc, IOSTOUCH_CLUSTER_GAP from both
        // DUCK and ACT
        iosTouchButton* d = &iosTouch_aButtons[BTN_DUCK];
        CGFloat dist = d->radius + R + IOSTOUCH_CLUSTER_GAP; // DUCK and ACT are the same size
        CGFloat mx = (d->x + c->x) * 0.5, my = (d->y + c->y) * 0.5;
        CGFloat vx = c->x - d->x, vy = c->y - d->y;
        CGFloat L = sqrt(vx * vx + vy * vy);
        CGFloat h = (dist > L * 0.5) ? sqrt(dist * dist - L * L * 0.25) : 0;
        CGFloat px = vy / L, py = -vx / L; // the perpendicular pointing away from FIRE
        if ((mx - fire.x) * px + (my - fire.y) * py < 0) { px = -px; py = -py; }
        CGPoint best = CGPointMake(mx + px * h, my + py * h);
        int bFound = 0;
        for (int pass = 0; pass < IOSTOUCH_COUNT(aGap) && !bFound; pass++) {
            const CGFloat gap = aGap[pass];
            const CGFloat D = j->radius + R + gap;
            for (int deg = 0; deg <= aMaxDeg[pass]; deg++) {
                CGFloat rad = deg * (CGFloat)M_PI / 180.0;
                CGPoint p = CGPointMake(j->x + D * cos(rad), j->y - D * sin(rad));
                if (p.x + R > W - 4 || p.y - R < top + 60) continue;                 // on screen, below the top row
                if (hypot(p.x - a->x, p.y - a->y) < R + a->radius + gap) continue;     // clear of ALT
                if (hypot(p.x - c->x, p.y - c->y) < R + c->radius + gap) continue;     // ACT
                if (hypot(p.x - fire.x, p.y - fire.y) < R + FR + gap) continue;        // FIRE
                if (iosTouch_bLayoutGauge) {                                           // the gauge
                    CGFloat gx = MIN(MAX(p.x, g[0]), g[2]), gy = MIN(MAX(p.y, g[1]), g[3]);
                    if (hypot(p.x - gx, p.y - gy) < R + 4) continue;
                }
                if (bCutout && iosTouch_DistToSegment(p, cutX, H * 0.5 - cutHalf, cutX, H * 0.5 + cutHalf) < R + cutR + 2)
                    continue;                                                          // the cutout
                int bNearTop = 0;                                                      // the top row
                for (int t = 0; t < IOSTOUCH_COUNT(aTopRow); t++) {
                    iosTouchButton* q = &iosTouch_aButtons[aTopRow[t]];
                    if (hypot(p.x - q->x, p.y - q->y) < R + q->radius + topGap) bNearTop = 1;
                }
                if (bNearTop) continue;
                CGFloat trayClear, trayForce;                                          // MENU's tray
                iosTouch_TrayKeysAt(menu, left, &p, &trayClear, &trayForce);
                if (trayClear < trayNeed || trayForce < IOSTOUCH_TRAY_CLEAR) continue;
                best = p;
                bFound = 1;
                break;
            }
        }
        f->x = best.x;
        f->y = best.y;
    }

    // MENU's tray: a row just under the corner -- SENS, GYRO, FPS and the
    // keyboard, right below MENU (a held thumb slides straight down onto it).
    // Where the buttons round FIRE reach up into that corner (FORCE, on a
    // smaller screen with the cutout on the right; any of them with Display
    // Zoom or a large HUD scale) the row moves left until its backing is
    // IOSTOUCH_TRAY_CLEAR clear of all of them, never off the left of the
    // screen (iosTouch_TrayKeysAt); FORCE's placement above makes sure FORCE
    // never stops it, unless FORCE fell back to its last resort. (While open,
    // the tray is on top and takes its own touches.)
    {
        iosTouchButton* m = &iosTouch_aButtons[BTN_MENU];
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        const CGPoint force = CGPointMake(f->x, f->y);
        CGPoint k = iosTouch_TrayKeysAt(CGPointMake(m->x, m->y), left, &force, NULL, NULL);
        const CGFloat len = IOSTOUCH_TRAY_STEP * (BTN_TRAY_LAST - BTN_TRAY_FIRST);
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) {
            iosTouch_aButtons[i].x = k.x - IOSTOUCH_TRAY_STEP * (BTN_TRAY_LAST - i);
            iosTouch_aButtons[i].y = k.y;
        }
        trayBack.frame = CGRectMake(k.x - len - 26, k.y - 26, len + 52, 52);
    }
    // The FPS readout where the keyboard button used to be, left of QUICK
    // SAVE: the game draws its messages and typing line centred at the top
    fpsLabel.center = CGPointMake(right - 216, top + 26);

    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        aButtonViews[i].center = CGPointMake(iosTouch_aButtons[i].x, iosTouch_aButtons[i].y);
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    forceRing.position = CGPointMake(iosTouch_aButtons[BTN_FORCE].x, iosTouch_aButtons[BTN_FORCE].y);
    [CATransaction commit];
    if (iosTouch_bWheelOpen) [self layoutWheel];
}

// Closest button the touch is on (with a little forgiveness), so a touch in the
// gap between two buttons picks the nearer one rather than the first listed.
// The open MENU tray is drawn on top of the rest (on a very short screen it can
// lie over the buttons round FIRE), so its buttons come first: anywhere on its
// backing (which reaches as far round each as that forgiveness does) is on the
// nearest one, the corners between two of them too.
- (int)buttonAt:(CGPoint)p
{
    int best = -1;
    CGFloat bestDist = 0;
    iosTouchButton* l = &iosTouch_aButtons[BTN_TRAY_FIRST];
    iosTouchButton* r = &iosTouch_aButtons[BTN_TRAY_LAST];
    if (iosTouch_bTrayOpen && iosTouch_DistToSegment(p, l->x, l->y, r->x, r->y) <= 26) {
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) {
            CGFloat d = fabs(p.x - iosTouch_aButtons[i].x);
            if (best < 0 || d < bestDist) {
                best = i;
                bestDist = d;
            }
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (aButtonViews[i].hidden) continue; // an item the player doesn't have, or the closed tray
        CGFloat dx = p.x - b->x, dy = p.y - b->y;
        CGFloat d = sqrt(dx * dx + dy * dy);
        if (d <= b->radius + IOSTOUCH_TOUCH_SLOP && (best < 0 || d - b->radius < bestDist)) {
            best = i;
            bestDist = d - b->radius;
        }
    }
    return best;
}

- (void)hideStick
{
    stickBase.hidden = YES;
    stickKnob.hidden = YES;
    runMarker.hidden = YES;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    runLink.hidden = YES;
    [CATransaction commit];
    iosTouch_bRunMarker = 0;
    iosTouch_bStickRunning = 0;
}

- (void)refreshButtonLooks
{
    int bGyro = iosTouch_GyroAvailable();
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        int bHeld = iosTouch_aButtonHeld[i] != 0;
        // A thumb still on MENU after its tray opened lights the tray button it is over
        for (int t = 0; t < IOSTOUCH_MAX_TOUCHES && !bHeld; t++) {
            iosTouchSlot* s = &iosTouch_aSlots[t];
            bHeld = s->touch && s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && s->trayButton == i;
        }
        // FORCE WHEEL stays lit above the dimmed screen while the wheel is open
        int bWheel = i == BTN_WHEEL && iosTouch_bWheelOpen;
        aButtonViews[i].backgroundColor = bWheel ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.55]
                                                 : [UIColor colorWithWhite:(bHeld ? 1.0 : 0.0) alpha:(bHeld ? 0.30 : 0.22)];
        // A switched-on item (field light, IR goggles), the open typing line
        // (on MENU and the tray's keyboard), the FPS readout being on (on
        // the tray's FPS) and gyro aiming being on (on the tray's GYRO)
        // stand out in yellow, at full strength
        int bOn = (iosTouch_aButtons[i].kind == KIND_ITEM && aItemActive[i])
                  || ((i == BTN_TRAYKEYS || i == BTN_MENU) && jkHud_bChatOpen)
                  || (i == BTN_TRAYFPS && iosTouch_bShowFps)
                  || (i == BTN_TRAYGYRO && bGyro && iosTouch_gyroMode != IOSTOUCH_GYRO_OFF);
        int bTray = iosTouch_IsTrayButton(i); // only there while the tray is open
        CGFloat alpha = (bHeld || bOn || bWheel || bTray) ? 1.0 : IOSTOUCH_IDLE_ALPHA;
        if (i == BTN_FORCE && bForceLabelSet && !forceLabelName) alpha *= 0.45; // no power to use yet
        if (i == BTN_TRAYGYRO && !bGyro) alpha *= 0.45;                           // no gyro here
        if (i == BTN_TRAYSENS && (!bGyro || iosTouch_gyroMode == IOSTOUCH_GYRO_OFF)) alpha *= 0.45; // ...or it's off
        aButtonViews[i].alpha = alpha;
        if (i != BTN_FIRE) {
            aButtonViews[i].layer.borderColor = bOn ? [UIColor colorWithRed:1.0 green:0.85 blue:0.3 alpha:0.95].CGColor
                                                    : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        }
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    forceRing.opacity = bForceRingFull ? 1.0 : (iosTouch_aButtonHeld[BTN_FORCE] ? 1.0 : IOSTOUCH_IDLE_ALPHA);
    [CATransaction commit];
}

- (void)setRing:(CAShapeLayer*)ring progress:(CGFloat)progress
{
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    ring.strokeEnd = progress;
    [CATransaction commit];
}

- (void)updateStickVisual:(iosTouchSlot*)s
{
    stickBase.center = s->origin;
    // the knob follows the thumb, up to the ring -- or sits on the run marker
    stickKnob.center = iosTouch_bStickRunning ? iosTouch_runMarkerPos
                                              : CGPointMake(s->origin.x + iosTouch_stickX * IOSTOUCH_STICK_RADIUS,
                                                            s->origin.y + iosTouch_stickY * IOSTOUCH_STICK_RADIUS);
    stickBase.hidden = NO;
    stickKnob.hidden = NO;
    [self setRunLit:iosTouch_bStickRunning];
}

// The run marker's look: as an idle button (a little darker inside, like the
// FPS readout, so its "^^" shows over bright walls too), or lit (blue, like
// FORCE WHEEL while its wheel is open) at full strength while the thumb is on it
- (void)setRunLit:(int)bLit
{
    bLit = bLit != 0;
    if (bRunLit == bLit) return;
    bRunLit = bLit;
    runMarker.alpha = bLit ? 1.0 : IOSTOUCH_IDLE_ALPHA;
    runMarker.backgroundColor = bLit ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.55]
                                     : [UIColor colorWithWhite:0.0 alpha:0.35];
    runMarker.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:(bLit ? 0.85 : 0.45)].CGColor;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    runChevrons.strokeColor = [UIColor colorWithWhite:1.0 alpha:(bLit ? 1.0 : 0.85)].CGColor;
    runLink.strokeColor = bLit ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor
                               : [UIColor colorWithWhite:1.0 alpha:0.45].CGColor;
    runLink.opacity = bLit ? 1.0 : IOSTOUCH_IDLE_ALPHA;
    [CATransaction commit];
}

// Puts the run marker in place for a stick centred on o -- if Always Run is
// off (iosTouch_bAlwaysRun). It goes IOSTOUCH_RUN_DIST straight above the
// centre or, where the top of the screen or a button (the top row) is in the
// way, as near that as it fits, 4pt clear of them: no nearer the centre than
// IOSTOUCH_RUN_MIN_DIST, else there is no marker this time. Its edge also
// keeps 4pt right of the safe area's left edge (the camera cutout may be
// there; the knob, on the marker while running, then stays clear of it too),
// so for a thumb down that far left it is a little right of straight above.
- (void)placeRunMarker:(CGPoint)o
{
    iosTouch_bRunMarker = 0;
    iosTouch_bStickRunning = 0;
    iosTouch_runDeg = 90.0;
    const CGFloat R = IOSTOUCH_RUN_R;
    UIEdgeInsets in = self.safeAreaInsets;
    CGFloat x = MAX(o.x, MAX(in.left, 8.0) + 4.0 + R);
    CGFloat minY = MAX(in.top, 0.0) + 4.0 + R;
    for (CGFloat h = IOSTOUCH_RUN_DIST; h >= IOSTOUCH_RUN_MIN_DIST && iosTouch_bAlwaysRun == 0; h -= 2.0) {
        CGPoint c = CGPointMake(x, o.y - h);
        if (c.y < minY) continue;
        int bClear = 1;
        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS && bClear; i++) {
            iosTouchButton* b = &iosTouch_aButtons[i];
            if (aButtonViews[i].hidden) continue;
            bClear = hypot(c.x - b->x, c.y - b->y) >= b->radius + R + 4.0;
        }
        if (!bClear) continue;
        iosTouch_bRunMarker = 1;
        iosTouch_runMarkerPos = c;
        iosTouch_runDeg = atan2(h, x - o.x) * 180.0 / M_PI;
        break;
    }

    runMarker.hidden = !iosTouch_bRunMarker;
    runMarker.center = iosTouch_runMarkerPos;
    // The line, from the ring to the marker's edge -- none once the marker
    // has come down onto the ring
    CGPoint c = iosTouch_runMarkerPos;
    CGFloat dx = c.x - o.x, dy = c.y - o.y, d = sqrt(dx * dx + dy * dy);
    int bLink = iosTouch_bRunMarker && d - R - IOSTOUCH_STICK_RADIUS > 1.0;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (bLink) {
        UIBezierPath* p = [UIBezierPath bezierPath];
        [p moveToPoint:CGPointMake(o.x + dx / d * IOSTOUCH_STICK_RADIUS, o.y + dy / d * IOSTOUCH_STICK_RADIUS)];
        [p addLineToPoint:CGPointMake(o.x + dx / d * (d - R), o.y + dy / d * (d - R))];
        runLink.path = p.CGPath;
    }
    runLink.hidden = !bLink;
    [CATransaction commit];
    if (iosTouch_bRunMarker) [runTick prepare];
}

// The stick's thumb now at p: onto the run marker or off it, with a light
// tick as it starts running
- (void)updateStickRunAt:(CGPoint)p origin:(CGPoint)o
{
    int bRun = iosTouch_StickOnRunMarker(p, o, iosTouch_bStickRunning);
    if (bRun && !iosTouch_bStickRunning) [runTick selectionChanged];
    iosTouch_bStickRunning = bRun;
}

// ------------------------------------------------------------ MENU tray, FPS

// Opens or closes MENU's tray. Closing it lets go of any touch on its buttons.
- (void)setTrayOpen:(int)bOpen
{
    bOpen = bOpen != 0;
    if (iosTouch_bTrayOpen == bOpen) return;
    iosTouch_bTrayOpen = bOpen;
    trayBack.hidden = !bOpen;
    for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) aButtonViews[i].hidden = !bOpen;
    if (!bOpen) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (!s->touch || s->role != ROLE_BUTTON) continue;
            if (iosTouch_IsTrayButton(s->button)) {
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                s->role = ROLE_IGNORED;
            }
            s->trayButton = -1;
        }
    }
    [self refreshButtonLooks];
}

- (void)setShowFps:(int)bShow
{
    iosTouch_bShowFps = bShow != 0;
    fpsLabel.hidden = !iosTouch_bShowFps;
    fpsLabel.text = @"-- FPS";
    bFpsBase = 0;
    [[NSUserDefaults standardUserDefaults] setBool:(iosTouch_bShowFps ? YES : NO) forKey:IOSTOUCH_FPS_DEFAULTS_KEY];
    [self refreshButtonLooks];
}

// What the tray's gyro buttons say: the mode and the sensitivity
- (void)refreshGyroLabels
{
    static const char* aModeName[IOSTOUCH_GYRO_NUM_MODES] = { "OFF", "TOUCH", "ALWAYS" };
    aButtonViews[BTN_TRAYGYRO].text = iosTouch_GyroAvailable() ? [NSString stringWithFormat:@"GYRO\n%s", aModeName[iosTouch_gyroMode]]
                                                               : @"NO\nGYRO";
    aButtonViews[BTN_TRAYSENS].text = [NSString stringWithFormat:@"SENS\n%.1fx", (double)iosTouch_aGyroSens[iosTouch_gyroSens]];
}

// A tray button picked: it does its thing, and the tray closes -- except
// after GYRO and SENS, which stay for another tap to go on to the next
- (void)useTrayButton:(int)button
{
    int kind = iosTouch_aButtons[button].kind;
    if (kind == KIND_CHAT) {
        iosGame_ToggleChat();
    }
    else if (kind == KIND_FPS) {
        [self setShowFps:!iosTouch_bShowFps];
    }
    else if ((kind == KIND_GYRO || kind == KIND_GYROSENS) && iosTouch_GyroAvailable()) {
        if (kind == KIND_GYRO) iosTouch_GyroNextMode();
        else iosTouch_GyroNextSens();
        [self refreshGyroLabels];
    }
    if (kind == KIND_GYRO || kind == KIND_GYROSENS) [self refreshButtonLooks];
    else [self setTrayOpen:0];
}

// ------------------------------------------------------------ force wheel

// Fits a slice's label inside it: the biggest size (11pt down to 7pt) at which
// it fits somewhere along the slice, as near the middle of the band as it can
// go at that size (s11: its size at 11pt; text width goes with font size). With
// bStars, the label and the star row under it are fitted as one block, so
// neither crosses the slice's edges. Returns whether it fitted.
- (int)fitWheelLabel:(int)i size:(CGSize)s11 stars:(int)bStars
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, R1 = iosTouch_wheelR1;
    CGFloat starW = ((IOSTOUCH_WHEEL_STARS - 1) * IOSTOUCH_WHEEL_STAR_STEP + IOSTOUCH_WHEEL_STAR_CELL) * wheelStarScale;
    CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
    // no room for the stars: the label goes on alone
    aSliceFont[i][bStars] = bStars ? 0.0 : 7.0;
    aSliceLabelR[i][bStars] = R0 + (R1 - R0) * 0.58;
    aSliceLabelSize[i][bStars] = CGSizeMake(s11.width * 7.0 / 11.0, s11.height * 7.0 / 11.0);
    int bFound = 0;
    for (CGFloat size = 11.0; size >= 7.0 && !bFound; size -= 0.5) {
        CGFloat w = s11.width * size / 11.0, h = s11.height * size / 11.0;
        CGFloat bw = bStars ? MAX(w, starW) : w;
        CGFloat bh = bStars ? h + IOSTOUCH_WHEEL_STAR_GAP + IOSTOUCH_WHEEL_STAR_CELL * wheelStarScale : h;
        CGFloat bestOff = 0.0;
        for (int k = 0; k <= 20; k++) {
            CGFloat f = 0.30 + 0.50 * k / 20.0;
            CGFloat rr = R0 + (R1 - R0) * f;
            CGPoint p = CGPointMake(c.x + rr * cos(a), c.y - rr * sin(a));
            int bIn = 1;
            for (int sx = -1; sx <= 1 && bIn; sx++) {
                for (int sy = -1; sy <= 1 && bIn; sy++) {
                    bIn = iosTouch_InWheelSlice(CGPointMake(p.x + sx * bw * 0.5, p.y + sy * bh * 0.5), m->aSlices[i].deg, 2.0);
                }
            }
            if (bIn && (!bFound || fabs(f - 0.58) < bestOff)) {
                bFound = 1;
                bestOff = fabs(f - 0.58);
                aSliceFont[i][bStars] = size;
                aSliceLabelR[i][bStars] = rr;
                aSliceLabelSize[i][bStars] = CGSizeMake(w, h);
            }
        }
    }
    return bFound;
}

// Fits every slice's label, alone and with its star row. The stars are one
// size all round the wheel: the biggest, in steps of 5% down to
// IOSTOUCH_WHEEL_STAR_MIN of full size, at which every slice's row fits (Jedi
// Knight's slices take them full size; Mysteries of the Sith's narrower ones
// need them smaller).
- (void)fitWheelLabels
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    CGSize aS11[IOSTOUCH_WHEEL_MAX];
    for (int i = 0; i < m->numSlices; i++) {
        // two-word names on two lines
        const char* name = iosGame_GetPowerName(m->aSlices[i].bin);
        UILabel* l = aSliceLabels[i];
        l.text = [[NSString stringWithUTF8String:(name ? name : "?")] stringByReplacingOccurrencesOfString:@" " withString:@"\n"];
        l.font = IOSTouch_WheelFont(11.0);
        aS11[i] = [l sizeThatFits:CGSizeMake(300.0, 300.0)];
        [self fitWheelLabel:i size:aS11[i] stars:0];
    }
    for (int n = 0; ; n++) {
        int bAll = 1;
        wheelStarScale = 1.0 - 0.05 * n;
        int bLast = wheelStarScale <= IOSTOUCH_WHEEL_STAR_MIN + 0.001;
        // (one that doesn't fit: on to the next size, but at the last, every slice)
        for (int i = 0; i < m->numSlices && (bAll || bLast); i++) {
            if (![self fitWheelLabel:i size:aS11[i] stars:1]) bAll = 0;
        }
        if (bAll || bLast) break; // else the ones that don't fit go without
    }
}

// Lays the wheel out round the middle of the safe area, as big as fits with
// room for a slice to pop out, every power in its place on the map
- (void)layoutWheel
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (!m) return;
    CGRect b = self.bounds;
    UIEdgeInsets in = self.safeAreaInsets;
    CGFloat left = MAX(in.left, 8.0), right = b.size.width - MAX(in.right, 8.0);
    CGFloat top = MAX(in.top, 8.0), bottom = b.size.height - MAX(in.bottom, 8.0);
    CGPoint c = CGPointMake((left + right) * 0.5, (top + bottom) * 0.5 + 4.0);
    CGFloat R1 = MIN(IOSTOUCH_WHEEL_R1_MAX, (bottom - top) * 0.5 - IOSTOUCH_WHEEL_POP - 6.0);
    CGFloat R0 = round(R1 * IOSTOUCH_WHEEL_R0_FRAC);
    iosTouch_wheelCentre = c;
    iosTouch_wheelR0 = R0;
    iosTouch_wheelR1 = R1;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    wheelDisc.path = [UIBezierPath bezierPathWithArcCenter:c radius:R1 + 14.0 startAngle:0.0 endAngle:2.0 * M_PI clockwise:YES].CGPath;
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
        aSliceLayers[i].hidden = i >= m->numSlices;
        aSlicePopped[i] = -1; // shapes made again by refreshWheelLooks
    }
    [CATransaction commit];

    if (m != fitMap || R1 != fitR1) {
        fitMap = m;
        fitR1 = R1;
        [self fitWheelLabels];
    }
    // Each name in its slice. Under a learned power's, its level: that many
    // filled stars, then empty ones (made again at every opening, as the
    // levels are read then; refreshWheelLooks only colours them).
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
        UILabel* l = aSliceLabels[i];
        int bStars = i < m->numSlices && iosTouch_aWheelEarned[i] && aSliceFont[i][1] > 0.0;
        aSliceStars[i][0].hidden = !bStars;
        aSliceStars[i][1].hidden = !bStars;
        if (i >= m->numSlices) {
            l.hidden = YES;
            continue;
        }
        CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
        CGSize ts = aSliceLabelSize[i][bStars];
        CGPoint p = CGPointMake(c.x + aSliceLabelR[i][bStars] * cos(a), c.y - aSliceLabelR[i][bStars] * sin(a));
        l.font = IOSTouch_WheelFont(aSliceFont[i][bStars]);
        l.bounds = CGRectMake(0, 0, ceil(ts.width) + 4.0, ceil(ts.height) + 2.0);
        // with the stars, p is the middle of the name and the row under it
        CGFloat cell = IOSTOUCH_WHEEL_STAR_CELL * wheelStarScale;
        l.center = bStars ? CGPointMake(p.x, p.y - (IOSTOUCH_WHEEL_STAR_GAP + cell) * 0.5) : p;
        l.hidden = NO;
        if (bStars) {
            UIBezierPath* aPath[2] = { [UIBezierPath bezierPath], [UIBezierPath bezierPath] }; // filled, empty
            for (int k = 0; k < IOSTOUCH_WHEEL_STARS; k++) {
                CGFloat x = p.x + (k - (IOSTOUCH_WHEEL_STARS - 1) * 0.5) * IOSTOUCH_WHEEL_STAR_STEP * wheelStarScale;
                IOSTouch_AddStar(aPath[k >= iosTouch_aWheelLevel[i]], CGPointMake(x, p.y + (ts.height + IOSTOUCH_WHEEL_STAR_GAP) * 0.5), wheelStarScale);
            }
            aSliceStars[i][0].path = aPath[0].CGPath;
            aSliceStars[i][1].path = aPath[1].CGPath;
            aSliceStars[i][0].lineWidth = IOSTOUCH_WHEEL_STAR_EDGE * wheelStarScale;
            aSliceStars[i][1].lineWidth = IOSTOUCH_WHEEL_STAR_LINE * wheelStarScale;
        }
    }
    [CATransaction commit];

    // Group names beside the ring: at about their angle, pushed out until
    // clear of it, then turned a little either way until clear of the screen
    // edges, the top row and every button showing (they are dimmed, but a
    // name over one is hard to read); left out if nothing fits
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX_GROUPS; i++) {
        UILabel* l = aGroupLabels[i];
        const iosTouchWheelGroup* grp = (i < m->numGroups) ? &m->aGroups[i] : NULL;
        l.hidden = YES;
        if (!grp || !grp->title) continue;
        l.text = [NSString stringWithUTF8String:grp->title];
        l.textColor = IOSTouch_WheelColour(grp, 1.0);
        CGSize ts = [l sizeThatFits:CGSizeMake(200.0, 100.0)];
        l.bounds = CGRectMake(0, 0, ts.width, ts.height);
        for (int k = 0; k <= 30 && l.hidden; k++) {
            CGFloat a = (grp->titleDeg + ((k & 1) ? 2.0 : -2.0) * ((k + 1) / 2)) * M_PI / 180.0;
            CGPoint pos = c;
            for (CGFloat r = R1 + 20.0; r < R1 + 140.0; r += 2.0) {
                pos = CGPointMake(c.x + r * cos(a), c.y - r * sin(a));
                CGFloat ex = MAX(fabs(pos.x - c.x) - ts.width * 0.5, 0.0);
                CGFloat ey = MAX(fabs(pos.y - c.y) - ts.height * 0.5, 0.0);
                if (sqrt(ex * ex + ey * ey) >= R1 + 20.0) break;
            }
            if (pos.x - ts.width * 0.5 < left || pos.x + ts.width * 0.5 > right
                || pos.y - ts.height * 0.5 < top + 52.0 || pos.y + ts.height * 0.5 > bottom)
                continue;
            int bClear = 1;
            for (int j = 0; j < IOSTOUCH_NUM_BUTTONS && bClear; j++) {
                iosTouchButton* bt = &iosTouch_aButtons[j];
                if (aButtonViews[j].hidden || j == BTN_WHEEL) continue;
                CGFloat ex = MAX(fabs(bt->x - pos.x) - ts.width * 0.5, 0.0);
                CGFloat ey = MAX(fabs(bt->y - pos.y) - ts.height * 0.5, 0.0);
                bClear = sqrt(ex * ex + ey * ey) > bt->radius + 4.0;
            }
            if (!bClear) continue;
            l.center = pos;
            l.hidden = NO;
        }
    }

    // The middle: the name, what lifting does under it, "cancel" in the gap
    wheelTitle.bounds = CGRectMake(0, 0, 2.0 * R0 * 0.8, 24.0);
    wheelTitle.center = CGPointMake(c.x, c.y - 7.0);
    wheelHint.bounds = CGRectMake(0, 0, 2.0 * R0, 14.0);
    wheelHint.center = CGPointMake(c.x, c.y + 13.0);
    wheelCancelLabel.center = CGPointMake(c.x, c.y + R1 - 22.0);
    bWheelLayingOut = 1;
    [self refreshWheelLooks];
    bWheelLayingOut = 0;
}

// The opener's slide: the direction from where it went down picks the slice.
// Inside the dead zone nothing is picked (once it has been out, lifting there
// cancels); the point it is measured from trails behind the thumb. Dragged
// onto the wheel itself, the slice under the thumb is picked instead (as
// dragging onto a power always did): none in the middle, the gap cancels.
// Back where it went down, nothing is picked, however far it went.
- (void)aimWheel:(iosTouchSlot*)s at:(CGPoint)p
{
    CGFloat dx = p.x - s->wheelOrigin.x, dy = p.y - s->wheelOrigin.y;
    CGFloat r = sqrt(dx * dx + dy * dy);
    if (r > IOSTOUCH_WHEEL_LEASH) {
        CGFloat k = (r - IOSTOUCH_WHEEL_LEASH) / r;
        s->wheelOrigin.x += dx * k;
        s->wheelOrigin.y += dy * k;
        dx = p.x - s->wheelOrigin.x;
        dy = p.y - s->wheelOrigin.y;
        r = IOSTOUCH_WHEEL_LEASH;
    }
    CGFloat cx = p.x - iosTouch_wheelCentre.x, cy = iosTouch_wheelCentre.y - p.y;
    if (sqrt(cx * cx + cy * cy) <= iosTouch_wheelR1 + IOSTOUCH_WHEEL_POP + 14.0) {
        s->bArmed = 1;
        s->wheelAim = atan2(cy, cx) * 180.0 / M_PI;
        s->wheelSlot = iosTouch_WheelSliceAt(p, s->wheelSlot);
        return;
    }
    if (s->bArmed && hypot(p.x - s->origin.x, p.y - s->origin.y) < IOSTOUCH_WHEEL_DEAD) {
        s->wheelOrigin = s->origin;
        s->wheelSlot = -1;
        return;
    }
    if (r < IOSTOUCH_WHEEL_DEAD) {
        s->wheelSlot = -1;
        return;
    }
    s->bArmed = 1;
    s->wheelAim = atan2(-dy, dx) * 180.0 / M_PI;
    s->wheelSlot = iosTouch_WheelSliceForAngle(s->wheelAim, s->wheelSlot);
}

// Each slice in its look: learned ones in their group's colour, the selected
// one edged in white, the one pointed at popped out in full colour, the ones
// not learned yet dark. The middle names what lifting would pick, or says
// what to do.
- (void)refreshWheelLooks
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (!m) return;
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, R1 = iosTouch_wheelR1;

    // What is pointed at: by the opener once it has slid, else by the latest
    // other finger on the wheel. Off every slice (back in the middle, in the
    // gap, or a finger off the ring), lifting cancels.
    iosTouchSlot* pSlide = NULL;
    iosTouchSlot* pFinger = NULL;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_WHEEL) continue;
        if (s->bWheelOpener && !iosTouch_bWheelTapMode) pSlide = s;
        else pFinger = s;
    }
    int hot = -1, bCancel = 0;
    if (pSlide && pSlide->bArmed) {
        hot = pSlide->wheelSlot;
        bCancel = hot < 0;
    }
    else if (pFinger) {
        hot = pFinger->wheelSlot;
        bCancel = hot < 0;
    }

    int cur = iosGame_GetCurPower(), curSlice = -1, bAnyEarned = 0;
    for (int i = 0; i < m->numSlices; i++) {
        if (iosTouch_aWheelEarned[i]) bAnyEarned = 1;
        if (m->aSlices[i].bin == cur) curSlice = i;
    }

    // A slice pops out quickly (shape layers animate changes, 0.25s by default)
    [CATransaction begin];
    if (bWheelLayingOut) [CATransaction setDisableActions:YES];
    else [CATransaction setAnimationDuration:0.08];
    for (int i = 0; i < m->numSlices; i++) {
        const iosTouchWheelSlice* sl = &m->aSlices[i];
        const iosTouchWheelGroup* g = &m->aGroups[sl->group];
        CAShapeLayer* L = aSliceLayers[i];
        int bEarned = iosTouch_aWheelEarned[i];
        int bHot = i == hot;
        int bPop = bHot && bEarned;
        if (aSlicePopped[i] != bPop) {
            aSlicePopped[i] = bPop;
            CGFloat half = m->sliceDeg * 0.5 - 1.0; // a degree in from each edge
            L.path = IOSTouch_WedgePath(c, sl->deg - half, sl->deg + half, R0, R1 + (bPop ? IOSTOUCH_WHEEL_POP : 0.0)).CGPath;
            // the stars: gold, or dark like the name on a popped-out slice
            CGFloat sr = bPop ? 25.0 / 255.0 : 1.0, sg = bPop ? 20.0 / 255.0 : 204.0 / 255.0, sb = bPop ? 10.0 / 255.0 : 64.0 / 255.0;
            aSliceStars[i][0].fillColor = [UIColor colorWithRed:sr green:sg blue:sb alpha:1.0].CGColor;
            aSliceStars[i][0].strokeColor = bPop ? [UIColor clearColor].CGColor : [UIColor colorWithWhite:0.0 alpha:0.78].CGColor;
            aSliceStars[i][1].strokeColor = [UIColor colorWithRed:sr green:sg blue:sb alpha:(bPop ? 0.47 : 0.43)].CGColor;
        }
        UIColor* text;
        if (!bEarned) {
            L.fillColor = [UIColor colorWithWhite:(bHot ? 0.24 : 0.18) alpha:0.92].CGColor;
            L.strokeColor = IOSTouch_WheelColour(g, bHot ? 0.7 : 0.45).CGColor;
            L.lineWidth = bHot ? 2.5 : 2.0;
            text = [UIColor colorWithWhite:(bHot ? 0.75 : 0.59) alpha:1.0];
        }
        else if (bPop) {
            L.fillColor = IOSTouch_WheelColour(g, 1.0).CGColor;
            L.strokeColor = [UIColor whiteColor].CGColor;
            L.lineWidth = 2.5;
            text = [UIColor colorWithWhite:0.08 alpha:1.0];
        }
        else {
            L.fillColor = IOSTouch_WheelColour(g, 0.42).CGColor;
            L.strokeColor = (i == curSlice) ? [UIColor whiteColor].CGColor : IOSTouch_WheelColour(g, 0.8).CGColor;
            L.lineWidth = (i == curSlice) ? 2.5 : 2.0;
            text = [UIColor whiteColor];
        }
        L.zPosition = bPop ? 1.0 : 0.0;
        aSliceLabels[i].textColor = text;
    }

    // A short needle at the edge of the middle, the way the sliding thumb points
    if (pSlide && pSlide->bArmed && hot != -1) {
        CGFloat a = pSlide->wheelAim * M_PI / 180.0;
        UIBezierPath* p = [UIBezierPath bezierPath];
        [p moveToPoint:CGPointMake(c.x + (R0 - 14.0) * cos(a), c.y - (R0 - 14.0) * sin(a))];
        [p addLineToPoint:CGPointMake(c.x + (R0 - 3.0) * cos(a), c.y - (R0 - 3.0) * sin(a))];
        wheelNeedle.path = p.CGPath;
        wheelNeedle.strokeColor = (hot >= 0 && iosTouch_aWheelEarned[hot]) ? IOSTouch_WheelColour(&m->aGroups[m->aSlices[hot].group], 1.0).CGColor
                                                                          : [UIColor colorWithWhite:0.6 alpha:1.0].CGColor;
        wheelNeedle.hidden = NO;
    }
    else {
        wheelNeedle.hidden = YES;
    }
    [CATransaction commit];

    UIColor* grey = [UIColor colorWithWhite:0.6 alpha:1.0];
    const char* name = NULL;
    if (hot >= 0) {
        name = iosGame_GetPowerName(m->aSlices[hot].bin);
        wheelTitle.textColor = iosTouch_aWheelEarned[hot] ? IOSTouch_WheelColour(&m->aGroups[m->aSlices[hot].group], 1.0) : grey;
        wheelHint.text = iosTouch_aWheelEarned[hot] ? @"lift to select" : @"not learned yet";
    }
    else if (bCancel) {
        name = "CANCEL";
        wheelTitle.textColor = grey;
        wheelHint.text = @"lift to cancel";
    }
    else if (!bAnyEarned) {
        name = "NO POWERS";
        wheelTitle.textColor = grey;
        wheelHint.text = @"none learned yet";
    }
    else {
        name = (curSlice >= 0) ? iosGame_GetPowerName(cur) : "FORCE";
        wheelTitle.textColor = (curSlice >= 0) ? IOSTouch_WheelColour(&m->aGroups[m->aSlices[curSlice].group], 1.0) : [UIColor whiteColor];
        wheelHint.text = iosTouch_bWheelTapMode ? @"tap a power" : @"slide toward a power";
    }
    wheelTitle.text = [NSString stringWithUTF8String:(name ? name : "")];

    // A light tick each time the thumb moves onto another slice (or the gap)
    if (hot != wheelLastHot) {
        if (hot != -1) [wheelTick selectionChanged];
        wheelLastHot = hot;
    }
}

// Opens the wheel, with every power of this game in its place and the ones
// the player has learned lit, with their levels. Every other touch lets go of
// what it was holding and is ignored until it lifts, and the game holds still
// (iosGame_SetHold) until the wheel closes.
- (void)openWheel
{
    [self setTrayOpen:0];
    iosTouch_pWheelMap = iosGame_IsMots() ? &iosTouch_motsWheel : &iosTouch_jkWheel;
    for (int i = 0; i < iosTouch_pWheelMap->numSlices; i++) {
        iosTouch_aWheelEarned[i] = iosGame_IsPowerAvailable(iosTouch_pWheelMap->aSlices[i].bin);
        iosTouch_aWheelLevel[i] = iosGame_GetPowerLevel(iosTouch_pWheelMap->aSlices[i].bin);
    }
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_BUTTON && iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
        if (s->role == ROLE_BUTTON && s->button == BTN_QUICKSAVE) [self setRing:saveRing progress:0.0];
        if (s->role == ROLE_BUTTON && s->button == BTN_QUICKLOAD) [self setRing:loadRing progress:0.0];
        if (s->role == ROLE_BUTTON && s->button == BTN_MENU) [self setRing:menuRing progress:0.0];
        if (s->role == ROLE_STICK) {
            iosTouch_bStickActive = 0;
            iosTouch_stickX = iosTouch_stickY = 0.0f;
            [self hideStick];
        }
        s->role = ROLE_IGNORED;
    }
    iosTouch_bWheelOpen = 1;
    iosTouch_bWheelTapMode = 0;
    wheelLastHot = -1;
    iosGame_SetHold(1);
    [self layoutWheel];
    wheelView.hidden = NO;
    [self bringSubviewToFront:wheelView];
    [self insertSubview:aButtonViews[BTN_WHEEL] aboveSubview:wheelView];
    [wheelTick prepare];
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    [self refreshWheelLooks];
}

// Closes the wheel, selecting the power in slice (if it is one, and learned)
- (void)closeWheelSelecting:(int)slice
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (m && slice >= 0 && slice < m->numSlices && iosTouch_aWheelEarned[slice]) iosGame_SelectPower(m->aSlices[slice].bin);
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        // fingers still on the wheel are ignored until they lift
        if (iosTouch_aSlots[i].touch && iosTouch_aSlots[i].role == ROLE_WHEEL) iosTouch_aSlots[i].role = ROLE_IGNORED;
    }
    iosTouch_bWheelOpen = 0;
    iosTouch_bWheelTapMode = 0;
    iosGame_SetHold(0);
    wheelView.hidden = YES;
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
}

// ------------------------------------------------------------ touches

- (int)isPoint:(CGPoint)p onButton:(int)button
{
    // (an open tray's button: anywhere on its backing that -buttonAt: puts on it)
    if (iosTouch_bTrayOpen && iosTouch_IsTrayButton(button)) return [self buttonAt:p] == button;
    iosTouchButton* b = &iosTouch_aButtons[button];
    return hypot(p.x - b->x, p.y - b->y) <= b->radius + IOSTOUCH_TOUCH_SLOP;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches) {
        int slot = -1;
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            if (iosTouch_aSlots[i].touch == nil) { slot = i; break; }
        }
        if (slot < 0) continue;

        CGPoint p = [t locationInView:self];
        iosTouchSlot* s = &iosTouch_aSlots[slot];
        memset(s, 0, sizeof(*s));
        s->touch = t;
        s->origin = p;
        s->last = p;
        s->tDown = CACurrentMediaTime();
        s->tDownTouch = t.timestamp;
        s->button = -1;
        s->trayButton = -1;
        s->wheelSlot = -1;

        if (iosTouch_bWheelOpen) {
            s->role = ROLE_WHEEL;
            s->wheelSlot = iosTouch_WheelSliceAt(p, -1);
            continue;
        }

        s->button = [self buttonAt:p];
        // An open tray closes at a touch anywhere else, which then does what
        // it would anyway -- except on MENU, where it only closes the tray
        if (iosTouch_bTrayOpen && !iosTouch_IsTrayButton(s->button)) {
            [self setTrayOpen:0];
            if (s->button == BTN_MENU) {
                s->role = ROLE_IGNORED;
                continue;
            }
        }
        if (s->button >= 0 && iosTouch_aButtons[s->button].kind == KIND_WHEEL) {
            // Opens at once: sliding toward a power and lifting picks it, and
            // a quick tap leaves it open to tap one. The typing line closes
            // first: its keyboard would cover the lower half of the wheel.
            if (jkHud_bChatOpen) iosGame_ToggleChat();
            [self openWheel];
            s->role = ROLE_WHEEL;
            s->bWheelOpener = 1;
            s->wheelOrigin = p;
            break; // any other new touch would be ignored anyway
        }
        else if (s->button >= 0) {
            s->role = ROLE_BUTTON;
            iosTouch_aButtonHeld[s->button]++;
        }
        else if (p.x < self.bounds.size.width * IOSTOUCH_STICK_ZONE && !iosTouch_bStickActive) {
            s->role = ROLE_STICK;
            iosTouch_bStickActive = 1;
            iosTouch_stickX = iosTouch_stickY = 0.0f;
            iosTouch_bAlwaysRun = iosGame_IsAlwaysRun();
            [self placeRunMarker:p];
            [self updateStickVisual:s];
        }
        else {
            s->role = ROLE_LOOK;
        }
    }
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    int bLooksChanged = 0;
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];

            // QUICK SAVE, QUICK LOAD: a finger that leaves the button, even
            // between two frames, doesn't save or load, even if it comes back
            // on -- so the places UIKit merged into this move count too (the
            // last of them is p). -tick checks as well, for a button laid out
            // again away from a finger keeping still.
            if (s->role == ROLE_BUTTON && iosTouch_IsQuickHold(s->button) && !s->bFired) {
                for (UITouch* c in [event coalescedTouchesForTouch:t]) {
                    if (![self isPoint:[c locationInView:self] onButton:s->button]) s->bFired = 1;
                }
                if (![self isPoint:p onButton:s->button]) s->bFired = 1;
            }

            if (s->role == ROLE_WHEEL) {
                if (s->bWheelOpener && !iosTouch_bWheelTapMode) [self aimWheel:s at:p];
                else s->wheelSlot = iosTouch_WheelSliceAt(p, s->wheelSlot);
            }
            else if (s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && iosTouch_bTrayOpen) {
                // Held until the tray opened: the tray button under the thumb lights up
                int tb = [self buttonAt:p];
                tb = iosTouch_IsTrayButton(tb) ? tb : -1;
                if (tb != s->trayButton) {
                    s->trayButton = tb;
                    bLooksChanged = 1;
                }
            }
            else if (s->role == ROLE_STICK) {
                float dx = (p.x - s->origin.x) / IOSTOUCH_STICK_RADIUS;
                float dy = (p.y - s->origin.y) / IOSTOUCH_STICK_RADIUS;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1.0f) { dx /= len; dy /= len; }
                iosTouch_stickX = dx;
                iosTouch_stickY = dy;
                [self updateStickRunAt:p origin:s->origin];
                [self updateStickVisual:s];
            }
            else if (s->role == ROLE_LOOK ||
                     (s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].bLookWhileHeld)) {
                iosTouch_lookX += (float)(p.x - s->last.x) * IOSTOUCH_LOOK_SCALE_X;
                iosTouch_lookY += (float)(p.y - s->last.y) * IOSTOUCH_LOOK_SCALE_Y;
            }
            s->last = p;
        }
    }
    iosTouch_RecomputeKeys();
    if (bLooksChanged) [self refreshButtonLooks];
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

// bCancelled: iOS took the touch away (a call, a system gesture...) -- release
// whatever it held, but don't treat it as a finished tap
- (void)endTouches:(NSSet<UITouch*>*)touches cancelled:(int)bCancelled
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];
            if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && bCancelled) {
                // the opener taken away: the wheel stays open, for tapping
                if (s->bWheelOpener) iosTouch_bWheelTapMode = 1;
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && s->bWheelOpener && !iosTouch_bWheelTapMode) {
                [self aimWheel:s at:p];
                if (!s->bArmed) {
                    // Never slid: a quick tap leaves the wheel open to tap a
                    // power (as tapping FORCE WHEEL always did); a long press
                    // lets it go. Timed by the touch's own clock, so a slow
                    // frame between the two doesn't make a tap long.
                    if (t.timestamp - s->tDownTouch < IOSTOUCH_WHEEL_TAP_TIME) iosTouch_bWheelTapMode = 1;
                    else [self closeWheelSelecting:-1];
                }
                else {
                    [self closeWheelSelecting:s->wheelSlot]; // a learned power, or no change
                }
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen) {
                // A tap: on a learned power picks it; on one not learned yet
                // the wheel stays open; anywhere else (the middle, the gap,
                // off the ring, FORCE WHEEL) it closes with no change --
                // except off the ring while the opener is still sliding and
                // pointing: that finger is only resting, the slide goes on
                int slice = iosTouch_WheelSliceAt(p, s->wheelSlot);
                int bSliding = 0;
                for (int k = 0; k < IOSTOUCH_MAX_TOUCHES; k++) {
                    iosTouchSlot* o = &iosTouch_aSlots[k];
                    if (o != s && o->touch && o->role == ROLE_WHEEL && o->bWheelOpener && o->bArmed && !iosTouch_bWheelTapMode)
                        bSliding = 1;
                }
                if (!(slice == -1 && bSliding) && !(slice >= 0 && !iosTouch_aWheelEarned[slice])) {
                    [self closeWheelSelecting:slice];
                }
            }
            else if (s->role == ROLE_BUTTON) {
                iosTouchButton* b = &iosTouch_aButtons[s->button];
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                // A tap so quick that it began and ended between two reads of
                // the controls (a slow frame) still counts as one press
                if (!bCancelled && !jkHud_bChatOpen && b->kind == KIND_KEY && b->scancode >= 0 && !s->bSeen) {
                    iosTouch_QueuePress(b->scancode);
                }
                // These act when the finger lifts on the button, so a slip onto
                // one can be dragged off again. While typing, only MENU and
                // the tray's buttons (MENU and the keyboard close the line):
                // the game isn't reading the controls then, so the rest would
                // only go off later.
                int bWhileTyping = b->kind == KIND_MENU || iosTouch_IsTrayButton(s->button);
                if (!bCancelled && [self isPoint:p onButton:s->button] && (!jkHud_bChatOpen || bWhileTyping)) {
                    if (b->kind == KIND_ITEM && !aButtonViews[s->button].hidden
                             // one item at a time: the use key acts on whichever item is selected when it is read
                             && !iosTouch_aPulseQueue[b->scancode] && !iosTouch_aPulseReads[b->scancode]
                             && !iosTouch_aPulseGap[b->scancode]) {
                        iosGame_SelectItem(b->bin);
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (iosTouch_IsTrayButton(s->button)) {
                        [self useTrayButton:s->button];
                    }
                    else if (b->kind == KIND_MENU && !s->bFired) {
                        // A tap: Escape (the menu, or closing the typing line)
                        iosTouch_menuPulse = IOSTOUCH_MENU_PULSE_UPDATES;
                    }
                }
                // MENU held until its tray opened, then slid onto one of its buttons
                if (!bCancelled && s->button == BTN_MENU && s->bFired && iosTouch_bTrayOpen) {
                    int tb = [self buttonAt:p];
                    if (iosTouch_IsTrayButton(tb)) [self useTrayButton:tb];
                }
                if (s->button == BTN_QUICKSAVE) [self setRing:saveRing progress:0.0];
                if (s->button == BTN_QUICKLOAD) [self setRing:loadRing progress:0.0];
                if (s->button == BTN_MENU) [self setRing:menuRing progress:0.0];
            }
            else if (s->role == ROLE_STICK) {
                iosTouch_bStickActive = 0;
                iosTouch_stickX = iosTouch_stickY = 0.0f;
                [self hideStick];
            }
            memset(s, 0, sizeof(*s));
        }
    }
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:0]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:1]; }

// A full force meter glows, gently pulsing
- (void)setForceRingGlow:(int)bGlow
{
    [forceRing removeAnimationForKey:@"glow"];
    if (bGlow) {
        forceRing.shadowOpacity = 0.9;
        CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"shadowOpacity"];
        a.fromValue = @(0.35);
        a.toValue = @(1.0);
        a.duration = 0.9;
        a.autoreverses = YES;
        a.repeatCount = HUGE_VALF;
        a.removedOnCompletion = NO; // keep it when the app comes back from the background
        a.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [forceRing addAnimation:a forKey:@"glow"];
    }
    else {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        forceRing.shadowOpacity = 0.0;
        [CATransaction commit];
    }
}

// Once per frame while shown: the QUICK SAVE, QUICK LOAD and MENU holds, the
// FORCE label and its force meter ring, which item buttons show and what they
// say, and the FPS readout
- (void)tick
{
    // The holds go off only once the finger is known to have stayed down long
    // enough: this runs before the frame's touches are delivered, so a lift
    // during a slow frame hasn't arrived yet -- but every one made before the
    // last tick has
    CFTimeInterval now = CACurrentMediaTime();
    CFTimeInterval known = lastTickTime;
    lastTickTime = now;
    // How far QUICK SAVE's and QUICK LOAD's rings have filled (the furthest
    // along of each one's holds), and the one whose hold completed
    CGFloat saveProgress = 0.0, loadProgress = 0.0;
    int done = -1;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON) continue;
        if (iosTouch_IsQuickHold(s->button) && !s->bFired) {
            if (![self isPoint:s->last onButton:s->button] || jkHud_bChatOpen) {
                // off the button as this frame sees it (a finger that slid
                // off is already done, in touchesMoved; this is the button
                // laid out again away from a finger keeping still), or the
                // typing line is open (the game would only read the key once
                // it closes): this touch doesn't save or load, even if it
                // comes back on or the line closes
                s->bFired = 1;
                continue;
            }
            CFTimeInterval hold = (s->button == BTN_QUICKSAVE) ? IOSTOUCH_QUICKSAVE_HOLD : IOSTOUCH_QUICKLOAD_HOLD;
            if (known - s->tDown >= hold) {
                // the ring is full, with the finger still on it. QUICK SAVE
                // and QUICK LOAD both found done in the same tick (one long
                // frame can cover both, whichever was touched first): QUICK
                // SAVE, as only a load throws away the game being played
                if (done != BTN_QUICKSAVE) done = s->button;
            }
            else {
                CGFloat progress = MIN((CGFloat)((now - s->tDown) / hold), 1.0);
                if (s->button == BTN_QUICKSAVE) saveProgress = MAX(saveProgress, progress);
                else loadProgress = MAX(loadProgress, progress);
            }
        }
        else if (s->button == BTN_MENU && !s->bFired) {
            if (![self isPoint:s->last onButton:BTN_MENU]) {
                // slid off: no tray, and no menu either
                s->bFired = 1;
                [self setRing:menuRing progress:0.0];
                continue;
            }
            CGFloat progress = (CGFloat)((now - s->tDown) / IOSTOUCH_MENU_HOLD);
            if (known - s->tDown >= IOSTOUCH_MENU_HOLD) {
                s->bFired = 1;
                [self setRing:menuRing progress:0.0];
                [self setTrayOpen:1];
            }
            else {
                [self setRing:menuRing progress:MIN(progress, 1.0)];
            }
        }
    }

    if (done >= 0) {
        // One save or load, once: it ends every hold on QUICK SAVE and QUICK
        // LOAD, so two fingers on one button save or load once (a finger that
        // comes down after that starts a new hold), and the two buttons held
        // together do whichever completes first, never both (both would only
        // load back the game just saved, or save again the one just loaded)
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* o = &iosTouch_aSlots[i];
            if (o->touch && o->role == ROLE_BUTTON && iosTouch_IsQuickHold(o->button)) o->bFired = 1;
        }
        saveProgress = loadProgress = 0.0;
        if (done == BTN_QUICKSAVE) iosTouch_QueuePress(iosTouch_aButtons[BTN_QUICKSAVE].scancode); // saves once
        else iosGame_QuickLoad();
    }
    [self setRing:saveRing progress:saveProgress];
    [self setRing:loadRing progress:loadProgress];

    int bLooksChanged = 0;

    // FORCE shows the power it uses; dimmed until there is one
    const char* name = iosGame_GetForcePowerName();
    if (!bForceLabelSet || name != forceLabelName) {
        bForceLabelSet = 1;
        forceLabelName = name;
        UILabel* l = aButtonViews[BTN_FORCE];
        l.text = name ? [NSString stringWithFormat:@"FORCE\n%s", name] : @"FORCE";
        l.font = [UIFont boldSystemFontOfSize:(name ? 10 : 13)];
        bLooksChanged = 1;
    }

    // An item button shows while the player has some of it; bacta says how many
    int bItemsChanged = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (iosTouch_aButtons[i].kind != KIND_ITEM) continue;
        int amount = 0, bActive = 0;
        int bHave = iosGame_GetItem(iosTouch_aButtons[i].bin, &amount, &bActive);
        if (!bHave) { amount = 0; bActive = 0; }
        if ((int)aButtonViews[i].hidden == bHave) {
            aButtonViews[i].hidden = !bHave;
            bLooksChanged = 1;
            bItemsChanged = 1;
        }
        if (amount != aItemAmount[i]) {
            aItemAmount[i] = amount;
            if (i == BTN_BACTA) {
                aButtonViews[i].text = amount > 1 ? [NSString stringWithFormat:@"BACTA\n%d", amount] : @"BACTA";
                aButtonViews[i].font = [UIFont boldSystemFontOfSize:(amount > 1 ? 9 : 10)];
            }
        }
        if (bActive != aItemActive[i]) {
            aItemActive[i] = bActive;
            bLooksChanged = 1;
        }
    }

    // The game's Always Run option can change (in its options menu): the
    // stick has its run marker only while it is off. The marker also moves
    // out of the way of an item button that has just appeared.
    int bAlwaysRun = iosGame_IsAlwaysRun();
    if (bAlwaysRun != iosTouch_bAlwaysRun || bItemsChanged) {
        iosTouch_bAlwaysRun = bAlwaysRun;
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (!s->touch || s->role != ROLE_STICK) continue;
            int bWasRunning = iosTouch_bStickRunning;
            [self placeRunMarker:s->origin];
            iosTouch_bStickRunning = bWasRunning && iosTouch_bRunMarker; // still on it, unless it's gone
            [self updateStickRunAt:s->last origin:s->origin];
            [self updateStickVisual:s];
        }
        iosTouch_RecomputeKeys();
    }

    // MENU (and the tray's keyboard) light up while the typing line is open.
    // Presses queued while it opens or closes are dropped: the game isn't
    // reading the controls while it is open, so they would go off much later.
    static int bChatWasOpen = 0;
    if ((jkHud_bChatOpen != 0) != bChatWasOpen) {
        bChatWasOpen = jkHud_bChatOpen != 0;
        memset(iosTouch_aPulseQueue, 0, sizeof(iosTouch_aPulseQueue));
        memset(iosTouch_aPulseReads, 0, sizeof(iosTouch_aPulseReads));
        memset(iosTouch_aPulseGap, 0, sizeof(iosTouch_aPulseGap));
        bLooksChanged = 1;
    }

    // The force meter around FORCE (hidden when empty: a round-capped stroke
    // of no length would still show as a dot)
    int bFull = 0;
    float frac = iosGame_GetForceMana(&bFull);
    if (fabsf(frac - forceRingFrac) >= 0.004f || bFull != bForceRingFull) {
        forceRingFrac = frac;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        forceRing.hidden = frac < 0.005f;
        forceRing.strokeEnd = frac < 0.0f ? 0.0 : frac;
        [CATransaction commit];
        if (bFull != bForceRingFull) {
            bForceRingFull = bFull;
            [self setForceRingGlow:bFull];
            bLooksChanged = 1;
        }
    }

    // The FPS readout: the frames the game drew in the last
    // IOSTOUCH_FPS_PERIOD or so (counting starts over whenever the game's
    // count does)
    if (iosTouch_bShowFps) {
        unsigned int frames = iosGame_GetFrameCount();
        if (!bFpsBase || frames < fpsBaseFrames) {
            bFpsBase = 1;
            fpsBaseFrames = frames;
            fpsBaseTime = now;
        }
        else if (now - fpsBaseTime >= IOSTOUCH_FPS_PERIOD) {
            double fps = (double)(frames - fpsBaseFrames) / (now - fpsBaseTime);
            fpsLabel.text = [NSString stringWithFormat:@"%d FPS", (int)lround(fps)];
            fpsBaseFrames = frames;
            fpsBaseTime = now;
        }
    }

    if (bLooksChanged) [self refreshButtonLooks];
}

- (void)resetAll
{
    if (iosTouch_bWheelOpen) {
        iosTouch_bWheelOpen = 0;
        iosTouch_bWheelTapMode = 0;
        iosGame_SetHold(0);
        wheelView.hidden = YES;
    }
    [self setTrayOpen:0];
    memset(iosTouch_aSlots, 0, sizeof(iosTouch_aSlots));
    memset(iosTouch_aButtonHeld, 0, sizeof(iosTouch_aButtonHeld));
    memset(iosTouch_aPulseQueue, 0, sizeof(iosTouch_aPulseQueue));
    memset(iosTouch_aPulseReads, 0, sizeof(iosTouch_aPulseReads));
    memset(iosTouch_aPulseGap, 0, sizeof(iosTouch_aPulseGap));
    iosTouch_bStickActive = 0;
    iosTouch_stickX = iosTouch_stickY = 0.0f;
    iosTouch_lookX = iosTouch_lookY = 0.0f;
    iosTouch_bGyroWasOk = 0; // the gyro starts over too
    iosTouch_gyroCountX = iosTouch_gyroCountY = 0.0f;
    iosTouch_menuPulse = 0;
    bFpsBase = 0; // time spent in a menu doesn't count
    [self hideStick];
    [self setRing:saveRing progress:0.0];
    [self setRing:loadRing progress:0.0];
    [self setRing:menuRing progress:0.0];
    iosTouch_RecomputeKeys();
    stdControl_bControllerEscapeKey = 0;
    [self refreshButtonLooks];
}

@end

// ---------------------------------------------------------------- C API

static IOSTouchOverlay* iosTouch_pOverlay = nil;

static UIView* iosTouch_GetHostView(void)
{
    if (!displayWindow) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(displayWindow);
    UIWindow* w = (__bridge UIWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    if (!w) return nil;
    return w.rootViewController.view ? w.rootViewController.view : w;
}

// Whether the overlay is wanted: gameplay controls active and no cutscene
// playing -- and not over a GUI menu either, e.g. the objectives screen at
// level start waits for Ok while gameplay controls are already active.
static int iosTouch_WantOverlay(void)
{
    return stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive();
}

static void iosTouch_UpdateInPool(void)
{
    // A MENU tap holds Escape down for a couple of updates -- the game acts on
    // it once, when it first sees it (Window_SdlUpdate) -- then lets it go
    if (iosTouch_menuPulse > 0 && --iosTouch_menuPulse == 0) stdControl_bControllerEscapeKey = 0;

    int bWant = iosTouch_WantOverlay();

    if (!iosTouch_pOverlay) {
        if (!bWant) return;
        UIView* host = iosTouch_GetHostView();
        if (!host) return;
        iosTouch_pOverlay = [[IOSTouchOverlay alloc] initWithFrame:host.bounds];
        [host addSubview:iosTouch_pOverlay];
    }
    else {
        // If the SDL window was ever recreated, the overlay is still sitting in
        // the old, no longer shown one: move it across.
        UIView* host = iosTouch_GetHostView();
        if (host && iosTouch_pOverlay.superview != host) {
            [iosTouch_pOverlay removeFromSuperview];
            iosTouch_pOverlay.frame = host.bounds;
            [host addSubview:iosTouch_pOverlay];
            [iosTouch_pOverlay resetAll];
        }
    }

    // keep it on top of anything SDL adds later
    if (iosTouch_pOverlay.superview && iosTouch_pOverlay.superview.subviews.lastObject != iosTouch_pOverlay) {
        [iosTouch_pOverlay.superview bringSubviewToFront:iosTouch_pOverlay];
    }

    if (bWant == iosTouch_pOverlay.hidden) {
        iosTouch_pOverlay.hidden = !bWant;
        [iosTouch_pOverlay resetAll]; // touches on a hidden view never end; start clean
    }

    if (bWant) {
        // The HUD is laid out again on level start, resize and HUD scale
        // changes; FIRE and ALT follow the right gauge. Turning the phone the
        // other way up changes neither the size nor the insets, but moves the
        // cutout to the other side.
        float g[4] = {0, 0, 0, 0};
        int bGauge = jkHud_IosGetRightGaugeRectPt(&g[0], &g[1], &g[2], &g[3]);
        if (bGauge != iosTouch_bLayoutGauge || memcmp(g, iosTouch_aLayoutGauge, sizeof(g)) != 0
            || iosTouch_CutoutMayBeRight(iosTouch_pOverlay) != iosTouch_layoutCutoutRight) {
            [iosTouch_pOverlay setNeedsLayout];
        }

        [iosTouch_pOverlay tick];

        int dx = (int)iosTouch_lookX;
        int dy = (int)iosTouch_lookY;
        iosTouch_lookX -= (float)dx;
        iosTouch_lookY -= (float)dy;
        Window_lastXRel += dx;
        Window_lastYRel += dy;
    }
}

void iosTouch_Update(void)
{
    // This runs in the game's loop, outside any autorelease pool that ever
    // drains (SDL calls the game's main from its app delegate and never
    // returns): without one of its own, what the overlay autoreleases here
    // (labels' strings, colours, subviews arrays) would pile up all game long
    @autoreleasepool {
        iosTouch_UpdateInPool();
    }
}

void iosTouch_UpdateGyro(void)
{
    @autoreleasepool { // (as iosTouch_Update)
        if (!iosTouch_pOverlay) return;
        // Not while the overlay is hidden -- nor if this frame's events just
        // left gameplay, which hides it next frame
        int bHadGyro = iosTouch_GyroAvailable();
        iosTouch_GyroUpdate(!iosTouch_pOverlay.hidden && iosTouch_WantOverlay(), iosTouch_pOverlay);
        if (iosTouch_GyroAvailable() != bHadGyro) {
            // iOS refused the motion data: the tray's GYRO says NO GYRO
            [iosTouch_pOverlay refreshGyroLabels];
            [iosTouch_pOverlay refreshButtonLooks];
        }
    }
}

// Called once per scancode each time the game reads the keyboard. Held
// buttons report down for as long as they're held; a queued one-off press is
// reported down for IOSTOUCH_PULSE_READS reads and then up for one, so the
// game sees each queued press as its own key press.
int iosTouch_IsScancodeDown(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return 0;
    if (!iosTouch_pOverlay || iosTouch_pOverlay.hidden) return 0;

    int bHeld = iosTouch_aKeyDown[scancode];
    if (bHeld) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch && s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].scancode == scancode)
                s->bSeen = 1;
        }
    }
    if (iosTouch_aPulseReads[scancode]) {
        if (--iosTouch_aPulseReads[scancode] == 0) iosTouch_aPulseGap[scancode] = 1;
        return 1;
    }
    if (iosTouch_aPulseGap[scancode]) {
        iosTouch_aPulseGap[scancode] = 0;
        return bHeld;
    }
    if (iosTouch_aPulseQueue[scancode]) {
        iosTouch_aPulseQueue[scancode]--;
        iosTouch_aPulseReads[scancode] = IOSTOUCH_PULSE_READS - 1;
        if (!iosTouch_aPulseReads[scancode]) iosTouch_aPulseGap[scancode] = 1;
        return 1;
    }
    return bHeld;
}

#endif // TARGET_IOS

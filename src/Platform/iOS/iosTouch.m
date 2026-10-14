// Added: entire file. See iosTouch.h.
// Written to work with or without ARC (no weak refs; the overlay lives for
// the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreMotion/CoreMotion.h>
#import <AudioToolbox/AudioToolbox.h>
#include <SDL3/SDL.h>
#include <os/lock.h>
#include <math.h>
#include <string.h>

#include "Platform/iOS/iosTouch.h"
#include "Platform/iOS/iosGame.h"
#include "Platform/iOS/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosCheatLaugh.h"
#include "Platform/iOS/iosSelfie.h"
#include "Platform/iOS/iosSelfieShots.h"
#include "Platform/iOS/iosFreeze.h"
#include "Platform/iOS/iosCinema.h"
#include "Platform/iOS/iosSaber.h"

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
// The wheels (force and weapons, see the maps below): the outer radius, at most
// IOSTOUCH_WHEEL_R1_MAX points; the hole in the middle, a fraction of that;
// how far the slice pointed at pops out
#define IOSTOUCH_WHEEL_R1_MAX 168.0
#define IOSTOUCH_WHEEL_R0_FRAC 0.37
#define IOSTOUCH_WHEEL_POP 10.0
// FORCE WHEEL and NEXT WPN each take three gestures. A quick tap (lifted
// sooner than IOSTOUCH_WHEEL_HOLD by the touch's own clock, never
// IOSTOUCH_WHEEL_OPEN_SLIDE from where it went down) presses the button's key:
// the next learned power, the next weapon. Sliding that far opens the wheel
// at once, to slide to a slice; held still for IOSTOUCH_WHEEL_HOLD (known as
// the QUICK SAVE hold is) it opens to tap one.
// Sliding to pick is measured from where the thumb went down on the button:
// it points nowhere until IOSTOUCH_WHEEL_DEAD points away (keep this small:
// the slices pointing up only have ~30pt of screen above the button), the
// point it is measured from trails at most IOSTOUCH_WHEEL_LEASH behind the
// thumb (so turning back after a long slide is quick), and a slice stays
// picked until the thumb points IOSTOUCH_WHEEL_HYST degrees past its edge. On
// the wheel itself the slice under the thumb is picked. A lift that never
// slid as far as IOSTOUCH_WHEEL_DEAD leaves the wheel open to tap a slice.
#define IOSTOUCH_WHEEL_OPEN_SLIDE 10.0
#define IOSTOUCH_WHEEL_HOLD 0.3
#define IOSTOUCH_WHEEL_DEAD 14.0
#define IOSTOUCH_WHEEL_LEASH 60.0
#define IOSTOUCH_WHEEL_HYST 4.0
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
// A weapon's ammo count shows under its name the same way, as a number in
// the IOSTOUCH_WHEEL_AMMO_FONT pt digits font, fitted for three digits; on a
// wheel too narrow for that, smaller, down to IOSTOUCH_WHEEL_AMMO_MIN. Its row
// is as tall as the digits themselves (IOSTOUCH_WHEEL_AMMO_INK of the font
// size: the system font's digits are as tall as its capitals, 1443 of its
// 2048 units), not the whole line, so they sit the star gap under the name as
// the stars do; the digits are about centred in their line, so the label,
// a line tall, is centred on the row.
#define IOSTOUCH_WHEEL_AMMO_FONT 9.0
#define IOSTOUCH_WHEEL_AMMO_MIN 0.7
#define IOSTOUCH_WHEEL_AMMO_INK 0.705
// The name in the middle of the wheel shrinks to fit on one line, down to
// IOSTOUCH_WHEEL_TITLE_MIN of its size; a weapon's name that would need to go
// smaller ("STORMTROOPER RIFLE") goes on two lines instead
#define IOSTOUCH_WHEEL_TITLE_MIN 0.6
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
// The game's top message text sits this far under the top row, and keeps
// clear sideways of any other button whose top is less than
// IOSTOUCH_TOPTEXT_DEPTH below the text's top (about three lines)
#define IOSTOUCH_TOPTEXT_GAP 6.0
#define IOSTOUCH_TOPTEXT_DEPTH 70.0
#define IOSTOUCH_TOPTEXT_SIDE 8.0
#define IOSTOUCH_FPS_DEFAULTS_KEY @"iosTouchShowFps"
// The CAMERA button: shown while switched on in MENU's tray (remembered); a
// photo request that no frame takes this soon is dropped
#define IOSTOUCH_CAMERA_DEFAULTS_KEY @"iosTouchShowCamera"
#define IOSTOUCH_CAMERA_TIMEOUT 0.5
// Which photos a tap takes (IOSCAPTURE_* bits), set by holding the tray's
// camera switch: CLEAN alone unless changed (remembered)
#define IOSTOUCH_CAMKINDS_DEFAULTS_KEY @"iosTouchCameraKinds"
// The SABER button (between CAMERA and QUICK SAVE): shown once the player
// has the saber for real, or while sithewok is on (iosGame_IsSaberButtonShown,
// read every frame), it opens the saber wheel (iosSaber: the blade's colour).
// The colour picked for each blade of each style is remembered by its key,
// the style by its name.
// The CHEAT button (right of SABER): cheat mode. Switched on in MENU's tray
// (off unless switched on; remembered), it shows, and sithewok goes on;
// switched off, sithewok goes off and it hides. Held still this long
// (sliding off cancels; a tap does nothing) it switches sithewok on or off,
// as typing "sithewok" or "sithewok off" does. Red whenever the cheat is
// on, however it was switched (iosGame_IsSithEwokOn, read every frame).
#define IOSTOUCH_CHEAT_DEFAULTS_KEY @"iosTouchShowCheat"
#define IOSTOUCH_CHEAT_HOLD 0.5
// BACTA uses a bacta tank only once held this long (a tap does nothing;
// lifted or slid off sooner, nothing); one a press, its ring filling meanwhile
#define IOSTOUCH_BACTA_HOLD 0.3
#define IOSTOUCH_SABERSTYLE_DEFAULTS_KEY @"iosTouchSaberStyle"
#define IOSTOUCH_SABERCOLOUR_DEFAULTS_KEY @"iosTouchSaberColour" // + style name + blade number
#define IOSTOUCH_SABERTARGET_DEFAULTS_KEY @"iosTouchSaberTarget"
// The saber wheel's choices beside the ring: SINGLE and DUAL on its left;
// with DUAL, which blade a colour goes to (LEFT, BOTH, RIGHT) on its right.
// Each IOSTOUCH_SABER_CHIP_W by _H, this far out from the ring and apart.
enum { IOSTOUCH_CHIP_SINGLE = 0, IOSTOUCH_CHIP_DUAL, IOSTOUCH_CHIP_LEFT, IOSTOUCH_CHIP_BOTH, IOSTOUCH_CHIP_RIGHT,
       IOSTOUCH_CHIP_EQUIP, // (in the middle: the saber in hand, as it is set; lit while the player has it)
       IOSTOUCH_NUM_CHIPS };
#define IOSTOUCH_SABER_CHIP_W 64.0
#define IOSTOUCH_SABER_CHIP_H 28.0
#define IOSTOUCH_SABER_CHIP_OUT 22.0
#define IOSTOUCH_SABER_CHIP_GAP 8.0
// Where the FPS readout (54 pt wide) and the burst's count (30 pt) sit, which
// SABER keeps clear of
#define IOSTOUCH_FPS_LABEL_X 216.0     // (its middle, this far in from the right)
#define IOSTOUCH_FPS_LABEL_HALF 27.0
#define IOSTOUCH_BURST_LABEL_HALF 15.0
// On the CAMERA button: lifted sooner than this (not slid) takes the photos;
// held still this long is the selfie; slid IOSTOUCH_WHEEL_OPEN_SLIDE starts a
// burst of clean shots this far apart, for as long as the finger stays down
#define IOSTOUCH_SELFIE_HOLD 0.5
// How many photos a selfie takes (the camera options' SELFIES; remembered,
// 3 unless changed): its timing is iosSelfieShots'
#define IOSTOUCH_SELFIES_DEFAULTS_KEY @"iosTouchSelfieShots"
// Action Freeze (iosFreeze), with FREEZE on in the camera options (on by
// default): CAMERA slid down this far (mostly down) stops the action to look
// round it -- further than a burst's slide, so it isn't set off by chance;
// slid up as far, the cinematic burst (iosCinema: five photos round the
// frozen action, then the game goes on). Sideways, still the burst.
// While it is up: one finger turns the camera round what it looks at, this
// many degrees per point; two fingers move it (and pinch to zoom); CAMERA
// takes a clean photo; the X goes back to the game.
// The sithewok cheat's flight (iosGame_ToggleFlight) and noclip
// (iosGame_ToggleNoclip): JUMP or ACT held this long, and still (moved less
// than IOSTOUCH_FLY_STILL: a drag on them turns the view), switches it on or
// off; their rings only show while it can be switched. Except that while
// flying, holding JUMP is how to rise: then it never stops the flight; two
// quick taps on JUMP (each shorter than IOSTOUCH_FLY_TAP, the second down
// within IOSTOUCH_FLY_DOUBLE of the first's lift) do.
// Its level pick (iosGame_GoToLevel): DUCK held as long, and still, shows 1
// on the button, then one more every IOSTOUCH_LEVEL_STEP up to the last
// level, then round to 1 again, for as long as it is held; lifted, that
// level loads. Moved as far, no level.
#define IOSTOUCH_FLY_HOLD 0.5
#define IOSTOUCH_FLY_TAP 0.25
#define IOSTOUCH_FLY_DOUBLE 0.35
#define IOSTOUCH_FLY_STILL 10.0
#define IOSTOUCH_LEVEL_STEP 0.4
#define IOSTOUCH_FREEZE_DEFAULTS_KEY @"iosTouchFreeze"
#define IOSTOUCH_FREEZE_SWIPE 36.0
#define IOSTOUCH_FREEZE_EDGE 6.0       // (up: to this near the top of the screen counts as far enough...)
#define IOSTOUCH_FREEZE_SWIPE_MIN 16.0 // (...but never less than this)
#define IOSTOUCH_FREEZE_ORBIT 0.3
// The fingers' moves are gathered as they come and the camera follows them
// once per frame, smoothly (through two smoothing steps of this many seconds
// each): touches and frames don't keep step, and moving it at each touch
// gave frames that moved twice as far as the next (seen as jitter on an
// iPhone)
#define IOSTOUCH_FREEZE_SMOOTH 0.03
#define IOSTOUCH_FREEZE_CLOSE_RADIUS 22.0
// (the overlay's views, at most, whose see-through-ness a selfie keeps)
#define IOSTOUCH_MAX_SUBVIEWS 96
#define IOSTOUCH_BURST_GAP 0.2
#define IOSTOUCH_BURST_MAX 10
// iOS's camera shutter sound (not a documented ID, but long stable); a burst
// plays its own short tick instead (burst_tick.wav in the app)
#define IOSTOUCH_CAMERA_SOUND 1108
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
#define IOSTOUCH_GYRO_LAST_DEFAULTS_KEY @"iosTouchGyroLastMode" // the mode GYRO's tap switches back on
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
    ROLE_WHEEL,   // a touch on the open wheel (force or weapons)
    ROLE_IGNORED, // was down when the wheel opened, or only closed the MENU tray, or a second finger on FORCE WHEEL / NEXT WPN; ignored until it lifts
    ROLE_FREEZE,      // Action Freeze: moving the camera (one finger turns it, two move and zoom it)
    ROLE_FREEZESHOT,  // Action Freeze: on CAMERA (a tap takes a photo)
    ROLE_FREEZECLOSE, // Action Freeze: on its X (a tap goes back to the game)
};

enum {
    KIND_KEY = 0,  // holds its key while touched
    KIND_MENU,     // Escape (via stdControl_bControllerEscapeKey) when the touch lifts on it; held, opens the tray
    KIND_HOLDSAVE, // hold IOSTOUCH_QUICKSAVE_HOLD seconds for one press of its key (quick save)
    KIND_HOLDLOAD, // hold IOSTOUCH_QUICKLOAD_HOLD seconds to quick load
    KIND_WHEEL,    // tap: one press of its key; slide or hold: opens its wheel (FORCE WHEEL: force, NEXT WPN: weapons; SABER, which has no key: a tap opens it too, to tap)
    KIND_ITEM,     // uses an inventory item when the touch lifts on it; only shown while the player has it
    KIND_CHAT,     // (MENU tray) opens or closes the typing line for cheats, when the touch lifts on it
    KIND_FPS,      // (MENU tray) shows or hides the FPS readout, when the touch lifts on it
    KIND_GYRO,     // (MENU tray) gyro aiming on or off (back to its last mode), when the touch lifts on it; held: its options
    KIND_GYROMODE, // (gyro options) the mode gyro aiming goes on in: TOUCH or ALWAYS
    KIND_GYROSENS, // (gyro options) the next gyro aiming sensitivity, when the touch lifts on it
    KIND_CAMSWITCH, // (MENU tray) shows or hides the CAMERA button, when the touch lifts on it
    KIND_CHEAT,    // held still IOSTOUCH_CHEAT_HOLD: the sithewok cheat on or off (a tap does nothing)
    KIND_CHEATSWITCH, // (MENU tray) shows or hides the CHEAT button, when the touch lifts on it
    KIND_CAMERA,   // tap: the photos ticked in the camera options; slide: a burst; hold: the selfie
    KIND_CAMPHOTO, // (camera options) opens or closes the photo kinds' row under them
    KIND_SELFIES,  // (camera options) how many photos a selfie takes: 1, 3 or 5
    KIND_FREEZEOPT, // (camera options) Action Freeze on CAMERA's slide down, on or off
    KIND_CAMKIND,  // (photo kinds, under the camera options) ticks or unticks one kind of photo (bin: its IOSCAPTURE_* bit)
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
// a frame rate readout left of QUICK SAVE, the camera and saber switches
// (the CAMERA and SABER buttons along the top), and the keyboard, for the
// typing line (cheats). ACT is the door/switch key. NEXT WPN and FORCE WHEEL:
// a tap is the next weapon / learned power (G, E: the game's own keys for
// them), a slide or a hold opens the weapon / force wheel. SABER: a tap or a
// hold opens the saber wheel to tap a colour, a slide to slide to one.
enum {
    BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_CAMERA, // the middle of the top row, while switched on in the tray
    BTN_SABER,  // between CAMERA and QUICK SAVE, once the saber is had (or sithewok is on)
    BTN_CHEAT,  // ...right of SABER, in cheat mode (switched on in the tray)
    BTN_TRAYGYRO, BTN_TRAYFPS, BTN_TRAYCAM, BTN_TRAYCHEAT, BTN_TRAYKEYS, // MENU's tray (left to right), hidden unless it is open
    // Rows under the tray, one at a time (iosTouch_optRow), hidden until opened:
    BTN_GYROOPT_MODE, BTN_GYROOPT_SENS,  // the gyro options (a hold on the tray's GYRO)
    BTN_CAMROW_PHOTO, BTN_CAMROW_SELFIES, BTN_CAMROW_FREEZE, // the camera options (a hold on the tray's camera switch)
    BTN_CAMOPT_CLEAN, BTN_CAMOPT_HUD, BTN_CAMOPT_BTNS, // the photo kinds (PHOTO in the camera options), under those
    BTN_COUNT,
    BTN_TRAY_FIRST = BTN_TRAYGYRO, BTN_TRAY_LAST = BTN_TRAYKEYS,
    BTN_GYROOPT_FIRST = BTN_GYROOPT_MODE, BTN_GYROOPT_LAST = BTN_GYROOPT_SENS,
    BTN_CAMROW_FIRST = BTN_CAMROW_PHOTO, BTN_CAMROW_LAST = BTN_CAMROW_FREEZE,
    BTN_CAMOPT_FIRST = BTN_CAMOPT_CLEAN, BTN_CAMOPT_LAST = BTN_CAMOPT_BTNS
};
static iosTouchButton iosTouch_aButtons[] = {
    [BTN_FIRE]      = { "FIRE",        KIND_KEY,      SDL_SCANCODE_LCTRL,  42.0f, 1 },
    [BTN_ALT]       = { "ALT",         KIND_KEY,      SDL_SCANCODE_Z,      28.0f, 1 },
    [BTN_DUCK]      = { "DUCK",        KIND_KEY,      SDL_SCANCODE_C,      29.0f, 1 },
    [BTN_ACT]       = { "ACT",         KIND_KEY,      SDL_SCANCODE_SPACE,  29.0f, 1 },
    [BTN_JUMP]      = { "JUMP",        KIND_KEY,      SDL_SCANCODE_X,      31.0f, 1 },
    [BTN_FORCE]     = { "FORCE",       KIND_KEY,      SDL_SCANCODE_F,      30.0f, 1 },
    [BTN_NEXTWPN]   = { "NEXT\nWPN",   KIND_WHEEL,    SDL_SCANCODE_G,      22.0f, 0 },
    [BTN_WHEEL]     = { "FORCE\nWHEEL", KIND_WHEEL,   SDL_SCANCODE_E,      22.0f, 0 },
    [BTN_LIGHT]     = { "LIGHT",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_FIELDLIGHT_IOS },
    [BTN_IR]        = { "IR",          KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_IRGOGGLES_IOS },
    [BTN_BACTA]     = { "BACTA",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_BACTATANK_IOS },
    [BTN_QUICKSAVE] = { "QUICK\nSAVE", KIND_HOLDSAVE, SDL_SCANCODE_F9,     22.0f, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD", KIND_HOLDLOAD, -1,                  22.0f, 0 },
    [BTN_MENU]      = { "MENU",        KIND_MENU,     -1,                  22.0f, 0 },
    [BTN_CAMERA]    = { "",            KIND_CAMERA,   -1,                  22.0f, 0 },
    [BTN_SABER]     = { "",            KIND_WHEEL,    -1,                  22.0f, 0 },
    [BTN_CHEAT]     = { "",            KIND_CHEAT,    -1,                  22.0f, 0 },
    [BTN_TRAYGYRO]  = { "GYRO",        KIND_GYRO,     -1,                  20.0f, 0 },
    [BTN_TRAYFPS]   = { "FPS",         KIND_FPS,      -1,                  20.0f, 0 },
    [BTN_TRAYCAM]   = { "",            KIND_CAMSWITCH, -1,                 20.0f, 0 },
    [BTN_TRAYCHEAT] = { "",            KIND_CHEATSWITCH, -1,               20.0f, 0 },
    [BTN_TRAYKEYS]  = { "",            KIND_CHAT,     -1,                  20.0f, 0 },
    [BTN_GYROOPT_MODE] = { "MODE",     KIND_GYROMODE, -1,                  20.0f, 0 },
    [BTN_GYROOPT_SENS] = { "SENS",     KIND_GYROSENS, -1,                  20.0f, 0 },
    [BTN_CAMROW_PHOTO] = { "PHOTO",    KIND_CAMPHOTO, -1,                  20.0f, 0 },
    [BTN_CAMROW_SELFIES] = { "SELFIES", KIND_SELFIES, -1,                  20.0f, 0 },
    [BTN_CAMROW_FREEZE] = { "FREEZE",  KIND_FREEZEOPT, -1,                 20.0f, 0 },
    [BTN_CAMOPT_CLEAN] = { "CLEAN",    KIND_CAMKIND,  -1,                  20.0f, 0, IOSCAPTURE_CLEAN },
    [BTN_CAMOPT_HUD]   = { "HUD",      KIND_CAMKIND,  -1,                  20.0f, 0, IOSCAPTURE_HUD },
    [BTN_CAMOPT_BTNS]  = { "HUD+\nBTNS", KIND_CAMKIND, -1,                 20.0f, 0, IOSCAPTURE_HUDBTNS },
};
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))
typedef char iosTouch_assertButtonCount[(IOSTOUCH_NUM_BUTTONS == BTN_COUNT) ? 1 : -1];

static int iosTouch_IsTrayButton(int button)
{
    return button >= BTN_TRAY_FIRST && button <= BTN_TRAY_LAST;
}

static int iosTouch_IsCamOptButton(int button)
{
    return button >= BTN_CAMOPT_FIRST && button <= BTN_CAMOPT_LAST;
}

// The rows under the tray: the gyro options or the camera options (one at a
// time), and the photo kinds under the camera options
enum { IOSTOUCH_ROW_NONE = 0, IOSTOUCH_ROW_GYRO, IOSTOUCH_ROW_CAMERA };
static int iosTouch_IsOptButton(int button)
{
    return (button >= BTN_GYROOPT_FIRST && button <= BTN_GYROOPT_LAST)
           || (button >= BTN_CAMROW_FIRST && button <= BTN_CAMROW_LAST) || iosTouch_IsCamOptButton(button);
}

// A tray button held still opens its row (GYRO, the camera switch)
static int iosTouch_IsRowOpener(int button)
{
    return button == BTN_TRAYGYRO || button == BTN_TRAYCAM;
}

static int iosTouch_IsQuickHold(int button)
{
    return button == BTN_QUICKSAVE || button == BTN_QUICKLOAD;
}

// ------------------------------------------------------------ wheel maps

// Every power has its own fixed slice of the force wheel, whether or not the
// player has learned it yet (those are dimmed), so a power is always in the
// same place; every weapon likewise on the weapon wheel. The slices come in
// coloured groups, a small gap between groups and a wider one at the bottom,
// which cancels. Angles are the slice's middle, in degrees counter-clockwise
// from pointing right (90 is up).
#define IOSTOUCH_WHEEL_MAX 17
#define IOSTOUCH_WHEEL_MAX_GROUPS 5
typedef struct {
    int bin;   // SITHBIN_F_* or a weapon's SITHBIN_* (types_enums.h can't be included here)
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
// to bottom down the left, tier IV its column down the right except that
// Deadly Sight swaps with Protection to sit by the gap, as in JK; tier II is
// in the game's own power order, and tier III puts its long names where the
// slices have room for them. Defense is multiplayer only and can't be
// selected, so it isn't on the wheel.
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

// The weapon wheels: every weapon of the game in its slot, owned or not
// (those are dimmed), each with its ammo count under its name, in the game's
// own order (the number keys, and NEXT WPN's), clockwise from the bottom
// left, with the gap at the bottom to cancel. The groups are colours only,
// by ammo: none, energy, power cells, explosives, and Mysteries of the
// Sith's heavy weapons.
static const iosTouchWheelGroup iosTouch_aWeaponGroups[] = {
    { NULL, 0.0f, 255, 214, 120 }, // fists, lightsaber
    { NULL, 0.0f, 120, 200, 255 }, // energy
    { NULL, 0.0f, 110, 220, 170 }, // power cells
    { NULL, 0.0f, 255, 105,  90 }, // thermals, rail charges, sequencers
    { NULL, 0.0f, 190, 150, 255 }, // E-Web rounds, carbonite (Mysteries of the Sith)
};
// Jedi Knight: keys 1-9 and 0 (bins 1-10), the bowcaster and the repeater
// either side of straight up, the fists and the lightsaber either side of the gap
static const iosTouchWheelSlice iosTouch_aJkWeaponSlices[] = {
    {  1, 0, 234.0f }, {  2, 1, 202.0f }, {  3, 1, 170.0f }, {  4, 3, 138.0f }, {  5, 2, 106.0f }, // Fists, Bryar, Rifle, Thermal, Bowcaster
    {  6, 2,  74.0f }, {  7, 3,  42.0f }, {  8, 3,  10.0f }, {  9, 2, -22.0f }, { 10, 0, -54.0f }, // Repeater, Rail det, Sequencer, Concussion, Saber
};
// Mysteries of the Sith (bins 121-140: weapon index + 120): each key's two
// weapons side by side, in the order NEXT WPN goes through them. 125, 136 and
// 139 are left out: the all-weapons cheat (jkDev_CmdAllWeapons) doesn't give them.
static const iosTouchWheelSlice iosTouch_aMotsWeaponSlices[] = {
    { 121, 0, 242.0f }, { 131, 0, 223.0f }, { 122, 1, 204.0f }, { 132, 1, 185.0f }, { 123, 1, 166.0f }, { 133, 1, 147.0f }, // Fists, Saber, Bryar, Blastech, Rifle, Scope rifle
    { 124, 3, 128.0f }, { 134, 3, 109.0f }, { 135, 2,  90.0f }, { 126, 2,  71.0f }, { 127, 3,  52.0f }, { 137, 3,  33.0f }, // Thermal, Flash bomb, Bowcaster, Repeater, Rail det, Rail seeker
    { 128, 3,  14.0f }, { 138, 3,  -5.0f }, { 129, 2, -24.0f }, { 130, 4, -43.0f }, { 140, 4, -62.0f },                     // Sequencer, Manual seq, Concussion, E-Web, Carbo gun
};
static const iosTouchWheelMap iosTouch_jkWeaponWheel = {
    iosTouch_aJkWeaponSlices, IOSTOUCH_COUNT(iosTouch_aJkWeaponSlices),
    iosTouch_aWeaponGroups, IOSTOUCH_COUNT(iosTouch_aWeaponGroups), 32.0f, 0.0f
};
static const iosTouchWheelMap iosTouch_motsWeaponWheel = {
    iosTouch_aMotsWeaponSlices, IOSTOUCH_COUNT(iosTouch_aMotsWeaponSlices),
    iosTouch_aWeaponGroups, IOSTOUCH_COUNT(iosTouch_aWeaponGroups), 19.0f, 0.0f
};
typedef char iosTouch_assertWheelSize[(IOSTOUCH_COUNT(iosTouch_aJkWheelSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aMotsWheelSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aJkWeaponSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aMotsWeaponSlices) <= IOSTOUCH_WHEEL_MAX
                                       && IOSTOUCH_COUNT(iosTouch_aMotsWheelGroups) <= IOSTOUCH_WHEEL_MAX_GROUPS
                                       && IOSTOUCH_COUNT(iosTouch_aWeaponGroups) <= IOSTOUCH_WHEEL_MAX_GROUPS) ? 1 : -1];
// The saber wheel: every blade colour (iosSaber) in its own slice and in its
// own colour, the game's own by the gap on the left, then red round through
// the hues to pink, white and black by the gap on the right. Filled in as the
// overlay starts (iosTouch_SetupSaberWheel).
#define IOSTOUCH_SABER_SLICE_DEG 18.0f
static iosTouchWheelSlice iosTouch_aSaberSlices[IOSTOUCH_WHEEL_MAX];
static iosTouchWheelGroup iosTouch_aSaberGroups[IOSTOUCH_WHEEL_MAX]; // (one per slice: its colour)
static iosTouchWheelMap iosTouch_saberWheel = {
    iosTouch_aSaberSlices, 0, iosTouch_aSaberGroups, 0, IOSTOUCH_SABER_SLICE_DEG, 0.0f
};
static void iosTouch_SetupSaberWheel(void)
{
    const int n = MIN(iosSaber_NumColours(), IOSTOUCH_WHEEL_MAX);
    for (int i = 0; i < n; i++) {
        iosTouchWheelGroup* g = &iosTouch_aSaberGroups[i];
        g->title = NULL;
        g->titleDeg = 0.0f;
        iosSaber_ColourRGB(i, &g->r, &g->g, &g->b);
        iosTouch_aSaberSlices[i].bin = i;
        iosTouch_aSaberSlices[i].group = i;
        iosTouch_aSaberSlices[i].deg = 90.0f + IOSTOUCH_SABER_SLICE_DEG * ((n - 1) * 0.5f - i);
    }
    iosTouch_saberWheel.numSlices = n;
    iosTouch_saberWheel.numGroups = n;
}
enum { IOSTOUCH_WHEEL_FORCE = 0, IOSTOUCH_WHEEL_WEAPONS, IOSTOUCH_WHEEL_SABER, IOSTOUCH_WHEEL_NUM_KINDS };

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
    int bWheelOpener;     // ROLE_WHEEL: the touch on FORCE WHEEL / NEXT WPN that opened it...
    int bWheelHeld;       // ...by holding still: the wheel is open to tap, until it slides IOSTOUCH_WHEEL_DEAD
    int bArmed;           // ...has slid out of the dead zone
    CGPoint wheelOrigin;  // ...where its slide is measured from
    CGFloat wheelAim;     // ...which way it points (degrees, as for slices)
    int bSwipe;           // CAMERA, with FREEZE on: slid up or down, on its way to the cinematic burst or Action Freeze (no tap, no selfie hold)
    int bFlyDone;         // JUMP, ACT: this press has switched sithewok's flight or noclip, or can't (it moved). DUCK: no level pick on this press (it moved, or sithewok is off)
    int levelPick;        // DUCK: the level number on show (sithewok's level pick), or 0
} iosTouchSlot;

static iosTouchSlot iosTouch_aSlots[IOSTOUCH_MAX_TOUCHES];
static int iosTouch_levelToLoad; // DUCK's level pick, lifted: loaded at the next tick (as quick load)
static double iosTouch_tJumpTapUp = -1.0; // when the last quick tap on JUMP lifted (by the touch's clock), for the double tap that lands
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
// Where the game's top message text goes, in points: top, left, right
static int iosTouch_bTopText = 0;
static float iosTouch_aTopText[3];
static float iosTouch_aLayoutGauge[4];
static int iosTouch_layoutCutoutRight = -1;
// The wheel: open or not (and whether it is open to tap a slice), which one
// (force or weapons, and the button that opened it), the map it shows, which
// of the map's slices can be picked (a learned power; a weapon the player has
// and could switch to), and where it is. All read as it opens: the game holds
// still while it is open.
static int iosTouch_bWheelOpen = 0;
static int iosTouch_bWheelTapMode = 0;
static int iosTouch_wheelKind = IOSTOUCH_WHEEL_FORCE;
static int iosTouch_wheelButton = BTN_WHEEL;
static const iosTouchWheelMap* iosTouch_pWheelMap = NULL;
static int iosTouch_aWheelEarned[IOSTOUCH_WHEEL_MAX];
static int iosTouch_aWheelLevel[IOSTOUCH_WHEEL_MAX]; // force: as the wheel opened (iosGame_GetPowerLevel)
static int iosTouch_aWheelOwned[IOSTOUCH_WHEEL_MAX]; // weapons: the player has it...
static int iosTouch_aWheelAmmo[IOSTOUCH_WHEEL_MAX];  // ...and its ammo count (-1: none to show)
static CGPoint iosTouch_wheelCentre;
static CGFloat iosTouch_wheelR0 = 0.0, iosTouch_wheelR1 = 0.0;
// MENU's tray; Escape held down for a MENU tap (iosTouch_Update calls left);
// the FPS readout
static int iosTouch_bTrayOpen = 0;
static int iosTouch_menuPulse = 0;
static int iosTouch_bShowFps = 0;
static int iosTouch_bShowCamera = 0;
static int iosTouch_bShowSaber = 0;
static int iosTouch_bShowCheat = 0;
static int iosTouch_bCheatOn = 0;   // the sithewok cheat, as last read (CHEAT red)
static int iosTouch_bFpsBottom = 0; // the FPS readout at the bottom of the screen, making room for SABER and CHEAT
// With two blades, the one a colour picked on the saber wheel goes to
enum { IOSTOUCH_SABER_RIGHT = 0, IOSTOUCH_SABER_LEFT = 1, IOSTOUCH_SABER_BOTH = 2 };
static int iosTouch_saberTarget = IOSTOUCH_SABER_BOTH;
static int iosTouch_bBurstUnder = 0; // the burst's count under CAMERA, not beside it (making room for SABER)
static CFTimeInterval iosTouch_tCameraAsked = 0; // when the CAMERA button asked for photos (0: not waiting)
static int iosTouch_cameraKinds = IOSCAPTURE_CLEAN;
static int iosTouch_optRow = 0;      // IOSTOUCH_ROW_*: the row open under the tray
static int iosTouch_bKindsOpen = 0;  // the photo kinds' row, under the camera options
static int iosTouch_selfieShots = 3; // photos a selfie takes (1, 3 or 5; remembered)
static int iosTouch_bFreezeOn = 1;   // CAMERA's slide down: Action Freeze (remembered)
static int iosTouch_bFreezing = 0;   // Action Freeze is up
static int iosTouch_bCinema = 0;     // the cinematic burst is running
// Action Freeze: the fingers' moves not yet followed (degrees round and up,
// screen heights across and down, and the zoom as a log), and part way
// through the smoothing
enum { IOSTOUCH_FZ_YAW = 0, IOSTOUCH_FZ_PITCH, IOSTOUCH_FZ_PANX, IOSTOUCH_FZ_PANY, IOSTOUCH_FZ_ZOOM, IOSTOUCH_FZ_NUM };
static float iosTouch_aFreezeMove[IOSTOUCH_FZ_NUM], iosTouch_aFreezeMid[IOSTOUCH_FZ_NUM];
static UITouch* iosTouch_pBurstTouch = nil; // the finger a burst goes on for (not retained; only compared)
static int iosTouch_burstShots = 0;
static int iosTouch_selfieFlashes = 0; // a selfie's photos taken so far (after the first, a burst's lighter flash and click)
static SystemSoundID iosTouch_burstSound = 0; // (0: not loaded; then the shutter)
static CFTimeInterval iosTouch_tBurstNext = 0;
// A selfie going on (iosSelfieShots runs it; the overlay hides its buttons
// and ignores touches meanwhile)
enum { IOSTOUCH_SELFIE_NONE = 0, IOSTOUCH_SELFIE_POSING };
static int iosTouch_selfieStage = IOSTOUCH_SELFIE_NONE;
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
static int iosTouch_gyroLastMode = IOSTOUCH_GYRO_TOUCH; // the mode GYRO switches back on in
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

// The name of a slice's power or weapon on the open wheel
static const char* iosTouch_WheelSlotName(int bin)
{
    if (iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER) return iosSaber_ColourName(bin);
    return (iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS) ? iosGame_GetWeaponName(bin) : iosGame_GetPowerName(bin);
}

// A press on FORCE WHEEL or NEXT WPN still waiting to see which gesture it
// is: a tap, a slide or a hold
// The wheel a KIND_WHEEL button opens
static int iosTouch_WheelKindOf(int button)
{
    if (button == BTN_NEXTWPN) return IOSTOUCH_WHEEL_WEAPONS;
    if (button == BTN_SABER) return IOSTOUCH_WHEEL_SABER;
    return IOSTOUCH_WHEEL_FORCE;
}

static int iosTouch_IsWheelPress(const iosTouchSlot* s)
{
    return s->touch && s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].kind == KIND_WHEEL;
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
    if ([d objectForKey:IOSTOUCH_GYRO_LAST_DEFAULTS_KEY]) {
        NSInteger m = [d integerForKey:IOSTOUCH_GYRO_LAST_DEFAULTS_KEY];
        iosTouch_gyroLastMode = (m == IOSTOUCH_GYRO_ALWAYS) ? IOSTOUCH_GYRO_ALWAYS : IOSTOUCH_GYRO_TOUCH;
    }
    if (iosTouch_gyroMode != IOSTOUCH_GYRO_OFF) iosTouch_gyroLastMode = iosTouch_gyroMode;
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

// GYRO's tap (MENU's tray): off, or back on in the mode it was last on in;
// the gyro options' MODE: the other of TOUCH and ALWAYS (and on in it, if
// it is on); all kept in the app's settings
static void iosTouch_GyroSave(void)
{
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    [d setInteger:iosTouch_gyroMode forKey:IOSTOUCH_GYRO_DEFAULTS_KEY];
    [d setInteger:iosTouch_gyroLastMode forKey:IOSTOUCH_GYRO_LAST_DEFAULTS_KEY];
}
static void iosTouch_GyroToggle(void)
{
    if (iosTouch_gyroMode != IOSTOUCH_GYRO_OFF) {
        iosTouch_gyroLastMode = iosTouch_gyroMode;
        iosTouch_gyroMode = IOSTOUCH_GYRO_OFF;
    }
    else {
        iosTouch_gyroMode = iosTouch_gyroLastMode;
    }
    iosTouch_GyroSave();
}
static void iosTouch_GyroNextMode(void)
{
    iosTouch_gyroLastMode = (iosTouch_gyroLastMode == IOSTOUCH_GYRO_TOUCH) ? IOSTOUCH_GYRO_ALWAYS : IOSTOUCH_GYRO_TOUCH;
    if (iosTouch_gyroMode != IOSTOUCH_GYRO_OFF) iosTouch_gyroMode = iosTouch_gyroLastMode;
    iosTouch_GyroSave();
}
static void iosTouch_GyroNextSens(void)
{
    iosTouch_gyroSens = (iosTouch_gyroSens + 1) % IOSTOUCH_COUNT(iosTouch_aGyroSens);
    [[NSUserDefaults standardUserDefaults] setFloat:iosTouch_aGyroSens[iosTouch_gyroSens] forKey:IOSTOUCH_GYRO_SENS_DEFAULTS_KEY];
}

// Whether either thumb is down on the game, for TOUCH: the left one on the
// move stick (pushed or resting, on the run marker or not), or a right one
// where it aims -- on the look area, or on one of the buttons under the
// right thumb that pass a drag through to looking (FIRE, ALT, DUCK, ACT,
// JUMP, FORCE). So with the left thumb on the stick, the right one can hop
// from the look area onto FIRE (and is in the air for a moment) without the
// aiming stopping. Not the top row, the wheel, the tray, or a touch being
// ignored.
static int iosTouch_GyroThumbDown(void)
{
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_STICK || s->role == ROLE_LOOK
            || (s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].bLookWhileHeld)) return 1;
    }
    return 0;
}

// Once per frame, from iosTouch_UpdateGyro, after the frame's touches came in
// (bShown: whether the overlay v is up). The motion updates run only while it
// is shown, the app is active, the gyro is on and the screen is turned either
// landscape way. What the phone turned since the last frame goes to the mouse
// axes, on top of the look drag's movement -- if it could aim both then and
// now, so in TOUCH lifting the last thumb freezes the view where it is (the
// frame it lifted in counts for nothing, as does the frame the first one
// lands in) and nothing springs back, and touching again carries on from
// there, from however the phone is held by then. Never while the force
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

// A wheel's labels fitted to its slices (fitWheelLabels), each twice: [0]
// alone, [1] with the row under it (a learned power's stars, a weapon's ammo
// count). One for each wheel, kept until the screen size changes.
typedef struct {
    const iosTouchWheelMap* map;  // the map and size it was fitted for
    CGFloat R1;
    CGFloat rowScale;             // the rows' size, as a fraction of full size
    CGFloat font[IOSTOUCH_WHEEL_MAX][2];   // the label's size (0: the row doesn't fit)...
    CGFloat labelR[IOSTOUCH_WHEEL_MAX][2]; // ...how far out it sits (with the row: their middle)...
    CGSize labelSize[IOSTOUCH_WHEEL_MAX][2]; // ...and how big it is
} iosTouchWheelFit;

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
    CAShapeLayer* jumpRing; // JUMP's hold progress (sithewok's flight)
    CAShapeLayer* actRing;  // ACT's hold progress (sithewok's noclip)
    CAShapeLayer* duckRing; // DUCK's hold progress (sithewok's level pick)
    CAShapeLayer* loadRing; // QUICK LOAD's
    CAShapeLayer* menuRing; // MENU's
    CAShapeLayer* wheelRing;  // FORCE WHEEL's (held still, it opens the wheel to tap)
    CAShapeLayer* weaponRing; // NEXT WPN's
    CAShapeLayer* saberRing;  // SABER's
    CAShapeLayer* cheatRing;  // CHEAT's hold progress
    CAShapeLayer* bactaRing;  // BACTA's
    UILabel* aCheatSub[3];    // under JUMP, ACT and DUCK while sithewok is on: FLY, NOCLIP, LVL SEL
    CAShapeLayer* cheatEmblem; // CHEAT's Sith emblem (red, with its edge, while the cheat is on)
    // The saber's picture, on SABER and on the tray's saber switch: a hilt and
    // its blade, in the blade's colour (refreshSaberIcons)
    CAShapeLayer* aSaberBlade[1];
    CAShapeLayer* aSaberCore[1];
    CAShapeLayer* aSaberBlade2[1]; // ...and with two blades, the second (crossing the first)
    CAShapeLayer* aSaberCore2[1];
    CAShapeLayer* aSaberHilt2[1];
    int saberIconRGB, saberIconRGB2; // the colours they are in (-2: none yet; the second, -1: one blade)
    UILabel* aSaberChips[IOSTOUCH_NUM_CHIPS]; // the saber wheel's choices beside the ring
    UIView* trayBack;       // behind MENU's tray
    UIView* cameraFlash;    // the CAMERA button's white flash, over everything
    UIImpactFeedbackGenerator* cameraTap;
    UIView* camOptBack;     // behind the photo kinds
    UIView* camRowBack;     // behind the camera options
    UIView* gyroOptBack;    // behind the gyro options
    CAShapeLayer* camOptRing; // the tray's camera switch, held to open the camera options
    CAShapeLayer* gyroOptRing; // the tray's GYRO, held to open the gyro options
    UILabel* burstLabel;    // beside CAMERA: how many shots a burst has taken
    UILabel* freezeClose;   // Action Freeze's X (back to the game)
    CAShapeLayer* cameraRing; // CAMERA's, held for the selfie
    CGFloat aSelfieAlpha[IOSTOUCH_MAX_SUBVIEWS]; // the overlay's views as they were, hidden during a selfie
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
    iosTouchWheelFit aWheelFit[IOSTOUCH_WHEEL_NUM_KINDS];
    CAShapeLayer* aSliceStars[IOSTOUCH_WHEEL_MAX][2]; // a learned power's level: its filled stars, its empty ones
    UILabel* aSliceRows[IOSTOUCH_WHEEL_MAX];          // a weapon's ammo count
    CGSize ammoRowSize;                    // "000" in the ammo count's font, at full size
    UILabel* aGroupLabels[IOSTOUCH_WHEEL_MAX_GROUPS];
    CAShapeLayer* wheelNeedle;             // in the middle: which way the sliding thumb points
    UILabel* wheelTitle;                   // in the middle: the power pointed at, or the selected one
    const char* wheelTitleName;            // the name it shows (laid out for it: setWheelTitle)
    int bWheelTitleSet;                    // (0: lay it out again)
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
- (void)endLevelPick:(iosTouchSlot*)s;
- (void)cameraFeedback:(int)mode;
- (void)endSelfie;
- (void)refreshGyroLabels;
- (void)setOptRow:(int)row;
- (void)setKindsOpen:(int)bOpen;
- (void)useOptButton:(int)button;
- (void)releaseOptTouches;
- (void)hideForPhotosKeeping:(UIView*)keep;
- (void)showAfterPhotos;
- (void)startFreeze;
- (void)endFreeze;
- (void)moveFreeze:(iosTouchSlot*)s to:(CGPoint)p;
- (void)takeFreezePhoto;
- (void)tickFreeze:(CFTimeInterval)dt;
- (void)startCinema;
- (void)endCinema;
- (void)refreshButtonLooks;
@end

// CHEAT's picture: the Sith emblem, as drawn from the owner's picture of it:
// the outer hexagon (broken at each side's middle), six spokes from a round
// middle, each ending in a chevron. Points for the top's piece of each,
// turned six ways; the unit is the hexagon's corner radius, y up.
static const CGPoint iosTouch_aSithRing[6] = {
    { 0.4019, 0.7680 }, { 0.0000, 1.0000 }, { -0.4019, 0.7680 }, { -0.3610, 0.6972 }, { 0.0000, 0.9057 }, { 0.3610, 0.6972 }
};
static const CGPoint iosTouch_aSithSpoke[13] = {
    { 0.0438, 0.1961 }, { 0.0980, 0.6679 }, { 0.2026, 0.6075 }, { 0.2026, 0.5761 }, { 0.2288, 0.5761 }, { 0.2288, 0.6491 },
    { 0.0000, 0.7811 },
    { -0.2288, 0.6491 }, { -0.2288, 0.5761 }, { -0.2026, 0.5761 }, { -0.2026, 0.6075 }, { -0.0980, 0.6679 }, { -0.0438, 0.1961 }
};
#define IOSTOUCH_SITH_DISC 0.2843f
// (its size on CHEAT: the hexagon's corners at this much of the button's radius)
#define IOSTOUCH_SITH_SIZE 0.64f

static UIBezierPath* IOSTouch_SithEmblemPath(CGPoint c, CGFloat r)
{
    UIBezierPath* path = [UIBezierPath bezierPath];
    for (int k = 0; k < 6; k++) {
        const CGFloat a = k * M_PI / 3.0, ca = cos(a), sa = sin(a);
        for (int piece = 0; piece < 2; piece++) {
            const CGPoint* aPt = piece ? iosTouch_aSithSpoke : iosTouch_aSithRing;
            const int n = piece ? 13 : 6;
            for (int i = 0; i < n; i++) {
                // (turned, then y flipped for the screen)
                CGPoint q = CGPointMake(c.x + r * (aPt[i].x * ca - aPt[i].y * sa), c.y - r * (aPt[i].x * sa + aPt[i].y * ca));
                if (i == 0) [path moveToPoint:q]; else [path addLineToPoint:q];
            }
            [path closePath];
        }
    }
    // the round middle, wound the same way as the spokes it overlaps (so,
    // filled by the non-zero rule, the overlaps stay filled)
    for (int i = 0; i < 48; i++) {
        const CGFloat a = -i * 2.0 * M_PI / 48.0;
        CGPoint q = CGPointMake(c.x + r * IOSTOUCH_SITH_DISC * cos(a), c.y + r * IOSTOUCH_SITH_DISC * sin(a));
        if (i == 0) [path moveToPoint:q]; else [path addLineToPoint:q];
    }
    [path closePath];
    return path;
}

static void iosTouch_PhotosAsked(void);

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

// A lightsaber drawn on a button r points round, pointing up to the right
// (or, bMirror, up to the left): the hilt, then the blade in a colour
// (refreshSaberIcons) with its white middle; returns the blade, its middle
// and the hilt
static void IOSTouch_AddSaberIcon(UIView* v, CGFloat r, int bMirror, CAShapeLayer** pBlade, CAShapeLayer** pCore, CAShapeLayer** pHilt)
{
    CGFloat sx = bMirror ? -1.0 : 1.0; // (mirrored: pointing up to the left)
    CGPoint hilt0 = CGPointMake(r + sx * r * -0.42, r * 1.42), hilt1 = CGPointMake(r + sx * r * -0.18, r * 1.18);
    CGPoint tip = CGPointMake(r + sx * r * 0.46, r * 0.54);
    UIBezierPath* hp = [UIBezierPath bezierPath];
    [hp moveToPoint:hilt0];
    [hp addLineToPoint:hilt1];
    UIBezierPath* bp = [UIBezierPath bezierPath];
    [bp moveToPoint:hilt1];
    [bp addLineToPoint:tip];
    CAShapeLayer* hilt = [CAShapeLayer layer];
    hilt.frame = CGRectMake(0, 0, r * 2, r * 2);
    hilt.path = hp.CGPath;
    hilt.strokeColor = [UIColor colorWithWhite:0.78 alpha:1.0].CGColor;
    hilt.lineWidth = MAX(r * 0.2, 3.5);
    CAShapeLayer* blade = [CAShapeLayer layer];
    blade.frame = hilt.frame;
    blade.path = bp.CGPath;
    blade.lineWidth = MAX(r * 0.17, 3.0);
    blade.lineCap = kCALineCapRound;
    blade.shadowOffset = CGSizeZero;
    blade.shadowRadius = 3.0;
    blade.shadowOpacity = 0.9;
    CAShapeLayer* core = [CAShapeLayer layer];
    core.frame = hilt.frame;
    core.path = bp.CGPath;
    core.strokeColor = [UIColor colorWithWhite:1.0 alpha:0.9].CGColor;
    core.lineWidth = MAX(r * 0.06, 1.2);
    core.lineCap = kCALineCapRound;
    [v.layer addSublayer:blade];
    [v.layer addSublayer:core];
    [v.layer addSublayer:hilt];
    *pBlade = blade;
    *pCore = core;
    if (pHilt) *pHilt = hilt;
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
        jumpRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_JUMP].radius);
        [aButtonViews[BTN_JUMP].layer addSublayer:jumpRing];
        actRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_ACT].radius);
        [aButtonViews[BTN_ACT].layer addSublayer:actRing];
        duckRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_DUCK].radius);
        [aButtonViews[BTN_DUCK].layer addSublayer:duckRing];
        loadRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_QUICKLOAD].radius);
        [aButtonViews[BTN_QUICKLOAD].layer addSublayer:loadRing];
        menuRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_MENU].radius);
        [aButtonViews[BTN_MENU].layer addSublayer:menuRing];
        // ...and FORCE WHEEL and NEXT WPN, held to open their wheels to tap
        wheelRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_WHEEL].radius);
        [aButtonViews[BTN_WHEEL].layer addSublayer:wheelRing];
        weaponRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_NEXTWPN].radius);
        [aButtonViews[BTN_NEXTWPN].layer addSublayer:weaponRing];
        saberRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_SABER].radius);
        [aButtonViews[BTN_SABER].layer addSublayer:saberRing];

        // SABER: the saber's picture; shown once the game says so (tick);
        // the colours picked, from last time
        saberIconRGB = saberIconRGB2 = -2;
        IOSTouch_AddSaberIcon(aButtonViews[BTN_SABER], iosTouch_aButtons[BTN_SABER].radius, 1, &aSaberBlade2[0], &aSaberCore2[0], &aSaberHilt2[0]);
        IOSTouch_AddSaberIcon(aButtonViews[BTN_SABER], iosTouch_aButtons[BTN_SABER].radius, 0, &aSaberBlade[0], &aSaberCore[0], NULL);
        iosTouch_SetupSaberWheel();
        [self loadSaberChoices];
        iosTouch_bShowSaber = 0;
        aButtonViews[BTN_SABER].hidden = YES;
        [self refreshSaberIcons];

        // CHEAT: the Sith emblem, in the white of the other buttons' labels
        // (red, with the button's edge, while the cheat is on), under the
        // hold's ring; its tray switch just says CHEAT
        {
            const CGFloat r = iosTouch_aButtons[BTN_CHEAT].radius;
            cheatEmblem = [CAShapeLayer layer];
            cheatEmblem.frame = CGRectMake(0, 0, 2 * r, 2 * r);
            cheatEmblem.path = IOSTouch_SithEmblemPath(CGPointMake(r, r), r * IOSTOUCH_SITH_SIZE).CGPath;
            cheatEmblem.fillColor = [UIColor colorWithWhite:1.0 alpha:0.85].CGColor;
            [aButtonViews[BTN_CHEAT].layer addSublayer:cheatEmblem];
        }
        // BACTA's hold ring
        bactaRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_BACTA].radius);
        [aButtonViews[BTN_BACTA].layer addSublayer:bactaRing];
        // sithewok's own uses of JUMP, ACT and DUCK (held), named small under
        // their labels while it is on
        {
            const int aSubBtn[3] = { BTN_JUMP, BTN_ACT, BTN_DUCK };
            NSString* aSubText[3] = { @"FLY", @"NOCLIP", @"LVL SEL" };
            for (int k = 0; k < 3; k++) {
                const CGFloat r = iosTouch_aButtons[aSubBtn[k]].radius;
                UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 2.0 * r, 10.0)];
                l.center = CGPointMake(r, r + r * 0.5);
                l.text = aSubText[k];
                l.textAlignment = NSTextAlignmentCenter;
                l.font = [UIFont boldSystemFontOfSize:7.5];
                l.textColor = [UIColor colorWithRed:1.0 green:0.58 blue:0.52 alpha:0.95];
                l.hidden = YES;
                aCheatSub[k] = l;
                [aButtonViews[aSubBtn[k]] addSubview:l];
            }
        }
        cheatRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_CHEAT].radius);
        [aButtonViews[BTN_CHEAT].layer addSublayer:cheatRing];
        aButtonViews[BTN_TRAYCHEAT].text = @"CHEAT";
        aButtonViews[BTN_TRAYCHEAT].font = [UIFont boldSystemFontOfSize:IOSTouch_FontSize("CHEAT", iosTouch_aButtons[BTN_TRAYCHEAT].radius)];
        iosTouch_bShowCheat = [[NSUserDefaults standardUserDefaults] boolForKey:IOSTOUCH_CHEAT_DEFAULTS_KEY] ? 1 : 0;
        aButtonViews[BTN_CHEAT].hidden = !iosTouch_bShowCheat;
        iosGame_SetCheatShown(iosTouch_bShowCheat);
        iosTouch_bCheatOn = iosGame_IsSithEwokOn();

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
        const int aTwoLine[] = { BTN_TRAYGYRO, BTN_GYROOPT_MODE, BTN_GYROOPT_SENS, BTN_CAMROW_SELFIES, BTN_CAMROW_FREEZE };
        for (int k = 0; k < IOSTOUCH_COUNT(aTwoLine); k++) {
            const int i = aTwoLine[k];
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
        // The CAMERA button and its switch in the tray: the camera symbol
        // (filled on the button, an outline on the switch)
        UIImageSymbolConfiguration* camCfg = [UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightSemibold];
        const int aCamBtn[2] = { BTN_CAMERA, BTN_TRAYCAM };
        NSString* aCamSym[2] = { @"camera.fill", @"camera" };
        for (int c = 0; c < 2; c++) {
            UIImage* cam = [UIImage systemImageNamed:aCamSym[c] withConfiguration:(c ? cfg : camCfg)];
            if (cam) {
                NSTextAttachment* att = [[NSTextAttachment alloc] init];
                att.image = [cam imageWithTintColor:[UIColor colorWithWhite:1.0 alpha:0.85] renderingMode:UIImageRenderingModeAlwaysOriginal];
                aButtonViews[aCamBtn[c]].attributedText = [NSAttributedString attributedStringWithAttachment:att];
            }
            else {
                aButtonViews[aCamBtn[c]].text = c ? @"CAM" : @"PHOTO";
            }
        }
        // (shown unless the player has switched it off)
        iosTouch_bShowCamera = [[NSUserDefaults standardUserDefaults] objectForKey:IOSTOUCH_CAMERA_DEFAULTS_KEY]
                               ? ([[NSUserDefaults standardUserDefaults] boolForKey:IOSTOUCH_CAMERA_DEFAULTS_KEY] ? 1 : 0) : 1;
        aButtonViews[BTN_CAMERA].hidden = !iosTouch_bShowCamera;
        // (shown from the start: Photos access asked now, as switching it on
        // asks, before any play; the game holds still while the question is up)
        if (iosTouch_bShowCamera && iosPhotos_AskAccess(iosTouch_PhotosAsked))
            iosGame_SetHoldReason(IOSGAME_HOLD_PHOTOS, 1);
        NSInteger kinds = [[NSUserDefaults standardUserDefaults] integerForKey:IOSTOUCH_CAMKINDS_DEFAULTS_KEY];
        iosTouch_cameraKinds = (kinds & IOSCAPTURE_ALL) ? (int)(kinds & IOSCAPTURE_ALL) : IOSCAPTURE_CLEAN;

        // The camera options: a row like the tray's, under it, hidden until a
        // hold on the tray's camera switch opens it
        camOptBack = [[UIView alloc] initWithFrame:CGRectZero];
        camOptBack.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
        camOptBack.layer.cornerRadius = 26.0;
        camOptBack.layer.borderWidth = 1.5;
        camOptBack.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        camOptBack.userInteractionEnabled = NO;
        camOptBack.hidden = YES;
        [self insertSubview:camOptBack belowSubview:aButtonViews[BTN_CAMOPT_FIRST]];
        for (int i = BTN_CAMOPT_FIRST; i <= BTN_CAMOPT_LAST; i++) aButtonViews[i].hidden = YES;
        camOptRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_TRAYCAM].radius);
        [aButtonViews[BTN_TRAYCAM].layer addSublayer:camOptRing];
        // ...and the camera options and the gyro options, rows like it
        UIView** aBack[2] = { &camRowBack, &gyroOptBack }; // (manual retain and release: the ivars keep the +1 from alloc, as the others)
        const int aBackFirst[2] = { BTN_CAMROW_FIRST, BTN_GYROOPT_FIRST };
        const int aBackLast[2] = { BTN_CAMROW_LAST, BTN_GYROOPT_LAST };
        for (int k = 0; k < 2; k++) {
            UIView* v = [[UIView alloc] initWithFrame:CGRectZero];
            v.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
            v.layer.cornerRadius = 26.0;
            v.layer.borderWidth = 1.5;
            v.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
            v.userInteractionEnabled = NO;
            v.hidden = YES;
            [self insertSubview:v belowSubview:aButtonViews[aBackFirst[k]]];
            *aBack[k] = v;
            for (int i = aBackFirst[k]; i <= aBackLast[k]; i++) aButtonViews[i].hidden = YES;
        }
        gyroOptRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_TRAYGYRO].radius);
        [aButtonViews[BTN_TRAYGYRO].layer addSublayer:gyroOptRing];
        if ([[NSUserDefaults standardUserDefaults] objectForKey:IOSTOUCH_SELFIES_DEFAULTS_KEY]) {
            NSInteger n = [[NSUserDefaults standardUserDefaults] integerForKey:IOSTOUCH_SELFIES_DEFAULTS_KEY];
            iosTouch_selfieShots = (n == 1 || n == 5) ? (int)n : 3;
        }
        if ([[NSUserDefaults standardUserDefaults] objectForKey:IOSTOUCH_FREEZE_DEFAULTS_KEY])
            iosTouch_bFreezeOn = [[NSUserDefaults standardUserDefaults] boolForKey:IOSTOUCH_FREEZE_DEFAULTS_KEY] ? 1 : 0;
        [self refreshGyroLabels]; // (SELFIES and FREEZE, as just read)

        freezeClose = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, IOSTOUCH_FREEZE_CLOSE_RADIUS * 2, IOSTOUCH_FREEZE_CLOSE_RADIUS * 2)];
        freezeClose.text = @"\u2715";
        freezeClose.textAlignment = NSTextAlignmentCenter;
        freezeClose.textColor = [UIColor whiteColor];
        freezeClose.font = [UIFont systemFontOfSize:20 weight:UIFontWeightBold];
        freezeClose.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
        freezeClose.layer.cornerRadius = IOSTOUCH_FREEZE_CLOSE_RADIUS;
        freezeClose.layer.borderWidth = 1.5;
        freezeClose.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.6].CGColor;
        freezeClose.clipsToBounds = YES;
        freezeClose.userInteractionEnabled = NO;
        freezeClose.hidden = YES;
        [self addSubview:freezeClose];

        // A burst's count, beside CAMERA while it goes on
        burstLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 2.0 * IOSTOUCH_BURST_LABEL_HALF, 18)];
        burstLabel.textAlignment = NSTextAlignmentCenter;
        burstLabel.textColor = [UIColor colorWithRed:1.0 green:0.85 blue:0.3 alpha:0.95];
        burstLabel.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightBold];
        burstLabel.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
        burstLabel.layer.cornerRadius = 9.0;
        burstLabel.clipsToBounds = YES;
        burstLabel.userInteractionEnabled = NO;
        burstLabel.hidden = YES;
        [self addSubview:burstLabel];
        cameraRing = IOSTouch_MakeHoldRing(iosTouch_aButtons[BTN_CAMERA].radius);
        [aButtonViews[BTN_CAMERA].layer addSublayer:cameraRing];
        cameraTap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        NSURL* tickUrl = [[NSBundle mainBundle] URLForResource:@"burst_tick" withExtension:@"wav"];
        if (tickUrl && AudioServicesCreateSystemSoundID((__bridge CFURLRef)tickUrl, &iosTouch_burstSound) != kAudioServicesNoError)
            iosTouch_burstSound = 0;

        // The FPS readout, shown if it was left on
        fpsLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 2.0 * IOSTOUCH_FPS_LABEL_HALF, 18)];
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

        // The flash after a photo: on top of everything, hidden between photos
        // (so it is never in one)
        cameraFlash = [[UIView alloc] initWithFrame:self.bounds];
        cameraFlash.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        cameraFlash.backgroundColor = [UIColor whiteColor];
        cameraFlash.userInteractionEnabled = NO;
        cameraFlash.hidden = YES;

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
            // a weapon's ammo count, under its name, up with the labels
            UILabel* r = [[UILabel alloc] initWithFrame:CGRectZero];
            r.textAlignment = NSTextAlignmentCenter;
            r.layer.zPosition = 2.0;
            r.hidden = YES;
            aSliceRows[i] = r;
            [wheelView addSubview:r];
        }
        aSliceRows[0].font = [UIFont monospacedDigitSystemFontOfSize:IOSTOUCH_WHEEL_AMMO_FONT weight:UIFontWeightSemibold];
        aSliceRows[0].text = @"000";
        ammoRowSize = [aSliceRows[0] sizeThatFits:CGSizeMake(300.0, 300.0)];
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
        wheelTitle.minimumScaleFactor = IOSTOUCH_WHEEL_TITLE_MIN; // (longer weapon names: two lines, setWheelTitle)
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
        const char* aChipName[IOSTOUCH_NUM_CHIPS] = { "SINGLE", "DUAL", "LEFT", "BOTH", "RIGHT", "EQUIP" };
        for (int i = 0; i < IOSTOUCH_NUM_CHIPS; i++) {
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, IOSTOUCH_SABER_CHIP_W, IOSTOUCH_SABER_CHIP_H)];
            l.text = [NSString stringWithUTF8String:aChipName[i]];
            l.textAlignment = NSTextAlignmentCenter;
            l.font = [UIFont boldSystemFontOfSize:11];
            l.textColor = [UIColor whiteColor];
            l.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
            l.layer.cornerRadius = IOSTOUCH_SABER_CHIP_H * 0.5;
            l.clipsToBounds = YES;
            l.layer.zPosition = 2.0;
            l.hidden = YES;
            aSaberChips[i] = l;
            [wheelView addSubview:l];
        }
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
    // CAMERA in the middle of the screen (well clear of BACTA and the FPS
    // readout even on the narrowest iPhone, 667 pt)
    iosTouch_aButtons[BTN_CAMERA].x = W * 0.5;
    // SABER and CHEAT (those switched on, in that order) between CAMERA and
    // QUICK SAVE, evenly: between the burst's count (right of CAMERA) and the
    // FPS readout (left of QUICK SAVE), where they leave room (one alone:
    // halfway, at least 4 pt each side; two: at least 8 pt apart). Where
    // they don't: with the burst's count under CAMERA; then with the FPS
    // readout at the bottom of the screen; then both. (Neither switched on:
    // where SABER alone would be.)
    iosTouch_bBurstUnder = 0;
    iosTouch_bFpsBottom = 0;
    {
        iosTouchButton* cam = &iosTouch_aButtons[BTN_CAMERA];
        iosTouchButton* aMid[2] = { &iosTouch_aButtons[BTN_SABER], &iosTouch_aButtons[BTN_CHEAT] };
        const int aShow[2] = { iosTouch_bShowSaber, iosTouch_bShowCheat };
        const int n = MAX(aShow[0] + aShow[1], 1);
        const CGFloat R = aMid[0]->radius, minGap = (n > 1) ? 8.0 : 4.0;
        const CGFloat camEdge = cam->x + cam->radius;
        const CGFloat burstEdge = camEdge + 20.0 + IOSTOUCH_BURST_LABEL_HALF;
        const CGFloat fpsEdge = right - IOSTOUCH_FPS_LABEL_X - IOSTOUCH_FPS_LABEL_HALF;
        const CGFloat qsEdge = iosTouch_aButtons[BTN_QUICKSAVE].x - iosTouch_aButtons[BTN_QUICKSAVE].radius;
        const CGFloat aLo[4] = { burstEdge, camEdge, burstEdge, camEdge };
        const CGFloat aHi[4] = { fpsEdge, fpsEdge, qsEdge, qsEdge };
        int k = 0;
        while (k < 3 && (aHi[k] - aLo[k] - n * 2.0 * R) / (n + 1) < minGap) k++;
        const int bAny = aShow[0] || aShow[1];
        iosTouch_bBurstUnder = bAny && (k & 1);
        iosTouch_bFpsBottom = bAny && k >= 2;
        const CGFloat G = (aHi[k] - aLo[k] - n * 2.0 * R) / (n + 1);
        int slot = 0;
        for (int b = 0; b < 2; b++) {
            const int i = aShow[b] ? slot++ : 0; // (one not shown: in the first place, out of the way)
            aMid[b]->x = aLo[k] + G * (i + 1) + R * (2 * i + 1);
        }
    }
    const int aTopRow[] = { BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
                            BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU, BTN_CAMERA, BTN_SABER, BTN_CHEAT };
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

    // MENU's tray: a row just under the corner -- GYRO, FPS, the camera
    // switch and the keyboard, right below MENU (a held thumb slides straight down onto it).
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
    // The rows under the tray, each centred under the button that opens it
    // (kept on screen): the gyro options under GYRO and the camera options
    // under the camera switch, a row down; the photo kinds under PHOTO, a
    // row further
    {
        const int aFirst[3] = { BTN_GYROOPT_FIRST, BTN_CAMROW_FIRST, BTN_CAMOPT_FIRST };
        const int aLast[3] = { BTN_GYROOPT_LAST, BTN_CAMROW_LAST, BTN_CAMOPT_LAST };
        const int aUnder[3] = { BTN_TRAYGYRO, BTN_TRAYCAM, BTN_CAMROW_PHOTO };
        UIView* aRowBack[3] = { gyroOptBack, camRowBack, camOptBack };
        for (int k = 0; k < 3; k++) {
            iosTouchButton* c = &iosTouch_aButtons[aUnder[k]]; // (PHOTO is laid out by the time its row is)
            const int num = aLast[k] - aFirst[k] + 1;
            const CGFloat len = IOSTOUCH_TRAY_STEP * (num - 1);
            CGFloat x0 = c->x - len * 0.5;
            x0 = MIN(x0, right - 26 - len);
            x0 = MAX(x0, left + 26);
            CGFloat y = c->y + 52;
            for (int i = aFirst[k]; i <= aLast[k]; i++) {
                iosTouch_aButtons[i].x = x0 + IOSTOUCH_TRAY_STEP * (i - aFirst[k]);
                iosTouch_aButtons[i].y = y;
            }
            aRowBack[k].frame = CGRectMake(x0 - 26, y - 26, len + 52, 52);
        }
    }
    burstLabel.center = iosTouch_bBurstUnder
        ? CGPointMake(iosTouch_aButtons[BTN_CAMERA].x, iosTouch_aButtons[BTN_CAMERA].y + iosTouch_aButtons[BTN_CAMERA].radius + 12)
        : CGPointMake(iosTouch_aButtons[BTN_CAMERA].x + iosTouch_aButtons[BTN_CAMERA].radius + 20, iosTouch_aButtons[BTN_CAMERA].y);

    // Action Freeze's X: top right, inside the safe area (clear of the
    // corner and any cutout)
    freezeClose.center = CGPointMake(right - IOSTOUCH_FREEZE_CLOSE_RADIUS - 4, top + IOSTOUCH_FREEZE_CLOSE_RADIUS + 4);

    // The FPS readout where the keyboard button used to be, left of QUICK
    // SAVE: the game draws its messages and typing line centred at the top
    fpsLabel.center = iosTouch_bFpsBottom ? CGPointMake(W * 0.5, bottom - 9.0)
                                          : CGPointMake(right - IOSTOUCH_FPS_LABEL_X, top + 26);

    // The game's top message text and typing line (iosTouch_GetTopTextRectPt):
    // just under the top row, centred on the screen, and only as wide as keeps
    // it clear of any other button up near it (FORCE or ALT, on smaller screens)
    {
        const CGFloat textTop = top + 26 + iosTouch_aButtons[BTN_MENU].radius + IOSTOUCH_TOPTEXT_GAP;
        CGFloat half = MIN(W * 0.5 - (left + IOSTOUCH_TOPTEXT_SIDE), (right - IOSTOUCH_TOPTEXT_SIDE) - W * 0.5);
        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
            if (iosTouch_IsTrayButton(i) || iosTouch_IsOptButton(i)) continue; // only over the text while open
            int bTopRow = 0;
            for (int t = 0; t < IOSTOUCH_COUNT(aTopRow); t++) {
                if (aTopRow[t] == i) bTopRow = 1;
            }
            if (bTopRow) continue;
            iosTouchButton* q = &iosTouch_aButtons[i];
            if (q->y - q->radius - IOSTOUCH_TOUCH_SLOP > textTop + IOSTOUCH_TOPTEXT_DEPTH) continue;
            half = MIN(half, fabs(q->x - W * 0.5) - q->radius - IOSTOUCH_TOUCH_SLOP);
        }
        half = MAX(half, 60.0);
        iosTouch_aTopText[0] = textTop;
        iosTouch_aTopText[1] = W * 0.5 - half;
        iosTouch_aTopText[2] = W * 0.5 + half;
        iosTouch_bTopText = 1;
    }

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
    // ...and the rows open under it, likewise
    const int aFirst[3] = { BTN_GYROOPT_FIRST, BTN_CAMROW_FIRST, BTN_CAMOPT_FIRST };
    const int aLast[3] = { BTN_GYROOPT_LAST, BTN_CAMROW_LAST, BTN_CAMOPT_LAST };
    const int aOpen[3] = { iosTouch_optRow == IOSTOUCH_ROW_GYRO, iosTouch_optRow == IOSTOUCH_ROW_CAMERA,
                           iosTouch_optRow == IOSTOUCH_ROW_CAMERA && iosTouch_bKindsOpen };
    for (int k = 0; k < 3 && best < 0; k++) {
        l = &iosTouch_aButtons[aFirst[k]];
        r = &iosTouch_aButtons[aLast[k]];
        if (!aOpen[k] || iosTouch_DistToSegment(p, l->x, l->y, r->x, r->y) > 26) continue;
        for (int i = aFirst[k]; i <= aLast[k]; i++) {
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
        // FORCE WHEEL / NEXT WPN stays lit above the dimmed screen while its wheel is open
        int bWheel = i == iosTouch_wheelButton && iosTouch_bWheelOpen;
        aButtonViews[i].backgroundColor = bWheel ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.55]
                                                 : [UIColor colorWithWhite:(bHeld ? 1.0 : 0.0) alpha:(bHeld ? 0.30 : 0.22)];
        // A switched-on item (field light, IR goggles), the open typing line
        // (on MENU and the tray's keyboard), the FPS readout being on (on
        // the tray's FPS) and gyro aiming being on (on the tray's GYRO)
        // stand out in yellow, at full strength
        int bOn = (iosTouch_aButtons[i].kind == KIND_ITEM && aItemActive[i])
                  || ((i == BTN_TRAYKEYS || i == BTN_MENU) && jkHud_bChatOpen)
                  || (i == BTN_TRAYFPS && iosTouch_bShowFps)
                  || (i == BTN_TRAYCAM && iosTouch_bShowCamera)
                  || (i == BTN_TRAYCHEAT && iosTouch_bShowCheat)
                  || (iosTouch_IsCamOptButton(i) && (iosTouch_cameraKinds & iosTouch_aButtons[i].bin))
                  || (i == BTN_CAMROW_PHOTO && iosTouch_bKindsOpen)
                  || (i == BTN_CAMROW_FREEZE && iosTouch_bFreezeOn)
                  || (i == BTN_CAMERA && iosTouch_pBurstTouch)
                  || (i == BTN_TRAYGYRO && bGyro && iosTouch_gyroMode != IOSTOUCH_GYRO_OFF);
        int bTray = iosTouch_IsTrayButton(i) || iosTouch_IsOptButton(i); // only there while open
        CGFloat alpha = (bHeld || bOn || bWheel || bTray) ? 1.0 : IOSTOUCH_IDLE_ALPHA;
        if (i == BTN_FORCE && bForceLabelSet && !forceLabelName) alpha *= 0.45; // no power to use yet
        if (i == BTN_TRAYGYRO && !bGyro) alpha *= 0.45;                           // no gyro here
        if ((i == BTN_GYROOPT_MODE || i == BTN_GYROOPT_SENS) && (!bGyro || iosTouch_gyroMode == IOSTOUCH_GYRO_OFF))
            alpha *= 0.45; // ...or it's off (still set: they apply once it's on)
        // (a selfie shows no buttons; Action Freeze only CAMERA)
        if (iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE || iosTouch_bCinema || (iosTouch_bFreezing && i != BTN_CAMERA)) alpha = 0.0;
        aButtonViews[i].alpha = alpha;
        if (i != BTN_FIRE) {
            aButtonViews[i].layer.borderColor = bOn ? [UIColor colorWithRed:1.0 green:0.85 blue:0.3 alpha:0.95].CGColor
                                                    : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        }
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    forceRing.opacity = bForceRingFull ? 1.0 : (iosTouch_aButtonHeld[BTN_FORCE] ? 1.0 : IOSTOUCH_IDLE_ALPHA);
    for (int k = 0; k < 3; k++) aCheatSub[k].hidden = !iosTouch_bCheatOn;
    // CHEAT's emblem and edge in a quiet red while the cheat is on: only
    // their colour changes (the edge as wide as every other button's)
    cheatEmblem.fillColor = iosTouch_bCheatOn ? [UIColor colorWithRed:0.9 green:0.32 blue:0.28 alpha:0.95].CGColor
                                              : [UIColor colorWithWhite:1.0 alpha:0.85].CGColor;
    if (iosTouch_bCheatOn) aButtonViews[BTN_CHEAT].layer.borderColor = [UIColor colorWithRed:0.9 green:0.32 blue:0.28 alpha:0.8].CGColor;
    [CATransaction commit];
}

// DUCK's level pick over for this finger: DUCK's own label back (unless
// another finger on it is still counting), and the message line saying so
- (void)endLevelPick:(iosTouchSlot*)s
{
    // (the count on the message line: the level going, or none)
    if (s->levelPick > 0) {
        char msg[48];
        if (iosTouch_levelToLoad > 0) snprintf(msg, sizeof(msg), "Loading level %d", iosTouch_levelToLoad);
        else snprintf(msg, sizeof(msg), "No level picked");
        iosGame_ShowMessageInPlace(msg);
    }
    s->levelPick = 0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        const iosTouchSlot* o = &iosTouch_aSlots[i];
        if (o != s && o->touch && o->role == ROLE_BUTTON && o->levelPick > 0) return;
    }
    aButtonViews[BTN_DUCK].text = @"DUCK";
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
    if (!bOpen) [self setOptRow:IOSTOUCH_ROW_NONE];
    trayBack.hidden = !bOpen;
    for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) aButtonViews[i].hidden = !bOpen;
    if (!bOpen) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (!s->touch || s->role != ROLE_BUTTON) continue;
            if (s->button == BTN_TRAYCAM) [self setRing:camOptRing progress:0.0];
            if (s->button == BTN_TRAYGYRO) [self setRing:gyroOptRing progress:0.0];
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

static void iosTouch_PhotosAsked(void)
{
    iosGame_SetHoldReason(IOSGAME_HOLD_PHOTOS, 0);
}

// CHEAT shown or hidden (the tray's cheat switch), and remembered; the
// cheat itself as it was
- (void)setShowCheat:(int)bShow
{
    iosTouch_bShowCheat = bShow != 0;
    aButtonViews[BTN_CHEAT].hidden = !iosTouch_bShowCheat;
    iosGame_SetCheatShown(iosTouch_bShowCheat);
    [[NSUserDefaults standardUserDefaults] setBool:(iosTouch_bShowCheat ? YES : NO) forKey:IOSTOUCH_CHEAT_DEFAULTS_KEY];
    [self setNeedsLayout];
    [self refreshButtonLooks];
}

// Cheat mode left (the tray's switch): the cheat off, CHEAT hidden, the
// switch off (remembered). (Where the cheat can't be switched
// off now -- no level -- CHEAT stays, showing it on.)
- (void)leaveCheatMode
{
    if (iosGame_IsSithEwokOn() && iosGame_ToggleSithEwok() < 0) return;
    [self setShowCheat:0];
}

// The style and each blade's colour, as last picked (by name: a colour that
// is no more is the game's own)
- (void)loadSaberChoices
{
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    for (int st = 0; st < IOSSABER_NUM_STYLES; st++) {
        for (int b = 0; b < iosSaber_StyleNumBlades(st); b++) {
            NSString* key = [NSString stringWithFormat:@"%@.%s.%d", IOSTOUCH_SABERCOLOUR_DEFAULTS_KEY, iosSaber_StyleName(st), b];
            NSString* v = [d stringForKey:key];
            int id = v ? iosSaber_ColourByKey([v UTF8String]) : -1;
            iosSaber_SetColour(st, b, id >= 0 ? id : IOSSABER_COLOUR_GAME);
        }
    }
    NSString* style = [d stringForKey:IOSTOUCH_SABERSTYLE_DEFAULTS_KEY];
    for (int st = 0; style && st < IOSSABER_NUM_STYLES; st++) {
        if (!strcmp([style UTF8String], iosSaber_StyleName(st))) iosSaber_SetStyle(st);
    }
    if ([d objectForKey:IOSTOUCH_SABERTARGET_DEFAULTS_KEY]) {
        const NSInteger t = [d integerForKey:IOSTOUCH_SABERTARGET_DEFAULTS_KEY];
        if (t >= IOSTOUCH_SABER_RIGHT && t <= IOSTOUCH_SABER_BOTH) iosTouch_saberTarget = (int)t;
    }
}

// The blade of the style a colour on the saber wheel goes to (with BOTH,
// the right one: the one shown as picked)
static int iosTouch_SaberBlade(void)
{
    if (iosSaber_StyleNumBlades(iosSaber_GetStyle()) < 2) return 0;
    return iosTouch_saberTarget == IOSTOUCH_SABER_LEFT ? 1 : 0;
}

// The colour picked on the saber wheel: for the style's blade (with two:
// the left, the right or both), remembered, and shown on SABER
- (void)setSaberColour:(int)id
{
    const int st = iosSaber_GetStyle();
    const int bTwo = iosSaber_StyleNumBlades(st) >= 2;
    for (int b = 0; b < (bTwo ? 2 : 1); b++) {
        if (bTwo && iosTouch_saberTarget != IOSTOUCH_SABER_BOTH && b != iosTouch_SaberBlade()) continue;
        iosSaber_SetColour(st, b, id);
        NSString* key = [NSString stringWithFormat:@"%@.%s.%d", IOSTOUCH_SABERCOLOUR_DEFAULTS_KEY, iosSaber_StyleName(st), b];
        [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:iosSaber_ColourKey(id)] forKey:key];
    }
    [self refreshSaberIcons];
}

// Whether EQUIP can take the saber now: the player has it, and can switch to it
- (int)canEquipSaber
{
    int ammo = 0, bSelectable = 0;
    return iosGame_GetWeapon(iosGame_IsMots() ? 131 : 10, &ammo, &bSelectable) && bSelectable;
}

// One of the saber wheel's choices tapped: one saber or two (each keeps its
// own colours), or which of the two a colour goes to -- the wheel stays open,
// to tap a colour; or EQUIP, the saber in hand, the wheel closed.

- (void)useSaberChip:(int)chip
{
    if (chip == IOSTOUCH_CHIP_EQUIP) {
        // the saber in hand, as set (colours, one or two): the wheel closes
        // with nothing else changed, and the game takes it once it runs again
        // (as NEXT WPN's wheel does); not had, nothing
        if (![self canEquipSaber]) return;
        const int bin = iosGame_IsMots() ? 131 : 10;
        [self closeWheelSelecting:-1];
        if (bin != iosGame_GetCurWeapon()) iosGame_SelectWeapon(bin);
        return;
    }
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    if (chip == IOSTOUCH_CHIP_SINGLE || chip == IOSTOUCH_CHIP_DUAL) {
        iosSaber_SetStyle(chip == IOSTOUCH_CHIP_DUAL ? IOSSABER_STYLE_DUAL : IOSSABER_STYLE_SINGLE);
        [d setObject:[NSString stringWithUTF8String:iosSaber_StyleName(iosSaber_GetStyle())] forKey:IOSTOUCH_SABERSTYLE_DEFAULTS_KEY];
        [self refreshSaberIcons];
    }
    else {
        iosTouch_saberTarget = chip == IOSTOUCH_CHIP_LEFT ? IOSTOUCH_SABER_LEFT : chip == IOSTOUCH_CHIP_RIGHT ? IOSTOUCH_SABER_RIGHT : IOSTOUCH_SABER_BOTH;
        [d setInteger:iosTouch_saberTarget forKey:IOSTOUCH_SABERTARGET_DEFAULTS_KEY];
    }
    iosTouch_bWheelTapMode = 1;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        // (a thumb that opened it, still down: lifted, it leaves it open to tap)
        iosTouchSlot* o = &iosTouch_aSlots[i];
        if (o->touch && o->role == ROLE_WHEEL && o->bWheelOpener) o->bWheelHeld = 1;
    }
    [wheelTick selectionChanged];
    [self layoutSaberChips];
    [self refreshWheelLooks];
}

// The saber wheel's choice under p (a little round each), or -1
- (int)saberChipAt:(CGPoint)p
{
    if (!iosTouch_bWheelOpen || iosTouch_wheelKind != IOSTOUCH_WHEEL_SABER) return -1;
    for (int i = 0; i < IOSTOUCH_NUM_CHIPS; i++) {
        if (!aSaberChips[i].hidden && CGRectContainsPoint(CGRectInset(aSaberChips[i].frame, -6.0, -6.0), p)) return i;
    }
    return -1;
}

// The saber wheel's choices beside the ring (only on that wheel; the blades'
// only with two), each lit if it is the one set: SINGLE and DUAL stacked on
// the left, LEFT, BOTH and RIGHT on the right, each blade's in its colour
- (void)layoutSaberChips
{
    const int bSaber = iosTouch_bWheelOpen && iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER;
    const int st = iosSaber_GetStyle();
    const int bTwo = iosSaber_StyleNumBlades(st) >= 2;
    const CGPoint c = iosTouch_wheelCentre;
    const CGFloat R = iosTouch_wheelR1 + IOSTOUCH_WHEEL_POP + IOSTOUCH_SABER_CHIP_OUT + IOSTOUCH_SABER_CHIP_W * 0.5;
    const CGFloat step = IOSTOUCH_SABER_CHIP_H + IOSTOUCH_SABER_CHIP_GAP;
    // Each column moved up or down, as little as it can be, until clear of
    // every button showing (dimmed under the wheel, but a choice over one is
    // hard to read) and on screen. Where the right has no such place (the
    // smallest screens: JUMP and ACT), the blades' choices go on the left
    // too, under SINGLE and DUAL.
    CGFloat aShift[2] = { 0.0, 0.0 };
    int bOneColumn = 0;
    if (bSaber) {
        const UIEdgeInsets in = self.safeAreaInsets;
        const CGFloat top = MAX(in.top, 8.0), bottom = self.bounds.size.height - MAX(in.bottom, 8.0);
        for (int col = 0; col < 2; col++) {
            const int n = col ? 3 : 2;
            const CGFloat x = c.x + (col ? R : -R);
            int bFound = 0;
            for (int k = 0; k <= 24 && !bFound; k++) {
                const CGFloat dy = ((k & 1) ? 6.0 : -6.0) * ((k + 1) / 2);
                const CGFloat y0 = c.y + dy - (n - 1) * 0.5 * step - IOSTOUCH_SABER_CHIP_H * 0.5;
                const CGFloat y1 = y0 + (n - 1) * step + IOSTOUCH_SABER_CHIP_H;
                if (y0 < top + 52.0 || y1 > bottom) continue; // (under the top row)
                int bClear = 1;
                for (int j = 0; j < IOSTOUCH_NUM_BUTTONS && bClear; j++) {
                    iosTouchButton* bt = &iosTouch_aButtons[j];
                    if (aButtonViews[j].hidden || j == iosTouch_wheelButton) continue;
                    const CGFloat ex = MAX(fabs(bt->x - x) - IOSTOUCH_SABER_CHIP_W * 0.5, 0.0);
                    const CGFloat ey = MAX(MAX(y0 - bt->y, bt->y - y1), 0.0);
                    bClear = sqrt(ex * ex + ey * ey) > bt->radius + 4.0;
                }
                if (bClear) {
                    aShift[col] = dy;
                    bFound = 1;
                }
            }
            if (col == 1 && !bFound) bOneColumn = 1;
        }
        if (bOneColumn) {
            // the one column: as near the middle as it fits between the top
            // row and the bottom
            const CGFloat half = 2.5 * step + IOSTOUCH_SABER_CHIP_H * 0.5;
            aShift[0] = MIN(MAX(0.0, top + 52.0 + half - c.y), bottom - half - c.y);
        }
    }
    for (int i = 0; i < IOSTOUCH_NUM_CHIPS; i++) {
        UILabel* l = aSaberChips[i];
        if (i == IOSTOUCH_CHIP_EQUIP) {
            // in the middle, under the colour's name and the hint; dim while
            // the player has no saber to take
            l.hidden = !bSaber;
            l.center = CGPointMake(c.x, c.y + 34.0);
            const int bCan = [self canEquipSaber];
            l.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
            l.layer.borderWidth = 1.5;
            l.alpha = bCan ? 1.0 : 0.35;
            continue;
        }
        const int bStyle = i <= IOSTOUCH_CHIP_DUAL;
        l.hidden = !bSaber || (!bStyle && !bTwo) || (i == IOSTOUCH_CHIP_DUAL && !iosSaber_StyleAvailable(IOSSABER_STYLE_DUAL));
        if (l.hidden) continue;
        const int k = bStyle ? i - IOSTOUCH_CHIP_SINGLE : i - IOSTOUCH_CHIP_LEFT;
        const int n = bStyle ? 2 : 3;
        if (bOneColumn) // (all five down the left, a choice's gap between the two kinds, kept on screen)
            l.center = CGPointMake(c.x - R, c.y + aShift[0] + (i + (bStyle ? 0 : 1) - 2.5) * step);
        else
            l.center = CGPointMake(c.x + (bStyle ? -R : R), c.y + aShift[bStyle ? 0 : 1] + (k - (n - 1) * 0.5) * step);
        int bOn;
        if (i == IOSTOUCH_CHIP_SINGLE) bOn = !bTwo;
        else if (i == IOSTOUCH_CHIP_DUAL) bOn = bTwo;
        else bOn = (i == IOSTOUCH_CHIP_LEFT && iosTouch_saberTarget == IOSTOUCH_SABER_LEFT)
                   || (i == IOSTOUCH_CHIP_BOTH && iosTouch_saberTarget == IOSTOUCH_SABER_BOTH)
                   || (i == IOSTOUCH_CHIP_RIGHT && iosTouch_saberTarget == IOSTOUCH_SABER_RIGHT);
        l.layer.borderColor = bOn ? [UIColor colorWithRed:1.0 green:0.85 blue:0.3 alpha:0.95].CGColor
                                  : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        l.layer.borderWidth = bOn ? 2.0 : 1.5;
        l.alpha = bOn ? 1.0 : 0.8;
        if (i == IOSTOUCH_CHIP_LEFT || i == IOSTOUCH_CHIP_RIGHT) {
            unsigned char r, g, b;
            iosSaber_ColourRGB(iosSaber_GetColour(st, i == IOSTOUCH_CHIP_LEFT ? 1 : 0), &r, &g, &b);
            l.textColor = [UIColor colorWithRed:(r * 0.7 + 255 * 0.3) / 255.0 green:(g * 0.7 + 255 * 0.3) / 255.0 blue:(b * 0.7 + 255 * 0.3) / 255.0 alpha:1.0];
        }
    }
}

// The saber's picture in the colour picked (the game's own: as near as can
// be told, iosSaber_ColourRGB)
- (void)refreshSaberIcons
{
    const int st = iosSaber_GetStyle();
    const int bTwo = iosSaber_StyleNumBlades(st) >= 2;
    unsigned char r, g, b, r2, g2, b2;
    iosSaber_ColourRGB(iosSaber_GetColour(st, 0), &r, &g, &b);
    iosSaber_ColourRGB(iosSaber_GetColour(st, 1), &r2, &g2, &b2);
    const int rgb = (r << 16) | (g << 8) | b, rgb2 = bTwo ? ((r2 << 16) | (g2 << 8) | b2) : -1;
    if (rgb == saberIconRGB && rgb2 == saberIconRGB2) return;
    saberIconRGB = rgb;
    saberIconRGB2 = rgb2;
    UIColor* c = [UIColor colorWithRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0];
    UIColor* c2 = [UIColor colorWithRed:r2 / 255.0 green:g2 / 255.0 blue:b2 / 255.0 alpha:1.0];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int k = 0; k < 1; k++) {
        aSaberBlade[k].strokeColor = c.CGColor;
        aSaberBlade[k].shadowColor = c.CGColor;
        aSaberBlade2[k].strokeColor = c2.CGColor;
        aSaberBlade2[k].shadowColor = c2.CGColor;
        aSaberBlade2[k].hidden = aSaberCore2[k].hidden = aSaberHilt2[k].hidden = !bTwo;
    }
    [CATransaction commit];
}

// The ring a wheel button fills while held still
- (CAShapeLayer*)wheelRingOf:(int)button
{
    if (button == BTN_NEXTWPN) return weaponRing;
    if (button == BTN_SABER) return saberRing;
    return wheelRing;
}

- (void)setShowCamera:(int)bShow
{
    iosTouch_bShowCamera = bShow != 0;
    aButtonViews[BTN_CAMERA].hidden = !iosTouch_bShowCamera;
    [[NSUserDefaults standardUserDefaults] setBool:(iosTouch_bShowCamera ? YES : NO) forKey:IOSTOUCH_CAMERA_DEFAULTS_KEY];
    [self refreshButtonLooks];
    // Photos access is asked for now, the first time, never in the middle of
    // play; the game holds still while the question is up
    if (iosTouch_bShowCamera && iosPhotos_AskAccess(iosTouch_PhotosAsked))
        iosGame_SetHoldReason(IOSGAME_HOLD_PHOTOS, 1);
}

// Lets go of any touch on a row's buttons that is going away
- (void)releaseOptTouches
{
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (s->touch && s->role == ROLE_BUTTON && iosTouch_IsOptButton(s->button) && aButtonViews[s->button].hidden) {
            if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
            s->role = ROLE_IGNORED;
        }
    }
}

// Opens a row under the tray (IOSTOUCH_ROW_*), closing any other: only one
// is open at a time (the photo kinds go with the camera options)
- (void)setOptRow:(int)row
{
    if (iosTouch_optRow == row) return;
    iosTouch_optRow = row;
    if (row != IOSTOUCH_ROW_CAMERA) iosTouch_bKindsOpen = 0;
    gyroOptBack.hidden = row != IOSTOUCH_ROW_GYRO;
    for (int i = BTN_GYROOPT_FIRST; i <= BTN_GYROOPT_LAST; i++) aButtonViews[i].hidden = row != IOSTOUCH_ROW_GYRO;
    camRowBack.hidden = row != IOSTOUCH_ROW_CAMERA;
    for (int i = BTN_CAMROW_FIRST; i <= BTN_CAMROW_LAST; i++) aButtonViews[i].hidden = row != IOSTOUCH_ROW_CAMERA;
    camOptBack.hidden = !iosTouch_bKindsOpen;
    for (int i = BTN_CAMOPT_FIRST; i <= BTN_CAMOPT_LAST; i++) aButtonViews[i].hidden = !iosTouch_bKindsOpen;
    [self releaseOptTouches];
    [self refreshGyroLabels];
    [self refreshButtonLooks];
}

// Opens or closes the photo kinds' row, under the camera options
- (void)setKindsOpen:(int)bOpen
{
    bOpen = bOpen != 0 && iosTouch_optRow == IOSTOUCH_ROW_CAMERA;
    if (iosTouch_bKindsOpen == bOpen) return;
    iosTouch_bKindsOpen = bOpen;
    camOptBack.hidden = !bOpen;
    for (int i = BTN_CAMOPT_FIRST; i <= BTN_CAMOPT_LAST; i++) aButtonViews[i].hidden = !bOpen;
    [self releaseOptTouches];
    [self refreshButtonLooks];
}

// A row's button tapped. The row stays open for another tap (a touch
// anywhere else closes it).
- (void)useOptButton:(int)button
{
    switch (iosTouch_aButtons[button].kind) {
        case KIND_GYROMODE:
            if (iosTouch_GyroAvailable()) iosTouch_GyroNextMode();
            break;
        case KIND_GYROSENS:
            if (iosTouch_GyroAvailable()) iosTouch_GyroNextSens();
            break;
        case KIND_CAMPHOTO:
            [self setKindsOpen:!iosTouch_bKindsOpen];
            break;
        case KIND_SELFIES:
            iosTouch_selfieShots = iosTouch_selfieShots == 1 ? 3 : (iosTouch_selfieShots == 3 ? 5 : 1);
            [[NSUserDefaults standardUserDefaults] setInteger:iosTouch_selfieShots forKey:IOSTOUCH_SELFIES_DEFAULTS_KEY];
            break;
        case KIND_CAMKIND:
            [self toggleCameraKind:button];
            break;
        case KIND_FREEZEOPT:
            iosTouch_bFreezeOn = !iosTouch_bFreezeOn;
            [[NSUserDefaults standardUserDefaults] setBool:(iosTouch_bFreezeOn ? YES : NO) forKey:IOSTOUCH_FREEZE_DEFAULTS_KEY];
            break;
        default:
            break;
    }
    [self refreshGyroLabels];
    [self refreshButtonLooks];
}

// A camera option tapped: its kind of photo ticked or unticked (the last one
// ticked stays: a tap always takes something)
- (void)toggleCameraKind:(int)button
{
    int bit = iosTouch_aButtons[button].bin;
    if ((iosTouch_cameraKinds & bit) && !(iosTouch_cameraKinds & ~bit)) return;
    iosTouch_cameraKinds ^= bit;
    [[NSUserDefaults standardUserDefaults] setInteger:iosTouch_cameraKinds forKey:IOSTOUCH_CAMKINDS_DEFAULTS_KEY];
    [self refreshButtonLooks];
}

// The CAMERA button tapped: the ticked photos at the next frame drawn
- (void)takePhotos
{
    if (!iosCapture_Request(iosTouch_cameraKinds, IOSCAPTURE_MODE_PHOTO)) return; // the last ones are still being saved
    iosTouch_tCameraAsked = CACurrentMediaTime();
    [cameraTap prepare];
}

// A burst: starts on a finger slid off CAMERA, then a clean shot every
// IOSTOUCH_BURST_GAP (or as soon after as the saving keeps up) until it
// lifts, up to IOSTOUCH_BURST_MAX
- (void)startBurst:(UITouch*)t
{
    iosTouch_pBurstTouch = t;
    iosTouch_burstShots = 0;
    iosTouch_tBurstNext = 0;
    iosPhotos_SetBatch(1);
    burstLabel.text = @"0";
    burstLabel.hidden = NO;
    [self refreshButtonLooks];
}

- (void)tickBurst:(CFTimeInterval)now
{
    if (!iosTouch_pBurstTouch || iosTouch_burstShots >= IOSTOUCH_BURST_MAX || now < iosTouch_tBurstNext) return;
    if (!iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_BURST)) return; // try again next frame
    iosTouch_tCameraAsked = now;
    iosTouch_burstShots++;
    iosTouch_tBurstNext = now + IOSTOUCH_BURST_GAP;
    burstLabel.text = [NSString stringWithFormat:@"%d", iosTouch_burstShots];
}

- (void)endBurst
{
    if (!iosTouch_pBurstTouch) return;
    iosTouch_pBurstTouch = nil;
    burstLabel.hidden = YES;
    iosPhotos_SetBatch(0);
    [self refreshButtonLooks];
}

// The selfie: the CAMERA button held still IOSTOUCH_SELFIE_HOLD. The game
// holds still and everyone near poses for a camera held out at arm's length,
// for one photo or several (the camera options' SELFIES), each posed afresh
// (iosSelfieShots runs it, with iosSelfie and iosCapture); the touch buttons
// go while it is up, and touches are ignored, so nothing can let the game go
// on in the middle of it. Then all as it was.
- (void)startSelfie
{
    if (iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE || iosTouch_bFreezing || iosTouch_bCinema) return;
    // (a burst on another finger ends first: its end would take the selfie's
    // photos out of their batch, and its waiting capture would hold the
    // selfie up)
    [self endBurst];
    int numPeople = 0;
    const int r = iosSelfieShots_Start(iosTouch_selfieShots, CACurrentMediaTime(), &numPeople);
    if (r != IOSSELFIESHOTS_STARTED) {
        iosGame_ShowMessage(r == IOSSELFIESHOTS_BUSY ? "Still saving the last photo" : "Can't take a selfie here");
        return;
    }
    iosTouch_selfieFlashes = 0;
    iosTouch_selfieStage = IOSTOUCH_SELFIE_POSING;
    [self hideForPhotosKeeping:nil];
    [cameraTap prepare];
}

// A selfie or Action Freeze starting: every touch now down is let go (the
// game sees no buttons held, no stick), the tray closes and everything is
// hidden but `keep` (put back by -showAfterPhotos)
- (void)hideForPhotosKeeping:(UIView*)keep
{
    [self setTrayOpen:0];
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (s->touch) s->role = ROLE_IGNORED;
    }
    memset(iosTouch_aButtonHeld, 0, sizeof(iosTouch_aButtonHeld));
    iosTouch_bStickActive = 0;
    iosTouch_stickX = iosTouch_stickY = 0.0f;
    [self hideStick];
    iosTouch_RecomputeKeys();
    // (counted without the flash, which moves to the top when it goes off)
    int k = 0;
    for (UIView* v in self.subviews) {
        if (v == cameraFlash || k >= IOSTOUCH_MAX_SUBVIEWS) continue;
        aSelfieAlpha[k++] = v.alpha;
        if (v != keep) v.alpha = 0.0;
    }
    [self refreshButtonLooks];
}

- (void)showAfterPhotos
{
    // (a mouse or trackpad moved meanwhile doesn't turn the view afterwards)
    Window_lastXRel = 0;
    Window_lastYRel = 0;
    int k = 0;
    for (UIView* v in self.subviews) {
        if (v == cameraFlash || k >= IOSTOUCH_MAX_SUBVIEWS) continue;
        v.alpha = aSelfieAlpha[k++];
    }
    [self refreshButtonLooks];
}

// Action Freeze (CAMERA slid down, with FREEZE on): the game stops just as
// it is, and the camera can move round it to take photos; the X goes back
- (void)startFreeze
{
    if (iosTouch_bFreezing || iosTouch_bCinema || iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE) return;
    [self endBurst];
    if (!iosFreeze_Begin()) {
        iosGame_ShowMessage("Can't freeze here");
        return;
    }
    iosTouch_bFreezing = 1;
    memset(iosTouch_aFreezeMove, 0, sizeof(iosTouch_aFreezeMove));
    memset(iosTouch_aFreezeMid, 0, sizeof(iosTouch_aFreezeMid));
    iosFreeze_SetSmooth(1);
    [self hideForPhotosKeeping:aButtonViews[BTN_CAMERA]];
    freezeClose.hidden = NO;
    freezeClose.alpha = 1.0; // (not brought to the front: -showAfterPhotos puts each view back by its place in the order)
    [cameraTap prepare];
}

- (void)endFreeze
{
    if (!iosTouch_bFreezing) return;
    iosTouch_bFreezing = 0;
    // (a photo asked for and not taken yet would be of the game's own view)
    if (iosTouch_tCameraAsked) {
        iosCapture_Cancel();
        iosTouch_tCameraAsked = 0;
    }
    iosFreeze_End(); // (all as it was: the game goes on from the same moment)
    // Its touches are let go (still down, they only lift)
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (s->touch) s->role = ROLE_IGNORED;
    }
    freezeClose.hidden = YES;
    [self showAfterPhotos];
}

// A finger moving the Action Freeze camera: alone, it turns it round what
// it looks at (what is on screen follows the finger); with a second one, the
// two move it across (by their middle) and zoom (by their spread)
- (void)moveFreeze:(iosTouchSlot*)s to:(CGPoint)p
{
    iosTouchSlot* other = NULL;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES && !other; i++) {
        iosTouchSlot* o = &iosTouch_aSlots[i];
        if (o != s && o->touch && o->role == ROLE_FREEZE) other = o;
    }
    if (!other) {
        iosTouch_aFreezeMove[IOSTOUCH_FZ_YAW] -= (float)(p.x - s->last.x) * IOSTOUCH_FREEZE_ORBIT;
        iosTouch_aFreezeMove[IOSTOUCH_FZ_PITCH] -= (float)(p.y - s->last.y) * IOSTOUCH_FREEZE_ORBIT;
        return;
    }
    const CGFloat h = self.bounds.size.height > 1.0 ? self.bounds.size.height : 1.0;
    const CGPoint o = other->last;
    const CGFloat oldDist = hypot(s->last.x - o.x, s->last.y - o.y), newDist = hypot(p.x - o.x, p.y - o.y);
    // (the middle moves by half of this finger's move)
    iosTouch_aFreezeMove[IOSTOUCH_FZ_PANX] += (float)((p.x - s->last.x) * 0.5 / h);
    iosTouch_aFreezeMove[IOSTOUCH_FZ_PANY] += (float)((p.y - s->last.y) * 0.5 / h);
    if (oldDist > 8.0 && newDist > 8.0) iosTouch_aFreezeMove[IOSTOUCH_FZ_ZOOM] += (float)log(newDist / oldDist);
}

// Once per frame in Action Freeze: the camera follows the fingers' moves so
// far, part of the way (the rest next frame), so each frame moves it about
// as much as the last; it glides to a stop after they lift
- (void)tickFreeze:(CFTimeInterval)dt
{
    if (dt > 0.1) dt = 0.1;
    const float k = (float)(1.0 - exp(-(dt > 0 ? dt : 0.0) / IOSTOUCH_FREEZE_SMOOTH));
    float a[IOSTOUCH_FZ_NUM];
    for (int i = 0; i < IOSTOUCH_FZ_NUM; i++) {
        // (two smoothing steps: what an uneven run of touches leaves
        // uneven after the first, the second evens out)
        const float m = iosTouch_aFreezeMove[i] * k;
        iosTouch_aFreezeMove[i] -= m;
        iosTouch_aFreezeMid[i] += m;
        a[i] = iosTouch_aFreezeMid[i] * k;
        iosTouch_aFreezeMid[i] -= a[i];
        if (fabsf(iosTouch_aFreezeMove[i]) + fabsf(iosTouch_aFreezeMid[i]) < 1e-5f) iosTouch_aFreezeMove[i] = iosTouch_aFreezeMid[i] = 0;
    }
    if ((a[IOSTOUCH_FZ_YAW] != 0 || a[IOSTOUCH_FZ_PITCH] != 0) && !iosFreeze_Orbit(a[IOSTOUCH_FZ_YAW], a[IOSTOUCH_FZ_PITCH])) {
        // (a wall that way: the rest is dropped)
        iosTouch_aFreezeMove[IOSTOUCH_FZ_YAW] = iosTouch_aFreezeMid[IOSTOUCH_FZ_YAW] = 0;
        iosTouch_aFreezeMove[IOSTOUCH_FZ_PITCH] = iosTouch_aFreezeMid[IOSTOUCH_FZ_PITCH] = 0;
    }
    if (a[IOSTOUCH_FZ_PANX] != 0 || a[IOSTOUCH_FZ_PANY] != 0) iosFreeze_Pan(a[IOSTOUCH_FZ_PANX], a[IOSTOUCH_FZ_PANY]);
    if (a[IOSTOUCH_FZ_ZOOM] != 0) iosFreeze_Zoom(expf(a[IOSTOUCH_FZ_ZOOM]));
    iosFreeze_Frame();
}

// The cinematic burst (CAMERA slid up, with FREEZE on): the action frozen,
// five photos round it (iosCinema), then the game goes on; nothing shown
// and every touch ignored meanwhile, as for a selfie
- (void)startCinema
{
    if (iosTouch_bCinema || iosTouch_bFreezing || iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE) return;
    [self endBurst]; // (its waiting capture would hold it up)
    const int r = iosCinema_Start(CACurrentMediaTime());
    if (r != IOSCINEMA_STARTED) {
        iosGame_ShowMessage(r == IOSCINEMA_BUSY ? "Still saving the last photo" : "Can't freeze here");
        return;
    }
    iosTouch_bCinema = 1;
    [self hideForPhotosKeeping:nil];
    [cameraTap prepare];
}

- (void)endCinema
{
    if (!iosTouch_bCinema) return;
    iosTouch_bCinema = 0;
    iosCinema_Stop(); // (when ended early: a menu, the overlay going...)
    [self showAfterPhotos];
}

// CAMERA tapped in Action Freeze: one clean photo of the frozen moment (the
// HUD isn't drawn from its camera, so the other kinds would be the same)
- (void)takeFreezePhoto
{
    if (!iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_PHOTO)) {
        iosGame_ShowMessage("Still saving the last photo");
        return;
    }
    iosTouch_tCameraAsked = CACurrentMediaTime();
    [cameraTap prepare];
}

- (void)tickSelfie:(CFTimeInterval)now
{
    if (iosTouch_selfieStage == IOSTOUCH_SELFIE_NONE) return;
    // (all put back by it once the last photo has been shown, or it gave up)
    if (iosSelfieShots_Tick(now) == IOSSELFIESHOTS_IDLE) [self endSelfie];
}

// Everything as it was; the game goes on
- (void)endSelfie
{
    if (iosTouch_selfieStage == IOSTOUCH_SELFIE_NONE) return;
    iosTouch_selfieStage = IOSTOUCH_SELFIE_NONE;
    iosSelfieShots_Stop(); // (when ended early: the app going to the background...)
    [self showAfterPhotos];
}

// Right after photos were taken (iosPhotos), so none of it is in them; a
// lighter flash for each shot of a burst
- (void)cameraFeedback:(int)mode
{
    // (a selfie's photos come a frame apart: after its first, the lighter
    // flash and click a burst's take)
    const int bBurst = mode == IOSCAPTURE_MODE_BURST || (mode == IOSCAPTURE_MODE_SELFIE && iosTouch_selfieFlashes++ > 0);
    iosTouch_tCameraAsked = 0;
    [cameraFlash.layer removeAllAnimations];
    [self addSubview:cameraFlash]; // on top
    cameraFlash.frame = self.bounds;
    cameraFlash.alpha = bBurst ? 0.35 : 0.85;
    cameraFlash.hidden = NO;
    [UIView animateWithDuration:(bBurst ? 0.12 : 0.3) delay:0 options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionAllowUserInteraction
                     animations:^{ self->cameraFlash.alpha = 0.0; }
                     completion:^(BOOL done) { if (done) self->cameraFlash.hidden = YES; }];
    AudioServicesPlaySystemSound((bBurst && iosTouch_burstSound) ? iosTouch_burstSound : IOSTOUCH_CAMERA_SOUND);
    [cameraTap impactOccurred];
}

// What the tray's GYRO and its options say (the mode it is on in, or OFF;
// the mode it goes on in; the sensitivity), and the camera options' SELFIES
- (void)refreshGyroLabels
{
    static const char* aModeName[IOSTOUCH_GYRO_NUM_MODES] = { "OFF", "TOUCH", "ALWAYS" };
    aButtonViews[BTN_TRAYGYRO].text = iosTouch_GyroAvailable() ? [NSString stringWithFormat:@"GYRO\n%s", aModeName[iosTouch_gyroMode]]
                                                               : @"NO\nGYRO";
    aButtonViews[BTN_GYROOPT_MODE].text = [NSString stringWithFormat:@"MODE\n%s", aModeName[iosTouch_gyroLastMode]];
    aButtonViews[BTN_GYROOPT_SENS].text = [NSString stringWithFormat:@"SENS\n%.1fx", (double)iosTouch_aGyroSens[iosTouch_gyroSens]];
    aButtonViews[BTN_CAMROW_SELFIES].text = [NSString stringWithFormat:@"SELFIES\n%d", iosTouch_selfieShots];
    aButtonViews[BTN_CAMROW_FREEZE].text = iosTouch_bFreezeOn ? @"FREEZE\nON" : @"FREEZE\nOFF";
}

// A tray button picked: it does its thing, and the tray closes -- except
// after GYRO, which stays for another tap
- (void)useTrayButton:(int)button
{
    int kind = iosTouch_aButtons[button].kind;
    if (kind == KIND_CHAT) {
        iosGame_ToggleChat();
    }
    else if (kind == KIND_FPS) {
        [self setShowFps:!iosTouch_bShowFps];
    }
    else if (kind == KIND_CAMSWITCH) {
        [self setShowCamera:!iosTouch_bShowCamera];
    }
    else if (kind == KIND_CHEATSWITCH) {
        // cheat mode: on, CHEAT shown and the cheat on; off, left
        if (iosTouch_bShowCheat) {
            [self leaveCheatMode];
        }
        else {
            [self setShowCheat:1];
            if (!iosGame_IsSithEwokOn()) iosGame_ToggleSithEwok();
            iosCheatLaugh_Play(iosGame_SfxVolume()); // (the Emperor approves; also on typing it, CHEAT hidden)
        }
    }
    else if (kind == KIND_GYRO && iosTouch_GyroAvailable()) {
        iosTouch_GyroToggle();
        [self refreshGyroLabels];
    }
    if (kind == KIND_GYRO) [self refreshButtonLooks];
    else [self setTrayOpen:0];
}

// ------------------------------------------------------------ the wheels

// The row under a slice's name at the wheel's row scale: a learned power's
// star row, or a weapon's ammo count (as wide as three digits, as tall as a
// digit)
- (CGSize)wheelRowSize
{
    CGFloat k = aWheelFit[iosTouch_wheelKind].rowScale;
    if (iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS)
        return CGSizeMake(ammoRowSize.width * k, IOSTOUCH_WHEEL_AMMO_FONT * IOSTOUCH_WHEEL_AMMO_INK * k);
    return CGSizeMake(((IOSTOUCH_WHEEL_STARS - 1) * IOSTOUCH_WHEEL_STAR_STEP + IOSTOUCH_WHEEL_STAR_CELL) * k,
                      IOSTOUCH_WHEEL_STAR_CELL * k);
}

// Whether a slice of the open wheel ever has a row under its name: every
// power (once learned); every weapon but the ones with no ammo (group 0); no
// colour
static int iosTouch_WheelSliceHasRow(int i)
{
    if (iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER) return 0;
    return iosTouch_wheelKind != IOSTOUCH_WHEEL_WEAPONS || iosTouch_pWheelMap->aSlices[i].group != 0;
}

// Fits a slice's label inside it: the biggest size (11pt down to 7pt) at which
// it fits somewhere along the slice, as near the middle of the band as it can
// go at that size (s11: its size at 11pt; text width goes with font size). With
// bRow, the label and the row under it (wheelRowSize) are fitted as one block,
// so neither crosses the slice's edges. Returns whether it fitted.
- (int)fitWheelLabel:(int)i size:(CGSize)s11 row:(int)bRow
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    iosTouchWheelFit* F = &aWheelFit[iosTouch_wheelKind];
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, R1 = iosTouch_wheelR1;
    CGSize row = [self wheelRowSize];
    CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
    // no room for the row: the label goes on alone
    F->font[i][bRow] = bRow ? 0.0 : 7.0;
    F->labelR[i][bRow] = R0 + (R1 - R0) * 0.58;
    F->labelSize[i][bRow] = CGSizeMake(s11.width * 7.0 / 11.0, s11.height * 7.0 / 11.0);
    int bFound = 0;
    for (CGFloat size = 11.0; size >= 7.0 && !bFound; size -= 0.5) {
        CGFloat w = s11.width * size / 11.0, h = s11.height * size / 11.0;
        CGFloat bw = bRow ? MAX(w, row.width) : w;
        CGFloat bh = bRow ? h + IOSTOUCH_WHEEL_STAR_GAP + row.height : h;
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
                F->font[i][bRow] = size;
                F->labelR[i][bRow] = rr;
                F->labelSize[i][bRow] = CGSizeMake(w, h);
            }
        }
    }
    return bFound;
}

// Fits every slice's label, alone and with its row. The row is one size all
// round the wheel: the biggest, in steps of 5% down to IOSTOUCH_WHEEL_STAR_MIN
// of full size for the stars (Jedi Knight's slices take them full size;
// Mysteries of the Sith's narrower ones need them smaller), or
// IOSTOUCH_WHEEL_AMMO_MIN for the ammo counts, at which every slice's row
// fits. A weapon with no ammo has no row: its label is only fitted alone.
- (void)fitWheelLabels
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    iosTouchWheelFit* F = &aWheelFit[iosTouch_wheelKind];
    CGFloat rowMin = (iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS) ? IOSTOUCH_WHEEL_AMMO_MIN : IOSTOUCH_WHEEL_STAR_MIN;
    CGSize aS11[IOSTOUCH_WHEEL_MAX];
    for (int i = 0; i < m->numSlices; i++) {
        UILabel* l = aSliceLabels[i]; // (its name: layoutWheel)
        l.font = IOSTouch_WheelFont(11.0);
        aS11[i] = [l sizeThatFits:CGSizeMake(300.0, 300.0)];
        [self fitWheelLabel:i size:aS11[i] row:0];
        F->font[i][1] = 0.0; // (a slice with no row: no fit with one)
    }
    for (int n = 0; ; n++) {
        int bAll = 1;
        F->rowScale = 1.0 - 0.05 * n;
        int bLast = F->rowScale <= rowMin + 0.001;
        // (one that doesn't fit: on to the next size, but at the last, every slice)
        for (int i = 0; i < m->numSlices && (bAll || bLast); i++) {
            if (iosTouch_WheelSliceHasRow(i) && ![self fitWheelLabel:i size:aS11[i] row:1]) bAll = 0;
        }
        if (bAll || bLast) break; // else the ones that don't fit go without
    }
}

// Lays the wheel out round the middle of the safe area, as big as fits with
// room for a slice to pop out, every power or weapon in its place on the map
- (void)layoutWheel
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (!m) return;
    iosTouchWheelFit* F = &aWheelFit[iosTouch_wheelKind];
    int bWeapons = iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS;
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

    // Each slice's name (two-word names on two lines; "BOW- CASTER" and
    // Mysteries of the Sith's "LIGHT- SABER" break after their hyphen), then
    // the fits -- kept for each wheel, until the screen size changes
    for (int i = 0; i < m->numSlices; i++) {
        const char* name = iosTouch_WheelSlotName(m->aSlices[i].bin);
        aSliceLabels[i].text = [[NSString stringWithUTF8String:(name ? name : "?")] stringByReplacingOccurrencesOfString:@" " withString:@"\n"];
    }
    if (m != F->map || R1 != F->R1) {
        F->map = m;
        F->R1 = R1;
        [self fitWheelLabels];
    }
    // Each name in its slice. Under a learned power's, its level: that many
    // filled stars, then empty ones; under a weapon's the player has, its
    // ammo count (made again at every opening, as the levels and counts are
    // read then; refreshWheelLooks only colours them).
    CGSize row = [self wheelRowSize];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
        UILabel* l = aSliceLabels[i];
        int bRow = i < m->numSlices && F->font[i][1] > 0.0 && iosTouch_WheelSliceHasRow(i)
                   && (bWeapons ? iosTouch_aWheelAmmo[i] >= 0 : iosTouch_aWheelEarned[i]);
        int bStars = bRow && !bWeapons;
        aSliceStars[i][0].hidden = !bStars;
        aSliceStars[i][1].hidden = !bStars;
        aSliceRows[i].hidden = !(bRow && bWeapons);
        if (i >= m->numSlices) {
            l.hidden = YES;
            continue;
        }
        CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
        CGSize ts = F->labelSize[i][bRow];
        CGPoint p = CGPointMake(c.x + F->labelR[i][bRow] * cos(a), c.y - F->labelR[i][bRow] * sin(a));
        l.font = IOSTouch_WheelFont(F->font[i][bRow]);
        l.bounds = CGRectMake(0, 0, ceil(ts.width) + 4.0, ceil(ts.height) + 2.0);
        // with the row, p is the middle of the name and the row under it
        l.center = bRow ? CGPointMake(p.x, p.y - (IOSTOUCH_WHEEL_STAR_GAP + row.height) * 0.5) : p;
        l.hidden = NO;
        CGPoint rc = CGPointMake(p.x, p.y + (ts.height + IOSTOUCH_WHEEL_STAR_GAP) * 0.5); // the row's middle
        if (bStars) {
            CGFloat k = F->rowScale;
            UIBezierPath* aPath[2] = { [UIBezierPath bezierPath], [UIBezierPath bezierPath] }; // filled, empty
            for (int j = 0; j < IOSTOUCH_WHEEL_STARS; j++) {
                CGFloat x = p.x + (j - (IOSTOUCH_WHEEL_STARS - 1) * 0.5) * IOSTOUCH_WHEEL_STAR_STEP * k;
                IOSTouch_AddStar(aPath[j >= iosTouch_aWheelLevel[i]], CGPointMake(x, rc.y), k);
            }
            aSliceStars[i][0].path = aPath[0].CGPath;
            aSliceStars[i][1].path = aPath[1].CGPath;
            aSliceStars[i][0].lineWidth = IOSTOUCH_WHEEL_STAR_EDGE * k;
            aSliceStars[i][1].lineWidth = IOSTOUCH_WHEEL_STAR_LINE * k;
        }
        else if (bRow) {
            // the count, as the HUD shows it (at most three digits fit)
            UILabel* r = aSliceRows[i];
            r.text = [NSString stringWithFormat:@"%d", MIN(iosTouch_aWheelAmmo[i], 999)];
            r.font = [UIFont monospacedDigitSystemFontOfSize:IOSTOUCH_WHEEL_AMMO_FONT * F->rowScale weight:UIFontWeightSemibold];
            r.bounds = CGRectMake(0, 0, ceil(row.width) + 4.0, ceil(ammoRowSize.height * F->rowScale)); // (a whole line)
            r.center = rc; // (the digits about centred on the row)
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
                if (aButtonViews[j].hidden || j == iosTouch_wheelButton) continue;
                CGFloat ex = MAX(fabs(bt->x - pos.x) - ts.width * 0.5, 0.0);
                CGFloat ey = MAX(fabs(bt->y - pos.y) - ts.height * 0.5, 0.0);
                bClear = sqrt(ex * ex + ey * ey) > bt->radius + 4.0;
            }
            if (!bClear) continue;
            l.center = pos;
            l.hidden = NO;
        }
    }

    // The middle: the name (setWheelTitle, from refreshWheelLooks), what
    // lifting does under it, "cancel" in the gap
    bWheelTitleSet = 0;
    wheelHint.bounds = CGRectMake(0, 0, 2.0 * R0, 14.0);
    wheelHint.center = CGPointMake(c.x, c.y + 13.0);
    wheelCancelLabel.center = CGPointMake(c.x, c.y + R1 - 22.0);
    [self layoutSaberChips];
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

// Each slice in its look: the ones that can be picked in their group's
// colour, the selected power / the weapon in hand edged in white, the one
// pointed at popped out in full colour; the powers not learned yet and the
// weapons not found yet dark; a weapon the player has but can't switch to
// (no ammo for it) in a darker shade of its colour. The middle names what
// lifting would pick, or says what to do.
- (void)refreshWheelLooks
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (!m) return;
    int bWeapons = iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS;
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, R1 = iosTouch_wheelR1;

    // What is pointed at: by the opener once it has slid, else by the latest
    // other finger on the wheel. Off every slice (back in the middle, in the
    // gap, or a finger off the ring), lifting cancels. An opener that opened
    // it by holding still points at nothing until it slides.
    iosTouchSlot* pSlide = NULL;
    iosTouchSlot* pFinger = NULL;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_WHEEL) continue;
        if (s->bWheelOpener && s->bWheelHeld) continue;
        if (!s->bWheelOpener && [self saberChipAt:[s->touch locationInView:self]] >= 0) continue; // (on a choice beside the saber wheel: not cancelling)
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

    const int bSaber = iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER;
    int cur = bSaber ? iosSaber_GetColour(iosSaber_GetStyle(), iosTouch_SaberBlade()) : bWeapons ? iosGame_GetCurWeapon() : iosGame_GetCurPower();
    int curSlice = -1, bAnyEarned = 0;
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
        if (bWeapons && !bEarned && iosTouch_aWheelOwned[i]) {
            // has it, can't switch to it: no ammo for it
            L.fillColor = IOSTouch_WheelColour(g, 0.22).CGColor;
            L.strokeColor = (i == curSlice) ? [UIColor whiteColor].CGColor : IOSTouch_WheelColour(g, bHot ? 0.8 : 0.55).CGColor;
            L.lineWidth = (bHot || i == curSlice) ? 2.5 : 2.0;
            text = [UIColor colorWithWhite:1.0 alpha:0.75];
        }
        else if (!bEarned) {
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
        aSliceRows[i].textColor = text; // (shown on the weapon wheel only)
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
        name = iosTouch_WheelSlotName(m->aSlices[hot].bin);
        wheelTitle.textColor = iosTouch_aWheelEarned[hot] ? IOSTouch_WheelColour(&m->aGroups[m->aSlices[hot].group], 1.0) : grey;
        if (iosTouch_aWheelEarned[hot]) wheelHint.text = @"lift to select";
        else if (!bWeapons) wheelHint.text = @"not learned yet";
        else wheelHint.text = iosTouch_aWheelOwned[hot] ? @"no ammo" : @"not found yet";
    }
    else if (bCancel) {
        name = "CANCEL";
        wheelTitle.textColor = grey;
        wheelHint.text = @"lift to cancel";
    }
    else if (!bAnyEarned && !bWeapons) {
        name = "NO POWERS";
        wheelTitle.textColor = grey;
        wheelHint.text = @"none learned yet";
    }
    else {
        name = (curSlice >= 0) ? iosTouch_WheelSlotName(cur) : (bSaber ? "SABER" : bWeapons ? "WEAPONS" : "FORCE");
        wheelTitle.textColor = (curSlice >= 0) ? IOSTouch_WheelColour(&m->aGroups[m->aSlices[curSlice].group], 1.0) : [UIColor whiteColor];
        if (bSaber && iosSaber_StyleNumBlades(iosSaber_GetStyle()) >= 2)
            wheelHint.text = iosTouch_saberTarget == IOSTOUCH_SABER_LEFT ? @"colour for the left saber"
                           : iosTouch_saberTarget == IOSTOUCH_SABER_RIGHT ? @"colour for the right saber" : @"colour for both sabers";
        else if (bSaber) wheelHint.text = iosTouch_bWheelTapMode ? @"tap a colour" : @"slide toward a colour";
        else if (bWeapons) wheelHint.text = iosTouch_bWheelTapMode ? @"tap a weapon" : @"slide toward a weapon";
        else wheelHint.text = iosTouch_bWheelTapMode ? @"tap a power" : @"slide toward a power";
    }
    if (!bWheelTitleSet || name != wheelTitleName) [self setWheelTitle:name];

    // A light tick each time the thumb moves onto another slice (or the gap)
    if (hot != wheelLastHot) {
        if (hot != -1) [wheelTick selectionChanged];
        wheelLastHot = hot;
    }
}

// The name in the middle of the wheel, on one line ("BOW- CASTER" and
// "LIGHT- SABER", which break after their hyphen in their slices, are
// BOWCASTER and LIGHTSABER here), shrunk to fit if need be. A weapon's name
// that would have to shrink below IOSTOUCH_WHEEL_TITLE_MIN of its size goes
// on two lines instead, as big as they fit (17 pt at most), its foot where
// the one line's is: above the hint, inside the hole.
- (void)setWheelTitle:(const char*)name
{
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, W = 2.0 * R0 * 0.8;
    NSString* text = [[NSString stringWithUTF8String:(name ? name : "")] stringByReplacingOccurrencesOfString:@"- " withString:@""];
    wheelTitleName = name;
    bWheelTitleSet = 1;
    wheelTitle.numberOfLines = 1;
    wheelTitle.font = [UIFont boldSystemFontOfSize:17.0];
    wheelTitle.text = text;
    CGFloat w1 = [wheelTitle sizeThatFits:CGSizeMake(1000.0, 1000.0)].width;
    if (iosTouch_wheelKind == IOSTOUCH_WHEEL_WEAPONS && name && strchr(name, ' ') && w1 * IOSTOUCH_WHEEL_TITLE_MIN > W) {
        wheelTitle.numberOfLines = 2;
        wheelTitle.text = [text stringByReplacingOccurrencesOfString:@" " withString:@"\n"];
        CGSize s2 = [wheelTitle sizeThatFits:CGSizeMake(1000.0, 1000.0)];
        // as wide as the one line may be, and its top corners inside the hole
        CGFloat k = MIN(1.0, MIN(W / s2.width, (0.6 * R0 + 5.0) / s2.height));
        CGFloat size = floor(17.0 * k * 2.0) / 2.0;
        wheelTitle.font = [UIFont boldSystemFontOfSize:size];
        CGFloat h = ceil(s2.height * size / 17.0);
        wheelTitle.bounds = CGRectMake(0, 0, W, h);
        wheelTitle.center = CGPointMake(c.x, c.y + 5.0 - h * 0.5);
    }
    else {
        wheelTitle.bounds = CGRectMake(0, 0, W, 24.0);
        wheelTitle.center = CGPointMake(c.x, c.y - 7.0);
    }
}

// Opens a wheel (kind), for the button that opened it: every power or weapon
// of this game in its place, the ones that can be picked lit -- a learned
// power with its level, a weapon the player has with its ammo count. Open to
// slide to a slice, or (bTapMode) to tap one. Every other touch lets go of
// what it was holding and is ignored until it lifts (so a press waiting on
// the other wheel button is dropped), and the game holds still
// (iosGame_SetHold) until the wheel closes.
- (void)openWheel:(int)kind button:(int)button tapMode:(int)bTapMode
{
    [self setTrayOpen:0];
    int bMots = iosGame_IsMots();
    iosTouch_wheelKind = kind;
    iosTouch_wheelButton = button;
    if (kind == IOSTOUCH_WHEEL_WEAPONS) iosTouch_pWheelMap = bMots ? &iosTouch_motsWeaponWheel : &iosTouch_jkWeaponWheel;
    else if (kind == IOSTOUCH_WHEEL_SABER) {
        iosTouch_SetupSaberWheel(); // (the game's own colour, as it is now)
        iosTouch_pWheelMap = &iosTouch_saberWheel;
    }
    else iosTouch_pWheelMap = bMots ? &iosTouch_motsWheel : &iosTouch_jkWheel;
    for (int i = 0; i < iosTouch_pWheelMap->numSlices; i++) {
        int bin = iosTouch_pWheelMap->aSlices[i].bin;
        if (kind == IOSTOUCH_WHEEL_SABER) {
            // (every colour can be picked, saber or not: it is only how it looks)
            iosTouch_aWheelEarned[i] = iosTouch_aWheelOwned[i] = 1;
            iosTouch_aWheelAmmo[i] = -1;
            iosTouch_aWheelLevel[i] = 0;
        }
        else if (kind == IOSTOUCH_WHEEL_WEAPONS) {
            int ammo = -1, bSelectable = 0;
            iosTouch_aWheelOwned[i] = iosGame_GetWeapon(bin, &ammo, &bSelectable) != 0;
            iosTouch_aWheelEarned[i] = iosTouch_aWheelOwned[i] && bSelectable;
            iosTouch_aWheelAmmo[i] = iosTouch_aWheelOwned[i] ? ammo : -1;
            iosTouch_aWheelLevel[i] = 0;
        }
        else {
            iosTouch_aWheelEarned[i] = iosGame_IsPowerAvailable(bin);
            iosTouch_aWheelLevel[i] = iosGame_GetPowerLevel(bin);
            iosTouch_aWheelOwned[i] = iosTouch_aWheelEarned[i];
            iosTouch_aWheelAmmo[i] = -1;
        }
    }
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_BUTTON && iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
        if (s->role == ROLE_BUTTON && s->button == BTN_QUICKSAVE) [self setRing:saveRing progress:0.0];
        if (s->role == ROLE_BUTTON && s->button == BTN_QUICKLOAD) [self setRing:loadRing progress:0.0];
        if (s->role == ROLE_BUTTON && s->button == BTN_MENU) [self setRing:menuRing progress:0.0];
        if (s->role == ROLE_BUTTON && s->button == BTN_TRAYCAM) [self setRing:camOptRing progress:0.0];
        if (s->role == ROLE_STICK) {
            iosTouch_bStickActive = 0;
            iosTouch_stickX = iosTouch_stickY = 0.0f;
            [self hideStick];
        }
        s->role = ROLE_IGNORED;
    }
    [self setRing:wheelRing progress:0.0];
    [self setRing:weaponRing progress:0.0];
    [self setRing:saberRing progress:0.0];
    // A tap on either wheel button the game hasn't read yet (a slow frame:
    // it is only read by a gameplay update, which the hold now skips) would
    // go off after the wheel closes and undo its pick: the wheel is the newer
    // choice, so the tap is dropped. (A press the game has started reading
    // is left to finish.)
    iosTouch_aPulseQueue[iosTouch_aButtons[BTN_WHEEL].scancode] = 0;
    iosTouch_aPulseQueue[iosTouch_aButtons[BTN_NEXTWPN].scancode] = 0;
    iosTouch_bWheelOpen = 1;
    iosTouch_bWheelTapMode = bTapMode != 0;
    wheelLastHot = -1;
    iosGame_SetHold(1);
    [self layoutWheel];
    wheelView.hidden = NO;
    [self bringSubviewToFront:wheelView];
    [self insertSubview:aButtonViews[button] aboveSubview:wheelView];
    [wheelTick prepare];
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    [self refreshWheelLooks];
}

// FORCE WHEEL or NEXT WPN (the press s, still waiting to see which gesture
// it is) opens its wheel: slid (to slide to a slice, measured from where it
// went down) or held still (to tap one). It goes on as the wheel's opener.
// The typing line closes first: its keyboard would cover the lower half of
// the wheel.
- (void)openWheelFrom:(iosTouchSlot*)s held:(int)bHeld
{
    int button = s->button;
    [self endBurst]; // the game holds still: no more shots
    if (jkHud_bChatOpen) iosGame_ToggleChat();
    [self openWheel:iosTouch_WheelKindOf(button) button:button tapMode:bHeld];
    s->role = ROLE_WHEEL;
    s->button = button;
    s->bWheelOpener = 1;
    s->bWheelHeld = bHeld != 0;
    s->bArmed = 0;
    s->wheelSlot = -1;
    s->wheelOrigin = s->origin;
}

// Closes the wheel, selecting what is in slice (if it is one that can be
// picked): the power, the weapon (unless it is the one in hand already), or
// the blade colour
- (void)closeWheelSelecting:(int)slice
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    if (m && iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER && slice >= 0 && slice < m->numSlices) {
        // The saber wheel stays open on a colour: set now, and open to tap
        // more (the other blade, another colour) until the gap, a tap off
        // the ring or EQUIP closes it
        [self setSaberColour:m->aSlices[slice].bin];
        iosTouch_bWheelTapMode = 1;
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* o = &iosTouch_aSlots[i];
            if (o->touch && o->role == ROLE_WHEEL) {
                if (o->bWheelOpener) o->bWheelHeld = 1; // (lifted, it leaves the wheel open)
                o->wheelSlot = -1;
            }
        }
        [wheelTick selectionChanged];
        [self layoutSaberChips];
        [self refreshWheelLooks];
        return;
    }
    if (m && slice >= 0 && slice < m->numSlices && iosTouch_aWheelEarned[slice]) {
        int bin = m->aSlices[slice].bin;
        if (iosTouch_wheelKind == IOSTOUCH_WHEEL_SABER) [self setSaberColour:bin];
        else if (iosTouch_wheelKind != IOSTOUCH_WHEEL_WEAPONS) iosGame_SelectPower(bin);
        else if (bin != iosGame_GetCurWeapon()) iosGame_SelectWeapon(bin); // the game takes it once it is running again
    }
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        // fingers still on the wheel are ignored until they lift
        if (iosTouch_aSlots[i].touch && iosTouch_aSlots[i].role == ROLE_WHEEL) iosTouch_aSlots[i].role = ROLE_IGNORED;
    }
    iosTouch_bWheelOpen = 0;
    iosTouch_bWheelTapMode = 0;
    iosGame_SetHold(0);
    wheelView.hidden = YES;
    [self layoutSaberChips];
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

        if (iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE || iosTouch_bCinema) {
            s->role = ROLE_IGNORED; // nothing during a selfie or the cinematic burst
            continue;
        }
        if (iosTouch_bFreezing) {
            // Action Freeze: its X, CAMERA, or moving the camera
            if (hypot(p.x - freezeClose.center.x, p.y - freezeClose.center.y) <= IOSTOUCH_FREEZE_CLOSE_RADIUS + IOSTOUCH_TOUCH_SLOP)
                s->role = ROLE_FREEZECLOSE;
            else if ([self isPoint:p onButton:BTN_CAMERA])
                s->role = ROLE_FREEZESHOT;
            else
                s->role = ROLE_FREEZE;
            continue;
        }
        if (iosTouch_bWheelOpen) {
            s->role = ROLE_WHEEL;
            s->wheelSlot = iosTouch_WheelSliceAt(p, -1);
            continue;
        }

        s->button = [self buttonAt:p];
        // An open tray closes at a touch anywhere else, which then does what
        // it would anyway -- except on MENU, where it only closes the tray
        // ...and a row open under it at a touch anywhere but on the rows
        // (on the tray, the tray stays: holding GYRO or the camera switch
        // there opens its row instead)
        if (iosTouch_optRow != IOSTOUCH_ROW_NONE && !iosTouch_IsOptButton(s->button)) [self setOptRow:IOSTOUCH_ROW_NONE];
        if (iosTouch_bTrayOpen && !iosTouch_IsTrayButton(s->button) && !iosTouch_IsOptButton(s->button)) {
            [self setTrayOpen:0];
            if (s->button == BTN_MENU) {
                s->role = ROLE_IGNORED;
                continue;
            }
        }
        if (s->button >= 0 && iosTouch_aButtons[s->button].kind == KIND_WHEEL) {
            // FORCE WHEEL, NEXT WPN: nothing yet -- a tap, a slide or a hold
            // (touchesMoved, -tick, endTouches). A second finger on the same
            // button is ignored.
            int bOther = 0;
            for (int k = 0; k < IOSTOUCH_MAX_TOUCHES; k++) {
                iosTouchSlot* o = &iosTouch_aSlots[k];
                if (o != s && iosTouch_IsWheelPress(o) && o->button == s->button) bOther = 1;
            }
            if (bOther) {
                s->role = ROLE_IGNORED;
            }
            else {
                s->role = ROLE_BUTTON;
                iosTouch_aButtonHeld[s->button]++;
            }
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

            if (s->role == ROLE_FREEZE) {
                [self moveFreeze:s to:p];
                s->last = p;
                continue;
            }

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

            // FORCE WHEEL, NEXT WPN: slid IOSTOUCH_WHEEL_OPEN_SLIDE from where
            // it went down (at any of the places UIKit merged into this move,
            // too), the wheel opens at once, to slide to a slice
            // CAMERA: slid IOSTOUCH_WHEEL_OPEN_SLIDE (anywhere), before its hold
            // was up, starts a burst that goes on until this finger lifts.
            // The tray's camera switch or GYRO slid off isn't held any more.
            if (s->role == ROLE_BUTTON && !s->bFired && (s->button == BTN_CAMERA || iosTouch_IsRowOpener(s->button))) {
                int bOut = hypot(p.x - s->origin.x, p.y - s->origin.y) >= IOSTOUCH_WHEEL_OPEN_SLIDE;
                for (UITouch* c in [event coalescedTouchesForTouch:t]) {
                    CGPoint q = [c locationInView:self];
                    if (hypot(q.x - s->origin.x, q.y - s->origin.y) >= IOSTOUCH_WHEEL_OPEN_SLIDE) bOut = 1;
                }
                // With FREEZE on, a slide mostly up or down waits for the
                // longer IOSTOUCH_FREEZE_SWIPE, then stops the action: down,
                // Action Freeze; up, the cinematic burst. Sideways, it is
                // the burst as ever.
                // (CAMERA is at the top of the screen: slid up, the finger
                // meets the edge first -- far enough is up to near it)
                const CGFloat dx = p.x - s->origin.x, dy = p.y - s->origin.y;
                const int bDown = s->button == BTN_CAMERA && iosTouch_bFreezeOn && fabs(dy) >= fabs(dx) && dy != 0;
                const CGFloat upFar = MIN(IOSTOUCH_FREEZE_SWIPE, MAX(s->origin.y - IOSTOUCH_FREEZE_EDGE, IOSTOUCH_FREEZE_SWIPE_MIN));
                if (bDown && (dy > 0 ? dy : -dy) >= (dy > 0 ? IOSTOUCH_FREEZE_SWIPE : upFar)) {
                    s->bFired = 1;
                    [self setRing:cameraRing progress:0.0];
                    if (!jkHud_bChatOpen && t.timestamp - s->tDownTouch < IOSTOUCH_SELFIE_HOLD) {
                        if (dy > 0) [self startFreeze];
                        else [self startCinema];
                        if (iosTouch_bFreezing || iosTouch_bCinema) return; // (every touch now belongs to it)
                    }
                }
                else if (bDown && bOut) {
                    s->bSwipe = 1; // (on its way: not a tap, nor a hold)
                    [self setRing:cameraRing progress:0.0];
                }
                else if (bOut && s->button == BTN_CAMERA) {
                    s->bFired = 1;
                    [self setRing:cameraRing progress:0.0];
                    // (not a finger already held long enough for the selfie,
                    // by its own clock, that a frame hasn't seen yet)
                    if (!iosTouch_pBurstTouch && !jkHud_bChatOpen && t.timestamp - s->tDownTouch < IOSTOUCH_SELFIE_HOLD)
                        [self startBurst:t];
                }
                else if (bOut) {
                    s->bFired = 1;
                    [self setRing:(s->button == BTN_TRAYGYRO ? gyroOptRing : camOptRing) progress:0.0];
                }
            }

            if (iosTouch_IsWheelPress(s)) {
                int bOut = hypot(p.x - s->origin.x, p.y - s->origin.y) >= IOSTOUCH_WHEEL_OPEN_SLIDE;
                for (UITouch* c in [event coalescedTouchesForTouch:t]) {
                    CGPoint q = [c locationInView:self];
                    if (hypot(q.x - s->origin.x, q.y - s->origin.y) >= IOSTOUCH_WHEEL_OPEN_SLIDE) bOut = 1;
                }
                if (bOut) [self openWheelFrom:s held:0];
            }

            if (s->role == ROLE_WHEEL) {
                if (s->bWheelOpener && s->bWheelHeld) {
                    // Opened by holding still: open to tap, until it slides
                    // out of the dead zone -- then it picks by sliding
                    if (hypot(p.x - s->origin.x, p.y - s->origin.y) >= IOSTOUCH_WHEEL_DEAD) {
                        iosTouch_bWheelTapMode = 0;
                        s->bWheelHeld = 0;
                        [self aimWheel:s at:p];
                    }
                }
                else if (s->bWheelOpener && !iosTouch_bWheelTapMode) [self aimWheel:s at:p];
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
    int bEndFreeze = 0;
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];
            if (t == iosTouch_pBurstTouch) [self endBurst];
            if (s->role == ROLE_FREEZE || s->role == ROLE_FREEZESHOT || s->role == ROLE_FREEZECLOSE) {
                // Action Freeze: lifted on its X, back to the game; a tap on
                // CAMERA, a photo
                if (!bCancelled && s->role == ROLE_FREEZECLOSE
                    && hypot(p.x - freezeClose.center.x, p.y - freezeClose.center.y) <= IOSTOUCH_FREEZE_CLOSE_RADIUS + IOSTOUCH_TOUCH_SLOP)
                    bEndFreeze = 1;
                if (!bCancelled && s->role == ROLE_FREEZESHOT && [self isPoint:p onButton:BTN_CAMERA]
                    && t.timestamp - s->tDownTouch < IOSTOUCH_SELFIE_HOLD)
                    [self takeFreezePhoto];
                memset(s, 0, sizeof(*s));
                continue;
            }
            if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && bCancelled) {
                // the opener taken away: the wheel stays open, for tapping
                if (s->bWheelOpener) iosTouch_bWheelTapMode = 1;
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && s->bWheelOpener && s->bWheelHeld) {
                // opened by holding still, lifted without sliding: open to tap
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && s->bWheelOpener && !iosTouch_bWheelTapMode
                     && [self saberChipAt:p] == IOSTOUCH_CHIP_EQUIP) {
                // slid onto EQUIP and lifted there
                [self useSaberChip:IOSTOUCH_CHIP_EQUIP];
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && s->bWheelOpener && !iosTouch_bWheelTapMode) {
                [self aimWheel:s at:p];
                if (!s->bArmed) iosTouch_bWheelTapMode = 1; // never slid out of the dead zone: open to tap
                else [self closeWheelSelecting:s->wheelSlot]; // what it points at, or no change
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && !bCancelled && [self saberChipAt:p] >= 0) {
                // the saber wheel's choices beside the ring: set, the wheel open
                [self useSaberChip:[self saberChipAt:p]];
            }
            else if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen) {
                // A tap: on a learned power or a weapon it can switch to picks
                // it; on one it can't the wheel stays open; anywhere else (the
                // middle, the gap, off the ring, FORCE WHEEL, NEXT WPN) it
                // closes with no change --
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
            else if (iosTouch_IsWheelPress(s)) {
                // FORCE WHEEL, NEXT WPN lifted before its wheel opened
                iosTouchButton* b = &iosTouch_aButtons[s->button];
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                [self setRing:[self wheelRingOf:s->button] progress:0.0];
                if (bCancelled) {
                    // iOS took it: nothing
                }
                else if (hypot(p.x - s->origin.x, p.y - s->origin.y) >= IOSTOUCH_WHEEL_OPEN_SLIDE) {
                    // a flick, with no move in between: as a slide, then its lift
                    [self openWheelFrom:s held:0];
                    [self aimWheel:s at:p];
                    if (s->bArmed) [self closeWheelSelecting:s->wheelSlot];
                    else iosTouch_bWheelTapMode = 1;
                }
                else if (t.timestamp - s->tDownTouch < IOSTOUCH_WHEEL_HOLD && s->button == BTN_SABER) {
                    // SABER has no key: a tap opens its wheel, to tap a colour
                    [self openWheelFrom:s held:1];
                }
                else if (t.timestamp - s->tDownTouch < IOSTOUCH_WHEEL_HOLD) {
                    // A quick tap (by the touch's own clock, so a slow frame
                    // between the two doesn't make it long): the next learned
                    // power (E) or weapon (G) -- the newer choice over a weapon
                    // picked on the wheel still waiting. Not while typing: the
                    // game isn't reading the controls.
                    if (!jkHud_bChatOpen) {
                        if (s->button == BTN_NEXTWPN) iosGame_CancelWeaponPick();
                        iosTouch_QueuePress(b->scancode);
                    }
                }
                else {
                    // held long enough, but lifted before a frame could see it
                    // (a slow one): the hold, the wheel open to tap
                    [self openWheelFrom:s held:1];
                }
            }
            else if (s->role == ROLE_BUTTON) {
                iosTouchButton* b = &iosTouch_aButtons[s->button];
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                // JUMP: a quick still tap; the second of two while flying lands
                if (s->button == BTN_JUMP && !bCancelled && t.timestamp - s->tDownTouch < IOSTOUCH_FLY_TAP
                    && hypot(p.x - s->origin.x, p.y - s->origin.y) < IOSTOUCH_FLY_STILL) {
                    if (iosTouch_tJumpTapUp >= 0.0 && s->tDownTouch - iosTouch_tJumpTapUp < IOSTOUCH_FLY_DOUBLE
                        && !jkHud_bChatOpen && iosGame_IsFlying()) {
                        if (iosGame_ToggleFlight() >= 0) [cameraTap impactOccurred];
                        iosTouch_tJumpTapUp = -1.0;
                    }
                    else iosTouch_tJumpTapUp = t.timestamp;
                }
                // DUCK lifted with a level on show: that level (at the next tick)
                if (s->button == BTN_DUCK && s->levelPick > 0) {
                    if (!bCancelled) iosTouch_levelToLoad = s->levelPick;
                    [self endLevelPick:s];
                }
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
                int bWhileTyping = b->kind == KIND_MENU || iosTouch_IsTrayButton(s->button) || iosTouch_IsOptButton(s->button);
                if (!bCancelled && [self isPoint:p onButton:s->button] && (!jkHud_bChatOpen || bWhileTyping)) {
                    if (b->kind == KIND_ITEM && s->button != BTN_BACTA && !aButtonViews[s->button].hidden
                             // one item at a time: the use key acts on whichever item is selected when it is read
                             && !iosTouch_aPulseQueue[b->scancode] && !iosTouch_aPulseReads[b->scancode]
                             && !iosTouch_aPulseGap[b->scancode]) {
                        iosGame_SelectItem(b->bin);
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (iosTouch_IsTrayButton(s->button) && !(iosTouch_IsRowOpener(s->button) && s->bFired)) {
                        // (GYRO or the camera switch held until its row opened, or
                        // slid off: not a tap)
                        [self useTrayButton:s->button];
                    }
                    else if (iosTouch_IsOptButton(s->button) && !aButtonViews[s->button].hidden) {
                        [self useOptButton:s->button];
                    }
                    else if (b->kind == KIND_CAMERA && !aButtonViews[s->button].hidden && !s->bFired && !s->bSwipe
                             && t.timestamp - s->tDownTouch < IOSTOUCH_SELFIE_HOLD) {
                        // A tap (by the touch's own clock); not after a burst or a hold
                        [self takePhotos];
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
                if (s->button == BTN_BACTA) [self setRing:bactaRing progress:0.0];
                if (s->button == BTN_QUICKLOAD) [self setRing:loadRing progress:0.0];
                if (s->button == BTN_MENU) [self setRing:menuRing progress:0.0];
                if (s->button == BTN_TRAYCAM) [self setRing:camOptRing progress:0.0];
                if (s->button == BTN_TRAYGYRO) [self setRing:gyroOptRing progress:0.0];
                if (s->button == BTN_CAMERA) [self setRing:cameraRing progress:0.0];
            }
            else if (s->role == ROLE_STICK) {
                iosTouch_bStickActive = 0;
                iosTouch_stickX = iosTouch_stickY = 0.0f;
                [self hideStick];
            }
            memset(s, 0, sizeof(*s));
        }
    }
    if (bEndFreeze) [self endFreeze];
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

// Once per frame while shown: the QUICK SAVE, QUICK LOAD, MENU, FORCE WHEEL
// and NEXT WPN holds, the
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
    // SABER's picture in the blade's colour (the game's own one can change:
    // a new level, a cutscene, Mysteries of the Sith's own)
    // SABER shown once the saber is had for real, or while sithewok is on
    // (not hidden from under a finger or its open wheel)
    {
        const int bSaber = iosGame_IsSaberButtonShown() ? 1 : 0;
        if (bSaber != iosTouch_bShowSaber && !iosTouch_aButtonHeld[BTN_SABER]
            && !(iosTouch_bWheelOpen && iosTouch_wheelButton == BTN_SABER)) {
            iosTouch_bShowSaber = bSaber;
            aButtonViews[BTN_SABER].hidden = !bSaber;
            [self setNeedsLayout]; // (the burst's count makes room for it on the narrowest screens)
            [self refreshButtonLooks];
        }
    }
    if (iosTouch_bShowSaber) [self refreshSaberIcons];
    // DUCK's level pick: dropped (DUCK's label back) once its finger is no
    // longer a button press -- a wheel, Action Freeze, the burst or a selfie
    // took every touch; and a level lifted for loads only when none of those
    // is going on
    int bPicking = 0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (s->levelPick > 0 && s->touch && s->role == ROLE_BUTTON) bPicking = 1;
        else s->levelPick = 0;
    }
    if (!bPicking && ![aButtonViews[BTN_DUCK].text isEqualToString:@"DUCK"]) aButtonViews[BTN_DUCK].text = @"DUCK";
    if (iosTouch_levelToLoad > 0) {
        const int level = iosTouch_levelToLoad;
        iosTouch_levelToLoad = 0;
        if (iosTouch_selfieStage == IOSTOUCH_SELFIE_NONE && !iosTouch_bCinema && !iosTouch_bFreezing
            && !iosTouch_bWheelOpen && !jkHud_bChatOpen)
            iosGame_GoToLevel(level);
    }
    // During a selfie nothing else goes on here
    if (iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE) {
        [self tickSelfie:now];
        return;
    }
    // ...or the cinematic burst...
    if (iosTouch_bCinema) {
        if (iosCinema_Tick(now) == IOSCINEMA_IDLE) [self endCinema];
        return;
    }
    // ...or Action Freeze (its camera moves with the touches themselves)
    if (iosTouch_bFreezing) {
        if (!iosFreeze_IsActive()) [self endFreeze];
        else [self tickFreeze:now - known];
        return;
    }
    // FORCE WHEEL and NEXT WPN held still (they haven't slid, or the wheel
    // would be open): known to be held IOSTOUCH_WHEEL_HOLD, the wheel opens to
    // tap a slice; until then the ring fills. The first to get there opens
    // its wheel (which drops the other). This comes before the QUICK SAVE,
    // QUICK LOAD and MENU holds: one that completes in the same tick (touched
    // with a wheel button at about the same time) is then dropped by the
    // wheel opening, which ignores every other touch, rather than saving or
    // loading under the wheel.
    CGFloat wheelProgress = 0.0, weaponProgress = 0.0, saberProgress = 0.0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!iosTouch_IsWheelPress(s)) continue;
        if (known - s->tDown >= IOSTOUCH_WHEEL_HOLD) {
            [self openWheelFrom:s held:1];
            wheelProgress = weaponProgress = saberProgress = 0.0;
            break; // (every other touch is ignored now)
        }
        CGFloat progress = MIN((CGFloat)((now - s->tDown) / IOSTOUCH_WHEEL_HOLD), 1.0);
        if (s->button == BTN_NEXTWPN) weaponProgress = progress;
        else if (s->button == BTN_SABER) saberProgress = progress;
        else wheelProgress = progress;
    }
    if (!iosTouch_bWheelOpen) {
        [self setRing:wheelRing progress:wheelProgress];
        [self setRing:weaponRing progress:weaponProgress];
        [self setRing:saberRing progress:saberProgress];
    }

    // JUMP or ACT held still (it jumps or acts as ever, and holds the key, as
    // ever): with sithewok on, IOSTOUCH_FLY_HOLD switches its flight (JUMP)
    // or noclip (ACT) on or off, once per press; the ring fills meanwhile
    // (the furthest along of its holds)
    // DUCK the same, for its level pick: from IOSTOUCH_FLY_HOLD the number
    // on show counts up (a tick each); lifted, that level (endTouches)
    CGFloat jumpProgress = 0.0, actProgress = 0.0, duckProgress = 0.0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON || (s->button != BTN_JUMP && s->button != BTN_ACT && s->button != BTN_DUCK) || s->bFlyDone) continue;
        CGPoint q = [s->touch locationInView:self];
        if (hypot(q.x - s->origin.x, q.y - s->origin.y) >= IOSTOUCH_FLY_STILL) {
            s->bFlyDone = 1; // (a drag, turning the view)
            [self endLevelPick:s];
            continue;
        }
        if (s->button == BTN_DUCK) {
            const int numLevels = jkHud_bChatOpen ? 0 : iosGame_NumPickableLevels();
            if (numLevels <= 0) {
                if (s->levelPick) {
                    s->bFlyDone = 1; // (sithewok went off, or typing: no pick)
                    [self endLevelPick:s];
                }
                continue;
            }
            const CFTimeInterval held = known - s->tDown;
            if (held >= IOSTOUCH_FLY_HOLD) {
                // (after the last, round to 1 again, for as long as it is held)
                int n = 1 + (int)((held - IOSTOUCH_FLY_HOLD) / IOSTOUCH_LEVEL_STEP) % numLevels;
                if (n != s->levelPick) {
                    s->levelPick = n;
                    aButtonViews[BTN_DUCK].text = [NSString stringWithFormat:@"%d", n];
                    // (and on the message line, where the thumb isn't)
                    char msg[48];
                    snprintf(msg, sizeof(msg), "Level %d of %d", n, numLevels);
                    iosGame_ShowMessageInPlace(msg);
                    [cameraTap impactOccurred];
                }
                continue;
            }
            duckProgress = MAX(duckProgress, MIN((CGFloat)((now - s->tDown) / IOSTOUCH_FLY_HOLD), 1.0));
            continue;
        }
        const int bJump = s->button == BTN_JUMP;
        if (jkHud_bChatOpen || !(bJump ? iosGame_CanFly() : iosGame_CanNoclip())) continue;
        if (bJump && iosGame_IsFlying()) {
            s->bFlyDone = 1; // (flying: held, JUMP rises; a double tap lands)
            continue;
        }
        if (known - s->tDown >= IOSTOUCH_FLY_HOLD) {
            s->bFlyDone = 1;
            if ((bJump ? iosGame_ToggleFlight() : iosGame_ToggleNoclip()) >= 0) [cameraTap impactOccurred];
            continue;
        }
        const CGFloat progress = MIN((CGFloat)((now - s->tDown) / IOSTOUCH_FLY_HOLD), 1.0);
        if (bJump) jumpProgress = MAX(jumpProgress, progress);
        else actProgress = MAX(actProgress, progress);
    }
    [self setRing:jumpRing progress:jumpProgress];
    [self setRing:actRing progress:actProgress];
    [self setRing:duckRing progress:duckProgress];

    // CHEAT held still IOSTOUCH_CHEAT_HOLD: the cheat on or off, once per
    // press (the ring fills meanwhile; moved IOSTOUCH_FLY_STILL, no); and its
    // red following the cheat however it was switched (typed too)
    CGFloat cheatProgress = 0.0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON || s->button != BTN_CHEAT || s->bFlyDone) continue;
        CGPoint q = [s->touch locationInView:self];
        if (hypot(q.x - s->origin.x, q.y - s->origin.y) >= IOSTOUCH_FLY_STILL || jkHud_bChatOpen || iosTouch_bWheelOpen
            || iosTouch_bFreezing || iosTouch_bCinema || iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE) {
            s->bFlyDone = 1; // (slid off, typing, or a wheel or the photos took over)
            continue;
        }
        if (known - s->tDown >= IOSTOUCH_CHEAT_HOLD) {
            s->bFlyDone = 1;
            if (iosGame_ToggleSithEwok() >= 0) [cameraTap impactOccurred];
            continue;
        }
        cheatProgress = MAX(cheatProgress, MIN((CGFloat)((now - s->tDown) / IOSTOUCH_CHEAT_HOLD), 1.0));
    }
    [self setRing:cheatRing progress:cheatProgress];

    // BACTA held IOSTOUCH_BACTA_HOLD on it: a bacta tank used, once per press
    // (as a tap on another item: selected, then the use key), waiting if the
    // use key is still busy; lifted or slid off sooner, or typing, nothing
    CGFloat bactaProgress = 0.0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON || s->button != BTN_BACTA || s->bFlyDone) continue;
        iosTouchButton* b = &iosTouch_aButtons[BTN_BACTA];
        if (![self isPoint:[s->touch locationInView:self] onButton:BTN_BACTA] || aButtonViews[BTN_BACTA].hidden || jkHud_bChatOpen
            || iosTouch_bWheelOpen || iosTouch_bFreezing || iosTouch_bCinema || iosTouch_selfieStage != IOSTOUCH_SELFIE_NONE) {
            s->bFlyDone = 1; // (slid off, gone, typing, or a wheel or the photos took over)
            continue;
        }
        if (known - s->tDown >= IOSTOUCH_BACTA_HOLD) {
            if (iosTouch_aPulseQueue[b->scancode] || iosTouch_aPulseReads[b->scancode] || iosTouch_aPulseGap[b->scancode]) {
                bactaProgress = 1.0; // (the use key busy: in a moment)
                continue;
            }
            s->bFlyDone = 1;
            iosGame_SelectItem(b->bin);
            iosTouch_QueuePress(b->scancode);
            [cameraTap impactOccurred];
            continue;
        }
        bactaProgress = MAX(bactaProgress, MIN((CGFloat)((now - s->tDown) / IOSTOUCH_BACTA_HOLD), 1.0));
    }
    [self setRing:bactaRing progress:bactaProgress];
    if (iosGame_IsSithEwokOn() != iosTouch_bCheatOn) {
        iosTouch_bCheatOn = iosGame_IsSithEwokOn();
        // (switched on some other way -- typed "sithewok" -- with CHEAT hidden: shown, and the
        // Emperor's laugh, as the tray's switch does)
        if (iosTouch_bCheatOn && !iosTouch_bShowCheat) {
            [self setShowCheat:1];
            iosCheatLaugh_Play(iosGame_SfxVolume());
        }
        [self refreshButtonLooks];
    }

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
        else if (iosTouch_IsRowOpener(s->button) && !s->bFired && iosTouch_bTrayOpen
                 && (s->button != BTN_TRAYGYRO || iosTouch_GyroAvailable())) {
            // The tray's camera switch or GYRO held still: its row opens (the
            // lift then doesn't switch it)
            CAShapeLayer* ring = s->button == BTN_TRAYGYRO ? gyroOptRing : camOptRing;
            CGFloat progress = (CGFloat)((now - s->tDown) / IOSTOUCH_MENU_HOLD);
            if (known - s->tDown >= IOSTOUCH_MENU_HOLD) {
                s->bFired = 1;
                [self setRing:ring progress:0.0];
                [self setOptRow:(s->button == BTN_TRAYGYRO ? IOSTOUCH_ROW_GYRO : IOSTOUCH_ROW_CAMERA)];
            }
            else {
                [self setRing:ring progress:MIN(progress, 1.0)];
            }
        }
        else if (s->button == BTN_CAMERA && !s->bFired && !s->bSwipe && !jkHud_bChatOpen) {
            // Held still: the ring fills, then the selfie (the lift takes no
            // photos). Not while typing.
            if (known - s->tDown >= IOSTOUCH_SELFIE_HOLD) {
                s->bFired = 1;
                [self setRing:cameraRing progress:0.0];
                [self startSelfie];
                return; // everything else waits for the selfie to end
            }
            [self setRing:cameraRing progress:MIN((CGFloat)((now - s->tDown) / IOSTOUCH_SELFIE_HOLD), 1.0)];
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

    [self tickBurst:now];

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
    iosGame_CancelWeaponPick(); // a weapon picked on the wheel, still waiting
    if (iosTouch_bWheelOpen) {
        iosTouch_bWheelOpen = 0;
        iosTouch_bWheelTapMode = 0;
        iosGame_SetHold(0);
        wheelView.hidden = YES;
    }
    [self endSelfie]; // (all put back: the overlay is going, a menu or the background)
    [self endFreeze];
    [self endCinema];
    [self setTrayOpen:0];
    [self endBurst];
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
    [self setRing:jumpRing progress:0.0];
    [self setRing:actRing progress:0.0];
    [self setRing:duckRing progress:0.0];
    [self setRing:cheatRing progress:0.0];
    aButtonViews[BTN_DUCK].text = @"DUCK"; // (a level pick on show, dropped)
    iosTouch_levelToLoad = 0;
    [self setRing:loadRing progress:0.0];
    [self setRing:menuRing progress:0.0];
    [self setRing:camOptRing progress:0.0];
    [self setRing:cameraRing progress:0.0];
    [self setRing:wheelRing progress:0.0];
    [self setRing:weaponRing progress:0.0];
    [self setRing:saberRing progress:0.0];
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

    // Photos asked for but not taken yet: dropped once the overlay goes (a
    // menu, a cutscene), or if no frame took them in time
    if (iosTouch_tCameraAsked && (!bWant || CACurrentMediaTime() - iosTouch_tCameraAsked > IOSTOUCH_CAMERA_TIMEOUT)) {
        iosCapture_Cancel();
        iosTouch_tCameraAsked = 0;
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

// iosPhotos: whether the FPS readout is on (photo timings go with it)
int iosTouch_IsFpsShown(void)
{
    return iosTouch_bShowFps;
}

UIView* iosTouch_OverlayView(void)
{
    return iosTouch_pOverlay;
}

void iosTouch_CameraFeedback(int mode)
{
    if (iosTouch_pOverlay) [iosTouch_pOverlay cameraFeedback:mode];
}

int iosTouch_GetTopTextRectPt(float* pTop, float* pLeft, float* pRight)
{
    if (!iosTouch_bTopText) return 0;
    *pTop = iosTouch_aTopText[0];
    *pLeft = iosTouch_aTopText[1];
    *pRight = iosTouch_aTopText[2];
    return 1;
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

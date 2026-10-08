// Added: entire file. See iosTouch.h.
// Written to work with or without ARC (no weak refs; the overlay lives for
// the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL3/SDL.h>
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
// QUICK LOAD has to be held this long, so a stray tap can't throw away progress
#define IOSTOUCH_QUICKLOAD_HOLD 1.0
// MENU held this long opens its tray (keyboard, FPS); a tap opens the menu as
// it lifts, holding Escape down for this many iosTouch_Update calls
#define IOSTOUCH_MENU_HOLD 0.45
#define IOSTOUCH_MENU_PULSE_UPDATES 2
// The FPS readout counts frames over at least this many seconds; whether it
// shows is kept in the app's settings under this key
#define IOSTOUCH_FPS_PERIOD 0.5
#define IOSTOUCH_FPS_DEFAULTS_KEY @"iosTouchShowFps"
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
    KIND_TAPKEY,   // one press of its key when the touch lifts on it
    KIND_HOLDLOAD, // hold IOSTOUCH_QUICKLOAD_HOLD seconds to quick load
    KIND_WHEEL,    // opens the force wheel
    KIND_ITEM,     // uses an inventory item when the touch lifts on it; only shown while the player has it
    KIND_CHAT,     // (MENU tray) opens or closes the typing line for cheats, when the touch lifts on it
    KIND_FPS,      // (MENU tray) shows or hides the FPS readout, when the touch lifts on it
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
// of JUMP, above ALT -- all IOSTOUCH_CLUSTER_GAP apart. FORCE uses the selected
// power for as long as it is held (Force Jump charges, Lightning keeps going).
// These all pass drags through to looking, so a thumb that lands on one while
// aiming keeps aiming. Top left: next weapon, the FORCE WHEEL that picks the
// power, and a button for each usable item while the player has it (field
// light, IR goggles, bacta), each always in its own place. Top right: quick
// save, quick load (hold) and the menu. Holding MENU opens a tray just under
// it: the keyboard, for the typing line (cheats), and FPS, which shows or
// hides a frame rate readout left of QUICK SAVE. ACT is the door/switch key.
enum {
    BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_TRAYFPS, BTN_TRAYKEYS, // MENU's tray, hidden unless it is open
    BTN_COUNT
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
    [BTN_QUICKSAVE] = { "QUICK\nSAVE", KIND_TAPKEY,   SDL_SCANCODE_F9,     22.0f, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD", KIND_HOLDLOAD, -1,                  22.0f, 0 },
    [BTN_MENU]      = { "MENU",        KIND_MENU,     -1,                  22.0f, 0 },
    [BTN_TRAYFPS]   = { "FPS",         KIND_FPS,      -1,                  20.0f, 0 },
    [BTN_TRAYKEYS]  = { "",            KIND_CHAT,     -1,                  20.0f, 0 },
};
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))
typedef char iosTouch_assertButtonCount[(IOSTOUCH_NUM_BUTTONS == BTN_COUNT) ? 1 : -1];

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
    int bFired;           // QUICK LOAD: already loaded for this touch. MENU: the hold is over (tray opened, or slid off)
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
static CGPoint iosTouch_wheelCentre;
static CGFloat iosTouch_wheelR0 = 0.0, iosTouch_wheelR1 = 0.0;
// MENU's tray; Escape held down for a MENU tap (iosTouch_Update calls left);
// the FPS readout
static int iosTouch_bTrayOpen = 0;
static int iosTouch_menuPulse = 0;
static int iosTouch_bShowFps = 0;

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
    CAShapeLayer* loadRing; // QUICK LOAD's hold progress
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
    CGFloat aSliceFont[IOSTOUCH_WHEEL_MAX];   // its label's size, fitted to the slice...
    CGFloat aSliceLabelR[IOSTOUCH_WHEEL_MAX]; // ...how far out it sits...
    CGSize aSliceLabelSize[IOSTOUCH_WHEEL_MAX]; // ...and how big it is
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

        // Rings that fill while QUICK LOAD and MENU are held
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

        // MENU's tray: its two buttons on a dark backing, hidden until a
        // hold on MENU opens it. The keyboard button shows the keyboard symbol.
        trayBack = [[UIView alloc] initWithFrame:CGRectZero];
        trayBack.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
        trayBack.layer.cornerRadius = 26.0;
        trayBack.layer.borderWidth = 1.5;
        trayBack.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        trayBack.userInteractionEnabled = NO;
        trayBack.hidden = YES;
        [self insertSubview:trayBack belowSubview:aButtonViews[BTN_TRAYFPS]];
        aButtonViews[BTN_TRAYFPS].hidden = YES;
        aButtonViews[BTN_TRAYKEYS].hidden = YES;
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

    // FORCE: right of JUMP and above ALT, out of the way of aiming -- on the
    // circle IOSTOUCH_CLUSTER_GAP out from JUMP, as far round towards pointing
    // right as it fits on screen, below the top row and clear of ALT, ACT,
    // FIRE, the gauge and the cutout.
    // Where the cutout (or, on smaller screens, ALT) takes that spot it goes
    // higher, over JUMP; failing that, the gaps shrink.
    {
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* c = &iosTouch_aButtons[BTN_ACT];
        const CGFloat R = f->radius;
        const CGFloat aGap[3] = { IOSTOUCH_CLUSTER_GAP, 20.0, 12.0 };
        // If nothing fits (a very short screen, e.g. with Display Zoom): just
        // outside the arc, IOSTOUCH_CLUSTER_GAP from both DUCK and ACT
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
        for (int pass = 0; pass < 3 && !bFound; pass++) {
            const CGFloat gap = aGap[pass];
            const CGFloat D = j->radius + R + gap;
            for (int deg = 0; deg <= 135; deg++) {
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
                best = p;
                bFound = 1;
                break;
            }
        }
        f->x = best.x;
        f->y = best.y;
    }

    // Top left: NEXT WPN | FORCE WHEEL | LIGHT, IR, BACTA -- each item keeps its
    // own place whether or not the ones before it are showing
    iosTouch_aButtons[BTN_NEXTWPN].x = left + 24;
    iosTouch_aButtons[BTN_WHEEL].x = left + 84;
    iosTouch_aButtons[BTN_LIGHT].x = left + 148;
    iosTouch_aButtons[BTN_IR].x = left + 204;
    iosTouch_aButtons[BTN_BACTA].x = left + 260;
    // Top right: QUICK SAVE, QUICK LOAD, MENU in the corner -- spaced well
    // apart, so a press meant for QUICK LOAD can't land on QUICK SAVE
    iosTouch_aButtons[BTN_QUICKSAVE].x = right - 156;
    iosTouch_aButtons[BTN_QUICKLOAD].x = right - 90;
    iosTouch_aButtons[BTN_MENU].x = right - 24;
    const int aTopRow[] = { BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
                            BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU };
    for (int i = 0; i < (int)(sizeof(aTopRow) / sizeof(aTopRow[0])); i++) {
        iosTouch_aButtons[aTopRow[i]].y = top + 26;
    }
    // MENU's tray: a row just under the corner, the keyboard right below MENU
    // (a held thumb slides straight down onto it) and FPS left of that. Where
    // FORCE sits high in that corner (a smaller screen with the cutout on the
    // right) the row moves left until it is clear of it -- or, if that never
    // happens, to wherever it is furthest from it. (While open, the tray is on
    // top and takes its own touches.)
    {
        iosTouchButton* m = &iosTouch_aButtons[BTN_MENU];
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        const CGFloat ty = m->y + 52;
        CGFloat kx = m->x, bestGap = -1e9;
        for (CGFloat x = m->x; x >= m->x - 120; x -= 4) {
            CGFloat gap = iosTouch_DistToSegment(CGPointMake(f->x, f->y), x - 50, ty, x, ty) - 26 - f->radius;
            if (gap > bestGap) {
                bestGap = gap;
                kx = x;
            }
            if (gap >= 8) break;
        }
        iosTouch_aButtons[BTN_TRAYKEYS].x = kx;
        iosTouch_aButtons[BTN_TRAYKEYS].y = ty;
        iosTouch_aButtons[BTN_TRAYFPS].x = kx - 50;
        iosTouch_aButtons[BTN_TRAYFPS].y = ty;
        trayBack.frame = CGRectMake(kx - 50 - 26, ty - 26, 50 + 52, 52);
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
// The open MENU tray is drawn on top of the rest (on a short screen it can lie
// over FORCE), so its buttons come first.
- (int)buttonAt:(CGPoint)p
{
    int best = -1;
    CGFloat bestDist = 0;
    for (int i = BTN_TRAYFPS; i <= BTN_TRAYKEYS && iosTouch_bTrayOpen; i++) {
        iosTouchButton* b = &iosTouch_aButtons[i];
        CGFloat d = hypot(p.x - b->x, p.y - b->y);
        if (d <= b->radius + 6.0 && (best < 0 || d - b->radius < bestDist)) {
            best = i;
            bestDist = d - b->radius;
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (aButtonViews[i].hidden) continue; // an item the player doesn't have, or the closed tray
        CGFloat dx = p.x - b->x, dy = p.y - b->y;
        CGFloat d = sqrt(dx * dx + dy * dy);
        if (d <= b->radius + 6.0 && (best < 0 || d - b->radius < bestDist)) {
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
        // (on MENU and the tray's keyboard) and the FPS readout being on (on
        // the tray's FPS) stand out in yellow, at full strength
        int bOn = (iosTouch_aButtons[i].kind == KIND_ITEM && aItemActive[i])
                  || ((i == BTN_TRAYKEYS || i == BTN_MENU) && jkHud_bChatOpen)
                  || (i == BTN_TRAYFPS && iosTouch_bShowFps);
        int bTray = i == BTN_TRAYKEYS || i == BTN_TRAYFPS; // only there while the tray is open
        CGFloat alpha = (bHeld || bOn || bWheel || bTray) ? 1.0 : IOSTOUCH_IDLE_ALPHA;
        if (i == BTN_FORCE && bForceLabelSet && !forceLabelName) alpha *= 0.45; // no power to use yet
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
    aButtonViews[BTN_TRAYKEYS].hidden = !bOpen;
    aButtonViews[BTN_TRAYFPS].hidden = !bOpen;
    if (!bOpen) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (!s->touch || s->role != ROLE_BUTTON) continue;
            if (s->button == BTN_TRAYKEYS || s->button == BTN_TRAYFPS) {
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

// A tray button picked: it does its thing, and the tray closes
- (void)useTrayButton:(int)button
{
    if (iosTouch_aButtons[button].kind == KIND_CHAT) {
        iosGame_ToggleChat();
    }
    else if (iosTouch_aButtons[button].kind == KIND_FPS) {
        [self setShowFps:!iosTouch_bShowFps];
    }
    [self setTrayOpen:0];
}

// ------------------------------------------------------------ force wheel

// Fits each slice's label inside it: the biggest size (11pt down to 7pt) at
// which it fits somewhere along the slice, as near the middle of the band as
// it can go at that size. Text width goes with font size, so it is measured
// once.
- (void)fitWheelLabels
{
    const iosTouchWheelMap* m = iosTouch_pWheelMap;
    CGPoint c = iosTouch_wheelCentre;
    CGFloat R0 = iosTouch_wheelR0, R1 = iosTouch_wheelR1;
    for (int i = 0; i < m->numSlices; i++) {
        // two-word names on two lines
        const char* name = iosGame_GetPowerName(m->aSlices[i].bin);
        UILabel* l = aSliceLabels[i];
        l.text = [[NSString stringWithUTF8String:(name ? name : "?")] stringByReplacingOccurrencesOfString:@" " withString:@"\n"];
        l.font = IOSTouch_WheelFont(11.0);
        CGSize s11 = [l sizeThatFits:CGSizeMake(300.0, 300.0)];
        CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
        aSliceFont[i] = 7.0;
        aSliceLabelR[i] = R0 + (R1 - R0) * 0.58;
        aSliceLabelSize[i] = CGSizeMake(s11.width * 7.0 / 11.0, s11.height * 7.0 / 11.0);
        int bFound = 0;
        for (CGFloat size = 11.0; size >= 7.0 && !bFound; size -= 0.5) {
            CGFloat w = s11.width * size / 11.0, h = s11.height * size / 11.0;
            CGFloat bestOff = 0.0;
            for (int k = 0; k <= 20; k++) {
                CGFloat f = 0.30 + 0.50 * k / 20.0;
                CGFloat rr = R0 + (R1 - R0) * f;
                CGPoint p = CGPointMake(c.x + rr * cos(a), c.y - rr * sin(a));
                int bIn = 1;
                for (int sx = -1; sx <= 1 && bIn; sx++) {
                    for (int sy = -1; sy <= 1 && bIn; sy++) {
                        bIn = iosTouch_InWheelSlice(CGPointMake(p.x + sx * w * 0.5, p.y + sy * h * 0.5), m->aSlices[i].deg, 2.0);
                    }
                }
                if (bIn && (!bFound || fabs(f - 0.58) < bestOff)) {
                    bFound = 1;
                    bestOff = fabs(f - 0.58);
                    aSliceFont[i] = size;
                    aSliceLabelR[i] = rr;
                    aSliceLabelSize[i] = CGSizeMake(w, h);
                }
            }
        }
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
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
        UILabel* l = aSliceLabels[i];
        if (i >= m->numSlices) {
            l.hidden = YES;
            continue;
        }
        CGFloat a = m->aSlices[i].deg * M_PI / 180.0;
        l.font = IOSTouch_WheelFont(aSliceFont[i]);
        l.bounds = CGRectMake(0, 0, ceil(aSliceLabelSize[i].width) + 4.0, ceil(aSliceLabelSize[i].height) + 2.0);
        l.center = CGPointMake(c.x + aSliceLabelR[i] * cos(a), c.y - aSliceLabelR[i] * sin(a));
        l.hidden = NO;
    }

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
// the player has learned lit. Every other touch lets go of what it was
// holding and is ignored until it lifts, and the game holds still
// (iosGame_SetHold) until the wheel closes.
- (void)openWheel
{
    [self setTrayOpen:0];
    iosTouch_pWheelMap = iosGame_IsMots() ? &iosTouch_motsWheel : &iosTouch_jkWheel;
    for (int i = 0; i < iosTouch_pWheelMap->numSlices; i++) {
        iosTouch_aWheelEarned[i] = iosGame_IsPowerAvailable(iosTouch_pWheelMap->aSlices[i].bin);
    }
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_BUTTON && iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
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
    iosTouchButton* b = &iosTouch_aButtons[button];
    return hypot(p.x - b->x, p.y - b->y) <= b->radius + 6.0;
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
        if (iosTouch_bTrayOpen && s->button != BTN_TRAYKEYS && s->button != BTN_TRAYFPS) {
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

            if (s->role == ROLE_WHEEL) {
                if (s->bWheelOpener && !iosTouch_bWheelTapMode) [self aimWheel:s at:p];
                else s->wheelSlot = iosTouch_WheelSliceAt(p, s->wheelSlot);
            }
            else if (s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && iosTouch_bTrayOpen) {
                // Held until the tray opened: the tray button under the thumb lights up
                int tb = [self buttonAt:p];
                tb = (tb == BTN_TRAYKEYS || tb == BTN_TRAYFPS) ? tb : -1;
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
                int bWhileTyping = b->kind == KIND_MENU || b->kind == KIND_CHAT || b->kind == KIND_FPS;
                if (!bCancelled && [self isPoint:p onButton:s->button] && (!jkHud_bChatOpen || bWhileTyping)) {
                    if (b->kind == KIND_TAPKEY) {
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (b->kind == KIND_ITEM && !aButtonViews[s->button].hidden
                             // one item at a time: the use key acts on whichever item is selected when it is read
                             && !iosTouch_aPulseQueue[b->scancode] && !iosTouch_aPulseReads[b->scancode]
                             && !iosTouch_aPulseGap[b->scancode]) {
                        iosGame_SelectItem(b->bin);
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (b->kind == KIND_CHAT || b->kind == KIND_FPS) {
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
                    if (tb == BTN_TRAYKEYS || tb == BTN_TRAYFPS) [self useTrayButton:tb];
                }
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

// Once per frame while shown: the QUICK LOAD and MENU holds, the FORCE label
// and its force meter ring, which item buttons show and what they say, and
// the FPS readout
- (void)tick
{
    // The holds go off only once the finger is known to have stayed down long
    // enough: this runs before the frame's touches are delivered, so a lift
    // during a slow frame hasn't arrived yet -- but every one made before the
    // last tick has
    CFTimeInterval now = CACurrentMediaTime();
    CFTimeInterval known = lastTickTime;
    lastTickTime = now;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON) continue;
        if (s->button == BTN_QUICKLOAD && !s->bFired) {
            CGFloat progress = (CGFloat)((now - s->tDown) / IOSTOUCH_QUICKLOAD_HOLD);
            if (known - s->tDown >= IOSTOUCH_QUICKLOAD_HOLD) {
                s->bFired = 1;
                [self setRing:loadRing progress:0.0];
                iosGame_QuickLoad();
            }
            else {
                [self setRing:loadRing progress:MIN(progress, 1.0)];
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
    iosTouch_menuPulse = 0;
    bFpsBase = 0; // time spent in a menu doesn't count
    [self hideStick];
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

static void iosTouch_UpdateInPool(void)
{
    // A MENU tap holds Escape down for a couple of updates -- the game acts on
    // it once, when it first sees it (Window_SdlUpdate) -- then lets it go
    if (iosTouch_menuPulse > 0 && --iosTouch_menuPulse == 0) stdControl_bControllerEscapeKey = 0;

    // Not over a GUI menu either -- e.g. the objectives screen at level start
    // waits for Ok while gameplay controls are already active.
    int bWant = stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive();

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

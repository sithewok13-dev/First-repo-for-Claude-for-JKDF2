// Added: entire file. See touchUi.h. The layout, the numbers and the
// gestures are iosTouch.m's (the iOS overlay), so the two feel the same; the
// comments there say why each one is as it is. Here it is plain C: touches
// in, keys, look, game actions, haptic requests and a draw list out.

#include "Platform/Touch/touchUi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TU_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TU_MAX(a, b) ((a) > (b) ? (a) : (b))
#define TU_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// ------------------------------------------------------------ the numbers (iosTouch.m's)

#define TU_LOOK_SCALE_X 1.6f
#define TU_LOOK_SCALE_Y 1.3f
#define TU_STICK_ZONE 0.42f
#define TU_STICK_RADIUS 60.0f
#define TU_STICK_KNOB 24.0f
#define TU_STICK_DEADZONE 0.30f
#define TU_RUN_R 15.0f
#define TU_RUN_DIST 100.0f
#define TU_RUN_MIN_DIST 76.0f
#define TU_RUN_ON 70.0f
#define TU_RUN_OFF 60.0f
#define TU_RUN_CONE 30.0f
#define TU_RUN_CONE_OFF 40.0f
#define TU_IDLE_ALPHA 0.65f
#define TU_CLUSTER_GAP 30.0f
#define TU_TOUCH_SLOP 6.0f
#define TU_WHEEL_R1_MAX 168.0f
#define TU_WHEEL_R0_FRAC 0.37f
#define TU_WHEEL_POP 10.0f
#define TU_WHEEL_OPEN_SLIDE 10.0f
#define TU_WHEEL_HOLD 0.3
#define TU_WHEEL_DEAD 14.0f
#define TU_WHEEL_LEASH 60.0f
#define TU_WHEEL_HYST 4.0f
#define TU_WHEEL_CANCEL (-2)
#define TU_WHEEL_STARS 4
#define TU_WHEEL_STAR_CELL 8.0f
#define TU_WHEEL_STAR_STEP 9.0f
#define TU_WHEEL_STAR_GAP 2.0f
#define TU_WHEEL_STAR_R 4.3f
#define TU_WHEEL_STAR_INNER 0.45f
#define TU_WHEEL_STAR_LINE 0.9f
#define TU_WHEEL_STAR_EDGE 0.2f
#define TU_WHEEL_STAR_MIN 0.6f
#define TU_WHEEL_AMMO_FONT 9.0f
#define TU_WHEEL_AMMO_MIN 0.7f
#define TU_WHEEL_AMMO_INK 0.705f
#define TU_WHEEL_TITLE_MIN 0.6f
#define TU_QUICKSAVE_HOLD 0.3
#define TU_QUICKLOAD_HOLD 0.3
#define TU_MENU_HOLD 0.45
#define TU_MENU_PULSE_FRAMES 2
#define TU_TRAY_STEP 50.0f
#define TU_TRAY_CLEAR 8.0f
#define TU_FPS_PERIOD 0.5
#define TU_TOPTEXT_GAP 6.0f
#define TU_TOPTEXT_DEPTH 70.0f
#define TU_TOPTEXT_SIDE 8.0f
#define TU_FPS_LABEL_X 216.0f
#define TU_FPS_LABEL_HALF 27.0f
#define TU_BURST_LABEL_HALF 15.0f
#define TU_CHEAT_HOLD 0.5
#define TU_BACTA_HOLD 0.3
#define TU_FLY_HOLD 0.5
#define TU_FLY_TAP 0.25
#define TU_FLY_DOUBLE 0.35
#define TU_FLY_STILL 10.0f
#define TU_LEVEL_STEP 0.4
#define TU_FORCEJUMP_BIN 21
#define TU_JUMP_STEP_SECS 0.35
#define TU_JUMP_SMOOTH 0
#define TU_JUMP_STEPS 1
#define TU_JUMP_CLASSIC 2
#define TU_JUMP_NUM_STYLES 3
#define TU_GYRO_YAW_RELAX 1.41
#define TU_GYRO_SMOOTH 4.0
#define TU_GYRO_SMOOTH_N 12
#define TU_GYRO_SOFT 1.5
#define TU_GYRO_MAX_DT 0.05
#define TU_GYRO_MAX_FRAME 0.5
#define TU_GYRO_TURN_HOLD 0.35
#define TU_GYRO_DEFAULT_SENS 1
#define TU_PULSE_READS 2
#define TU_MAX_TOUCHES 10
#define TU_NUM_SCANCODES 512
// On Android a cutout or a system bar shows as an inset on its side; the
// buttons round FIRE keep out of the whole inset strip on their side when it
// is at least this wide (iOS: the notch's capsule)
#define TU_CUTOUT_MIN_INSET 24.0f

// SDL scancodes (SDL_scancode.h; the core doesn't include SDL)
enum {
    TU_SC_A = 4, TU_SC_C = 6, TU_SC_D = 7, TU_SC_E = 8, TU_SC_F = 9, TU_SC_G = 10, TU_SC_S = 22, TU_SC_W = 26,
    TU_SC_X = 27, TU_SC_Z = 29, TU_SC_RETURN = 40, TU_SC_SPACE = 44, TU_SC_F9 = 66, TU_SC_LCTRL = 224, TU_SC_LSHIFT = 225
};

enum { ROLE_NONE = 0, ROLE_STICK, ROLE_LOOK, ROLE_BUTTON, ROLE_WHEEL, ROLE_IGNORED };

enum {
    KIND_KEY = 0, KIND_MENU, KIND_HOLDSAVE, KIND_HOLDLOAD, KIND_WHEEL, KIND_ITEM, KIND_CHAT, KIND_FPS, KIND_GYRO,
    KIND_GYROMODE, KIND_GYROSENS, KIND_CAMSWITCH, KIND_CHEAT, KIND_CHEATSWITCH, KIND_VIEW, KIND_VIEWSWITCH,
    KIND_CONTROLS, KIND_JUMPSTYLE, KIND_CAMERA,
};

#define BIN_BACTATANK 40
#define BIN_IRGOGGLES 41
#define BIN_FIELDLIGHT 42

enum {
    BTN_FIRE = TOUCHUI_BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_CAMERA, BTN_SABER, BTN_CHEAT, BTN_VIEW,
    BTN_TRAYGYRO, BTN_TRAYFPS, BTN_TRAYCAM, BTN_TRAYCHEAT, BTN_TRAYVIEW, BTN_TRAYJUMP, BTN_TRAYCTRL, BTN_TRAYKEYS,
    BTN_GYROOPT_MODE, BTN_GYROOPT_SENS,
    BTN_COUNT,
    BTN_TRAY_FIRST = BTN_TRAYGYRO, BTN_TRAY_LAST = BTN_TRAYKEYS,
    BTN_GYROOPT_FIRST = BTN_GYROOPT_MODE, BTN_GYROOPT_LAST = BTN_GYROOPT_SENS
};
typedef char tu_assertBtnCount[((int)BTN_COUNT == (int)TOUCHUI_BTN_COUNT) ? 1 : -1];

typedef struct {
    const char* label;
    int kind;
    int scancode;
    float radius;
    int bLookWhileHeld;
    int bin;
    float x, y;
} tuButton;

static tuButton tu_aButtons[BTN_COUNT] = {
    [BTN_FIRE]      = { "FIRE",         KIND_KEY,      TU_SC_LCTRL,  42.0f, 1, 0 },
    [BTN_ALT]       = { "ALT",          KIND_KEY,      TU_SC_Z,      28.0f, 1, 0 },
    [BTN_DUCK]      = { "DUCK",         KIND_KEY,      TU_SC_C,      29.0f, 1, 0 },
    [BTN_ACT]       = { "ACT",          KIND_KEY,      TU_SC_SPACE,  29.0f, 1, 0 },
    [BTN_JUMP]      = { "JUMP",         KIND_KEY,      TU_SC_X,      31.0f, 1, 0 },
    [BTN_FORCE]     = { "FORCE",        KIND_KEY,      TU_SC_F,      30.0f, 1, 0 },
    [BTN_NEXTWPN]   = { "NEXT\nWPN",    KIND_WHEEL,    TU_SC_G,      22.0f, 0, 0 },
    [BTN_WHEEL]     = { "FORCE\nWHEEL", KIND_WHEEL,    TU_SC_E,      22.0f, 0, 0 },
    [BTN_LIGHT]     = { "LIGHT",        KIND_ITEM,     TU_SC_RETURN, 22.0f, 0, BIN_FIELDLIGHT },
    [BTN_IR]        = { "IR",           KIND_ITEM,     TU_SC_RETURN, 22.0f, 0, BIN_IRGOGGLES },
    [BTN_BACTA]     = { "BACTA",        KIND_ITEM,     TU_SC_RETURN, 22.0f, 0, BIN_BACTATANK },
    [BTN_QUICKSAVE] = { "QUICK\nSAVE",  KIND_HOLDSAVE, TU_SC_F9,     22.0f, 0, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD",  KIND_HOLDLOAD, -1,           22.0f, 0, 0 },
    [BTN_MENU]      = { "MENU",         KIND_MENU,     -1,           22.0f, 0, 0 },
    [BTN_CAMERA]    = { "",             KIND_CAMERA,   -1,           22.0f, 0, 0 },
    [BTN_SABER]     = { "",             KIND_WHEEL,    -1,           22.0f, 0, 0 },
    [BTN_CHEAT]     = { "",             KIND_CHEAT,    -1,           22.0f, 0, 0 },
    [BTN_VIEW]      = { "1P",           KIND_VIEW,     -1,           22.0f, 0, 0 },
    [BTN_TRAYGYRO]  = { "GYRO",         KIND_GYRO,     -1,           20.0f, 0, 0 },
    [BTN_TRAYFPS]   = { "FPS",          KIND_FPS,      -1,           20.0f, 0, 0 },
    [BTN_TRAYCAM]   = { "",             KIND_CAMSWITCH, -1,          20.0f, 0, 0 },
    [BTN_TRAYCHEAT] = { "CHEAT",        KIND_CHEATSWITCH, -1,        20.0f, 0, 0 },
    [BTN_TRAYVIEW]  = { "VIEW",         KIND_VIEWSWITCH, -1,         20.0f, 0, 0 },
    [BTN_TRAYJUMP]  = { "JUMP",         KIND_JUMPSTYLE, -1,          20.0f, 0, 0 },
    [BTN_TRAYCTRL]  = { "CONTROLS",     KIND_CONTROLS, -1,           20.0f, 0, 0 },
    [BTN_TRAYKEYS]  = { "",             KIND_CHAT,     -1,           20.0f, 0, 0 },
    [BTN_GYROOPT_MODE] = { "MODE",      KIND_GYROMODE, -1,           20.0f, 0, 0 },
    [BTN_GYROOPT_SENS] = { "SENS",      KIND_GYROSENS, -1,           20.0f, 0, 0 },
};

static int tu_IsTrayButton(int b) { return b >= BTN_TRAY_FIRST && b <= BTN_TRAY_LAST; }
static int tu_IsOptButton(int b) { return b >= BTN_GYROOPT_FIRST && b <= BTN_GYROOPT_LAST; }
static int tu_IsQuickHold(int b) { return b == BTN_QUICKSAVE || b == BTN_QUICKLOAD; }

// ------------------------------------------------------------ wheel maps (iosTouch.m's)

#define TU_WHEEL_MAX 17
#define TU_WHEEL_MAX_GROUPS 5
typedef struct { int bin; int group; float deg; } tuWheelSlice;
typedef struct { const char* title; float titleDeg; unsigned char r, g, b; } tuWheelGroup;
typedef struct {
    const tuWheelSlice* aSlices;
    int numSlices;
    const tuWheelGroup* aGroups;
    int numGroups;
    float sliceDeg;
    float gapDeg;
} tuWheelMap;

static const tuWheelGroup tu_aJkWheelGroups[] = {
    { "LIGHT\nSIDE", 150.0f, 120, 200, 255 },
    { NULL,           90.0f, 255, 214, 120 },
    { "DARK\nSIDE",   30.0f, 255, 105,  90 },
};
static const tuWheelSlice tu_aJkWheelSlices[] = {
    { 29, 0, 239.0f }, { 28, 0, 217.0f }, { 27, 0, 195.0f }, { 26, 0, 173.0f }, { 25, 0, 151.0f },
    { 21, 1, 123.0f }, { 22, 1, 101.0f }, { 23, 1,  79.0f }, { 24, 1,  57.0f },
    { 30, 2,  29.0f }, { 31, 2,   7.0f }, { 32, 2, -15.0f }, { 33, 2, -37.0f }, { 34, 2, -59.0f },
};
static const tuWheelGroup tu_aMotsWheelGroups[] = {
    { "TIER I",   207.0f, 255, 214, 120 },
    { "TIER II",  120.0f, 110, 220, 170 },
    { "TIER III",  42.0f, 120, 200, 255 },
    { "TIER IV",  -36.0f, 190, 150, 255 },
};
static const tuWheelSlice tu_aMotsWheelSlices[] = {
    { 38, 0, 243.0f }, { 22, 0, 225.0f }, { 23, 0, 207.0f }, { 36, 0, 189.0f }, { 21, 0, 171.0f },
    { 24, 1, 147.0f }, { 31, 1, 129.0f }, { 35, 1, 111.0f }, { 37, 1,  93.0f },
    { 39, 2,  69.0f }, { 25, 2,  51.0f }, { 27, 2,  33.0f }, { 26, 2,  15.0f },
    { 28, 3,  -9.0f }, { 33, 3, -27.0f }, { 29, 3, -45.0f }, { 34, 3, -63.0f },
};
static const tuWheelGroup tu_aWeaponGroups[] = {
    { NULL, 0.0f, 255, 214, 120 },
    { NULL, 0.0f, 120, 200, 255 },
    { NULL, 0.0f, 110, 220, 170 },
    { NULL, 0.0f, 255, 105,  90 },
    { NULL, 0.0f, 190, 150, 255 },
};
static const tuWheelSlice tu_aJkWeaponSlices[] = {
    {  1, 0, 234.0f }, {  2, 1, 202.0f }, {  3, 1, 170.0f }, {  4, 3, 138.0f }, {  5, 2, 106.0f },
    {  6, 2,  74.0f }, {  7, 3,  42.0f }, {  8, 3,  10.0f }, {  9, 2, -22.0f }, { 10, 0, -54.0f },
};
static const tuWheelSlice tu_aMotsWeaponSlices[] = {
    { 121, 0, 242.0f }, { 131, 0, 223.0f }, { 122, 1, 204.0f }, { 132, 1, 185.0f }, { 123, 1, 166.0f }, { 133, 1, 147.0f },
    { 124, 3, 128.0f }, { 134, 3, 109.0f }, { 135, 2,  90.0f }, { 126, 2,  71.0f }, { 127, 3,  52.0f }, { 137, 3,  33.0f },
    { 128, 3,  14.0f }, { 138, 3,  -5.0f }, { 129, 2, -24.0f }, { 130, 4, -43.0f }, { 140, 4, -62.0f },
};
static const tuWheelMap tu_jkWheel = { tu_aJkWheelSlices, TU_COUNT(tu_aJkWheelSlices), tu_aJkWheelGroups, TU_COUNT(tu_aJkWheelGroups), 22.0f, 6.0f };
static const tuWheelMap tu_motsWheel = { tu_aMotsWheelSlices, TU_COUNT(tu_aMotsWheelSlices), tu_aMotsWheelGroups, TU_COUNT(tu_aMotsWheelGroups), 18.0f, 6.0f };
static const tuWheelMap tu_jkWeaponWheel = { tu_aJkWeaponSlices, TU_COUNT(tu_aJkWeaponSlices), tu_aWeaponGroups, TU_COUNT(tu_aWeaponGroups), 32.0f, 0.0f };
static const tuWheelMap tu_motsWeaponWheel = { tu_aMotsWeaponSlices, TU_COUNT(tu_aMotsWeaponSlices), tu_aWeaponGroups, TU_COUNT(tu_aWeaponGroups), 19.0f, 0.0f };

// A wheel's labels fitted to its slices: [0] alone, [1] with the row under
// it (stars, ammo). Kept for each wheel until its size changes.
typedef struct {
    const tuWheelMap* map;
    float R1;
    float rowScale;
    float font[TU_WHEEL_MAX][2];
    float labelR[TU_WHEEL_MAX][2];
    float labelW[TU_WHEEL_MAX][2], labelH[TU_WHEEL_MAX][2];
} tuWheelFit;

// ------------------------------------------------------------ state

typedef struct {
    int bUsed;
    long long id;
    int role;
    int button;
    float ox, oy;       // where it went down
    float lx, ly;       // where it is
    double tDown;       // by the event's clock
    int bFired;
    int bSeen;
    int trayButton;
    int wheelSlot;
    int bWheelOpener;
    int bWheelHeld;
    int bArmed;
    float wox, woy;
    float wheelAim;
    int bFlyDone;
    int levelPick;
} tuSlot;

static struct {
    const touchUiGame* game;
    char settingsPath[1024];
    float W, H, inL, inT, inR, inB;
    int bLaidOut;
    int bShown;
    int bOtherInput;

    tuSlot aSlots[TU_MAX_TOUCHES];
    int aButtonHeld[BTN_COUNT];
    unsigned char aKeyDown[TU_NUM_SCANCODES];
    unsigned char aPulseQueue[TU_NUM_SCANCODES];
    unsigned char aPulseReads[TU_NUM_SCANCODES];
    unsigned char aPulseGap[TU_NUM_SCANCODES];
    float lookX, lookY;
    float stickX, stickY;
    int bStickActive;
    float stickOx, stickOy;
    int bAlwaysRun;
    int bRunMarker;
    float runX, runY, runDeg;
    int bStickRunning;
    int menuPulse;
    int haptics;
    int levelToLoad;
    double tJumpTapUp;

    // layout
    int bLayoutGauge;
    float aLayoutGauge[4];
    int bTopText;
    float aTopText[3];
    int bFpsBottom;
    float fpsX, fpsY;
    float trayBack[4], gyroRowBack[4];
    // MENU's tray and its gyro row
    int bTrayOpen;
    int bGyroRow;
    // settings
    int bShowFps, bShowCheat, bShowView, bThirdPerson, bSwapped, jumpStyle;
    int gyroMode, gyroLastMode, gyroSens;
    // what the buttons show
    int bCheatOn;
    const char* forceName;
    int bForceNameSet;
    int aItemHave[BTN_COUNT], aItemAmount[BTN_COUNT], aItemActive[BTN_COUNT];
    float forceFrac;
    int bForceFull;
    double tForceFull;
    float aRing[BTN_COUNT];  // the hold rings' progress
    float fjumpRing;
    int jumpTicked;
    int bChatWasOpen;
    // FPS
    int bFpsBase;
    unsigned int fpsBaseFrames;
    double fpsBaseTime;
    char fpsText[16];
    double lastNow;

    // the wheel
    int bWheelOpen, bWheelTapMode, wheelKind, wheelButton;
    const tuWheelMap* pWheelMap;
    int aWheelEarned[TU_WHEEL_MAX], aWheelLevel[TU_WHEEL_MAX], aWheelOwned[TU_WHEEL_MAX], aWheelAmmo[TU_WHEEL_MAX];
    float wheelCx, wheelCy, wheelR0, wheelR1;
    int wheelLastHot;
    tuWheelFit aWheelFit[2];

    // gyro
    int bGyroAvailable;
    int bGyroWasWanted;
    int gyroSide, gyroLastSide;
    double gyroTurnedRound;
    double gyroYaw, gyroPitch, gyroLastT;
    double aGyroSmooth[TU_GYRO_SMOOTH_N][2];
    int gyroSmoothNext;
    int bGyroWasOk;
    double gyroLastFrame;
    float gyroCountX, gyroCountY;
} tu;

static const float tu_aGyroSens[] = { 1.0f, 1.5f, 2.0f, 3.0f };

static void tu_Layout(void);
static void tu_RecomputeKeys(void);
static void tu_CloseWheelSelecting(int slice);

// ------------------------------------------------------------ the game, safely

#define G(fn) (tu.game && tu.game->fn)
static int tu_IsMots(void) { return G(IsMots) ? tu.game->IsMots() : 0; }
static int tu_IsChatOpen(void) { return G(IsChatOpen) ? tu.game->IsChatOpen() : 0; }
static int tu_IsHolding(void) { return G(IsHolding) ? tu.game->IsHolding() : 0; }
static void tu_SetHold(int b) { if (G(SetHold)) tu.game->SetHold(b); }
static int tu_IsCheatOn(void) { return G(IsSithEwokOn) ? tu.game->IsSithEwokOn() : 0; }

static void tu_Haptic(int kind) { tu.haptics |= kind; }

// ------------------------------------------------------------ settings

static void tu_SaveSettings(void)
{
    if (!tu.settingsPath[0]) return;
    FILE* f = fopen(tu.settingsPath, "w");
    if (!f) return;
    fprintf(f, "gyroMode=%d\ngyroLastMode=%d\ngyroSens=%.1f\nshowFps=%d\nshowCheat=%d\nshowView=%d\nthirdPerson=%d\njumpStyle=%d\nswapped=%d\n",
            tu.gyroMode, tu.gyroLastMode, (double)tu_aGyroSens[tu.gyroSens], tu.bShowFps, tu.bShowCheat, tu.bShowView,
            tu.bThirdPerson, tu.jumpStyle, tu.bSwapped);
    fclose(f);
}

static void tu_LoadSettings(void)
{
    tu.gyroMode = TOUCHUI_GYRO_OFF;
    tu.gyroLastMode = TOUCHUI_GYRO_TOUCH;
    tu.gyroSens = TU_GYRO_DEFAULT_SENS;
    tu.bShowFps = tu.bShowCheat = tu.bShowView = tu.bThirdPerson = tu.bSwapped = 0;
    tu.jumpStyle = TU_JUMP_STEPS;
    if (!tu.settingsPath[0]) return;
    FILE* f = fopen(tu.settingsPath, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line;
        double v = atof(eq + 1);
        int n = (int)v;
        if (!strcmp(k, "gyroMode")) tu.gyroMode = (n >= 0 && n <= TOUCHUI_GYRO_ALWAYS) ? n : TOUCHUI_GYRO_OFF;
        else if (!strcmp(k, "gyroLastMode")) tu.gyroLastMode = (n == TOUCHUI_GYRO_ALWAYS) ? TOUCHUI_GYRO_ALWAYS : TOUCHUI_GYRO_TOUCH;
        else if (!strcmp(k, "gyroSens")) {
            for (int i = 0; i < TU_COUNT(tu_aGyroSens); i++)
                if (fabs(tu_aGyroSens[i] - v) < fabs(tu_aGyroSens[tu.gyroSens] - v)) tu.gyroSens = i;
        }
        else if (!strcmp(k, "showFps")) tu.bShowFps = n != 0;
        else if (!strcmp(k, "showCheat")) tu.bShowCheat = n != 0;
        else if (!strcmp(k, "showView")) tu.bShowView = n != 0;
        else if (!strcmp(k, "thirdPerson")) tu.bThirdPerson = n != 0;
        else if (!strcmp(k, "jumpStyle")) tu.jumpStyle = (n >= 0 && n < TU_JUMP_NUM_STYLES) ? n : TU_JUMP_STEPS;
        else if (!strcmp(k, "swapped")) tu.bSwapped = n != 0;
    }
    fclose(f);
    if (tu.gyroMode != TOUCHUI_GYRO_OFF) tu.gyroLastMode = tu.gyroMode;
}

// ------------------------------------------------------------ helpers

static float tu_DistToSegment(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return sqrtf(dx * dx + dy * dy);
}

static float tu_AngleDiff(float a, float b)
{
    float d = fmodf(a - b, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

static void tu_QueuePress(int scancode)
{
    if (scancode < 0 || scancode >= TU_NUM_SCANCODES) return;
    if (tu.aPulseQueue[scancode] < 255) tu.aPulseQueue[scancode]++;
}

// Whether a button is on show now
static int tu_Hidden(int i)
{
    const tuButton* b = &tu_aButtons[i];
    if (i == BTN_CAMERA || i == BTN_SABER) return 1; // (not in this round)
    if (b->kind == KIND_ITEM) return !tu.aItemHave[i];
    if (tu_IsTrayButton(i)) return !tu.bTrayOpen;
    if (tu_IsOptButton(i)) return !tu.bGyroRow;
    if (i == BTN_CHEAT) return !tu.bShowCheat;
    if (i == BTN_VIEW) return !tu.bShowView;
    return 0;
}

static int tu_IsWheelPress(const tuSlot* s)
{
    return s->bUsed && s->role == ROLE_BUTTON && tu_aButtons[s->button].kind == KIND_WHEEL;
}

static int tu_WheelKindOf(int button)
{
    return button == BTN_NEXTWPN ? TOUCHUI_WHEEL_WEAPONS : TOUCHUI_WHEEL_FORCE;
}

static const char* tu_WheelSlotName(int bin)
{
    if (tu.wheelKind == TOUCHUI_WHEEL_WEAPONS) return G(GetWeaponName) ? tu.game->GetWeaponName(bin) : NULL;
    return G(GetPowerName) ? tu.game->GetPowerName(bin) : NULL;
}

static int tu_GyroAvailable(void) { return tu.bGyroAvailable; }

static void tu_RecomputeKeys(void)
{
    memset(tu.aKeyDown, 0, sizeof(tu.aKeyDown));
    if (tu.bWheelOpen) return;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (!tu.aButtonHeld[i]) continue;
        const tuButton* b = &tu_aButtons[i];
        if (b->kind == KIND_KEY && b->scancode >= 0) tu.aKeyDown[b->scancode] = 1;
    }
    if (tu.bStickActive && tu.bStickRunning) {
        tu.aKeyDown[TU_SC_W] = 1;
        tu.aKeyDown[TU_SC_LSHIFT] = 1;
    }
    else if (tu.bStickActive) {
        if (tu.stickY < -TU_STICK_DEADZONE) tu.aKeyDown[TU_SC_W] = 1;
        if (tu.stickY > TU_STICK_DEADZONE) tu.aKeyDown[TU_SC_S] = 1;
        if (tu.stickX < -TU_STICK_DEADZONE) tu.aKeyDown[TU_SC_A] = 1;
        if (tu.stickX > TU_STICK_DEADZONE) tu.aKeyDown[TU_SC_D] = 1;
    }
}

// ------------------------------------------------------------ layout

static int tu_LayoutGaugeRect(float W, float g[4])
{
    if (!G(GetGaugeRect)) return 0;
    if (!tu.bSwapped) return tu.game->GetGaugeRect(0, &g[0], &g[1], &g[2], &g[3]);
    float l[4] = { 0, 0, 0, 0 };
    if (!tu.game->GetGaugeRect(1, &l[0], &l[1], &l[2], &l[3])) return 0;
    g[0] = W - l[2]; g[1] = l[1]; g[2] = W - l[0]; g[3] = l[3];
    return 1;
}

// iosTouch_TrayKeysAt: where MENU's tray goes, clear of the buttons round FIRE
static void tu_TrayKeysAt(float mx, float my, float left, const float* pForce, float* pKx, float* pKy, float* pClear, float* pForceClear)
{
    const float ty = my + 52;
    const float len = TU_TRAY_STEP * (BTN_TRAY_LAST - BTN_TRAY_FIRST);
    float kx = mx, bestClear = -1e9f, bestForce = 1e9f;
    for (float x = mx; ; x -= 4) {
        float clear = 1e9f, forceClear = 1e9f;
        for (int i = BTN_FIRE; i <= BTN_FORCE; i++) {
            const tuButton* c = &tu_aButtons[i];
            float px = c->x, py = c->y;
            if (i == BTN_FORCE) {
                if (!pForce) continue;
                px = pForce[0];
                py = pForce[1];
            }
            float d = tu_DistToSegment(px, py, x - len, ty, x, ty) - 26 - c->radius;
            clear = TU_MIN(clear, d);
            if (i == BTN_FORCE) forceClear = d;
        }
        if (clear > bestClear) {
            bestClear = clear;
            bestForce = forceClear;
            kx = x;
        }
        if (clear >= TU_TRAY_CLEAR) break;
        if (x - 4 - len - 26 < left) break;
    }
    if (pClear) *pClear = bestClear;
    if (pForceClear) *pForceClear = bestForce;
    *pKx = kx;
    *pKy = ty;
}

static void tu_Layout(void)
{
    const float W = tu.W, H = tu.H;
    if (W <= 0 || H <= 0) return;
    const float left = TU_MAX(tu.inL, 8.0f);
    const float right = W - TU_MAX(tu.inR, 8.0f);
    const float top = TU_MAX(tu.inT, 8.0f);
    const float bottom = H - TU_MAX(tu.inB, 8.0f);

    float g[4] = { 0, 0, 0, 0 };
    tu.bLayoutGauge = tu_LayoutGaugeRect(W, g);
    memcpy(tu.aLayoutGauge, g, sizeof(g));

    const float FR = tu_aButtons[BTN_FIRE].radius;
    float fireX = right - 76;
    if (tu.bLayoutGauge && fireX + FR + 4 > g[0]) fireX = g[0] - FR - 4;
    const float fx = fireX, fy = bottom - 58;
    tu_aButtons[BTN_FIRE].x = fx;
    tu_aButtons[BTN_FIRE].y = fy;

    {
        const float arc = 115.0f;
        const int aArcBtn[3] = { BTN_DUCK, BTN_ACT, BTN_JUMP };
        const float aArcDeg[3] = { 180.0f, 135.0f, 90.0f };
        for (int i = 0; i < 3; i++) {
            float rad = aArcDeg[i] * (float)M_PI / 180.0f;
            tu_aButtons[aArcBtn[i]].x = fx + arc * cosf(rad);
            tu_aButtons[aArcBtn[i]].y = fy - arc * sinf(rad);
        }
    }

    // The cutout or a system bar on the cluster's side (swapped: the left,
    // mirrored, as the cluster is laid out on the right and then mirrored):
    // its whole inset strip, as a capsule down that edge
    const float sideInset = tu.bSwapped ? tu.inL : tu.inR;
    const int bCutout = sideInset >= TU_CUTOUT_MIN_INSET;
    const float cutR = sideInset * 0.5f;
    const float cutX = W - cutR;
    const float cutHalf = H * 0.5f;

    // ALT
    {
        tuButton* a = &tu_aButtons[BTN_ALT];
        tuButton* j = &tu_aButtons[BTN_JUMP];
        const float AR = a->radius;
        const float D = FR + AR + TU_CLUSTER_GAP;
        float bx = fx + D * cosf((float)M_PI * 35.0f / 180.0f), by = fy - D * sinf((float)M_PI * 35.0f / 180.0f);
        int bFound = 0;
        for (int pass = 0; pass < 2 && !bFound; pass++) {
            const float jumpGap = pass ? 2.0f : 8.0f;
            for (int extra = 0; extra <= 80 && !bFound; extra += 2) {
                for (int deg = 10; deg <= 85; deg++) {
                    float rad = deg * (float)M_PI / 180.0f;
                    float px = fx + (D + extra) * cosf(rad), py = fy - (D + extra) * sinf(rad);
                    if (px + AR > W - 4 || py - AR < top + 60) continue;
                    if (tu.bLayoutGauge && py + AR > g[1] - 4) continue;
                    float dj = sqrtf((px - j->x) * (px - j->x) + (py - j->y) * (py - j->y));
                    if (dj < AR + j->radius + jumpGap) continue;
                    if (bCutout && tu_DistToSegment(px, py, cutX, H * 0.5f - cutHalf, cutX, H * 0.5f + cutHalf) < AR + cutR + 2) continue;
                    bx = px;
                    by = py;
                    bFound = 1;
                    break;
                }
            }
        }
        if (!bFound && tu.bLayoutGauge && by + AR > g[1] - 4) by = TU_MAX(g[1] - 4 - AR, top + 60 + AR);
        a->x = bx;
        a->y = by;
    }

    tu_aButtons[BTN_NEXTWPN].x = left + 24;
    tu_aButtons[BTN_WHEEL].x = left + 84;
    tu_aButtons[BTN_LIGHT].x = left + 148;
    tu_aButtons[BTN_IR].x = left + 204;
    tu_aButtons[BTN_BACTA].x = left + 260;
    tu_aButtons[BTN_QUICKSAVE].x = right - 156;
    tu_aButtons[BTN_QUICKLOAD].x = right - 90;
    tu_aButtons[BTN_MENU].x = right - 24;
    tu_aButtons[BTN_CAMERA].x = W * 0.5f;
    // SABER, CHEAT and VIEW between CAMERA and QUICK SAVE (iosTouch.m's
    // layoutSubviews: CAMERA's place and the burst's count are kept, so the
    // buttons sit where they do on iOS)
    tu.bFpsBottom = 0;
    {
        tuButton* cam = &tu_aButtons[BTN_CAMERA];
        tuButton* aMid[3] = { &tu_aButtons[BTN_SABER], &tu_aButtons[BTN_CHEAT], &tu_aButtons[BTN_VIEW] };
        const int aShow[3] = { 0, tu.bShowCheat, tu.bShowView };
        const int n = TU_MAX(aShow[0] + aShow[1] + aShow[2], 1);
        const float R = aMid[0]->radius, minGap = (n > 1) ? 8.0f : 4.0f;
        const float qsLeft = tu_aButtons[BTN_QUICKSAVE].x - tu_aButtons[BTN_QUICKSAVE].radius;
        {
            const float need = n * 2.0f * R + (n + 1) * minGap;
            float leftRight = tu_aButtons[BTN_WHEEL].x + tu_aButtons[BTN_WHEEL].radius;
            const int aItem[3] = { BTN_LIGHT, BTN_IR, BTN_BACTA };
            for (int i = 0; i < 3; i++)
                if (!tu_Hidden(aItem[i])) leftRight = TU_MAX(leftRight, tu_aButtons[aItem[i]].x + tu_aButtons[aItem[i]].radius);
            if (qsLeft - (cam->x + cam->radius) < need)
                cam->x = TU_MAX(qsLeft - need - cam->radius, leftRight + 10.0f + cam->radius);
        }
        const float camEdge = cam->x + cam->radius;
        const float burstEdge = camEdge + 20.0f + TU_BURST_LABEL_HALF;
        const float fpsEdge = right - TU_FPS_LABEL_X - TU_FPS_LABEL_HALF;
        const float qsEdge = tu_aButtons[BTN_QUICKSAVE].x - tu_aButtons[BTN_QUICKSAVE].radius;
        const float aLo[4] = { burstEdge, camEdge, burstEdge, camEdge };
        const float aHi[4] = { fpsEdge, fpsEdge, qsEdge, qsEdge };
        int k = 0;
        while (k < 3 && (aHi[k] - aLo[k] - n * 2.0f * R) / (n + 1) < minGap) k++;
        const int bAny = aShow[0] || aShow[1] || aShow[2];
        tu.bFpsBottom = bAny && k >= 2;
        const float Gp = (aHi[k] - aLo[k] - n * 2.0f * R) / (n + 1);
        int slot = 0;
        for (int b = 0; b < 3; b++) {
            const int i = aShow[b] ? slot++ : 0;
            aMid[b]->x = aLo[k] + Gp * (i + 1) + R * (2 * i + 1);
        }
    }
    static const int aTopRow[] = { BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
                                   BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU, BTN_CAMERA, BTN_SABER, BTN_CHEAT, BTN_VIEW };
    for (int i = 0; i < TU_COUNT(aTopRow); i++) tu_aButtons[aTopRow[i]].y = top + 26;

    // FORCE
    {
        tuButton* f = &tu_aButtons[BTN_FORCE];
        tuButton* j = &tu_aButtons[BTN_JUMP];
        tuButton* a = &tu_aButtons[BTN_ALT];
        tuButton* c = &tu_aButtons[BTN_ACT];
        const float R = f->radius;
        const float aGap[] = { TU_CLUSTER_GAP, 20.0f, 12.0f };
        const int aMaxDeg[] = { 135, 135, 180 };
        const float topGap = TU_TOUCH_SLOP + TU_TOUCH_SLOP + 10.0f;
        const float mx = tu_aButtons[BTN_MENU].x, my = tu_aButtons[BTN_MENU].y;
        float trayFree, kx, ky;
        tu_TrayKeysAt(mx, my, left, NULL, &kx, &ky, &trayFree, NULL);
        const float trayNeed = TU_MIN(TU_TRAY_CLEAR, trayFree);
        tuButton* d = &tu_aButtons[BTN_DUCK];
        float dist = d->radius + R + TU_CLUSTER_GAP;
        float mmx = (d->x + c->x) * 0.5f, mmy = (d->y + c->y) * 0.5f;
        float vx = c->x - d->x, vy = c->y - d->y;
        float L = sqrtf(vx * vx + vy * vy);
        float h = (dist > L * 0.5f) ? sqrtf(dist * dist - L * L * 0.25f) : 0;
        float px = vy / L, py = -vx / L;
        if ((mmx - fx) * px + (mmy - fy) * py < 0) { px = -px; py = -py; }
        float bestX = mmx + px * h, bestY = mmy + py * h;
        int bFound = 0;
        for (int pass = 0; pass < TU_COUNT(aGap) && !bFound; pass++) {
            const float gap = aGap[pass];
            const float D = j->radius + R + gap;
            for (int deg = 0; deg <= aMaxDeg[pass]; deg++) {
                float rad = deg * (float)M_PI / 180.0f;
                float qx = j->x + D * cosf(rad), qy = j->y - D * sinf(rad);
                if (qx + R > W - 4 || qy - R < top + 60) continue;
                if (hypotf(qx - a->x, qy - a->y) < R + a->radius + gap) continue;
                if (hypotf(qx - c->x, qy - c->y) < R + c->radius + gap) continue;
                if (hypotf(qx - fx, qy - fy) < R + FR + gap) continue;
                if (tu.bLayoutGauge) {
                    float gx = TU_MIN(TU_MAX(qx, g[0]), g[2]), gy = TU_MIN(TU_MAX(qy, g[1]), g[3]);
                    if (hypotf(qx - gx, qy - gy) < R + 4) continue;
                }
                if (bCutout && tu_DistToSegment(qx, qy, cutX, H * 0.5f - cutHalf, cutX, H * 0.5f + cutHalf) < R + cutR + 2) continue;
                int bNearTop = 0;
                for (int t = 0; t < TU_COUNT(aTopRow); t++) {
                    const tuButton* q = &tu_aButtons[aTopRow[t]];
                    const float qqx = tu.bSwapped ? W - q->x : q->x;
                    if (hypotf(qx - qqx, qy - q->y) < R + q->radius + topGap) bNearTop = 1;
                }
                if (bNearTop) continue;
                if (!tu.bSwapped) {
                    float trayClear, trayForce, kx2, ky2;
                    const float pf[2] = { qx, qy };
                    tu_TrayKeysAt(mx, my, left, pf, &kx2, &ky2, &trayClear, &trayForce);
                    if (trayClear < trayNeed || trayForce < TU_TRAY_CLEAR) continue;
                }
                bestX = qx;
                bestY = qy;
                bFound = 1;
                break;
            }
        }
        f->x = bestX;
        f->y = bestY;
    }
    if (tu.bSwapped) {
        for (int i = BTN_FIRE; i <= BTN_FORCE; i++) tu_aButtons[i].x = W - tu_aButtons[i].x;
    }

    // MENU's tray
    {
        const float force[2] = { tu_aButtons[BTN_FORCE].x, tu_aButtons[BTN_FORCE].y };
        float kx, ky;
        tu_TrayKeysAt(tu_aButtons[BTN_MENU].x, tu_aButtons[BTN_MENU].y, left, force, &kx, &ky, NULL, NULL);
        const float len = TU_TRAY_STEP * (BTN_TRAY_LAST - BTN_TRAY_FIRST);
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) {
            tu_aButtons[i].x = kx - TU_TRAY_STEP * (BTN_TRAY_LAST - i);
            tu_aButtons[i].y = ky;
        }
        tu.trayBack[0] = kx - len - 26; tu.trayBack[1] = ky - 26; tu.trayBack[2] = len + 52; tu.trayBack[3] = 52;
    }
    // The gyro options under GYRO
    {
        const tuButton* c = &tu_aButtons[BTN_TRAYGYRO];
        const int num = BTN_GYROOPT_LAST - BTN_GYROOPT_FIRST + 1;
        const float len = TU_TRAY_STEP * (num - 1);
        float x0 = c->x - len * 0.5f;
        x0 = TU_MIN(x0, right - 26 - len);
        x0 = TU_MAX(x0, left + 26);
        const float y = c->y + 52;
        for (int i = BTN_GYROOPT_FIRST; i <= BTN_GYROOPT_LAST; i++) {
            tu_aButtons[i].x = x0 + TU_TRAY_STEP * (i - BTN_GYROOPT_FIRST);
            tu_aButtons[i].y = y;
        }
        tu.gyroRowBack[0] = x0 - 26; tu.gyroRowBack[1] = y - 26; tu.gyroRowBack[2] = len + 52; tu.gyroRowBack[3] = 52;
    }

    tu.fpsX = tu.bFpsBottom ? W * 0.5f : right - TU_FPS_LABEL_X;
    tu.fpsY = tu.bFpsBottom ? bottom - 9.0f : top + 26;

    // The game's top message text
    {
        const float textTop = top + 26 + tu_aButtons[BTN_MENU].radius + TU_TOPTEXT_GAP;
        float half = TU_MIN(W * 0.5f - (left + TU_TOPTEXT_SIDE), (right - TU_TOPTEXT_SIDE) - W * 0.5f);
        for (int i = 0; i < BTN_COUNT; i++) {
            if (tu_IsTrayButton(i) || tu_IsOptButton(i)) continue;
            int bTopRow = 0;
            for (int t = 0; t < TU_COUNT(aTopRow); t++)
                if (aTopRow[t] == i) bTopRow = 1;
            if (bTopRow) continue;
            const tuButton* q = &tu_aButtons[i];
            if (q->y - q->radius - TU_TOUCH_SLOP > textTop + TU_TOPTEXT_DEPTH) continue;
            half = TU_MIN(half, fabsf(q->x - W * 0.5f) - q->radius - TU_TOUCH_SLOP);
        }
        half = TU_MAX(half, 60.0f);
        tu.aTopText[0] = textTop;
        tu.aTopText[1] = W * 0.5f - half;
        tu.aTopText[2] = W * 0.5f + half;
        tu.bTopText = 1;
    }
    tu.bLaidOut = 1;
}

// The button a touch at (px, py) is on (the open tray's buttons first: its
// whole backing is on the nearest of them; then the gyro row likewise)
static int tu_ButtonAt(float px, float py)
{
    int best = -1;
    float bestDist = 0;
    const tuButton* l = &tu_aButtons[BTN_TRAY_FIRST];
    const tuButton* r = &tu_aButtons[BTN_TRAY_LAST];
    if (tu.bTrayOpen && tu_DistToSegment(px, py, l->x, l->y, r->x, r->y) <= 26) {
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) {
            float d = fabsf(px - tu_aButtons[i].x);
            if (best < 0 || d < bestDist) { best = i; bestDist = d; }
        }
    }
    l = &tu_aButtons[BTN_GYROOPT_FIRST];
    r = &tu_aButtons[BTN_GYROOPT_LAST];
    if (best < 0 && tu.bGyroRow && tu_DistToSegment(px, py, l->x, l->y, r->x, r->y) <= 26) {
        for (int i = BTN_GYROOPT_FIRST; i <= BTN_GYROOPT_LAST; i++) {
            float d = fabsf(px - tu_aButtons[i].x);
            if (best < 0 || d < bestDist) { best = i; bestDist = d; }
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < BTN_COUNT; i++) {
        const tuButton* b = &tu_aButtons[i];
        if (tu_Hidden(i)) continue;
        float d = hypotf(px - b->x, py - b->y);
        if (d <= b->radius + TU_TOUCH_SLOP && (best < 0 || d - b->radius < bestDist)) {
            best = i;
            bestDist = d - b->radius;
        }
    }
    return best;
}

static int tu_IsPointOnButton(float px, float py, int button)
{
    if (tu.bTrayOpen && tu_IsTrayButton(button)) return tu_ButtonAt(px, py) == button;
    const tuButton* b = &tu_aButtons[button];
    return hypotf(px - b->x, py - b->y) <= b->radius + TU_TOUCH_SLOP;
}

// ------------------------------------------------------------ the stick's run marker

static void tu_PlaceRunMarker(float ox, float oy)
{
    tu.bRunMarker = 0;
    tu.bStickRunning = 0;
    tu.runDeg = 90.0f;
    const float R = TU_RUN_R;
    float x = TU_MAX(ox, TU_MAX(tu.inL, 8.0f) + 4.0f + R);
    x = TU_MIN(x, tu.W - TU_MAX(tu.inR, 8.0f) - 4.0f - R);
    const float minY = TU_MAX(tu.inT, 0.0f) + 4.0f + R;
    for (float h = TU_RUN_DIST; h >= TU_RUN_MIN_DIST && tu.bAlwaysRun == 0; h -= 2.0f) {
        float cy = oy - h;
        if (cy < minY) continue;
        int bClear = 1;
        for (int i = 0; i < BTN_COUNT && bClear; i++) {
            const tuButton* b = &tu_aButtons[i];
            if (tu_Hidden(i)) continue;
            bClear = hypotf(x - b->x, cy - b->y) >= b->radius + R + 4.0f;
        }
        if (!bClear) continue;
        tu.bRunMarker = 1;
        tu.runX = x;
        tu.runY = cy;
        tu.runDeg = atan2f(h, x - ox) * 180.0f / (float)M_PI;
        break;
    }
}

static int tu_StickOnRunMarker(float px, float py, float ox, float oy, int bRunning)
{
    if (!tu.bRunMarker) return 0;
    float dx = px - ox, dy = oy - py;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < (bRunning ? TU_RUN_OFF : TU_RUN_ON)) return 0;
    float off = fabsf(tu_AngleDiff(atan2f(dy, dx) * 180.0f / (float)M_PI, tu.runDeg));
    return off <= (bRunning ? TU_RUN_CONE_OFF : TU_RUN_CONE);
}

static void tu_UpdateStickRun(float px, float py)
{
    int bRun = tu_StickOnRunMarker(px, py, tu.stickOx, tu.stickOy, tu.bStickRunning);
    if (bRun && !tu.bStickRunning) tu_Haptic(TOUCHUI_HAPTIC_TICK);
    tu.bStickRunning = bRun;
}

static void tu_HideStick(void)
{
    tu.bStickActive = 0;
    tu.stickX = tu.stickY = 0.0f;
    tu.bRunMarker = 0;
    tu.bStickRunning = 0;
}

// ------------------------------------------------------------ gyro aiming

static void tu_GyroSave(void) { tu_SaveSettings(); }

static void tu_GyroToggle(void)
{
    if (tu.gyroMode != TOUCHUI_GYRO_OFF) {
        tu.gyroLastMode = tu.gyroMode;
        tu.gyroMode = TOUCHUI_GYRO_OFF;
    }
    else {
        tu.gyroMode = tu.gyroLastMode;
    }
    tu_GyroSave();
}

static void tu_GyroNextMode(void)
{
    tu.gyroLastMode = (tu.gyroLastMode == TOUCHUI_GYRO_TOUCH) ? TOUCHUI_GYRO_ALWAYS : TOUCHUI_GYRO_TOUCH;
    if (tu.gyroMode != TOUCHUI_GYRO_OFF) tu.gyroMode = tu.gyroLastMode;
    tu_GyroSave();
}

static void tu_GyroNextSens(void)
{
    tu.gyroSens = (tu.gyroSens + 1) % TU_COUNT(tu_aGyroSens);
    tu_GyroSave();
}

static int tu_GyroThumbDown(void)
{
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        const tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed) continue;
        if (s->role == ROLE_STICK || s->role == ROLE_LOOK || (s->role == ROLE_BUTTON && tu_aButtons[s->button].bLookWhileHeld)) return 1;
    }
    return 0;
}

int touchUi_IsGyroWanted(void)
{
    return tu.bShown && !tu.bOtherInput && tu.gyroMode != TOUCHUI_GYRO_OFF && tu.bGyroAvailable && tu.gyroSide != 0;
}

void touchUi_GyroSetSide(int side, double now)
{
    if (side != 0 && tu.gyroLastSide != 0 && side != tu.gyroLastSide) tu.gyroTurnedRound = now;
    if (side != 0) tu.gyroLastSide = side;
    tu.gyroSide = side;
}

void touchUi_GyroSample(double rx, double ry, double rz, double ux, double uy, double uz, double t)
{
    double dt = tu.gyroLastT > 0.0 ? t - tu.gyroLastT : 0.0;
    tu.gyroLastT = t;
    if (!touchUi_IsGyroWanted() || dt <= 0.0 || dt > TU_GYRO_MAX_DT) return;
    double tilt = sqrt(ux * ux + uy * uy);
    if (!(tilt < 0.5 || uy > -0.5 * tilt)) return; // held upside down for this side: on its way round
    double yaw = ry, pitch = rx;
    if (uz >= 0.0 && (tilt < 0.5 || uy > 0.5 * tilt)) {
        yaw = uy * ry + uz * rz;
        double mag = sqrt(ry * ry + rz * rz);
        yaw = copysign(fmin(fabs(yaw) * TU_GYRO_YAW_RELAX, mag), yaw);
    }
    yaw *= 180.0 / M_PI;
    pitch *= 180.0 / M_PI;
    double m = sqrt(yaw * yaw + pitch * pitch);
    double direct = (m - TU_GYRO_SMOOTH * 0.5) / (TU_GYRO_SMOOTH * 0.5);
    direct = direct < 0.0 ? 0.0 : (direct > 1.0 ? 1.0 : direct);
    tu.aGyroSmooth[tu.gyroSmoothNext][0] = yaw * (1.0 - direct);
    tu.aGyroSmooth[tu.gyroSmoothNext][1] = pitch * (1.0 - direct);
    tu.gyroSmoothNext = (tu.gyroSmoothNext + 1) % TU_GYRO_SMOOTH_N;
    double sumYaw = 0.0, sumPitch = 0.0;
    for (int i = 0; i < TU_GYRO_SMOOTH_N; i++) {
        sumYaw += tu.aGyroSmooth[i][0];
        sumPitch += tu.aGyroSmooth[i][1];
    }
    yaw = yaw * direct + sumYaw / TU_GYRO_SMOOTH_N;
    pitch = pitch * direct + sumPitch / TU_GYRO_SMOOTH_N;
    m = sqrt(yaw * yaw + pitch * pitch);
    if (m < TU_GYRO_SOFT) {
        yaw *= m / TU_GYRO_SOFT;
        pitch *= m / TU_GYRO_SOFT;
    }
    tu.gyroYaw += yaw * dt;
    tu.gyroPitch += pitch * dt;
}

void touchUi_GyroFrame(double now, int* pDx, int* pDy)
{
    *pDx = *pDy = 0;
    int bWanted = touchUi_IsGyroWanted();
    if (bWanted && !tu.bGyroWasWanted) {
        // from scratch: nothing from before it stopped, the smoothing empty
        tu.gyroLastT = 0.0;
        tu.gyroYaw = tu.gyroPitch = 0.0;
        memset(tu.aGyroSmooth, 0, sizeof(tu.aGyroSmooth));
    }
    tu.bGyroWasWanted = bWanted;
    double yaw = tu.gyroYaw, pitch = tu.gyroPitch;
    tu.gyroYaw = tu.gyroPitch = 0.0;
    int bOk = bWanted
              && (tu.gyroMode == TOUCHUI_GYRO_ALWAYS || tu_GyroThumbDown())
              && !tu.bWheelOpen && !tu_IsHolding() && !tu_IsChatOpen() && !tu.bTrayOpen
              && now - tu.gyroTurnedRound >= TU_GYRO_TURN_HOLD;
    int bUse = bOk && tu.bGyroWasOk && now - tu.gyroLastFrame <= TU_GYRO_MAX_FRAME;
    tu.bGyroWasOk = bOk;
    tu.gyroLastFrame = now;
    if (!bUse) return;
    float turn = 0.0f, tilt = 0.0f;
    if (G(GetMouseLookDegrees)) tu.game->GetMouseLookDegrees(&turn, &tilt);
    float k = tu_aGyroSens[tu.gyroSens];
    if (fabsf(turn) > 0.001f) tu.gyroCountX -= (float)yaw * k / turn;
    if (fabsf(tilt) > 0.001f) tu.gyroCountY -= (float)pitch * k / tilt;
    int dx = (int)tu.gyroCountX, dy = (int)tu.gyroCountY;
    tu.gyroCountX -= (float)dx;
    tu.gyroCountY -= (float)dy;
    *pDx = dx;
    *pDy = dy;
}

// ------------------------------------------------------------ MENU's tray

static void tu_SetGyroRow(int bOpen)
{
    bOpen = bOpen != 0;
    if (tu.bGyroRow == bOpen) return;
    tu.bGyroRow = bOpen;
    if (!bOpen) {
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (s->bUsed && s->role == ROLE_BUTTON && tu_IsOptButton(s->button)) {
                if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
                s->role = ROLE_IGNORED;
            }
        }
    }
}

static void tu_SetTrayOpen(int bOpen)
{
    bOpen = bOpen != 0;
    if (tu.bTrayOpen == bOpen) return;
    tu.bTrayOpen = bOpen;
    if (!bOpen) {
        tu_SetGyroRow(0);
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (!s->bUsed || s->role != ROLE_BUTTON) continue;
            if (s->button == BTN_TRAYGYRO) tu.aRing[BTN_TRAYGYRO] = 0.0f;
            if (tu_IsTrayButton(s->button)) {
                if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
                s->role = ROLE_IGNORED;
            }
            s->trayButton = -1;
        }
    }
}

static void tu_SetShowFps(int bShow)
{
    tu.bShowFps = bShow != 0;
    tu.bFpsBase = 0;
    snprintf(tu.fpsText, sizeof(tu.fpsText), "-- FPS");
    tu_SaveSettings();
}

static void tu_SetShowCheat(int bShow)
{
    tu.bShowCheat = bShow != 0;
    if (G(SetCheatShown)) tu.game->SetCheatShown(tu.bShowCheat);
    tu_SaveSettings();
    tu_Layout();
}

static void tu_SetShowView(int bShow)
{
    tu.bShowView = bShow != 0;
    tu_SaveSettings();
    tu_Layout();
}

static void tu_SetThirdPerson(int bThird)
{
    tu.bThirdPerson = bThird != 0;
    if (G(SetThirdPerson)) tu.game->SetThirdPerson(tu.bThirdPerson);
    tu_SaveSettings();
}

static void tu_LeaveCheatMode(void)
{
    if (tu_IsCheatOn() && G(ToggleSithEwok) && tu.game->ToggleSithEwok() < 0) return;
    tu_SetShowCheat(0);
}

static void tu_UseTrayButton(int button)
{
    int kind = tu_aButtons[button].kind;
    if (kind == KIND_CHAT) {
        if (G(ToggleChat)) tu.game->ToggleChat();
    }
    else if (kind == KIND_FPS) {
        tu_SetShowFps(!tu.bShowFps);
    }
    else if (kind == KIND_CAMSWITCH) {
        // (the camera's photos aren't in the Android app yet)
        if (G(ShowMessage)) tu.game->ShowMessage("The camera isn't on Android yet");
    }
    else if (kind == KIND_CHEATSWITCH) {
        if (tu.bShowCheat) {
            tu_LeaveCheatMode();
        }
        else {
            tu_SetShowCheat(1);
            if (!tu_IsCheatOn() && G(ToggleSithEwok)) tu.game->ToggleSithEwok();
        }
    }
    else if (kind == KIND_VIEWSWITCH) {
        tu_SetShowView(!tu.bShowView);
    }
    else if (kind == KIND_GYRO && tu_GyroAvailable()) {
        tu_GyroToggle();
    }
    else if (kind == KIND_CONTROLS) {
        tu.bSwapped = !tu.bSwapped;
        tu_SaveSettings();
        tu_Layout();
    }
    else if (kind == KIND_JUMPSTYLE) {
        tu.jumpStyle = (tu.jumpStyle + 1) % TU_JUMP_NUM_STYLES;
        if (G(SetForceJumpStyle)) tu.game->SetForceJumpStyle(tu.jumpStyle);
        tu_SaveSettings();
    }
    if (kind == KIND_GYRO || kind == KIND_JUMPSTYLE || kind == KIND_CONTROLS) {
        // (the tray stays, for another tap)
    }
    else {
        tu_SetTrayOpen(0);
    }
}

static void tu_UseOptButton(int button)
{
    if (tu_aButtons[button].kind == KIND_GYROMODE && tu_GyroAvailable()) tu_GyroNextMode();
    if (tu_aButtons[button].kind == KIND_GYROSENS && tu_GyroAvailable()) tu_GyroNextSens();
}

// ------------------------------------------------------------ the wheels

static int tu_WheelSliceForAngle(float deg, int cur)
{
    const tuWheelMap* m = tu.pWheelMap;
    if (!m) return TU_WHEEL_CANCEL;
    float half = m->sliceDeg * 0.5f;
    if (cur >= 0 && cur < m->numSlices && fabsf(tu_AngleDiff(deg, m->aSlices[cur].deg)) <= half + TU_WHEEL_HYST) return cur;
    int best = TU_WHEEL_CANCEL;
    float bestDiff = 0.0f;
    for (int i = 0; i < m->numSlices; i++) {
        float d = fabsf(tu_AngleDiff(deg, m->aSlices[i].deg));
        if (d <= half + m->gapDeg * 0.5f && (best < 0 || d < bestDiff)) {
            best = i;
            bestDiff = d;
        }
    }
    return best;
}

static int tu_WheelSliceAt(float px, float py, int cur)
{
    float dx = px - tu.wheelCx, dy = tu.wheelCy - py;
    float r = sqrtf(dx * dx + dy * dy);
    if (r < tu.wheelR0 - 4.0f || r > tu.wheelR1 + TU_WHEEL_POP + 14.0f) return -1;
    return tu_WheelSliceForAngle(atan2f(dy, dx) * 180.0f / (float)M_PI, cur);
}

static int tu_InWheelSlice(float px, float py, float deg, float pad)
{
    float dx = px - tu.wheelCx, dy = tu.wheelCy - py;
    float r = sqrtf(dx * dx + dy * dy);
    if (r < tu.wheelR0 + pad || r > tu.wheelR1 - pad) return 0;
    float edge = tu.pWheelMap->sliceDeg * 0.5f - 1.0f - pad / r * 180.0f / (float)M_PI;
    return fabsf(tu_AngleDiff(atan2f(dy, dx) * 180.0f / (float)M_PI, deg)) <= edge;
}

// The wheel labels' font: bold, condensed
#define TU_WHEEL_XSCALE 0.82f

static void tu_WheelLabelText(int i, char* out, int outLen)
{
    const char* name = tu_WheelSlotName(tu.pWheelMap->aSlices[i].bin);
    snprintf(out, outLen, "%s", name ? name : "?");
    for (char* p = out; *p; p++)
        if (*p == ' ') *p = '\n';
}

static void tu_WheelRowSize(float* pW, float* pH)
{
    float k = tu.aWheelFit[tu.wheelKind].rowScale;
    if (tu.wheelKind == TOUCHUI_WHEEL_WEAPONS) {
        float w;
        touchUiDraw_MeasureText("000", TU_WHEEL_AMMO_FONT, 1.0f, &w, NULL);
        *pW = w * k;
        *pH = TU_WHEEL_AMMO_FONT * TU_WHEEL_AMMO_INK * k;
        return;
    }
    *pW = ((TU_WHEEL_STARS - 1) * TU_WHEEL_STAR_STEP + TU_WHEEL_STAR_CELL) * k;
    *pH = TU_WHEEL_STAR_CELL * k;
}

static int tu_WheelSliceHasRow(int i)
{
    return tu.wheelKind != TOUCHUI_WHEEL_WEAPONS || tu.pWheelMap->aSlices[i].group != 0;
}

static int tu_FitWheelLabel(int i, float s11w, float s11h, int bRow)
{
    const tuWheelMap* m = tu.pWheelMap;
    tuWheelFit* F = &tu.aWheelFit[tu.wheelKind];
    const float cx = tu.wheelCx, cy = tu.wheelCy, R0 = tu.wheelR0, R1 = tu.wheelR1;
    float rowW, rowH;
    tu_WheelRowSize(&rowW, &rowH);
    float a = m->aSlices[i].deg * (float)M_PI / 180.0f;
    F->font[i][bRow] = bRow ? 0.0f : 7.0f;
    F->labelR[i][bRow] = R0 + (R1 - R0) * 0.58f;
    F->labelW[i][bRow] = s11w * 7.0f / 11.0f;
    F->labelH[i][bRow] = s11h * 7.0f / 11.0f;
    int bFound = 0;
    for (float size = 11.0f; size >= 7.0f && !bFound; size -= 0.5f) {
        float w = s11w * size / 11.0f, h = s11h * size / 11.0f;
        float bw = bRow ? TU_MAX(w, rowW) : w;
        float bh = bRow ? h + TU_WHEEL_STAR_GAP + rowH : h;
        float bestOff = 0.0f;
        for (int k = 0; k <= 20; k++) {
            float f = 0.30f + 0.50f * k / 20.0f;
            float rr = R0 + (R1 - R0) * f;
            float px = cx + rr * cosf(a), py = cy - rr * sinf(a);
            int bIn = 1;
            for (int sx = -1; sx <= 1 && bIn; sx++)
                for (int sy = -1; sy <= 1 && bIn; sy++)
                    bIn = tu_InWheelSlice(px + sx * bw * 0.5f, py + sy * bh * 0.5f, m->aSlices[i].deg, 2.0f);
            if (bIn && (!bFound || fabsf(f - 0.58f) < bestOff)) {
                bFound = 1;
                bestOff = fabsf(f - 0.58f);
                F->font[i][bRow] = size;
                F->labelR[i][bRow] = rr;
                F->labelW[i][bRow] = w;
                F->labelH[i][bRow] = h;
            }
        }
    }
    return bFound;
}

static void tu_FitWheelLabels(void)
{
    const tuWheelMap* m = tu.pWheelMap;
    tuWheelFit* F = &tu.aWheelFit[tu.wheelKind];
    float rowMin = (tu.wheelKind == TOUCHUI_WHEEL_WEAPONS) ? TU_WHEEL_AMMO_MIN : TU_WHEEL_STAR_MIN;
    float aW[TU_WHEEL_MAX], aH[TU_WHEEL_MAX];
    for (int i = 0; i < m->numSlices; i++) {
        char text[64];
        tu_WheelLabelText(i, text, sizeof(text));
        touchUiDraw_MeasureText(text, 11.0f, TU_WHEEL_XSCALE, &aW[i], &aH[i]);
        tu_FitWheelLabel(i, aW[i], aH[i], 0);
        F->font[i][1] = 0.0f;
    }
    for (int n = 0; ; n++) {
        int bAll = 1;
        F->rowScale = 1.0f - 0.05f * n;
        int bLast = F->rowScale <= rowMin + 0.001f;
        for (int i = 0; i < m->numSlices && (bAll || bLast); i++)
            if (tu_WheelSliceHasRow(i) && !tu_FitWheelLabel(i, aW[i], aH[i], 1)) bAll = 0;
        if (bAll || bLast) break;
    }
}

static void tu_LayoutWheel(void)
{
    const tuWheelMap* m = tu.pWheelMap;
    if (!m) return;
    const float left = TU_MAX(tu.inL, 8.0f), right = tu.W - TU_MAX(tu.inR, 8.0f);
    const float top = TU_MAX(tu.inT, 8.0f), bottom = tu.H - TU_MAX(tu.inB, 8.0f);
    tu.wheelCx = (left + right) * 0.5f;
    tu.wheelCy = (top + bottom) * 0.5f + 4.0f;
    tu.wheelR1 = TU_MIN(TU_WHEEL_R1_MAX, (bottom - top) * 0.5f - TU_WHEEL_POP - 6.0f);
    tu.wheelR0 = roundf(tu.wheelR1 * TU_WHEEL_R0_FRAC);
    tuWheelFit* F = &tu.aWheelFit[tu.wheelKind];
    if (m != F->map || tu.wheelR1 != F->R1) {
        F->map = m;
        F->R1 = tu.wheelR1;
        tu_FitWheelLabels();
    }
}

// What the wheel points at: by the opener once it has slid, else by the
// latest other finger on it (*pbCancel: lifting cancels)
static int tu_WheelHot(int* pbCancel, tuSlot** ppSlide)
{
    tuSlot* pSlide = NULL;
    tuSlot* pFinger = NULL;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_WHEEL) continue;
        if (s->bWheelOpener && s->bWheelHeld) continue;
        if (s->bWheelOpener && !tu.bWheelTapMode) pSlide = s;
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
    if (pbCancel) *pbCancel = bCancel;
    if (ppSlide) *ppSlide = pSlide;
    return hot;
}

// A tick each time the thumb moves onto another slice (or the gap)
static void tu_WheelTouched(void)
{
    if (!tu.bWheelOpen) return;
    int hot = tu_WheelHot(NULL, NULL);
    if (hot != tu.wheelLastHot) {
        if (hot != -1) tu_Haptic(TOUCHUI_HAPTIC_TICK);
        tu.wheelLastHot = hot;
    }
}

static void tu_OpenWheel(int kind, int button, int bTapMode)
{
    tu_SetTrayOpen(0);
    int bMots = tu_IsMots();
    tu.wheelKind = kind;
    tu.wheelButton = button;
    tu.pWheelMap = (kind == TOUCHUI_WHEEL_WEAPONS) ? (bMots ? &tu_motsWeaponWheel : &tu_jkWeaponWheel)
                                                   : (bMots ? &tu_motsWheel : &tu_jkWheel);
    for (int i = 0; i < tu.pWheelMap->numSlices; i++) {
        int bin = tu.pWheelMap->aSlices[i].bin;
        if (kind == TOUCHUI_WHEEL_WEAPONS) {
            int ammo = -1, bSelectable = 0;
            tu.aWheelOwned[i] = G(GetWeapon) ? tu.game->GetWeapon(bin, &ammo, &bSelectable) != 0 : 0;
            tu.aWheelEarned[i] = tu.aWheelOwned[i] && bSelectable;
            tu.aWheelAmmo[i] = tu.aWheelOwned[i] ? ammo : -1;
            tu.aWheelLevel[i] = 0;
        }
        else {
            tu.aWheelEarned[i] = G(IsPowerAvailable) ? tu.game->IsPowerAvailable(bin) : 0;
            tu.aWheelLevel[i] = G(GetPowerLevel) ? tu.game->GetPowerLevel(bin) : 0;
            tu.aWheelOwned[i] = tu.aWheelEarned[i];
            tu.aWheelAmmo[i] = -1;
        }
    }
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed) continue;
        if (s->role == ROLE_BUTTON && tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
        if (s->role == ROLE_STICK) tu_HideStick();
        s->role = ROLE_IGNORED;
    }
    tu.aRing[BTN_QUICKSAVE] = tu.aRing[BTN_QUICKLOAD] = tu.aRing[BTN_MENU] = 0.0f;
    tu.aRing[BTN_WHEEL] = tu.aRing[BTN_NEXTWPN] = 0.0f;
    // a tap on either wheel button not read yet would undo the pick
    tu.aPulseQueue[tu_aButtons[BTN_WHEEL].scancode] = 0;
    tu.aPulseQueue[tu_aButtons[BTN_NEXTWPN].scancode] = 0;
    tu.bWheelOpen = 1;
    tu.bWheelTapMode = bTapMode != 0;
    tu.wheelLastHot = -1;
    tu_SetHold(1);
    tu_LayoutWheel();
    tu_RecomputeKeys();
}

static void tu_OpenWheelFrom(tuSlot* s, int bHeld)
{
    int button = s->button;
    if (tu_IsChatOpen() && G(ToggleChat)) tu.game->ToggleChat();
    tu_OpenWheel(tu_WheelKindOf(button), button, bHeld);
    s->role = ROLE_WHEEL;
    s->button = button;
    s->bWheelOpener = 1;
    s->bWheelHeld = bHeld != 0;
    s->bArmed = 0;
    s->wheelSlot = -1;
    s->wox = s->ox;
    s->woy = s->oy;
}

static void tu_CloseWheelSelecting(int slice)
{
    const tuWheelMap* m = tu.pWheelMap;
    if (m && slice >= 0 && slice < m->numSlices && tu.aWheelEarned[slice]) {
        int bin = m->aSlices[slice].bin;
        if (tu.wheelKind != TOUCHUI_WHEEL_WEAPONS) {
            if (G(SelectPower)) tu.game->SelectPower(bin);
        }
        else if (!G(GetCurWeapon) || bin != tu.game->GetCurWeapon()) {
            if (G(SelectWeapon)) tu.game->SelectWeapon(bin);
        }
    }
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (tu.aSlots[i].bUsed && tu.aSlots[i].role == ROLE_WHEEL) tu.aSlots[i].role = ROLE_IGNORED;
    tu.bWheelOpen = 0;
    tu.bWheelTapMode = 0;
    tu_SetHold(0);
    tu_RecomputeKeys();
}

static void tu_AimWheel(tuSlot* s, float px, float py)
{
    float dx = px - s->wox, dy = py - s->woy;
    float r = sqrtf(dx * dx + dy * dy);
    if (r > TU_WHEEL_LEASH) {
        float k = (r - TU_WHEEL_LEASH) / r;
        s->wox += dx * k;
        s->woy += dy * k;
        dx = px - s->wox;
        dy = py - s->woy;
        r = TU_WHEEL_LEASH;
    }
    float cx = px - tu.wheelCx, cy = tu.wheelCy - py;
    if (sqrtf(cx * cx + cy * cy) <= tu.wheelR1 + TU_WHEEL_POP + 14.0f) {
        s->bArmed = 1;
        s->wheelAim = atan2f(cy, cx) * 180.0f / (float)M_PI;
        s->wheelSlot = tu_WheelSliceAt(px, py, s->wheelSlot);
        return;
    }
    if (s->bArmed && hypotf(px - s->ox, py - s->oy) < TU_WHEEL_DEAD) {
        s->wox = s->ox;
        s->woy = s->oy;
        s->wheelSlot = -1;
        return;
    }
    if (r < TU_WHEEL_DEAD) {
        s->wheelSlot = -1;
        return;
    }
    s->bArmed = 1;
    s->wheelAim = atan2f(-dy, dx) * 180.0f / (float)M_PI;
    s->wheelSlot = tu_WheelSliceForAngle(s->wheelAim, s->wheelSlot);
}

// ------------------------------------------------------------ touches

static tuSlot* tu_FindSlot(long long id)
{
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (tu.aSlots[i].bUsed && tu.aSlots[i].id == id) return &tu.aSlots[i];
    return NULL;
}

static void tu_TouchDown(long long id, float px, float py, double t)
{
    if (tu_FindSlot(id)) return;
    tuSlot* s = NULL;
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (!tu.aSlots[i].bUsed) { s = &tu.aSlots[i]; break; }
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->bUsed = 1;
    s->id = id;
    s->ox = s->lx = px;
    s->oy = s->ly = py;
    s->tDown = t;
    s->button = -1;
    s->trayButton = -1;
    s->wheelSlot = -1;

    if (tu.bOtherInput) {
        // hidden for a controller, keyboard or mouse: back now, and this touch
        // does nothing else -- but on an open wheel it works the wheel
        tu.bOtherInput = 0;
        if (!tu.bWheelOpen) {
            s->role = ROLE_IGNORED;
            return;
        }
    }
    if (tu.bWheelOpen) {
        s->role = ROLE_WHEEL;
        s->wheelSlot = tu_WheelSliceAt(px, py, -1);
        return;
    }
    s->button = tu_ButtonAt(px, py);
    if (tu.bGyroRow && !tu_IsOptButton(s->button)) tu_SetGyroRow(0);
    if (tu.bTrayOpen && !tu_IsTrayButton(s->button) && !tu_IsOptButton(s->button)) {
        tu_SetTrayOpen(0);
        if (s->button == BTN_MENU) {
            s->role = ROLE_IGNORED;
            return;
        }
    }
    if (s->button >= 0 && tu_aButtons[s->button].kind == KIND_WHEEL) {
        int bOther = 0;
        for (int k = 0; k < TU_MAX_TOUCHES; k++) {
            const tuSlot* o = &tu.aSlots[k];
            if (o != s && tu_IsWheelPress(o) && o->button == s->button) bOther = 1;
        }
        if (bOther) s->role = ROLE_IGNORED;
        else {
            s->role = ROLE_BUTTON;
            tu.aButtonHeld[s->button]++;
        }
    }
    else if (s->button >= 0) {
        s->role = ROLE_BUTTON;
        tu.aButtonHeld[s->button]++;
    }
    else if ((tu.bSwapped ? px > tu.W * (1.0f - TU_STICK_ZONE) : px < tu.W * TU_STICK_ZONE) && !tu.bStickActive) {
        s->role = ROLE_STICK;
        tu.bStickActive = 1;
        tu.stickX = tu.stickY = 0.0f;
        tu.stickOx = px;
        tu.stickOy = py;
        tu.bAlwaysRun = G(IsAlwaysRun) ? tu.game->IsAlwaysRun() : 0;
        tu_PlaceRunMarker(px, py);
    }
    else {
        s->role = ROLE_LOOK;
    }
}

static void tu_TouchMove(tuSlot* s, float px, float py)
{
    // QUICK SAVE, QUICK LOAD: off the button even once, no save or load
    if (s->role == ROLE_BUTTON && tu_IsQuickHold(s->button) && !s->bFired && !tu_IsPointOnButton(px, py, s->button))
        s->bFired = 1;
    // The tray's GYRO slid off isn't held any more
    if (s->role == ROLE_BUTTON && s->button == BTN_TRAYGYRO && !s->bFired && hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_OPEN_SLIDE) {
        s->bFired = 1;
        tu.aRing[BTN_TRAYGYRO] = 0.0f;
    }
    // FORCE WHEEL, NEXT WPN: slid 10 points, the wheel opens at once
    if (tu_IsWheelPress(s) && hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_OPEN_SLIDE) tu_OpenWheelFrom(s, 0);

    if (s->role == ROLE_WHEEL) {
        if (s->bWheelOpener && s->bWheelHeld) {
            if (hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_DEAD) {
                tu.bWheelTapMode = 0;
                s->bWheelHeld = 0;
                tu_AimWheel(s, px, py);
            }
        }
        else if (s->bWheelOpener && !tu.bWheelTapMode) tu_AimWheel(s, px, py);
        else s->wheelSlot = tu_WheelSliceAt(px, py, s->wheelSlot);
    }
    else if (s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && tu.bTrayOpen) {
        int tb = tu_ButtonAt(px, py);
        s->trayButton = tu_IsTrayButton(tb) ? tb : -1;
    }
    else if (s->role == ROLE_STICK) {
        float dx = (px - s->ox) / TU_STICK_RADIUS;
        float dy = (py - s->oy) / TU_STICK_RADIUS;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1.0f) { dx /= len; dy /= len; }
        tu.stickX = dx;
        tu.stickY = dy;
        tu_UpdateStickRun(px, py);
    }
    else if (s->role == ROLE_LOOK || (s->role == ROLE_BUTTON && tu_aButtons[s->button].bLookWhileHeld)) {
        tu.lookX += (px - s->lx) * TU_LOOK_SCALE_X;
        tu.lookY += (py - s->ly) * TU_LOOK_SCALE_Y;
    }
    s->lx = px;
    s->ly = py;
}

static void tu_EndLevelPick(tuSlot* s)
{
    if (s->levelPick > 0 && G(ShowMessageInPlace)) {
        char msg[48];
        if (tu.levelToLoad > 0) snprintf(msg, sizeof(msg), "Loading level %d", tu.levelToLoad);
        else snprintf(msg, sizeof(msg), "No level picked");
        tu.game->ShowMessageInPlace(msg);
    }
    s->levelPick = 0;
}

static void tu_TouchUp(tuSlot* s, float px, float py, double t, int bCancelled)
{
    int bChat = tu_IsChatOpen();
    if (s->role == ROLE_WHEEL && tu.bWheelOpen && bCancelled) {
        if (s->bWheelOpener) tu.bWheelTapMode = 1; // the opener taken away: open to tap
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && s->bWheelOpener && s->bWheelHeld) {
        // opened by holding still, lifted without sliding: open to tap
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && s->bWheelOpener && !tu.bWheelTapMode) {
        tu_AimWheel(s, px, py);
        if (!s->bArmed) tu.bWheelTapMode = 1;
        else tu_CloseWheelSelecting(s->wheelSlot);
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen) {
        int slice = tu_WheelSliceAt(px, py, s->wheelSlot);
        int bSliding = 0;
        for (int k = 0; k < TU_MAX_TOUCHES; k++) {
            const tuSlot* o = &tu.aSlots[k];
            if (o != s && o->bUsed && o->role == ROLE_WHEEL && o->bWheelOpener && o->bArmed && !tu.bWheelTapMode) bSliding = 1;
        }
        if (!(slice == -1 && bSliding) && !(slice >= 0 && !tu.aWheelEarned[slice])) tu_CloseWheelSelecting(slice);
    }
    else if (tu_IsWheelPress(s)) {
        const tuButton* b = &tu_aButtons[s->button];
        if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
        tu.aRing[s->button] = 0.0f;
        if (bCancelled) {
        }
        else if (hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_OPEN_SLIDE) {
            // a flick with no move in between: a slide, then its lift
            tu_OpenWheelFrom(s, 0);
            tu_AimWheel(s, px, py);
            if (s->bArmed) tu_CloseWheelSelecting(s->wheelSlot);
            else tu.bWheelTapMode = 1;
        }
        else if (t - s->tDown < TU_WHEEL_HOLD) {
            // a quick tap: the next power (E) or weapon (G)
            if (!bChat) {
                if (s->button == BTN_NEXTWPN && G(CancelWeaponPick)) tu.game->CancelWeaponPick();
                tu_QueuePress(b->scancode);
            }
        }
        else {
            tu_OpenWheelFrom(s, 1); // held long enough, lifted before a frame saw it
        }
    }
    else if (s->role == ROLE_BUTTON) {
        const tuButton* b = &tu_aButtons[s->button];
        if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
        // JUMP: a quick still tap; the second of two while flying lands
        if (s->button == BTN_JUMP && !bCancelled && t - s->tDown < TU_FLY_TAP && hypotf(px - s->ox, py - s->oy) < TU_FLY_STILL) {
            if (tu.tJumpTapUp >= 0.0 && s->tDown - tu.tJumpTapUp < TU_FLY_DOUBLE && !bChat && G(IsFlying) && tu.game->IsFlying()) {
                if (G(ToggleFlight) && tu.game->ToggleFlight() >= 0) tu_Haptic(TOUCHUI_HAPTIC_TAP);
                tu.tJumpTapUp = -1.0;
            }
            else tu.tJumpTapUp = t;
        }
        if (s->button == BTN_DUCK && s->levelPick > 0) {
            if (!bCancelled) tu.levelToLoad = s->levelPick;
            tu_EndLevelPick(s);
        }
        // a tap so quick the game never read it held still counts as one press
        if (!bCancelled && !bChat && b->kind == KIND_KEY && b->scancode >= 0 && !s->bSeen) tu_QueuePress(b->scancode);
        int bWhileTyping = b->kind == KIND_MENU || tu_IsTrayButton(s->button) || tu_IsOptButton(s->button);
        if (!bCancelled && tu_IsPointOnButton(px, py, s->button) && (!bChat || bWhileTyping)) {
            if (b->kind == KIND_ITEM && s->button != BTN_BACTA && !tu_Hidden(s->button)
                && !tu.aPulseQueue[b->scancode] && !tu.aPulseReads[b->scancode] && !tu.aPulseGap[b->scancode]) {
                if (G(SelectItem)) tu.game->SelectItem(b->bin);
                tu_QueuePress(b->scancode);
            }
            else if (tu_IsTrayButton(s->button) && !(s->button == BTN_TRAYGYRO && s->bFired)) {
                tu_UseTrayButton(s->button);
            }
            else if (tu_IsOptButton(s->button) && !tu_Hidden(s->button)) {
                tu_UseOptButton(s->button);
            }
            else if (b->kind == KIND_VIEW && !tu_Hidden(s->button)) {
                tu_SetThirdPerson(!tu.bThirdPerson);
            }
            else if (b->kind == KIND_MENU && !s->bFired) {
                tu.menuPulse = TU_MENU_PULSE_FRAMES; // Escape: the menu, or closing the typing line
            }
        }
        // MENU held until its tray opened, then slid onto one of its buttons
        if (!bCancelled && s->button == BTN_MENU && s->bFired && tu.bTrayOpen) {
            int tb = tu_ButtonAt(px, py);
            if (tu_IsTrayButton(tb)) tu_UseTrayButton(tb);
        }
        if (s->button == BTN_QUICKSAVE || s->button == BTN_QUICKLOAD || s->button == BTN_MENU || s->button == BTN_BACTA
            || s->button == BTN_TRAYGYRO)
            tu.aRing[s->button] = 0.0f;
    }
    else if (s->role == ROLE_STICK) {
        tu_HideStick();
    }
    memset(s, 0, sizeof(*s));
}

void touchUi_Finger(long long id, int phase, float x, float y, double t)
{
    if (!tu.bShown) return;
    if (phase == TOUCHUI_PHASE_DOWN) {
        tu_TouchDown(id, x, y, t);
    }
    else {
        tuSlot* s = tu_FindSlot(id);
        if (!s) return;
        if (phase == TOUCHUI_PHASE_MOVE) tu_TouchMove(s, x, y);
        else if (phase == TOUCHUI_PHASE_CANCEL) {
            // taken by the system: let go where it last was, nothing fires
            tu_TouchUp(s, s->lx, s->ly, t, 1);
        }
        else {
            // a lift where the finger moved since its last move: that move first
            if (s->role != ROLE_IGNORED && (x != s->lx || y != s->ly)) tu_TouchMove(s, x, y);
            tu_TouchUp(s, x, y, t, 0);
        }
    }
    tu_RecomputeKeys();
    tu_WheelTouched();
}

// ------------------------------------------------------------ once a frame

static void tu_ResetAll(void)
{
    if (G(CancelWeaponPick)) tu.game->CancelWeaponPick();
    if (tu.bWheelOpen) {
        tu.bWheelOpen = 0;
        tu.bWheelTapMode = 0;
        tu_SetHold(0);
    }
    tu_SetTrayOpen(0);
    memset(tu.aSlots, 0, sizeof(tu.aSlots));
    memset(tu.aButtonHeld, 0, sizeof(tu.aButtonHeld));
    memset(tu.aPulseQueue, 0, sizeof(tu.aPulseQueue));
    memset(tu.aPulseReads, 0, sizeof(tu.aPulseReads));
    memset(tu.aPulseGap, 0, sizeof(tu.aPulseGap));
    memset(tu.aRing, 0, sizeof(tu.aRing));
    tu.fjumpRing = 0.0f;
    tu_HideStick();
    tu.lookX = tu.lookY = 0.0f;
    tu.bGyroWasOk = 0;
    tu.gyroCountX = tu.gyroCountY = 0.0f;
    tu.menuPulse = 0;
    tu.bFpsBase = 0;
    tu.levelToLoad = 0;
    tu_RecomputeKeys();
}

void touchUi_Reset(void)
{
    tu_ResetAll();
}

void touchUi_Tick(double known, double now)
{
    if (!tu.bShown) return;
    tu.lastNow = now;
    const int bChat = tu_IsChatOpen();

    // VIEW: the view picked by a keyboard's camera key, shown and remembered
    if (G(IsThirdPerson) && tu.game->IsThirdPerson() != tu.bThirdPerson) {
        tu.bThirdPerson = tu.game->IsThirdPerson() != 0;
        tu_SaveSettings();
    }

    // DUCK's level pick
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!(s->levelPick > 0 && s->bUsed && s->role == ROLE_BUTTON)) s->levelPick = 0;
    }
    if (tu.levelToLoad > 0) {
        const int level = tu.levelToLoad;
        tu.levelToLoad = 0;
        if (!tu.bWheelOpen && !bChat && G(GoToLevel)) tu.game->GoToLevel(level);
    }

    // FORCE WHEEL and NEXT WPN held still: the wheel opens to tap
    float wheelProgress = 0.0f, weaponProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!tu_IsWheelPress(s)) continue;
        if (known - s->tDown >= TU_WHEEL_HOLD) {
            tu_OpenWheelFrom(s, 1);
            wheelProgress = weaponProgress = 0.0f;
            break;
        }
        float progress = (float)TU_MIN((now - s->tDown) / TU_WHEEL_HOLD, 1.0);
        if (s->button == BTN_NEXTWPN) weaponProgress = progress;
        else wheelProgress = progress;
    }
    if (!tu.bWheelOpen) {
        tu.aRing[BTN_WHEEL] = wheelProgress;
        tu.aRing[BTN_NEXTWPN] = weaponProgress;
    }

    // JUMP, ACT (sithewok's flight, noclip) and DUCK (its level pick) held still
    float jumpProgress = 0.0f, actProgress = 0.0f, duckProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_BUTTON || (s->button != BTN_JUMP && s->button != BTN_ACT && s->button != BTN_DUCK) || s->bFlyDone)
            continue;
        if (hypotf(s->lx - s->ox, s->ly - s->oy) >= TU_FLY_STILL) {
            s->bFlyDone = 1;
            tu_EndLevelPick(s);
            continue;
        }
        if (s->button == BTN_DUCK) {
            const int numLevels = (bChat || !G(NumPickableLevels)) ? 0 : tu.game->NumPickableLevels();
            if (numLevels <= 0) {
                if (s->levelPick) {
                    s->bFlyDone = 1;
                    tu_EndLevelPick(s);
                }
                continue;
            }
            const double held = known - s->tDown;
            if (held >= TU_FLY_HOLD) {
                int n = 1 + (int)((held - TU_FLY_HOLD) / TU_LEVEL_STEP) % numLevels;
                if (n != s->levelPick) {
                    s->levelPick = n;
                    if (G(ShowMessageInPlace)) {
                        char msg[48];
                        snprintf(msg, sizeof(msg), "Level %d of %d", n, numLevels);
                        tu.game->ShowMessageInPlace(msg);
                    }
                    tu_Haptic(TOUCHUI_HAPTIC_TAP);
                }
                continue;
            }
            duckProgress = TU_MAX(duckProgress, (float)TU_MIN((now - s->tDown) / TU_FLY_HOLD, 1.0));
            continue;
        }
        const int bJump = s->button == BTN_JUMP;
        if (bChat || !(bJump ? (G(CanFly) && tu.game->CanFly()) : (G(CanNoclip) && tu.game->CanNoclip()))) continue;
        if (bJump && G(IsFlying) && tu.game->IsFlying()) {
            s->bFlyDone = 1;
            continue;
        }
        if (known - s->tDown >= TU_FLY_HOLD) {
            s->bFlyDone = 1;
            int r = bJump ? (G(ToggleFlight) ? tu.game->ToggleFlight() : -1) : (G(ToggleNoclip) ? tu.game->ToggleNoclip() : -1);
            if (r >= 0) tu_Haptic(TOUCHUI_HAPTIC_TAP);
            continue;
        }
        const float progress = (float)TU_MIN((now - s->tDown) / TU_FLY_HOLD, 1.0);
        if (bJump) jumpProgress = TU_MAX(jumpProgress, progress);
        else actProgress = TU_MAX(actProgress, progress);
    }
    tu.aRing[BTN_JUMP] = jumpProgress;
    tu.aRing[BTN_ACT] = actProgress;
    tu.aRing[BTN_DUCK] = duckProgress;

    // CHEAT held still 0.5 s: the cheat on or off
    float cheatProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_BUTTON || s->button != BTN_CHEAT || s->bFlyDone) continue;
        if (hypotf(s->lx - s->ox, s->ly - s->oy) >= TU_FLY_STILL || bChat || tu.bWheelOpen) {
            s->bFlyDone = 1;
            continue;
        }
        if (known - s->tDown >= TU_CHEAT_HOLD) {
            s->bFlyDone = 1;
            if (G(ToggleSithEwok) && tu.game->ToggleSithEwok() >= 0) tu_Haptic(TOUCHUI_HAPTIC_TAP);
            continue;
        }
        cheatProgress = TU_MAX(cheatProgress, (float)TU_MIN((now - s->tDown) / TU_CHEAT_HOLD, 1.0));
    }
    tu.aRing[BTN_CHEAT] = cheatProgress;

    // BACTA held 0.3 s on it: one bacta tank
    float bactaProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_BUTTON || s->button != BTN_BACTA || s->bFlyDone) continue;
        const tuButton* b = &tu_aButtons[BTN_BACTA];
        if (!tu_IsPointOnButton(s->lx, s->ly, BTN_BACTA) || tu_Hidden(BTN_BACTA) || bChat || tu.bWheelOpen) {
            s->bFlyDone = 1;
            continue;
        }
        if (known - s->tDown >= TU_BACTA_HOLD) {
            if (tu.aPulseQueue[b->scancode] || tu.aPulseReads[b->scancode] || tu.aPulseGap[b->scancode]) {
                bactaProgress = 1.0f;
                continue;
            }
            s->bFlyDone = 1;
            if (G(SelectItem)) tu.game->SelectItem(b->bin);
            tu_QueuePress(b->scancode);
            tu_Haptic(TOUCHUI_HAPTIC_TAP);
            continue;
        }
        bactaProgress = TU_MAX(bactaProgress, (float)TU_MIN((now - s->tDown) / TU_BACTA_HOLD, 1.0));
    }
    tu.aRing[BTN_BACTA] = bactaProgress;

    // FORCE held with Force Jump selected: how high it will go
    float fjump = 0.0f;
    const int jumpRank = G(GetForceJumpRank) ? tu.game->GetForceJumpRank() : 0;
    if (tu.jumpStyle != TU_JUMP_CLASSIC && jumpRank >= 1 && G(GetCurPower) && tu.game->GetCurPower() == TU_FORCEJUMP_BIN && !bChat
        && !tu.bWheelOpen) {
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            const tuSlot* s = &tu.aSlots[i];
            if (!s->bUsed || s->role != ROLE_BUTTON || s->button != BTN_FORCE) continue;
            const double held = now - s->tDown, longest = jumpRank * TU_JUMP_STEP_SECS;
            int ticks;
            if (tu.jumpStyle == TU_JUMP_STEPS) {
                const int step = TU_MIN(jumpRank, 1 + (int)(held / TU_JUMP_STEP_SECS));
                fjump = TU_MAX(fjump, (float)step / jumpRank);
                ticks = step - 1;
            }
            else {
                fjump = TU_MAX(fjump, (float)TU_MIN(held / longest, 1.0));
                ticks = held >= longest ? 1 : 0;
            }
            if (ticks > tu.jumpTicked) tu_Haptic(TOUCHUI_HAPTIC_TAP);
            tu.jumpTicked = ticks;
        }
    }
    if (fjump <= 0.0f) tu.jumpTicked = 0;
    tu.fjumpRing = fjump;

    // The cheat switched some other way (typed)
    if (tu_IsCheatOn() != tu.bCheatOn) {
        tu.bCheatOn = tu_IsCheatOn();
        if (tu.bCheatOn && !tu.bShowCheat) tu_SetShowCheat(1);
        if (G(TakeSithEwokTypedOff) && tu.game->TakeSithEwokTypedOff() && !tu.bCheatOn && tu.bShowCheat) tu_SetShowCheat(0);
    }

    // QUICK SAVE, QUICK LOAD, the tray's GYRO and MENU held
    float saveProgress = 0.0f, loadProgress = 0.0f;
    int done = -1;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_BUTTON) continue;
        if (tu_IsQuickHold(s->button) && !s->bFired) {
            if (!tu_IsPointOnButton(s->lx, s->ly, s->button) || bChat) {
                s->bFired = 1;
                continue;
            }
            double hold = (s->button == BTN_QUICKSAVE) ? TU_QUICKSAVE_HOLD : TU_QUICKLOAD_HOLD;
            if (known - s->tDown >= hold) {
                if (done != BTN_QUICKSAVE) done = s->button; // (both in one frame: QUICK SAVE)
            }
            else {
                float progress = (float)TU_MIN((now - s->tDown) / hold, 1.0);
                if (s->button == BTN_QUICKSAVE) saveProgress = TU_MAX(saveProgress, progress);
                else loadProgress = TU_MAX(loadProgress, progress);
            }
        }
        else if (s->button == BTN_TRAYGYRO && !s->bFired && tu.bTrayOpen && tu_GyroAvailable()) {
            if (known - s->tDown >= TU_MENU_HOLD) {
                s->bFired = 1;
                tu.aRing[BTN_TRAYGYRO] = 0.0f;
                tu_SetGyroRow(1);
            }
            else tu.aRing[BTN_TRAYGYRO] = (float)TU_MIN((now - s->tDown) / TU_MENU_HOLD, 1.0);
        }
        else if (s->button == BTN_MENU && !s->bFired) {
            if (!tu_IsPointOnButton(s->lx, s->ly, BTN_MENU)) {
                s->bFired = 1;
                tu.aRing[BTN_MENU] = 0.0f;
                continue;
            }
            if (known - s->tDown >= TU_MENU_HOLD) {
                s->bFired = 1;
                tu.aRing[BTN_MENU] = 0.0f;
                tu_SetTrayOpen(1);
            }
            else tu.aRing[BTN_MENU] = (float)TU_MIN((now - s->tDown) / TU_MENU_HOLD, 1.0);
        }
    }
    if (done >= 0) {
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* o = &tu.aSlots[i];
            if (o->bUsed && o->role == ROLE_BUTTON && tu_IsQuickHold(o->button)) o->bFired = 1;
        }
        saveProgress = loadProgress = 0.0f;
        if (done == BTN_QUICKSAVE) tu_QueuePress(tu_aButtons[BTN_QUICKSAVE].scancode);
        else if (G(QuickLoad)) tu.game->QuickLoad();
    }
    tu.aRing[BTN_QUICKSAVE] = saveProgress;
    tu.aRing[BTN_QUICKLOAD] = loadProgress;

    // FORCE's label
    const char* name = G(GetForcePowerName) ? tu.game->GetForcePowerName() : NULL;
    tu.forceName = name;
    tu.bForceNameSet = 1;

    // The items the player has
    int bItemsChanged = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (tu_aButtons[i].kind != KIND_ITEM) continue;
        int amount = 0, bActive = 0;
        int bHave = G(GetItem) ? tu.game->GetItem(tu_aButtons[i].bin, &amount, &bActive) != 0 : 0;
        if (!bHave) { amount = 0; bActive = 0; }
        if (bHave != tu.aItemHave[i]) bItemsChanged = 1;
        tu.aItemHave[i] = bHave;
        tu.aItemAmount[i] = amount;
        tu.aItemActive[i] = bActive;
    }
    if (bItemsChanged && tu.bShowView) tu_Layout();

    // Always Run can change in the options; the marker moves out of the way of a new item
    int bAlwaysRun = G(IsAlwaysRun) ? tu.game->IsAlwaysRun() : 0;
    if (bAlwaysRun != tu.bAlwaysRun || bItemsChanged) {
        tu.bAlwaysRun = bAlwaysRun;
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (!s->bUsed || s->role != ROLE_STICK) continue;
            int bWasRunning = tu.bStickRunning;
            tu_PlaceRunMarker(s->ox, s->oy);
            tu.bStickRunning = bWasRunning && tu.bRunMarker;
            tu_UpdateStickRun(s->lx, s->ly);
        }
        tu_RecomputeKeys();
    }

    // Presses queued while the typing line opens or closes are dropped
    if ((bChat != 0) != tu.bChatWasOpen) {
        tu.bChatWasOpen = bChat != 0;
        memset(tu.aPulseQueue, 0, sizeof(tu.aPulseQueue));
        memset(tu.aPulseReads, 0, sizeof(tu.aPulseReads));
        memset(tu.aPulseGap, 0, sizeof(tu.aPulseGap));
    }

    // The force meter
    int bFull = 0;
    tu.forceFrac = G(GetForceMana) ? tu.game->GetForceMana(&bFull) : -1.0f;
    if (bFull && !tu.bForceFull) tu.tForceFull = now;
    tu.bForceFull = bFull;

    // The FPS readout
    if (tu.bShowFps && G(GetFrameCount)) {
        unsigned int frames = tu.game->GetFrameCount();
        if (!tu.bFpsBase || frames < tu.fpsBaseFrames) {
            tu.bFpsBase = 1;
            tu.fpsBaseFrames = frames;
            tu.fpsBaseTime = now;
        }
        else if (now - tu.fpsBaseTime >= TU_FPS_PERIOD) {
            double fps = (double)(frames - tu.fpsBaseFrames) / (now - tu.fpsBaseTime);
            snprintf(tu.fpsText, sizeof(tu.fpsText), "%d FPS", (int)lround(fps));
            tu.fpsBaseFrames = frames;
            tu.fpsBaseTime = now;
        }
    }
    tu_RecomputeKeys();
}

// ------------------------------------------------------------ API

void touchUi_Init(const touchUiGame* pGame, const char* pSettingsPath)
{
    memset(&tu, 0, sizeof(tu));
    tu.game = pGame;
    tu.tJumpTapUp = -1.0;
    tu.gyroTurnedRound = -1.0e9;
    tu.wheelLastHot = -1;
    tu.bAlwaysRun = -1;
    snprintf(tu.fpsText, sizeof(tu.fpsText), "-- FPS");
    if (pSettingsPath) snprintf(tu.settingsPath, sizeof(tu.settingsPath), "%s", pSettingsPath);
    tu_LoadSettings();
    if (G(SetCheatShown)) tu.game->SetCheatShown(tu.bShowCheat);
    if (G(SetThirdPerson)) tu.game->SetThirdPerson(tu.bThirdPerson);
    if (G(SetForceJumpStyle)) tu.game->SetForceJumpStyle(tu.jumpStyle);
    tu.bCheatOn = tu_IsCheatOn();
}

void touchUi_SetScreen(float w, float h, float l, float t, float r, float b)
{
    // the HUD's gauge can move too (a new level, the HUD's size)
    float g[4] = { 0, 0, 0, 0 };
    int bGauge = tu_LayoutGaugeRect(w, g);
    if (tu.bLaidOut && w == tu.W && h == tu.H && l == tu.inL && t == tu.inT && r == tu.inR && b == tu.inB
        && bGauge == tu.bLayoutGauge && !memcmp(g, tu.aLayoutGauge, sizeof(g)))
        return;
    tu.W = w; tu.H = h; tu.inL = l; tu.inT = t; tu.inR = r; tu.inB = b;
    tu_Layout();
    if (tu.bWheelOpen) tu_LayoutWheel();
}

void touchUi_Relayout(void)
{
    tu_Layout();
    if (tu.bWheelOpen) tu_LayoutWheel();
}

void touchUi_SetShown(int bShown)
{
    bShown = bShown != 0;
    if (bShown == tu.bShown) return;
    tu_ResetAll();
    tu.bShown = bShown;
}

int touchUi_IsShown(void) { return tu.bShown; }

void touchUi_SetOtherInput(int bInUse)
{
    if (!bInUse) {
        tu.bOtherInput = 0;
        return;
    }
    if (tu.bOtherInput) return;
    tu.bOtherInput = 1;
    tu_SetTrayOpen(0); // (nothing left open that can't be seen)
}

int touchUi_IsHiddenForOtherInput(void) { return tu.bOtherInput; }

void touchUi_TakeLook(int* pDx, int* pDy)
{
    int dx = (int)tu.lookX, dy = (int)tu.lookY;
    tu.lookX -= (float)dx;
    tu.lookY -= (float)dy;
    *pDx = dx;
    *pDy = dy;
}

int touchUi_IsKeyHeld(int scancode)
{
    if (scancode < 0 || scancode >= TU_NUM_SCANCODES || !tu.bShown) return 0;
    return tu.aKeyDown[scancode];
}

int touchUi_IsScancodeDown(int scancode)
{
    if (scancode < 0 || scancode >= TU_NUM_SCANCODES || !tu.bShown) return 0;
    int bHeld = tu.aKeyDown[scancode];
    if (bHeld) {
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (s->bUsed && s->role == ROLE_BUTTON && tu_aButtons[s->button].scancode == scancode) s->bSeen = 1;
        }
    }
    if (tu.aPulseReads[scancode]) {
        if (--tu.aPulseReads[scancode] == 0) tu.aPulseGap[scancode] = 1;
        return 1;
    }
    if (tu.aPulseGap[scancode]) {
        tu.aPulseGap[scancode] = 0;
        return bHeld;
    }
    if (tu.aPulseQueue[scancode]) {
        tu.aPulseQueue[scancode]--;
        tu.aPulseReads[scancode] = TU_PULSE_READS - 1;
        if (!tu.aPulseReads[scancode]) tu.aPulseGap[scancode] = 1;
        return 1;
    }
    return bHeld;
}

int touchUi_TakeEscape(void)
{
    if (tu.menuPulse > 0) {
        tu.menuPulse--;
        return 1;
    }
    return 0;
}

int touchUi_TakeHaptics(void)
{
    int h = tu.haptics;
    tu.haptics = 0;
    return h;
}

void touchUi_SetGyroAvailable(int bAvailable) { tu.bGyroAvailable = bAvailable != 0; }
int touchUi_GetGyroMode(void) { return tu.gyroMode; }
float touchUi_GetGyroSens(void) { return tu_aGyroSens[tu.gyroSens]; }
int touchUi_IsFpsShown(void) { return tu.bShowFps; }

int touchUi_GetTopTextRect(float* pTop, float* pLeft, float* pRight)
{
    if (!tu.bTopText) return 0;
    *pTop = tu.aTopText[0];
    *pLeft = tu.aTopText[1];
    *pRight = tu.aTopText[2];
    return 1;
}

int touchUi_GetButton(int button, float* pX, float* pY, float* pR)
{
    if (button < 0 || button >= BTN_COUNT) return 0;
    if (pX) *pX = tu_aButtons[button].x;
    if (pY) *pY = tu_aButtons[button].y;
    if (pR) *pR = tu_aButtons[button].radius;
    return !tu_Hidden(button);
}

int touchUi_IsWheelOpen(void) { return tu.bWheelOpen; }
int touchUi_GetWheelKind(void) { return tu.wheelKind; }
int touchUi_IsWheelTapMode(void) { return tu.bWheelTapMode; }
int touchUi_GetWheelHot(void) { return tu.bWheelOpen ? tu_WheelHot(NULL, NULL) : -1; }
int touchUi_GetWheelSliceBin(int slice)
{
    if (!tu.pWheelMap || slice < 0 || slice >= tu.pWheelMap->numSlices) return -1;
    return tu.pWheelMap->aSlices[slice].bin;
}
int touchUi_GetWheelSlice(int bin, float* pDeg)
{
    if (!tu.pWheelMap) return -1;
    for (int i = 0; i < tu.pWheelMap->numSlices; i++) {
        if (tu.pWheelMap->aSlices[i].bin == bin) {
            if (pDeg) *pDeg = tu.pWheelMap->aSlices[i].deg;
            return i;
        }
    }
    return -1;
}
void touchUi_GetWheelGeometry(float* pX, float* pY, float* pR0, float* pR1)
{
    *pX = tu.wheelCx; *pY = tu.wheelCy; *pR0 = tu.wheelR0; *pR1 = tu.wheelR1;
}
int touchUi_IsTrayOpen(void) { return tu.bTrayOpen; }
int touchUi_IsGyroRowOpen(void) { return tu.bGyroRow; }
int touchUi_IsStickActive(void) { return tu.bStickActive; }
int touchUi_IsStickRunning(void) { return tu.bStickRunning; }
float touchUi_GetRingProgress(int button) { return (button >= 0 && button < BTN_COUNT) ? tu.aRing[button] : 0.0f; }
int touchUi_IsCheatShown(void) { return tu.bShowCheat; }
int touchUi_IsViewShown(void) { return tu.bShowView; }
int touchUi_IsSwapped(void) { return tu.bSwapped; }
int touchUi_GetJumpStyle(void) { return tu.jumpStyle; }

// ------------------------------------------------------------ drawing

static touchUiDrawList tu_draw;

#define TU_BLUE(a) touchUi_Rgba(0.47f, 0.78f, 1.0f, (a))
#define TU_GOLD(a) touchUi_Rgba(1.0f, 0.85f, 0.3f, (a))

// iOS's label sizes (IOSTouch_FontSize)
static float tu_FontSize(const char* label, float radius)
{
    if (strchr(label, '\n')) return radius >= 28 ? 10.0f : 9.0f;
    return radius >= 40 ? 16.0f : (radius >= 25 ? 13.0f : 10.0f);
}

// A label in a button's circle, shrunk to fit its width (to 0.6 of its size)
static void tu_DrawLabel(const char* text, float x, float y, float size, float xscale, float maxW, touchUiColor c)
{
    float w;
    touchUiDraw_MeasureText(text, size, xscale, &w, NULL);
    if (w > maxW && w > 0.0f) size *= TU_MAX(0.6f, maxW / w);
    touchUiDraw_Text(&tu_draw, text, x, y, size, xscale, c);
}

// The hold ring round a button: fills clockwise from the top
static void tu_DrawHoldRing(const tuButton* b, float progress, float inset, touchUiColor c)
{
    if (progress <= 0.0f) return;
    touchUiDraw_Arc(&tu_draw, b->x, b->y, b->radius - inset, 3.0f, 0.0f, progress * 2.0f * (float)M_PI, 0, c);
}

// The Sith emblem (iosTouch.m's, from the owner's picture of it)
static void tu_DrawSithEmblem(float cx, float cy, float r, touchUiColor c)
{
    static const float aRing[6][2] = { { 0.4019f, 0.7680f }, { 0.0f, 1.0f }, { -0.4019f, 0.7680f }, { -0.3610f, 0.6972f }, { 0.0f, 0.9057f }, { 0.3610f, 0.6972f } };
    static const float aSpoke[13][2] = {
        { 0.0438f, 0.1961f }, { 0.0980f, 0.6679f }, { 0.2026f, 0.6075f }, { 0.2026f, 0.5761f }, { 0.2288f, 0.5761f }, { 0.2288f, 0.6491f },
        { 0.0f, 0.7811f },
        { -0.2288f, 0.6491f }, { -0.2288f, 0.5761f }, { -0.2026f, 0.5761f }, { -0.2026f, 0.6075f }, { -0.0980f, 0.6679f }, { -0.0438f, 0.1961f }
    };
    for (int k = 0; k < 6; k++) {
        const float a = k * (float)M_PI / 3.0f, ca = cosf(a), sa = sinf(a);
        for (int piece = 0; piece < 2; piece++) {
            const float (*aPt)[2] = piece ? aSpoke : aRing;
            const int n = piece ? 13 : 6;
            float xy[26];
            for (int i = 0; i < n; i++) {
                xy[2 * i] = cx + r * (aPt[i][0] * ca - aPt[i][1] * sa);
                xy[2 * i + 1] = cy - r * (aPt[i][0] * sa + aPt[i][1] * ca);
            }
            touchUiDraw_Polygon(&tu_draw, xy, n, c);
        }
    }
    touchUiDraw_Circle(&tu_draw, cx, cy, r * 0.2843f, c, 0.0f, touchUi_Clear());
}

// The keyboard's picture (the tray's keyboard button)
static void tu_DrawKeyboardIcon(float cx, float cy, touchUiColor c)
{
    touchUiDraw_RRect(&tu_draw, cx - 10.0f, cy - 6.5f, 20.0f, 13.0f, 2.5f, touchUi_Clear(), 1.5f, c);
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 5; i++)
            touchUiDraw_Circle(&tu_draw, cx - 6.0f + 3.0f * i, cy - 2.5f + 3.0f * row, 0.8f, c, 0.0f, touchUi_Clear());
    touchUiDraw_Line(&tu_draw, cx - 4.0f, cy + 3.5f, cx + 4.0f, cy + 3.5f, 1.4f, c);
}

// The camera's picture (the tray's camera switch, an outline)
static void tu_DrawCameraIcon(float cx, float cy, touchUiColor c)
{
    touchUiDraw_RRect(&tu_draw, cx - 9.0f, cy - 5.0f, 18.0f, 12.0f, 2.5f, touchUi_Clear(), 1.5f, c);
    touchUiDraw_RRect(&tu_draw, cx - 4.0f, cy - 8.0f, 8.0f, 3.5f, 1.0f, c, 0.0f, touchUi_Clear());
    touchUiDraw_Circle(&tu_draw, cx, cy + 1.0f, 3.5f, touchUi_Clear(), 1.5f, c);
}

// Whether a thumb still on MENU after its tray opened is over this tray button
static int tu_MenuThumbOver(int i)
{
    for (int t = 0; t < TU_MAX_TOUCHES; t++) {
        const tuSlot* s = &tu.aSlots[t];
        if (s->bUsed && s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && s->trayButton == i) return 1;
    }
    return 0;
}

static void tu_DrawButton(int i)
{
    const tuButton* b = &tu_aButtons[i];
    const int bHeld = tu.aButtonHeld[i] != 0 || tu_MenuThumbOver(i);
    const int bWheel = i == tu.wheelButton && tu.bWheelOpen;
    const int bGyro = tu_GyroAvailable();
    const int bOn = (b->kind == KIND_ITEM && tu.aItemActive[i])
                    || ((i == BTN_TRAYKEYS || i == BTN_MENU) && tu_IsChatOpen())
                    || (i == BTN_TRAYFPS && tu.bShowFps)
                    || (i == BTN_TRAYCHEAT && tu.bShowCheat)
                    || (i == BTN_TRAYVIEW && tu.bShowView)
                    || (i == BTN_TRAYGYRO && bGyro && tu.gyroMode != TOUCHUI_GYRO_OFF);
    const int bTray = tu_IsTrayButton(i) || tu_IsOptButton(i);
    float alpha = (bHeld || bOn || bWheel || bTray) ? 1.0f : TU_IDLE_ALPHA;
    if (i == BTN_FORCE && tu.bForceNameSet && !tu.forceName) alpha *= 0.45f;
    if (i == BTN_TRAYGYRO && !bGyro) alpha *= 0.45f;
    if (i == BTN_TRAYCAM) alpha *= 0.45f; // (no camera on Android yet)
    if ((i == BTN_GYROOPT_MODE || i == BTN_GYROOPT_SENS) && (!bGyro || tu.gyroMode == TOUCHUI_GYRO_OFF)) alpha *= 0.45f;
    tu_draw.alpha = alpha;

    touchUiColor bg = bWheel ? TU_BLUE(0.55f) : (bHeld ? touchUi_White(0.30f) : touchUi_Black(0.22f));
    touchUiColor border = touchUi_White(0.4f);
    if (i == BTN_FIRE) border = touchUi_Rgba(1.0f, 0.55f, 0.45f, 0.6f);
    else if (bOn) border = TU_GOLD(0.95f);
    if (i == BTN_CHEAT && tu.bCheatOn) border = touchUi_Rgba(0.9f, 0.32f, 0.28f, 0.8f);
    touchUiDraw_Circle(&tu_draw, b->x, b->y, b->radius, bg, 1.5f, border);

    const touchUiColor text = touchUi_White(0.85f);
    char buf[48];
    const char* label = b->label;
    float size = tu_FontSize(label, b->radius);
    float xscale = 1.0f;
    if (i == BTN_FORCE) {
        if (tu.forceName) {
            snprintf(buf, sizeof(buf), "FORCE\n%s", tu.forceName);
            label = buf;
            size = 10.0f;
        }
        else size = 13.0f;
    }
    else if (i == BTN_BACTA && tu.aItemAmount[i] > 1) {
        snprintf(buf, sizeof(buf), "BACTA\n%d", tu.aItemAmount[i]);
        label = buf;
        size = 9.0f;
    }
    else if (i == BTN_DUCK) {
        for (int t = 0; t < TU_MAX_TOUCHES; t++) {
            const tuSlot* s = &tu.aSlots[t];
            if (s->bUsed && s->role == ROLE_BUTTON && s->levelPick > 0) {
                snprintf(buf, sizeof(buf), "%d", s->levelPick);
                label = buf;
            }
        }
    }
    else if (i == BTN_VIEW) label = tu.bThirdPerson ? "3P" : "1P";
    else if (i == BTN_TRAYGYRO || i == BTN_TRAYJUMP || i == BTN_TRAYCTRL || i == BTN_GYROOPT_MODE || i == BTN_GYROOPT_SENS) {
        static const char* aModeName[3] = { "OFF", "TOUCH", "ALWAYS" };
        static const char* aJumpName[3] = { "SMOOTH", "STEPS", "CLASSIC" };
        if (i == BTN_TRAYGYRO) snprintf(buf, sizeof(buf), bGyro ? "GYRO\n%s" : "NO\nGYRO", aModeName[tu.gyroMode]);
        else if (i == BTN_TRAYJUMP) snprintf(buf, sizeof(buf), "JUMP\n%s", aJumpName[tu.jumpStyle]);
        else if (i == BTN_TRAYCTRL) snprintf(buf, sizeof(buf), "CONTROLS\n%s", tu.bSwapped ? "SWAPPED" : "NORMAL");
        else if (i == BTN_GYROOPT_MODE) snprintf(buf, sizeof(buf), "MODE\n%s", aModeName[tu.gyroLastMode]);
        else snprintf(buf, sizeof(buf), "SENS\n%.1fx", (double)tu_aGyroSens[tu.gyroSens]);
        label = buf;
        size = tu_FontSize("GYRO\nALWAYS", b->radius);
        xscale = TU_WHEEL_XSCALE;
    }
    if (i == BTN_CHEAT) {
        tu_DrawSithEmblem(b->x, b->y, b->radius * 0.64f, tu.bCheatOn ? touchUi_Rgba(0.9f, 0.32f, 0.28f, 0.95f) : text);
    }
    else if (i == BTN_TRAYKEYS) tu_DrawKeyboardIcon(b->x, b->y, text);
    else if (i == BTN_TRAYCAM) tu_DrawCameraIcon(b->x, b->y, text);
    else if (label[0]) tu_DrawLabel(label, b->x, b->y, size, xscale, 2.0f * b->radius, text);

    // sithewok's own uses of JUMP, ACT and DUCK, named under their labels
    if (tu.bCheatOn && (i == BTN_JUMP || i == BTN_ACT || i == BTN_DUCK)) {
        const char* sub = i == BTN_JUMP ? "FLY" : (i == BTN_ACT ? "NOCLIP" : "LVL SEL");
        touchUiDraw_Text(&tu_draw, sub, b->x, b->y + b->radius * 0.5f, 7.5f, 1.0f, touchUi_Rgba(1.0f, 0.58f, 0.52f, 0.95f));
    }
    // the hold rings
    if (i == BTN_QUICKSAVE || i == BTN_QUICKLOAD || i == BTN_MENU || i == BTN_WHEEL || i == BTN_NEXTWPN || i == BTN_JUMP
        || i == BTN_ACT || i == BTN_DUCK || i == BTN_CHEAT || i == BTN_BACTA || i == BTN_TRAYGYRO)
        tu_DrawHoldRing(b, tu.aRing[i], 2.5f, TU_BLUE(0.95f));
    if (i == BTN_FORCE) tu_DrawHoldRing(b, tu.fjumpRing, 7.5f, touchUi_Rgba(1.0f, 0.82f, 0.3f, 0.95f));
    tu_draw.alpha = 1.0f;
}

static void tu_DrawForceMeter(double now)
{
    const tuButton* b = &tu_aButtons[BTN_FORCE];
    if (tu.forceFrac < 0.005f) return;
    float op = tu.bForceFull ? 1.0f : (tu.aButtonHeld[BTN_FORCE] ? 1.0f : TU_IDLE_ALPHA);
    tu_draw.alpha = op;
    if (tu.bForceFull) {
        // a full meter glows, gently pulsing (0.9 s each way, eased)
        double ph = fmod(now - tu.tForceFull, 1.8) / 0.9;
        if (ph > 1.0) ph = 2.0 - ph;
        float k = (float)(0.5 - 0.5 * cos(ph * M_PI));
        float glow = 0.35f + 0.65f * k;
        touchUiDraw_Glow(&tu_draw, b->x, b->y, b->radius - 2.5f, 3.0f, 0.0f, 2.0f * (float)M_PI, 7.0f, TU_BLUE(0.9f * glow));
    }
    touchUiDraw_Arc(&tu_draw, b->x, b->y, b->radius - 2.5f, 3.0f, 0.0f, TU_MIN(tu.forceFrac, 1.0f) * 2.0f * (float)M_PI, 1, TU_BLUE(0.95f));
    tu_draw.alpha = 1.0f;
}

static void tu_DrawStick(void)
{
    if (!tu.bStickActive) return;
    const float ox = tu.stickOx, oy = tu.stickOy;
    touchUiDraw_Circle(&tu_draw, ox, oy, TU_STICK_RADIUS, touchUi_White(0.08f), 1.5f, touchUi_White(0.45f));
    const int bLit = tu.bStickRunning;
    if (tu.bRunMarker) {
        float dx = tu.runX - ox, dy = tu.runY - oy, d = sqrtf(dx * dx + dy * dy);
        if (d - TU_RUN_R - TU_STICK_RADIUS > 1.0f) {
            tu_draw.alpha = bLit ? 1.0f : TU_IDLE_ALPHA;
            touchUiDraw_Line(&tu_draw, ox + dx / d * TU_STICK_RADIUS, oy + dy / d * TU_STICK_RADIUS,
                             ox + dx / d * (d - TU_RUN_R), oy + dy / d * (d - TU_RUN_R), 1.5f,
                             bLit ? TU_BLUE(0.95f) : touchUi_White(0.45f));
            tu_draw.alpha = 1.0f;
        }
    }
    float kx = bLit ? tu.runX : ox + tu.stickX * TU_STICK_RADIUS;
    float ky = bLit ? tu.runY : oy + tu.stickY * TU_STICK_RADIUS;
    touchUiDraw_Circle(&tu_draw, kx, ky, TU_STICK_KNOB, touchUi_White(0.30f), 1.5f, touchUi_White(0.45f));
    if (tu.bRunMarker) {
        const float rr = TU_RUN_R;
        tu_draw.alpha = bLit ? 1.0f : TU_IDLE_ALPHA;
        touchUiDraw_Circle(&tu_draw, tu.runX, tu.runY, rr, bLit ? TU_BLUE(0.55f) : touchUi_Black(0.35f), 1.5f,
                           touchUi_White(bLit ? 0.85f : 0.45f));
        touchUiColor cc = touchUi_White(bLit ? 1.0f : 0.85f);
        for (int i = 0; i < 2; i++) {
            float tip = -5.5f + 5.5f * i;
            touchUiDraw_Line(&tu_draw, tu.runX - 6.0f, tu.runY + tip + 5.0f, tu.runX, tu.runY + tip, 2.0f, cc);
            touchUiDraw_Line(&tu_draw, tu.runX, tu.runY + tip, tu.runX + 6.0f, tu.runY + tip + 5.0f, 2.0f, cc);
        }
        tu_draw.alpha = 1.0f;
    }
}

// A wheel group's colour, mixed k of the way to it from the wheel's dark grey
static touchUiColor tu_WheelColour(const tuWheelGroup* g, float k, float a)
{
    return touchUi_Rgba((g->r * k + 32.0f * (1.0f - k)) / 255.0f, (g->g * k + 32.0f * (1.0f - k)) / 255.0f,
                        (g->b * k + 32.0f * (1.0f - k)) / 255.0f, a);
}

static void tu_DrawStar(float cx, float cy, float k, int bFilled, int bPop)
{
    float R = TU_WHEEL_STAR_R * k, y = cy + R * (1.0f - cosf((float)M_PI / 5.0f)) * 0.5f;
    float xy[20];
    for (int j = 0; j < 10; j++) {
        float r = (j & 1) ? R * TU_WHEEL_STAR_INNER : R;
        float a = (-90.0f + 36.0f * j) * (float)M_PI / 180.0f;
        xy[2 * j] = cx + r * cosf(a);
        xy[2 * j + 1] = y + r * sinf(a);
    }
    touchUiColor gold = bPop ? touchUi_Rgba(25.0f / 255.0f, 20.0f / 255.0f, 10.0f / 255.0f, 1.0f)
                             : touchUi_Rgba(1.0f, 204.0f / 255.0f, 64.0f / 255.0f, 1.0f);
    if (bFilled) {
        touchUiDraw_Polygon(&tu_draw, xy, 10, gold);
        if (!bPop)
            for (int j = 0; j < 10; j++)
                touchUiDraw_Line(&tu_draw, xy[2 * j], xy[2 * j + 1], xy[2 * ((j + 1) % 10)], xy[2 * ((j + 1) % 10) + 1],
                                 TU_WHEEL_STAR_EDGE * k, touchUi_Black(0.78f));
    }
    else {
        gold.a = bPop ? 0.47f : 0.43f;
        for (int j = 0; j < 10; j++)
            touchUiDraw_Line(&tu_draw, xy[2 * j], xy[2 * j + 1], xy[2 * ((j + 1) % 10)], xy[2 * ((j + 1) % 10) + 1],
                             TU_WHEEL_STAR_LINE * k, gold);
    }
}

static void tu_DrawWheel(void)
{
    const tuWheelMap* m = tu.pWheelMap;
    if (!m) return;
    const int bWeapons = tu.wheelKind == TOUCHUI_WHEEL_WEAPONS;
    const float cx = tu.wheelCx, cy = tu.wheelCy, R0 = tu.wheelR0, R1 = tu.wheelR1;
    tuWheelFit* F = &tu.aWheelFit[tu.wheelKind];

    // the game dimmed; the disc
    touchUiDraw_RRect(&tu_draw, 0.0f, 0.0f, tu.W, tu.H, 0.0f, touchUi_Black(0.45f), 0.0f, touchUi_Clear());
    touchUiDraw_Circle(&tu_draw, cx, cy, R1 + 14.0f, touchUi_Black(0.85f), 0.0f, touchUi_Clear());

    int bCancel = 0;
    tuSlot* pSlide = NULL;
    int hot = tu_WheelHot(&bCancel, &pSlide);
    int cur = bWeapons ? (G(GetCurWeapon) ? tu.game->GetCurWeapon() : -1) : (G(GetCurPower) ? tu.game->GetCurPower() : -1);
    int curSlice = -1, bAnyEarned = 0;
    for (int i = 0; i < m->numSlices; i++) {
        if (tu.aWheelEarned[i]) bAnyEarned = 1;
        if (m->aSlices[i].bin == cur) curSlice = i;
    }

    touchUiColor aText[TU_WHEEL_MAX];
    for (int pass = 0; pass < 2; pass++) { // (the popped-out slice on top)
        for (int i = 0; i < m->numSlices; i++) {
            const tuWheelSlice* sl = &m->aSlices[i];
            const tuWheelGroup* g = &m->aGroups[sl->group];
            const int bEarned = tu.aWheelEarned[i];
            const int bHot = i == hot;
            const int bPop = bHot && bEarned;
            if (bPop != pass) continue;
            touchUiColor fill, stroke, text;
            float lw;
            if (bWeapons && !bEarned && tu.aWheelOwned[i]) {
                fill = tu_WheelColour(g, 0.22f, 1.0f);
                stroke = (i == curSlice) ? touchUi_White(1.0f) : tu_WheelColour(g, bHot ? 0.8f : 0.55f, 1.0f);
                lw = (bHot || i == curSlice) ? 2.5f : 2.0f;
                text = touchUi_White(0.75f);
            }
            else if (!bEarned) {
                fill = touchUi_Rgba(bHot ? 0.24f : 0.18f, bHot ? 0.24f : 0.18f, bHot ? 0.24f : 0.18f, 0.92f);
                stroke = tu_WheelColour(g, bHot ? 0.7f : 0.45f, 1.0f);
                lw = bHot ? 2.5f : 2.0f;
                text = touchUi_Rgba(bHot ? 0.75f : 0.59f, bHot ? 0.75f : 0.59f, bHot ? 0.75f : 0.59f, 1.0f);
            }
            else if (bPop) {
                fill = tu_WheelColour(g, 1.0f, 1.0f);
                stroke = touchUi_White(1.0f);
                lw = 2.5f;
                text = touchUi_Rgba(0.08f, 0.08f, 0.08f, 1.0f);
            }
            else {
                fill = tu_WheelColour(g, 0.42f, 1.0f);
                stroke = (i == curSlice) ? touchUi_White(1.0f) : tu_WheelColour(g, 0.8f, 1.0f);
                lw = (i == curSlice) ? 2.5f : 2.0f;
                text = touchUi_White(1.0f);
            }
            aText[i] = text;
            const float half = m->sliceDeg * 0.5f - 1.0f;
            touchUiDraw_Wedge(&tu_draw, cx, cy, sl->deg - half, sl->deg + half, R0, R1 + (bPop ? TU_WHEEL_POP : 0.0f), fill, lw, stroke);
        }
    }

    // the needle: which way the sliding thumb points
    if (pSlide && pSlide->bArmed && hot != -1) {
        float a = pSlide->wheelAim * (float)M_PI / 180.0f;
        touchUiColor nc = (hot >= 0 && tu.aWheelEarned[hot]) ? tu_WheelColour(&m->aGroups[m->aSlices[hot].group], 1.0f, 1.0f)
                                                             : touchUi_Rgba(0.6f, 0.6f, 0.6f, 1.0f);
        touchUiDraw_Line(&tu_draw, cx + (R0 - 14.0f) * cosf(a), cy - (R0 - 14.0f) * sinf(a), cx + (R0 - 3.0f) * cosf(a),
                         cy - (R0 - 3.0f) * sinf(a), 3.0f, nc);
    }

    // each name in its slice, with its stars or its ammo count under it
    float rowW, rowH;
    tu_WheelRowSize(&rowW, &rowH);
    for (int i = 0; i < m->numSlices; i++) {
        const int bRow = F->font[i][1] > 0.0f && tu_WheelSliceHasRow(i) && (bWeapons ? tu.aWheelAmmo[i] >= 0 : tu.aWheelEarned[i]);
        const int bPop = i == hot && tu.aWheelEarned[i];
        float a = m->aSlices[i].deg * (float)M_PI / 180.0f;
        float px = cx + F->labelR[i][bRow] * cosf(a), py = cy - F->labelR[i][bRow] * sinf(a);
        char text[64];
        tu_WheelLabelText(i, text, sizeof(text));
        float lx = px, ly = bRow ? py - (TU_WHEEL_STAR_GAP + rowH) * 0.5f : py;
        touchUiDraw_Text(&tu_draw, text, lx, ly, F->font[i][bRow], TU_WHEEL_XSCALE, aText[i]);
        if (!bRow) continue;
        float rcy = py + (F->labelH[i][bRow] + TU_WHEEL_STAR_GAP) * 0.5f;
        if (!bWeapons) {
            float k = F->rowScale;
            for (int j = 0; j < TU_WHEEL_STARS; j++) {
                float x = px + (j - (TU_WHEEL_STARS - 1) * 0.5f) * TU_WHEEL_STAR_STEP * k;
                tu_DrawStar(x, rcy, k, j < tu.aWheelLevel[i], bPop);
            }
        }
        else {
            char num[16];
            snprintf(num, sizeof(num), "%d", TU_MIN(tu.aWheelAmmo[i], 999));
            touchUiDraw_Text(&tu_draw, num, px, rcy, TU_WHEEL_AMMO_FONT * F->rowScale, 1.0f, aText[i]);
        }
    }

    // group names beside the ring
    const float left = TU_MAX(tu.inL, 8.0f), right = tu.W - TU_MAX(tu.inR, 8.0f);
    const float top = TU_MAX(tu.inT, 8.0f), bottom = tu.H - TU_MAX(tu.inB, 8.0f);
    for (int gi = 0; gi < m->numGroups; gi++) {
        const tuWheelGroup* grp = &m->aGroups[gi];
        if (!grp->title) continue;
        float tw, th;
        touchUiDraw_MeasureText(grp->title, 12.0f, 1.0f, &tw, &th);
        for (int k = 0; k <= 30; k++) {
            float a = (grp->titleDeg + ((k & 1) ? 2.0f : -2.0f) * ((k + 1) / 2)) * (float)M_PI / 180.0f;
            float posx = cx, posy = cy;
            for (float r = R1 + 20.0f; r < R1 + 140.0f; r += 2.0f) {
                posx = cx + r * cosf(a);
                posy = cy - r * sinf(a);
                float ex = TU_MAX(fabsf(posx - cx) - tw * 0.5f, 0.0f);
                float ey = TU_MAX(fabsf(posy - cy) - th * 0.5f, 0.0f);
                if (sqrtf(ex * ex + ey * ey) >= R1 + 20.0f) break;
            }
            if (posx - tw * 0.5f < left || posx + tw * 0.5f > right || posy - th * 0.5f < top + 52.0f || posy + th * 0.5f > bottom) continue;
            int bClear = 1;
            for (int j = 0; j < BTN_COUNT && bClear; j++) {
                const tuButton* bt = &tu_aButtons[j];
                if (tu_Hidden(j) || j == tu.wheelButton) continue;
                float ex = TU_MAX(fabsf(bt->x - posx) - tw * 0.5f, 0.0f);
                float ey = TU_MAX(fabsf(bt->y - posy) - th * 0.5f, 0.0f);
                bClear = sqrtf(ex * ex + ey * ey) > bt->radius + 4.0f;
            }
            if (!bClear) continue;
            touchUiDraw_Text(&tu_draw, grp->title, posx, posy, 12.0f, 1.0f, tu_WheelColour(grp, 1.0f, 1.0f));
            break;
        }
    }

    // the middle: the name, what lifting does, "cancel" in the gap
    const char* name;
    touchUiColor tc;
    const char* hint;
    const touchUiColor grey = touchUi_Rgba(0.6f, 0.6f, 0.6f, 1.0f);
    if (hot >= 0) {
        name = tu_WheelSlotName(m->aSlices[hot].bin);
        tc = tu.aWheelEarned[hot] ? tu_WheelColour(&m->aGroups[m->aSlices[hot].group], 1.0f, 1.0f) : grey;
        if (tu.aWheelEarned[hot]) hint = "lift to select";
        else if (!bWeapons) hint = "not learned yet";
        else hint = tu.aWheelOwned[hot] ? "no ammo" : "not found yet";
    }
    else if (bCancel) {
        name = "CANCEL";
        tc = grey;
        hint = "lift to cancel";
    }
    else if (!bAnyEarned && !bWeapons) {
        name = "NO POWERS";
        tc = grey;
        hint = "none learned yet";
    }
    else {
        name = (curSlice >= 0) ? tu_WheelSlotName(cur) : (bWeapons ? "WEAPONS" : "FORCE");
        tc = (curSlice >= 0) ? tu_WheelColour(&m->aGroups[m->aSlices[curSlice].group], 1.0f, 1.0f) : touchUi_White(1.0f);
        if (bWeapons) hint = tu.bWheelTapMode ? "tap a weapon" : "slide toward a weapon";
        else hint = tu.bWheelTapMode ? "tap a power" : "slide toward a power";
    }
    {
        // the title: "BOW- CASTER" and "LIGHT- SABER" whole; a weapon's name too
        // long for one line goes on two
        char title[64];
        const char* src = name ? name : "";
        int o = 0;
        for (int k = 0; src[k] && o < (int)sizeof(title) - 1; k++) {
            if (src[k] == '-' && src[k + 1] == ' ') { k++; continue; }
            title[o++] = src[k];
        }
        title[o] = 0;
        const float Wt = 2.0f * R0 * 0.8f;
        float w1;
        touchUiDraw_MeasureText(title, 17.0f, 1.0f, &w1, NULL);
        if (bWeapons && strchr(title, ' ') && w1 * TU_WHEEL_TITLE_MIN > Wt) {
            for (char* p = title; *p; p++)
                if (*p == ' ') *p = '\n';
            float w2, h2;
            touchUiDraw_MeasureText(title, 17.0f, 1.0f, &w2, &h2);
            float k = TU_MIN(1.0f, TU_MIN(Wt / w2, (0.6f * R0 + 5.0f) / h2));
            float size = floorf(17.0f * k * 2.0f) / 2.0f;
            float h = ceilf(h2 * size / 17.0f);
            touchUiDraw_Text(&tu_draw, title, cx, cy + 5.0f - h * 0.5f, size, 1.0f, tc);
        }
        else {
            float size = 17.0f;
            if (w1 > Wt) size *= TU_MAX(TU_WHEEL_TITLE_MIN, Wt / w1);
            touchUiDraw_Text(&tu_draw, title, cx, cy - 7.0f, size, 1.0f, tc);
        }
    }
    {
        float hw, size = 10.0f;
        touchUiDraw_MeasureText(hint, size, 1.0f, &hw, NULL);
        if (hw > 2.0f * R0) size *= TU_MAX(0.8f, 2.0f * R0 / hw);
        touchUiDraw_Text(&tu_draw, hint, cx, cy + 13.0f, size, 1.0f, touchUi_Rgba(0.8f, 0.8f, 0.8f, 1.0f));
    }
    touchUiDraw_Text(&tu_draw, "cancel", cx, cy + R1 - 22.0f, 9.0f, 1.0f, touchUi_Rgba(0.59f, 0.59f, 0.59f, 1.0f));
}

static void tu_DrawFps(void)
{
    if (!tu.bShowFps) return;
    touchUiDraw_RRect(&tu_draw, tu.fpsX - TU_FPS_LABEL_HALF, tu.fpsY - 9.0f, 2.0f * TU_FPS_LABEL_HALF, 18.0f, 9.0f,
                      touchUi_Black(0.35f), 0.0f, touchUi_Clear());
    touchUiDraw_Text(&tu_draw, tu.fpsText, tu.fpsX, tu.fpsY, 11.0f, 1.0f, touchUi_White(0.85f));
}

const touchUiDrawList* touchUi_Draw(double now)
{
    touchUiDraw_Begin(&tu_draw);
    if (!tu.bShown || !tu.bLaidOut) return &tu_draw;
    if (tu.bOtherInput) {
        // hidden for another input: only an open wheel and the FPS readout
        tu_DrawFps();
        if (tu.bWheelOpen) {
            tu_DrawWheel();
            tu_DrawButton(tu.wheelButton);
        }
        return &tu_draw;
    }
    for (int i = 0; i < BTN_TRAY_FIRST; i++) {
        if (tu_Hidden(i) || (tu.bWheelOpen && i == tu.wheelButton)) continue;
        tu_DrawButton(i);
        if (i == BTN_FORCE) tu_DrawForceMeter(now);
    }
    if (tu.bTrayOpen) {
        touchUiDraw_RRect(&tu_draw, tu.trayBack[0], tu.trayBack[1], tu.trayBack[2], tu.trayBack[3], 26.0f, touchUi_Black(0.55f), 1.5f,
                          touchUi_White(0.4f));
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) tu_DrawButton(i);
    }
    if (tu.bGyroRow) {
        touchUiDraw_RRect(&tu_draw, tu.gyroRowBack[0], tu.gyroRowBack[1], tu.gyroRowBack[2], tu.gyroRowBack[3], 26.0f,
                          touchUi_Black(0.55f), 1.5f, touchUi_White(0.4f));
        for (int i = BTN_GYROOPT_FIRST; i <= BTN_GYROOPT_LAST; i++) tu_DrawButton(i);
    }
    tu_DrawFps();
    tu_DrawStick();
    if (tu.bWheelOpen) {
        tu_DrawWheel();
        tu_DrawButton(tu.wheelButton); // (lit, above the dimmed screen)
    }
    return &tu_draw;
}

// Added: entire file. See touchUi.h. The layout, the numbers and the
// gestures are iosTouch.m's (the iOS overlay), so the two feel the same; the
// comments there say why each one is as it is. Here it is plain C: touches
// in, keys, look, game actions, haptic requests and a draw list out.

#include "Platform/Touch/touchUi.h"
#include "Platform/Input/inputWheel.h" // the wheels: their maps, the pick, the drawing

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
#define TU_WHEEL_XSCALE 0.82f // (the labels' font: bold, condensed, as the wheels')
#define TU_WHEEL_POP 10.0f // (as inputWheel.c's: a finger this far past the ring still points)
#define TU_WHEEL_OPEN_SLIDE 10.0f
#define TU_WHEEL_HOLD 0.3
#define TU_WHEEL_DEAD 14.0f
#define TU_WHEEL_LEASH 60.0f
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
// The camera: a tap shorter than TU_SELFIE_HOLD takes the photos, held still
// that long the selfie, slid TU_WHEEL_OPEN_SLIDE a burst (a clean shot every
// TU_BURST_GAP, at most TU_BURST_MAX); a request no frame takes in
// TU_CAMERA_TIMEOUT is dropped. With FREEZE on, slid mostly down
// TU_FREEZE_SWIPE Action Freeze, up as far (or to TU_FREEZE_EDGE from the
// top, never less than TU_FREEZE_SWIPE_MIN) the cinematic burst. In Action
// Freeze one finger turns the camera TU_FREEZE_ORBIT degrees a point, the
// moves smoothed over TU_FREEZE_SMOOTH twice; its X is TU_FREEZE_CLOSE_R.
#define TU_SELFIE_HOLD 0.5
#define TU_CAMERA_TIMEOUT 0.5
#define TU_BURST_GAP 0.2
#define TU_BURST_MAX 10
#define TU_FREEZE_SWIPE 36.0f
#define TU_FREEZE_EDGE 6.0f
#define TU_FREEZE_SWIPE_MIN 16.0f
#define TU_FREEZE_ORBIT 0.3f
#define TU_FREEZE_SMOOTH 0.03
#define TU_FREEZE_CLOSE_R 22.0f
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

enum { ROLE_NONE = 0, ROLE_STICK, ROLE_LOOK, ROLE_BUTTON, ROLE_WHEEL, ROLE_IGNORED,
       ROLE_FREEZE,      // Action Freeze: moving the camera (one finger turns it, two move and zoom it)
       ROLE_FREEZESHOT,  // Action Freeze: on CAMERA (a tap takes a photo)
       ROLE_FREEZECLOSE  // Action Freeze: on its X (a tap goes back to the game)
};

enum {
    KIND_KEY = 0, KIND_MENU, KIND_HOLDSAVE, KIND_HOLDLOAD, KIND_WHEEL, KIND_ITEM, KIND_CHAT, KIND_FPS, KIND_GYRO,
    KIND_GYROMODE, KIND_GYROSENS, KIND_CAMSWITCH, KIND_CHEAT, KIND_CHEATSWITCH, KIND_VIEW, KIND_VIEWSWITCH,
    KIND_CONTROLS, KIND_JUMPSTYLE, KIND_CAMERA, KIND_CAMPHOTO, KIND_SELFIES, KIND_FREEZEOPT, KIND_CAMKIND,
    KIND_LEVEL, // (MENU tray, while sithewok is on) held still: counts the levels; lifted, goes to the one on show
};

#define BIN_BACTATANK 40
#define BIN_IRGOGGLES 41
#define BIN_FIELDLIGHT 42

enum {
    BTN_FIRE = TOUCHUI_BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_CAMERA, BTN_SABER, BTN_CHEAT, BTN_VIEW,
    BTN_TRAYGYRO, BTN_TRAYFPS, BTN_TRAYCAM, BTN_TRAYCHEAT, BTN_TRAYLEVEL, BTN_TRAYVIEW, BTN_TRAYJUMP, BTN_TRAYCTRL, BTN_TRAYKEYS,
    BTN_GYROOPT_MODE, BTN_GYROOPT_SENS,
    BTN_CAMROW_PHOTO, BTN_CAMROW_SELFIES, BTN_CAMROW_FREEZE,
    BTN_CAMOPT_CLEAN, BTN_CAMOPT_HUD, BTN_CAMOPT_BTNS,
    BTN_COUNT,
    BTN_TRAY_FIRST = BTN_TRAYGYRO, BTN_TRAY_LAST = BTN_TRAYKEYS,
    BTN_GYROOPT_FIRST = BTN_GYROOPT_MODE, BTN_GYROOPT_LAST = BTN_GYROOPT_SENS,
    BTN_CAMROW_FIRST = BTN_CAMROW_PHOTO, BTN_CAMROW_LAST = BTN_CAMROW_FREEZE,
    BTN_CAMOPT_FIRST = BTN_CAMOPT_CLEAN, BTN_CAMOPT_LAST = BTN_CAMOPT_BTNS
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
    [BTN_TRAYLEVEL] = { "LEVEL",        KIND_LEVEL,    -1,           20.0f, 0, 0 },
    [BTN_TRAYVIEW]  = { "VIEW",         KIND_VIEWSWITCH, -1,         20.0f, 0, 0 },
    [BTN_TRAYJUMP]  = { "JUMP",         KIND_JUMPSTYLE, -1,          20.0f, 0, 0 },
    [BTN_TRAYCTRL]  = { "CONTROLS",     KIND_CONTROLS, -1,           20.0f, 0, 0 },
    [BTN_TRAYKEYS]  = { "",             KIND_CHAT,     -1,           20.0f, 0, 0 },
    [BTN_GYROOPT_MODE] = { "MODE",      KIND_GYROMODE, -1,           20.0f, 0, 0 },
    [BTN_GYROOPT_SENS] = { "SENS",      KIND_GYROSENS, -1,           20.0f, 0, 0 },
    [BTN_CAMROW_PHOTO] = { "PHOTO",     KIND_CAMPHOTO, -1,           20.0f, 0, 0 },
    [BTN_CAMROW_SELFIES] = { "SELFIES", KIND_SELFIES,  -1,           20.0f, 0, 0 },
    [BTN_CAMROW_FREEZE] = { "FREEZE",   KIND_FREEZEOPT, -1,          20.0f, 0, 0 },
    [BTN_CAMOPT_CLEAN] = { "CLEAN",     KIND_CAMKIND,  -1,           20.0f, 0, TOUCHUI_CAPTURE_CLEAN },
    [BTN_CAMOPT_HUD]   = { "HUD",       KIND_CAMKIND,  -1,           20.0f, 0, TOUCHUI_CAPTURE_HUD },
    [BTN_CAMOPT_BTNS]  = { "HUD+\nBTNS", KIND_CAMKIND, -1,          20.0f, 0, TOUCHUI_CAPTURE_HUDBTNS },
};

static int tu_IsTrayButton(int b) { return b >= BTN_TRAY_FIRST && b <= BTN_TRAY_LAST; }
static int tu_IsCamOptButton(int b) { return b >= BTN_CAMOPT_FIRST && b <= BTN_CAMOPT_LAST; }
// The rows under the tray (one at a time): the gyro options, the camera
// options; and the photo kinds under the camera options
enum { TU_ROW_NONE = 0, TU_ROW_GYRO, TU_ROW_CAMERA };
static int tu_IsOptButton(int b)
{
    return (b >= BTN_GYROOPT_FIRST && b <= BTN_GYROOPT_LAST) || (b >= BTN_CAMROW_FIRST && b <= BTN_CAMROW_LAST) || tu_IsCamOptButton(b);
}
// A tray button held still opens its row (GYRO, the camera switch)
static int tu_IsRowOpener(int b) { return b == BTN_TRAYGYRO || b == BTN_TRAYCAM; }
static int tu_IsQuickHold(int b) { return b == BTN_QUICKSAVE || b == BTN_QUICKLOAD; }

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
    int bSwipe;         // CAMERA slid on its way to Action Freeze or the cinematic burst: not a tap, nor a hold
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
    float aTopTextTray[4][3]; // (while MENU's tray is open: under it, or under the row open below it)
    int bFpsBottom;
    float fpsX, fpsY;
    float trayBack[4], aRowBack[3][4]; // (the rows: the gyro options, the camera options, the photo kinds)
    int bBurstUnder;
    float freezeX, freezeY;
    // MENU's tray and the rows under it
    int bTrayOpen;
    int bTrayLevel; // the tray's LEVEL in its row (sithewok on, as the tray last read it)
    int bGyroRow; // (the gyro options' row is open: optRow == TU_ROW_GYRO)
    int optRow;
    int bKindsOpen;
    // settings
    int bShowFps, bShowCheat, bShowView, bThirdPerson, bSwapped, jumpStyle;
    int bShowCamera, cameraKinds, selfieShots, bFreezeOn, saberTarget;
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

    // the camera: a photo asked for, the burst, the photo modes, the flash
    double tCameraAsked;
    int bBurst;
    long long burstId;
    int burstShots;
    double tBurstNext;
    int bSelfie, bFreezing, bCinema;
    float aFreezeMove[5], aFreezeMid[5];
    double tFlash;
    int bFlashLight;
    int selfieFlashes;
    double prevTickNow;
    // the saber
    int bShowSaber;

    // the wheel
    // (opened by a finger: inputWheel.c's then; the pad's and the keys'
    // are inputOverlay.c's)
    int bWheelOpen, bWheelTapMode, wheelKind, wheelButton;
    const inputWheelMap* pWheelMap; // (the last one opened)
    float wheelCx, wheelCy, wheelR0, wheelR1;

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
static int tu_IsChatOpen(void) { return G(IsChatOpen) ? tu.game->IsChatOpen() : 0; }
static int tu_IsHolding(void) { return G(IsHolding) ? tu.game->IsHolding() : 0; }
static int tu_IsCheatOn(void) { return G(IsSithEwokOn) ? tu.game->IsSithEwokOn() : 0; }

static void tu_Haptic(int kind) { tu.haptics |= kind; }
static int tu_PhotoModeUp(void) { return tu.bSelfie || tu.bFreezing || tu.bCinema; }
// A photo mode the keyboard or a pad started (inputPhoto.c), not this
// overlay: theirs to work, the fingers do nothing meanwhile
static int tu_OtherPhotoModeUp(void)
{
    if (tu_PhotoModeUp()) return 0;
    return (G(SelfieIsActive) && tu.game->SelfieIsActive()) || (G(FreezeIsActive) && tu.game->FreezeIsActive())
           || (G(CinemaIsActive) && tu.game->CinemaIsActive());
}
static void tu_EndBurst(void);
static void tu_EndSelfie(void);
static void tu_EndFreeze(void);
static void tu_EndCinema(void);
static void tu_LayoutWheel(void);
static void tu_WheelClosed(void);
static int tu_SaberTargetToWheel(int t);

// ------------------------------------------------------------ settings

static void tu_SaveSettings(void)
{
    if (!tu.settingsPath[0]) return;
    FILE* f = fopen(tu.settingsPath, "w");
    if (!f) return;
    fprintf(f, "gyroMode=%d\ngyroLastMode=%d\ngyroSens=%.1f\nshowFps=%d\nshowCheat=%d\nshowView=%d\nthirdPerson=%d\njumpStyle=%d\nswapped=%d\n",
            tu.gyroMode, tu.gyroLastMode, (double)tu_aGyroSens[tu.gyroSens], tu.bShowFps, tu.bShowCheat, tu.bShowView,
            tu.bThirdPerson, tu.jumpStyle, tu.bSwapped);
    fprintf(f, "showCamera=%d\ncameraKinds=%d\nselfieShots=%d\nfreeze=%d\nsaberTarget=%d\n", tu.bShowCamera, tu.cameraKinds,
            tu.selfieShots, tu.bFreezeOn, tu.saberTarget);
    // the saber: the style by its name, each blade's colour of each style by its key
    if (G(SaberGetStyle) && G(SaberStyleName)) {
        const char* name = tu.game->SaberStyleName(tu.game->SaberGetStyle());
        if (name) fprintf(f, "saberStyle=%s\n", name);
    }
    if (G(SaberStyleName) && G(SaberStyleNumBlades) && G(SaberGetColour) && G(SaberColourKey)) {
        for (int st = TOUCHUI_SABER_SINGLE; st <= TOUCHUI_SABER_DUAL; st++) {
            const char* name = tu.game->SaberStyleName(st);
            for (int b = 0; name && b < tu.game->SaberStyleNumBlades(st); b++) {
                const char* key = tu.game->SaberColourKey(tu.game->SaberGetColour(st, b));
                if (key) fprintf(f, "saberColour.%s.%d=%s\n", name, b, key);
            }
        }
    }
    fclose(f);
}

static void tu_LoadSettings(void)
{
    tu.gyroMode = TOUCHUI_GYRO_OFF;
    tu.gyroLastMode = TOUCHUI_GYRO_TOUCH;
    tu.gyroSens = TU_GYRO_DEFAULT_SENS;
    tu.bShowFps = tu.bShowCheat = tu.bShowView = tu.bThirdPerson = tu.bSwapped = 0;
    tu.jumpStyle = TU_JUMP_STEPS;
    tu.bShowCamera = 1; // (shown unless switched off, as on iOS)
    tu.cameraKinds = TOUCHUI_CAPTURE_CLEAN;
    tu.selfieShots = 3;
    tu.bFreezeOn = 1;
    tu.saberTarget = TOUCHUI_CHIP_BOTH;
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
        else if (!strcmp(k, "showCamera")) tu.bShowCamera = n != 0;
        else if (!strcmp(k, "cameraKinds")) tu.cameraKinds = (n & 7) ? (n & 7) : TOUCHUI_CAPTURE_CLEAN;
        else if (!strcmp(k, "selfieShots")) tu.selfieShots = (n == 1 || n == 5) ? n : 3;
        else if (!strcmp(k, "freeze")) tu.bFreezeOn = n != 0;
        else if (!strcmp(k, "saberTarget"))
            tu.saberTarget = (n == TOUCHUI_CHIP_LEFT || n == TOUCHUI_CHIP_RIGHT) ? n : TOUCHUI_CHIP_BOTH;
        else if (!strncmp(k, "saber", 5)) {
            // the saber's: a key or a name, the line's end trimmed
            char* v = eq + 1;
            v[strcspn(v, "\r\n")] = 0;
            if (!strcmp(k, "saberStyle") && G(SaberStyleName) && G(SaberSetStyle)) {
                for (int st = TOUCHUI_SABER_SINGLE; st <= TOUCHUI_SABER_DUAL; st++) {
                    const char* name = tu.game->SaberStyleName(st);
                    if (name && !strcmp(name, v)) tu.game->SaberSetStyle(st);
                }
            }
            else if (!strncmp(k, "saberColour.", 12) && G(SaberStyleName) && G(SaberSetColour) && G(SaberColourByKey)) {
                const char* dot = strrchr(k, '.');
                for (int st = TOUCHUI_SABER_SINGLE; dot && st <= TOUCHUI_SABER_DUAL; st++) {
                    const char* name = tu.game->SaberStyleName(st);
                    if (!name || strlen(name) != (size_t)(dot - (k + 12)) || strncmp(k + 12, name, dot - (k + 12))) continue;
                    const int id = tu.game->SaberColourByKey(v);
                    tu.game->SaberSetColour(st, atoi(dot + 1), id >= 0 ? id : 0); // (a colour no more: the game's own)
                }
            }
        }
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
    if (i == BTN_CAMERA) return !tu.bShowCamera;
    if (i == BTN_SABER) return !tu.bShowSaber;
    if (b->kind == KIND_ITEM) return !tu.aItemHave[i];
    if (i == BTN_TRAYLEVEL) return !tu.bTrayOpen || !tu.bTrayLevel;
    if (tu_IsTrayButton(i)) return !tu.bTrayOpen;
    if (i >= BTN_GYROOPT_FIRST && i <= BTN_GYROOPT_LAST) return tu.optRow != TU_ROW_GYRO;
    if (i >= BTN_CAMROW_FIRST && i <= BTN_CAMROW_LAST) return tu.optRow != TU_ROW_CAMERA;
    if (tu_IsCamOptButton(i)) return !tu.bKindsOpen;
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
    if (button == BTN_NEXTWPN) return TOUCHUI_WHEEL_WEAPONS;
    if (button == BTN_SABER) return TOUCHUI_WHEEL_SABER;
    return TOUCHUI_WHEEL_FORCE;
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

// Whether a tray button is in its row now (LEVEL only while sithewok is on),
// and its place in the row counted from the right end (the keyboard, 0)
static int tu_TrayShown(int b) { return b != BTN_TRAYLEVEL || tu.bTrayLevel; }
static int tu_TrayFromRight(int b)
{
    int n = 0;
    for (int i = b + 1; i <= BTN_TRAY_LAST; i++) n += tu_TrayShown(i);
    return n;
}

// iosTouch_TrayKeysAt: where MENU's tray goes, clear of the buttons round
// FIRE. bAsShown: the row as it is now, with LEVEL while sithewok is on (the
// tray's own place); otherwise without it (FORCE's search: FORCE never moves
// with the cheat)
static void tu_TrayKeysAt(float mx, float my, float left, const float* pForce, float* pKx, float* pKy, float* pClear, float* pForceClear, int bAsShown)
{
    const float ty = my + 52;
    const float len = TU_TRAY_STEP * (bAsShown ? tu_TrayFromRight(BTN_TRAY_FIRST) : BTN_TRAY_LAST - BTN_TRAYGYRO - 1);
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
        const int aShow[3] = { tu.bShowSaber, tu.bShowCheat, tu.bShowView };
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
        tu.bBurstUnder = bAny && (k & 1);
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
        tu_TrayKeysAt(mx, my, left, NULL, &kx, &ky, &trayFree, NULL, 0);
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
                    tu_TrayKeysAt(mx, my, left, pf, &kx2, &ky2, &trayClear, &trayForce, 0);
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
        tu_TrayKeysAt(tu_aButtons[BTN_MENU].x, tu_aButtons[BTN_MENU].y, left, force, &kx, &ky, NULL, NULL, 1);
        // (LEVEL, while sithewok is on, right of the cheat switch; otherwise
        // the row closes up from the keyboard leftwards)
        const float len = TU_TRAY_STEP * tu_TrayFromRight(BTN_TRAY_FIRST);
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) {
            tu_aButtons[i].x = kx - TU_TRAY_STEP * tu_TrayFromRight(i);
            tu_aButtons[i].y = ky;
        }
        tu.trayBack[0] = kx - len - 26; tu.trayBack[1] = ky - 26; tu.trayBack[2] = len + 52; tu.trayBack[3] = 52;
    }
    // The rows under the tray, each centred under the button that opens it
    // (kept on screen): the gyro options under GYRO and the camera options
    // under the camera switch, a row down; the photo kinds under PHOTO, a
    // row further
    {
        const int aFirst[3] = { BTN_GYROOPT_FIRST, BTN_CAMROW_FIRST, BTN_CAMOPT_FIRST };
        const int aLast[3] = { BTN_GYROOPT_LAST, BTN_CAMROW_LAST, BTN_CAMOPT_LAST };
        const int aUnder[3] = { BTN_TRAYGYRO, BTN_TRAYCAM, BTN_CAMROW_PHOTO };
        for (int k = 0; k < 3; k++) {
            const tuButton* c = &tu_aButtons[aUnder[k]]; // (PHOTO is laid out by the time its row is)
            const int num = aLast[k] - aFirst[k] + 1;
            const float len = TU_TRAY_STEP * (num - 1);
            float x0 = c->x - len * 0.5f;
            x0 = TU_MIN(x0, right - 26 - len);
            x0 = TU_MAX(x0, left + 26);
            const float y = c->y + 52;
            for (int i = aFirst[k]; i <= aLast[k]; i++) {
                tu_aButtons[i].x = x0 + TU_TRAY_STEP * (i - aFirst[k]);
                tu_aButtons[i].y = y;
            }
            tu.aRowBack[k][0] = x0 - 26; tu.aRowBack[k][1] = y - 26; tu.aRowBack[k][2] = len + 52; tu.aRowBack[k][3] = 52;
        }
    }
    // Action Freeze's X: top right, inside the safe area
    tu.freezeX = right - TU_FREEZE_CLOSE_R - 4;
    tu.freezeY = top + TU_FREEZE_CLOSE_R + 4;

    tu.fpsX = tu.bFpsBottom ? W * 0.5f : right - TU_FPS_LABEL_X;
    tu.fpsY = tu.bFpsBottom ? bottom - 9.0f : top + 26;

    // The game's top message text; and, for while MENU's tray is open, just
    // under the tray or the row open under it (not covered by it), over the
    // widest stretch of game view no button reaches into, all the way down
    // TU_TOPTEXT_DEPTH (stacked lines too): never behind FORCE or DUCK
    for (int k = 0; k < 5; k++) {
        const float* aUnder[4] = { tu.trayBack, tu.aRowBack[0], tu.aRowBack[1], tu.aRowBack[2] };
        const float textTop = k == 0 ? top + 26 + tu_aButtons[BTN_MENU].radius + TU_TOPTEXT_GAP
                                     : aUnder[k - 1][1] + aUnder[k - 1][3] + TU_TOPTEXT_GAP;
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
        float x0 = W * 0.5f - half, x1 = W * 0.5f + half;
        if (k > 0) {
            float aL[BTN_COUNT], aR[BTN_COUNT];
            int n = 0;
            for (int i = 0; i < BTN_COUNT; i++) {
                if (tu_IsTrayButton(i) || tu_IsOptButton(i)) continue;
                const tuButton* q = &tu_aButtons[i];
                const float reach = q->radius + TU_TOUCH_SLOP;
                if (q->y + reach < textTop || q->y - reach > textTop + TU_TOPTEXT_DEPTH) continue;
                aL[n] = q->x - reach;
                aR[n] = q->x + reach;
                n++;
            }
            const float lo = left + TU_TOPTEXT_SIDE, hi = right - TU_TOPTEXT_SIDE;
            float best = -1.0f;
            for (int a = -1; a < n; a++) {
                const float gs = a < 0 ? lo : aR[a]; // (a stretch from the left edge or a button's right side...
                if (gs < lo || gs > hi) continue;
                float ge = hi;                     // ...to the nearest button to its right)
                int bInside = 0;
                for (int b = 0; b < n; b++) {
                    if (aL[b] < gs + 0.5f && aR[b] > gs + 0.5f) bInside = 1;
                    if (aL[b] >= gs) ge = TU_MIN(ge, aL[b]);
                }
                if (bInside || ge - gs <= best) continue;
                best = ge - gs;
                x0 = gs;
                x1 = ge;
            }
        }
        float* pText = k == 0 ? tu.aTopText : tu.aTopTextTray[k - 1];
        pText[0] = textTop;
        pText[1] = x0;
        pText[2] = x1;
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
            if (!tu_TrayShown(i)) continue;
            float d = fabsf(px - tu_aButtons[i].x);
            if (best < 0 || d < bestDist) { best = i; bestDist = d; }
        }
    }
    // (then the rows under it, each likewise)
    const int aFirst[3] = { BTN_GYROOPT_FIRST, BTN_CAMROW_FIRST, BTN_CAMOPT_FIRST };
    const int aLast[3] = { BTN_GYROOPT_LAST, BTN_CAMROW_LAST, BTN_CAMOPT_LAST };
    for (int k = 0; k < 3 && best < 0; k++) {
        if (tu_Hidden(aFirst[k])) continue;
        l = &tu_aButtons[aFirst[k]];
        r = &tu_aButtons[aLast[k]];
        if (tu_DistToSegment(px, py, l->x, l->y, r->x, r->y) > 26) continue;
        for (int i = aFirst[k]; i <= aLast[k]; i++) {
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

// Lets go of any touch on a row's buttons that went away
static void tu_ReleaseOptTouches(void)
{
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (s->bUsed && s->role == ROLE_BUTTON && tu_IsOptButton(s->button) && tu_Hidden(s->button)) {
            if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
            s->role = ROLE_IGNORED;
        }
    }
}

// Opens a row under the tray (TU_ROW_*), closing any other: one at a time
// (the photo kinds go with the camera options)
static void tu_SetOptRow(int row)
{
    if (tu.optRow == row) return;
    tu.optRow = row;
    tu.bGyroRow = row == TU_ROW_GYRO;
    if (row != TU_ROW_CAMERA) tu.bKindsOpen = 0;
    tu_ReleaseOptTouches();
}

// Opens or closes the photo kinds' row, under the camera options
static void tu_SetKindsOpen(int bOpen)
{
    bOpen = bOpen != 0 && tu.optRow == TU_ROW_CAMERA;
    if (tu.bKindsOpen == bOpen) return;
    tu.bKindsOpen = bOpen;
    tu_ReleaseOptTouches();
}

static void tu_EndLevelPick(tuSlot* s);

// The tray's LEVEL in its row or not, as the cheat is now (as the tray opens,
// and while it is open): the row laid out again; a count on it dropped if it goes
static void tu_RefreshTrayLevel(void)
{
    const int bLevel = tu_IsCheatOn() ? 1 : 0;
    if (bLevel == tu.bTrayLevel) return;
    tu.bTrayLevel = bLevel;
    if (!bLevel) {
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (!s->bUsed || s->role != ROLE_BUTTON || s->button != BTN_TRAYLEVEL) continue;
            tu_EndLevelPick(s);
            if (tu.aButtonHeld[BTN_TRAYLEVEL] > 0) tu.aButtonHeld[BTN_TRAYLEVEL]--;
            s->role = ROLE_IGNORED;
        }
        tu.aRing[BTN_TRAYLEVEL] = 0.0f;
    }
    if (tu.bLaidOut) tu_Layout();
}

static void tu_SetTrayOpen(int bOpen)
{
    bOpen = bOpen != 0;
    if (tu.bTrayOpen == bOpen) return;
    tu.bTrayOpen = bOpen;
    tu_RefreshTrayLevel(); // (LEVEL only while sithewok is on)
    if (!bOpen) {
        tu_SetOptRow(TU_ROW_NONE);
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* s = &tu.aSlots[i];
            if (!s->bUsed || s->role != ROLE_BUTTON) continue;
            if (tu_IsRowOpener(s->button)) tu.aRing[s->button] = 0.0f;
            if (s->button == BTN_TRAYLEVEL) {
                tu_EndLevelPick(s); // (closing the tray: no level)
                tu.aRing[BTN_TRAYLEVEL] = 0.0f;
            }
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
        // CAMERA shown or hidden, and remembered (Android's gallery needs no
        // question asked first, unlike iOS's Photos)
        tu.bShowCamera = !tu.bShowCamera;
        tu_SaveSettings();
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
    else if (kind == KIND_LEVEL) {
        // (a tap, or MENU's thumb lifted on it: nothing goes, it only says how)
        if (G(ShowMessageInPlace))
            tu.game->ShowMessageInPlace((G(NumPickableLevels) && tu.game->NumPickableLevels() > 0) ? "Hold LEVEL to count levels" : "No level to pick here");
    }
    else if (kind == KIND_JUMPSTYLE) {
        tu.jumpStyle = (tu.jumpStyle + 1) % TU_JUMP_NUM_STYLES;
        if (G(SetForceJumpStyle)) tu.game->SetForceJumpStyle(tu.jumpStyle);
        tu_SaveSettings();
    }
    if (kind == KIND_GYRO || kind == KIND_JUMPSTYLE || kind == KIND_CONTROLS || kind == KIND_LEVEL) {
        // (the tray stays, for another tap)
    }
    else {
        tu_SetTrayOpen(0);
    }
}

// A row's button tapped; the row stays open for another tap (a touch
// anywhere else closes it)
static void tu_UseOptButton(int button)
{
    switch (tu_aButtons[button].kind) {
        case KIND_GYROMODE:
            if (tu_GyroAvailable()) tu_GyroNextMode();
            break;
        case KIND_GYROSENS:
            if (tu_GyroAvailable()) tu_GyroNextSens();
            break;
        case KIND_CAMPHOTO:
            tu_SetKindsOpen(!tu.bKindsOpen);
            break;
        case KIND_SELFIES:
            tu.selfieShots = tu.selfieShots == 1 ? 3 : (tu.selfieShots == 3 ? 5 : 1);
            tu_SaveSettings();
            break;
        case KIND_FREEZEOPT:
            tu.bFreezeOn = !tu.bFreezeOn;
            tu_SaveSettings();
            break;
        case KIND_CAMKIND: {
            // a kind of photo ticked or unticked (the last one stays: a tap
            // always takes something)
            const int bit = tu_aButtons[button].bin;
            if ((tu.cameraKinds & bit) && !(tu.cameraKinds & ~bit)) break;
            tu.cameraKinds ^= bit;
            tu_SaveSettings();
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------ the camera (iosTouch.m's)

static void tu_Message(const char* m)
{
    if (G(ShowMessage)) tu.game->ShowMessage(m);
}

// CAMERA tapped: the ticked photos, at the next frame drawn
static void tu_TakePhotos(void)
{
    if (!G(CaptureRequest) || !tu.game->CaptureRequest(tu.cameraKinds, TOUCHUI_CAPTURE_PHOTO)) return; // (the last ones still being saved)
    tu.tCameraAsked = tu.lastNow;
}

// A burst: a finger slid off CAMERA, then a clean shot every TU_BURST_GAP (or
// as soon after as the saving keeps up) until it lifts, up to TU_BURST_MAX
static void tu_StartBurst(long long id)
{
    tu.bBurst = 1;
    tu.burstId = id;
    tu.burstShots = 0;
    tu.tBurstNext = 0.0;
    if (G(SetPhotoBatch)) tu.game->SetPhotoBatch(1);
}

static void tu_TickBurst(double now)
{
    if (!tu.bBurst || tu.burstShots >= TU_BURST_MAX || now < tu.tBurstNext) return;
    if (!G(CaptureRequest) || !tu.game->CaptureRequest(TOUCHUI_CAPTURE_CLEAN, TOUCHUI_CAPTURE_BURST)) return; // (next frame)
    tu.tCameraAsked = now;
    tu.burstShots++;
    tu.tBurstNext = now + TU_BURST_GAP;
}

static void tu_EndBurst(void)
{
    if (!tu.bBurst) return;
    tu.bBurst = 0;
    if (G(SetPhotoBatch)) tu.game->SetPhotoBatch(0);
}

// A selfie or Action Freeze starting: every touch now down let go (the game
// sees no buttons held, no stick), the tray closed; the buttons hide
static void tu_HideForPhotos(void)
{
    tu_SetTrayOpen(0);
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (tu.aSlots[i].bUsed) tu.aSlots[i].role = ROLE_IGNORED;
    memset(tu.aButtonHeld, 0, sizeof(tu.aButtonHeld));
    memset(tu.aRing, 0, sizeof(tu.aRing));
    tu_HideStick();
    tu_RecomputeKeys();
}

static void tu_ShowAfterPhotos(void)
{
    tu.lookX = tu.lookY = 0.0f; // (nothing moved meanwhile turns the view afterwards)
}

// The selfie: CAMERA held still TU_SELFIE_HOLD. The game holds still and
// everyone near poses for a camera held out at arm's length, for one photo
// or several (SELFIES), the engine running it (iosSelfieShots, enh_Tick);
// nothing shows and every touch is ignored meanwhile
static void tu_StartSelfie(void)
{
    if (tu_PhotoModeUp()) return;
    tu_EndBurst(); // (its end would take the selfie's photos out of their batch)
    const int r = G(SelfieStart) ? tu.game->SelfieStart(tu.selfieShots) : TOUCHUI_PHOTOMODE_CANT;
    if (r != TOUCHUI_PHOTOMODE_STARTED) {
        tu_Message(r == TOUCHUI_PHOTOMODE_BUSY ? "Still saving the last photo" : "Can't take a selfie here");
        return;
    }
    tu.selfieFlashes = 0;
    tu.bSelfie = 1;
    tu_HideForPhotos();
}

static void tu_EndSelfie(void)
{
    if (!tu.bSelfie) return;
    tu.bSelfie = 0;
    if (G(SelfieStop)) tu.game->SelfieStop(); // (ended early: the overlay going...)
    tu_ShowAfterPhotos();
}

// Action Freeze (CAMERA slid down, FREEZE on): the game stops just as it is
// and the camera moves round it for photos; only CAMERA and the X show
static void tu_StartFreeze(void)
{
    if (tu_PhotoModeUp()) return;
    tu_EndBurst();
    if (!G(FreezeBegin) || !tu.game->FreezeBegin()) {
        tu_Message("Can't freeze here");
        return;
    }
    tu.bFreezing = 1;
    memset(tu.aFreezeMove, 0, sizeof(tu.aFreezeMove));
    memset(tu.aFreezeMid, 0, sizeof(tu.aFreezeMid));
    if (G(FreezeSetSmooth)) tu.game->FreezeSetSmooth(1);
    tu_HideForPhotos();
}

static void tu_EndFreeze(void)
{
    if (!tu.bFreezing) return;
    tu.bFreezing = 0;
    // (a photo asked for and not taken yet would be of the game's own view)
    if (tu.tCameraAsked > 0.0) {
        if (G(CaptureCancel)) tu.game->CaptureCancel();
        tu.tCameraAsked = 0.0;
    }
    if (G(FreezeEnd)) tu.game->FreezeEnd(); // (the game goes on from the same moment)
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (tu.aSlots[i].bUsed) tu.aSlots[i].role = ROLE_IGNORED; // (still down, they only lift)
    tu_ShowAfterPhotos();
}

enum { TU_FZ_YAW = 0, TU_FZ_PITCH, TU_FZ_PANX, TU_FZ_PANY, TU_FZ_ZOOM, TU_FZ_NUM };

// A finger moving the Action Freeze camera: alone, it turns it round what it
// looks at (what is on screen follows the finger); with a second, the two
// move it across (by their middle) and zoom (by their spread)
static void tu_MoveFreeze(tuSlot* s, float px, float py)
{
    tuSlot* other = NULL;
    for (int i = 0; i < TU_MAX_TOUCHES && !other; i++) {
        tuSlot* o = &tu.aSlots[i];
        if (o != s && o->bUsed && o->role == ROLE_FREEZE) other = o;
    }
    if (!other) {
        tu.aFreezeMove[TU_FZ_YAW] -= (px - s->lx) * TU_FREEZE_ORBIT;
        tu.aFreezeMove[TU_FZ_PITCH] -= (py - s->ly) * TU_FREEZE_ORBIT;
        return;
    }
    const float h = tu.H > 1.0f ? tu.H : 1.0f;
    const float oldDist = hypotf(s->lx - other->lx, s->ly - other->ly), newDist = hypotf(px - other->lx, py - other->ly);
    tu.aFreezeMove[TU_FZ_PANX] += (px - s->lx) * 0.5f / h; // (the middle moves by half of this finger's move)
    tu.aFreezeMove[TU_FZ_PANY] += (py - s->ly) * 0.5f / h;
    if (oldDist > 8.0f && newDist > 8.0f) tu.aFreezeMove[TU_FZ_ZOOM] += logf(newDist / oldDist);
}

// Once a frame in Action Freeze: the camera follows the fingers' moves so
// far, part of the way (two smoothing steps), and glides to a stop after
// they lift; the engine's iosFreeze_Frame (enh_Tick) carries it on
static void tu_TickFreeze(double dt)
{
    if (dt > 0.1) dt = 0.1;
    const float k = (float)(1.0 - exp(-(dt > 0.0 ? dt : 0.0) / TU_FREEZE_SMOOTH));
    float a[TU_FZ_NUM];
    for (int i = 0; i < TU_FZ_NUM; i++) {
        const float m = tu.aFreezeMove[i] * k;
        tu.aFreezeMove[i] -= m;
        tu.aFreezeMid[i] += m;
        a[i] = tu.aFreezeMid[i] * k;
        tu.aFreezeMid[i] -= a[i];
        if (fabsf(tu.aFreezeMove[i]) + fabsf(tu.aFreezeMid[i]) < 1e-5f) tu.aFreezeMove[i] = tu.aFreezeMid[i] = 0.0f;
    }
    if ((a[TU_FZ_YAW] != 0.0f || a[TU_FZ_PITCH] != 0.0f) && G(FreezeOrbit) && !tu.game->FreezeOrbit(a[TU_FZ_YAW], a[TU_FZ_PITCH])) {
        // (a wall that way: the rest is dropped)
        tu.aFreezeMove[TU_FZ_YAW] = tu.aFreezeMid[TU_FZ_YAW] = 0.0f;
        tu.aFreezeMove[TU_FZ_PITCH] = tu.aFreezeMid[TU_FZ_PITCH] = 0.0f;
    }
    if ((a[TU_FZ_PANX] != 0.0f || a[TU_FZ_PANY] != 0.0f) && G(FreezePan)) tu.game->FreezePan(a[TU_FZ_PANX], a[TU_FZ_PANY]);
    if (a[TU_FZ_ZOOM] != 0.0f && G(FreezeZoom)) tu.game->FreezeZoom(expf(a[TU_FZ_ZOOM]));
}

// CAMERA tapped in Action Freeze: one clean photo of the frozen moment
static void tu_TakeFreezePhoto(void)
{
    if (!G(CaptureRequest) || !tu.game->CaptureRequest(TOUCHUI_CAPTURE_CLEAN, TOUCHUI_CAPTURE_PHOTO)) {
        tu_Message("Still saving the last photo");
        return;
    }
    tu.tCameraAsked = tu.lastNow;
}

// The cinematic burst (CAMERA slid up, FREEZE on): the action frozen, five
// photos round it (the engine's iosCinema), then the game goes on; nothing
// shows and every touch is ignored meanwhile, as for a selfie
static void tu_StartCinema(void)
{
    if (tu_PhotoModeUp()) return;
    tu_EndBurst(); // (its waiting capture would hold it up)
    const int r = G(CinemaStart) ? tu.game->CinemaStart() : TOUCHUI_PHOTOMODE_CANT;
    if (r != TOUCHUI_PHOTOMODE_STARTED) {
        tu_Message(r == TOUCHUI_PHOTOMODE_BUSY ? "Still saving the last photo" : "Can't freeze here");
        return;
    }
    tu.bCinema = 1;
    tu_HideForPhotos();
}

static void tu_EndCinema(void)
{
    if (!tu.bCinema) return;
    tu.bCinema = 0;
    if (G(CinemaStop)) tu.game->CinemaStop(); // (ended early)
    tu_ShowAfterPhotos();
}

void touchUi_CameraFeedback(int mode)
{
    // (a selfie's photos come a frame apart: after its first, a burst's
    // lighter flash)
    tu.bFlashLight = mode == TOUCHUI_CAPTURE_BURST || (mode == TOUCHUI_CAPTURE_SELFIE && tu.selfieFlashes++ > 0);
    tu.tCameraAsked = 0.0;
    tu.tFlash = tu.lastNow > 0.0 ? tu.lastNow : 1e-9;
    tu_Haptic(TOUCHUI_HAPTIC_TAP);
}

void touchUi_PhotoModeEnded(int mode)
{
    if (mode == TOUCHUI_MODE_SELFIE) tu_EndSelfie();
    else if (mode == TOUCHUI_MODE_CINEMA) tu_EndCinema();
}

int touchUi_IsPhotoModeUp(void) { return tu_PhotoModeUp(); }

// ------------------------------------------------------------ the saber (iosTouch.m's)

static int tu_SaberStyle(void) { return G(SaberGetStyle) ? tu.game->SaberGetStyle() : TOUCHUI_SABER_SINGLE; }
static int tu_SaberNumBlades(int st) { return G(SaberStyleNumBlades) ? tu.game->SaberStyleNumBlades(st) : 1; }

// The blade a colour goes to with two: the touch overlay's numbers
// (TOUCHUI_CHIP_LEFT, _BOTH, _RIGHT) and the wheel's (INPUTWHEEL_SABER_*).
// The wheel's is the one in use (a pad and the keys set it too); kept here
// as it changes, to remember.
static int tu_SaberTargetToWheel(int t)
{
    return t == TOUCHUI_CHIP_LEFT ? INPUTWHEEL_SABER_LEFT : t == TOUCHUI_CHIP_RIGHT ? INPUTWHEEL_SABER_RIGHT : INPUTWHEEL_SABER_BOTH;
}

static int tu_SaberTargetFromWheel(int t)
{
    return t == INPUTWHEEL_SABER_LEFT ? TOUCHUI_CHIP_LEFT : t == INPUTWHEEL_SABER_RIGHT ? TOUCHUI_CHIP_RIGHT : TOUCHUI_CHIP_BOTH;
}

static void tu_SaberTargetFromShared(void)
{
    const int t = tu_SaberTargetFromWheel(inputWheel_GetSaberTarget());
    if (t == tu.saberTarget) return;
    tu.saberTarget = t;
    tu_SaveSettings();
}

// One of the saber wheel's choices tapped: one saber or two (each keeps its
// own colours), or which of the two a colour goes to -- the wheel stays open
// to tap a colour; or EQUIP: the saber in hand, the wheel closed
static void tu_UseSaberChip(int chip)
{
    if (!tu.bWheelOpen) return;
    inputWheel_SaberChip(chip);
    if (!inputWheel_IsOpen()) {
        tu_WheelClosed(); // (EQUIP: the saber taken)
        return;
    }
    if (chip == TOUCHUI_CHIP_EQUIP) return; // (no saber to take)
    tu_SaberTargetFromShared();
    tu_SaveSettings();
    tu.bWheelTapMode = 1;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        // (a thumb that opened it, still down: lifted, it leaves it open to tap)
        tuSlot* o = &tu.aSlots[i];
        if (o->bUsed && o->role == ROLE_WHEEL && o->bWheelOpener) o->bWheelHeld = 1;
    }
    tu_LayoutWheel();
}

// The saber wheel's choice under (px, py) (a little round each), or -1
static int tu_SaberChipAt(float px, float py)
{
    return tu.bWheelOpen ? inputWheel_ChipAt(px, py) : -1;
}

// ------------------------------------------------------------ the wheels
//
// The wheels are inputWheel.c's, as the pad's and the keyboard's are: their
// maps, which slice an angle is in, what a pick does, how they look. Here:
// the fingers (a slide from the button, a hold, taps), each finger's slice
// handed to the wheel, and the wheel drawn in this overlay's list, under the
// button that opened it.

// Whether a box (points) is clear of every button showing but the wheel's
// own: the group names and the saber wheel's choices go only there
static int tu_WheelBoxClear(float x0, float y0, float x1, float y1)
{
    for (int j = 0; j < BTN_COUNT; j++) {
        const tuButton* bt = &tu_aButtons[j];
        if (tu_Hidden(j) || j == tu.wheelButton) continue;
        const float ex = TU_MAX(TU_MAX(x0 - bt->x, bt->x - x1), 0.0f);
        const float ey = TU_MAX(TU_MAX(y0 - bt->y, bt->y - y1), 0.0f);
        if (sqrtf(ex * ex + ey * ey) <= bt->radius + 4.0f) return 0;
    }
    return 1;
}

static void tu_WheelScreen(inputWheelScreen* pScreen)
{
    pScreen->w = tu.W;
    pScreen->h = tu.H;
    pScreen->inL = tu.inL;
    pScreen->inT = tu.inT;
    pScreen->inR = tu.inR;
    pScreen->inB = tu.inB;
}

// The open wheel laid out on the screen as it is now (under the top row of
// buttons, clear of the others), and where it is
static void tu_LayoutWheel(void)
{
    if (!tu.bWheelOpen) return;
    inputWheelScreen sc;
    tu_WheelScreen(&sc);
    inputWheel_SetKeepClear(tu_WheelBoxClear, TU_MAX(tu.inT, 8.0f) + 52.0f);
    inputWheel_Layout(&sc);
    inputWheel_GetGeometry(&tu.wheelCx, &tu.wheelCy, &tu.wheelR0, &tu.wheelR1);
}

static int tu_WheelSliceForAngle(float deg, int cur)
{
    return inputWheel_SliceForAngle(inputWheel_GetOpenMap(), deg, cur);
}

static int tu_WheelSliceAt(float px, float py, int cur)
{
    return inputWheel_SliceAt(px, py, cur);
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
        if (!s->bWheelOpener && tu_SaberChipAt(s->lx, s->ly) >= 0) continue; // (on a choice beside the saber wheel: not cancelling)
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

// The wheel told what the fingers point at (each new slice, or the gap,
// ticks in the hand there), the needle where the sliding thumb points, and
// whether it is open to tap (its hint)
static void tu_WheelTouched(void)
{
    if (!tu.bWheelOpen) return;
    tuSlot* pSlide = NULL;
    int bCancel = 0;
    const int hot = tu_WheelHot(&bCancel, &pSlide);
    const int bNeedle = pSlide && pSlide->bArmed;
    inputWheel_SetTapMode(tu.bWheelTapMode);
    inputWheel_Point(hot, bNeedle, bNeedle ? pSlide->wheelAim : 0.0f);
    inputWheel_SetCancelShown(bCancel); // (a finger off the ring or in the hole: lifting cancels, as it shows)
}

// Every finger down now does nothing more until lifted (a button let go,
// the stick gone, no half-filled ring left)
static void tu_IgnoreFingers(void)
{
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed) continue;
        if (s->role == ROLE_BUTTON && tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
        if (s->role == ROLE_STICK) tu_HideStick();
        s->role = ROLE_IGNORED;
    }
    tu.aRing[BTN_QUICKSAVE] = tu.aRing[BTN_QUICKLOAD] = tu.aRing[BTN_MENU] = tu.aRing[BTN_TRAYCAM] = 0.0f;
    tu.aRing[BTN_CAMERA] = tu.aRing[BTN_TRAYGYRO] = 0.0f;
    tu.aRing[BTN_WHEEL] = tu.aRing[BTN_NEXTWPN] = tu.aRing[BTN_SABER] = 0.0f;
}

// Opens a wheel from a button: 0 if one is open already (a pad's, the
// keys') or there is no saber for the saber wheel
static int tu_OpenWheel(int kind, int button, int bTapMode)
{
    tu_SetTrayOpen(0);
    if (tu.bWheelOpen || !inputWheel_Open(kind, bTapMode)) return 0; // (the game holds still)
    tu.wheelKind = kind;
    tu.wheelButton = button;
    tu.pWheelMap = inputWheel_GetOpenMap();
    tu_SaberTargetFromShared();
    tu_IgnoreFingers();
    // a tap on either wheel button not read yet would undo the pick
    tu.aPulseQueue[tu_aButtons[BTN_WHEEL].scancode] = 0;
    tu.aPulseQueue[tu_aButtons[BTN_NEXTWPN].scancode] = 0;
    tu.bWheelOpen = 1;
    tu.bWheelTapMode = bTapMode != 0;
    tu_LayoutWheel();
    tu_RecomputeKeys();
    return 1;
}

static int tu_OpenWheelFrom(tuSlot* s, int bHeld)
{
    int button = s->button;
    if (tu.bWheelOpen || inputWheel_IsOpen() || tu_OtherPhotoModeUp()) {
        // (a wheel, or a photo mode, the pad or the keys opened is up: this press does nothing)
        if (s->role == ROLE_BUTTON && tu.aButtonHeld[button] > 0) tu.aButtonHeld[button]--;
        tu.aRing[button] = 0.0f;
        s->role = ROLE_IGNORED;
        return 0;
    }
    tu_EndBurst(); // (the game holds still: no more shots)
    if (tu_IsChatOpen() && G(ToggleChat)) tu.game->ToggleChat();
    if (!tu_OpenWheel(tu_WheelKindOf(button), button, bHeld)) {
        if (s->role == ROLE_BUTTON && tu.aButtonHeld[button] > 0) tu.aButtonHeld[button]--;
        tu.aRing[button] = 0.0f;
        s->role = ROLE_IGNORED;
        return 0;
    }
    s->role = ROLE_WHEEL;
    s->button = button;
    s->bWheelOpener = 1;
    s->bWheelHeld = bHeld != 0;
    s->bArmed = 0;
    s->wheelSlot = -1;
    s->wox = s->ox;
    s->woy = s->oy;
    return 1;
}

// The wheel gone (picked, cancelled, EQUIP): its fingers do nothing more
static void tu_WheelClosed(void)
{
    for (int i = 0; i < TU_MAX_TOUCHES; i++)
        if (tu.aSlots[i].bUsed && tu.aSlots[i].role == ROLE_WHEEL) tu.aSlots[i].role = ROLE_IGNORED;
    tu.bWheelOpen = 0;
    tu.bWheelTapMode = 0;
    tu_RecomputeKeys();
}

static void tu_CloseWheelSelecting(int slice)
{
    if (!tu.bWheelOpen) return;
    if (inputWheel_Close(slice)) {
        // The saber wheel stays open on a colour: set now, and open to tap
        // more (the other blade, another colour) until the gap, a tap off the
        // ring or EQUIP closes it
        tu_SaveSettings();
        tu.bWheelTapMode = 1;
        for (int i = 0; i < TU_MAX_TOUCHES; i++) {
            tuSlot* o = &tu.aSlots[i];
            if (o->bUsed && o->role == ROLE_WHEEL) {
                if (o->bWheelOpener) o->bWheelHeld = 1; // (lifted, it leaves the wheel open)
                o->wheelSlot = -1;
            }
        }
        tu_LayoutWheel();
        return;
    }
    tu_WheelClosed();
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
        // does nothing else -- but on an open wheel it works the wheel (and a
        // photo mode, worked by touch, never hid them)
        tu.bOtherInput = 0;
        if (!tu.bWheelOpen && !tu_PhotoModeUp()) {
            s->role = ROLE_IGNORED;
            return;
        }
    }
    if (tu.bSelfie || tu.bCinema || tu_OtherPhotoModeUp()) {
        s->role = ROLE_IGNORED; // nothing during a selfie or the cinematic burst (or a photo mode the keys or a pad started)
        return;
    }
    if (!tu.bWheelOpen && inputWheel_IsOpen()) {
        s->role = ROLE_IGNORED; // a wheel the pad or the keys opened: theirs to work
        return;
    }
    if (tu.bFreezing) {
        // Action Freeze: its X, CAMERA, or moving the camera
        if (hypotf(px - tu.freezeX, py - tu.freezeY) <= TU_FREEZE_CLOSE_R + TU_TOUCH_SLOP) s->role = ROLE_FREEZECLOSE;
        else if (tu_IsPointOnButton(px, py, BTN_CAMERA)) s->role = ROLE_FREEZESHOT;
        else s->role = ROLE_FREEZE;
        return;
    }
    if (tu.bWheelOpen) {
        s->role = ROLE_WHEEL;
        s->wheelSlot = tu_WheelSliceAt(px, py, -1);
        return;
    }
    s->button = tu_ButtonAt(px, py);
    if (tu.optRow != TU_ROW_NONE && !tu_IsOptButton(s->button)) tu_SetOptRow(TU_ROW_NONE);
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

static void tu_TouchMove(tuSlot* s, float px, float py, double t)
{
    if (s->role == ROLE_FREEZE) {
        tu_MoveFreeze(s, px, py);
        s->lx = px;
        s->ly = py;
        return;
    }
    // QUICK SAVE, QUICK LOAD: off the button even once, no save or load
    if (s->role == ROLE_BUTTON && tu_IsQuickHold(s->button) && !s->bFired && !tu_IsPointOnButton(px, py, s->button))
        s->bFired = 1;
    // CAMERA slid TU_WHEEL_OPEN_SLIDE (anywhere) before its hold was up: a
    // burst until this finger lifts -- or, with FREEZE on, mostly up or down
    // as far as TU_FREEZE_SWIPE: down, Action Freeze; up (CAMERA is at the
    // top: up to near the edge counts), the cinematic burst. The tray's
    // camera switch or GYRO slid off isn't held any more.
    if (s->role == ROLE_BUTTON && !s->bFired && (s->button == BTN_CAMERA || tu_IsRowOpener(s->button))) {
        const int bOut = hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_OPEN_SLIDE;
        const float dx = px - s->ox, dy = py - s->oy;
        const int bDown = s->button == BTN_CAMERA && tu.bFreezeOn && fabsf(dy) >= fabsf(dx) && dy != 0.0f;
        const float upFar = TU_MIN(TU_FREEZE_SWIPE, TU_MAX(s->oy - TU_FREEZE_EDGE, TU_FREEZE_SWIPE_MIN));
        if (bDown && fabsf(dy) >= (dy > 0.0f ? TU_FREEZE_SWIPE : upFar)) {
            s->bFired = 1;
            tu.aRing[BTN_CAMERA] = 0.0f;
            if (!tu_IsChatOpen() && t - s->tDown < TU_SELFIE_HOLD) {
                if (dy > 0.0f) tu_StartFreeze();
                else tu_StartCinema();
                if (tu.bFreezing || tu.bCinema) return; // (every touch now belongs to it)
            }
        }
        else if (bDown && bOut) {
            s->bSwipe = 1; // (on its way: not a tap, nor a hold)
            tu.aRing[BTN_CAMERA] = 0.0f;
        }
        else if (bOut && s->button == BTN_CAMERA) {
            s->bFired = 1;
            tu.aRing[BTN_CAMERA] = 0.0f;
            // (not a finger already held long enough for the selfie, by its own clock)
            if (!tu.bBurst && !tu_IsChatOpen() && t - s->tDown < TU_SELFIE_HOLD) tu_StartBurst(s->id);
        }
        else if (bOut) {
            s->bFired = 1;
            tu.aRing[s->button] = 0.0f;
        }
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
    if (tu.bBurst && s->id == tu.burstId) tu_EndBurst();
    if (s->role == ROLE_FREEZE || s->role == ROLE_FREEZESHOT || s->role == ROLE_FREEZECLOSE) {
        // Action Freeze: lifted on its X, back to the game; a tap on CAMERA, a photo
        const int bClose = !bCancelled && s->role == ROLE_FREEZECLOSE
                           && hypotf(px - tu.freezeX, py - tu.freezeY) <= TU_FREEZE_CLOSE_R + TU_TOUCH_SLOP;
        if (!bCancelled && s->role == ROLE_FREEZESHOT && tu_IsPointOnButton(px, py, BTN_CAMERA) && t - s->tDown < TU_SELFIE_HOLD)
            tu_TakeFreezePhoto();
        memset(s, 0, sizeof(*s));
        if (bClose) tu_EndFreeze();
        return;
    }
    if (s->role == ROLE_WHEEL && tu.bWheelOpen && bCancelled) {
        if (s->bWheelOpener) tu.bWheelTapMode = 1; // the opener taken away: open to tap
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && s->bWheelOpener && s->bWheelHeld) {
        // opened by holding still, lifted without sliding: open to tap
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && s->bWheelOpener && !tu.bWheelTapMode
             && tu_SaberChipAt(px, py) == TOUCHUI_CHIP_EQUIP) {
        tu_UseSaberChip(TOUCHUI_CHIP_EQUIP); // (slid onto EQUIP and lifted there)
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && s->bWheelOpener && !tu.bWheelTapMode) {
        tu_AimWheel(s, px, py);
        if (!s->bArmed) tu.bWheelTapMode = 1;
        else tu_CloseWheelSelecting(s->wheelSlot);
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen && tu_SaberChipAt(px, py) >= 0) {
        tu_UseSaberChip(tu_SaberChipAt(px, py)); // (the saber wheel's choices: set, the wheel open)
    }
    else if (s->role == ROLE_WHEEL && tu.bWheelOpen) {
        int slice = tu_WheelSliceAt(px, py, s->wheelSlot);
        int bSliding = 0;
        for (int k = 0; k < TU_MAX_TOUCHES; k++) {
            const tuSlot* o = &tu.aSlots[k];
            if (o != s && o->bUsed && o->role == ROLE_WHEEL && o->bWheelOpener && o->bArmed && !tu.bWheelTapMode) bSliding = 1;
        }
        if (!(slice == -1 && bSliding) && !(slice >= 0 && !inputWheel_IsPickable(slice))) tu_CloseWheelSelecting(slice);
    }
    else if (tu_IsWheelPress(s)) {
        const tuButton* b = &tu_aButtons[s->button];
        if (tu.aButtonHeld[s->button] > 0) tu.aButtonHeld[s->button]--;
        tu.aRing[s->button] = 0.0f;
        if (bCancelled) {
        }
        else if (hypotf(px - s->ox, py - s->oy) >= TU_WHEEL_OPEN_SLIDE) {
            // a flick with no move in between: a slide, then its lift
            if (tu_OpenWheelFrom(s, 0)) {
                tu_AimWheel(s, px, py);
                if (s->bArmed) tu_CloseWheelSelecting(s->wheelSlot);
                else tu.bWheelTapMode = 1;
            }
        }
        else if (t - s->tDown < TU_WHEEL_HOLD && s->button == BTN_SABER) {
            tu_OpenWheelFrom(s, 1); // SABER has no key: a tap opens its wheel, to tap a colour
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
        // the tray's LEVEL lifted with a level on show: that level (next
        // frame), the tray closes; slid or dropped: no tap either
        int bNoTap = 0;
        if (s->button == BTN_TRAYLEVEL) tu.aRing[BTN_TRAYLEVEL] = 0.0f;
        if (s->button == BTN_TRAYLEVEL && s->levelPick > 0) {
            if (!bCancelled) tu.levelToLoad = s->levelPick;
            tu_EndLevelPick(s);
            if (!bCancelled) tu_SetTrayOpen(0);
            bNoTap = 1;
        }
        else if (s->button == BTN_TRAYLEVEL && s->bFlyDone) bNoTap = 1;
        // a tap so quick the game never read it held still counts as one press
        if (!bCancelled && !bChat && b->kind == KIND_KEY && b->scancode >= 0 && !s->bSeen) tu_QueuePress(b->scancode);
        int bWhileTyping = b->kind == KIND_MENU || tu_IsTrayButton(s->button) || tu_IsOptButton(s->button);
        if (!bCancelled && !bNoTap && tu_IsPointOnButton(px, py, s->button) && (!bChat || bWhileTyping)) {
            if (b->kind == KIND_ITEM && s->button != BTN_BACTA && !tu_Hidden(s->button)
                && !tu.aPulseQueue[b->scancode] && !tu.aPulseReads[b->scancode] && !tu.aPulseGap[b->scancode]) {
                if (G(SelectItem)) tu.game->SelectItem(b->bin);
                tu_QueuePress(b->scancode);
            }
            else if (tu_IsTrayButton(s->button) && !(tu_IsRowOpener(s->button) && s->bFired)) {
                // (GYRO or the camera switch held until its row opened, or slid off: not a tap)
                tu_UseTrayButton(s->button);
            }
            else if (tu_IsOptButton(s->button) && !tu_Hidden(s->button)) {
                tu_UseOptButton(s->button);
            }
            else if (b->kind == KIND_CAMERA && !tu_Hidden(s->button) && !s->bFired && !s->bSwipe && t - s->tDown < TU_SELFIE_HOLD) {
                tu_TakePhotos(); // a tap (by the touch's own clock); not after a burst or a hold
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
            || tu_IsRowOpener(s->button) || s->button == BTN_CAMERA)
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
        if (phase == TOUCHUI_PHASE_MOVE) tu_TouchMove(s, x, y, t);
        else if (phase == TOUCHUI_PHASE_CANCEL) {
            // taken by the system: let go where it last was, nothing fires
            tu_TouchUp(s, s->lx, s->ly, t, 1);
        }
        else {
            // a lift where the finger moved since its last move: that move first
            if (s->role != ROLE_IGNORED && (x != s->lx || y != s->ly)) tu_TouchMove(s, x, y, t);
            s = tu_FindSlot(id); // (the move may have started a photo mode: still this finger's slot)
            if (!s) return;
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
        inputWheel_Close(-1); // (nothing picked; the game runs again)
        tu.bWheelOpen = 0;
        tu.bWheelTapMode = 0;
    }
    tu_EndSelfie(); // (all put back: the overlay is going, a menu or the background)
    tu_EndFreeze();
    tu_EndCinema();
    tu_SetTrayOpen(0);
    tu_EndBurst();
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
    if (tu.bWheelOpen && !inputWheel_IsOpen()) tu_WheelClosed(); // (closed from outside: only in case)
    tu_SaberTargetFromShared(); // (a pad or the keys may have changed it: remembered)
    const double dt = tu.prevTickNow > 0.0 ? now - tu.prevTickNow : 0.0;
    tu.prevTickNow = now;
    tu.lastNow = now;
    const int bChat = tu_IsChatOpen();

    // Photos asked for that no frame took in time: dropped
    if (tu.tCameraAsked > 0.0 && now - tu.tCameraAsked > TU_CAMERA_TIMEOUT) {
        if (G(CaptureCancel)) tu.game->CaptureCancel();
        tu.tCameraAsked = 0.0;
    }
    // SABER shown once the saber is had for real, or while sithewok is on (not
    // hidden from under a finger or its open wheel)
    {
        const int bSaber = G(IsSaberButtonShown) ? (tu.game->IsSaberButtonShown() != 0) : 0;
        if (bSaber != tu.bShowSaber && !tu.aButtonHeld[BTN_SABER] && !(tu.bWheelOpen && tu.wheelButton == BTN_SABER)) {
            tu.bShowSaber = bSaber;
            tu_Layout(); // (the burst's count makes room for it on the narrowest screens)
        }
    }
    // VIEW: the view picked by a keyboard's camera key, shown and remembered
    if (G(IsThirdPerson) && tu.game->IsThirdPerson() != tu.bThirdPerson) {
        tu.bThirdPerson = tu.game->IsThirdPerson() != 0;
        tu_SaveSettings();
    }

    // The tray's LEVEL there while sithewok is on (gone if it goes off with
    // the tray open, or back); its level pick
    if (tu.bTrayOpen && (tu_IsCheatOn() ? 1 : 0) != tu.bTrayLevel) tu_RefreshTrayLevel();
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!(s->levelPick > 0 && s->bUsed && s->role == ROLE_BUTTON)) s->levelPick = 0;
    }
    if (tu.levelToLoad > 0) {
        const int level = tu.levelToLoad;
        tu.levelToLoad = 0;
        if (!tu.bWheelOpen && !bChat && !tu_PhotoModeUp() && G(GoToLevel)) tu.game->GoToLevel(level);
    }

    // A photo mode the keys or a pad started: nothing goes on here, the
    // fingers already down do nothing more
    if (tu_OtherPhotoModeUp()) {
        tu_IgnoreFingers();
        tu_RecomputeKeys();
        return;
    }

    // During a selfie or the cinematic burst nothing else goes on here (the
    // engine runs them, enh_Tick, and says when they are over:
    // touchUi_PhotoModeEnded; this only in case that was missed)
    if (tu.bSelfie) {
        if (G(SelfieIsActive) && !tu.game->SelfieIsActive()) tu_EndSelfie();
        return;
    }
    if (tu.bCinema) {
        if (G(CinemaIsActive) && !tu.game->CinemaIsActive()) tu_EndCinema();
        return;
    }
    // ...or Action Freeze (its camera moves with the touches themselves)
    if (tu.bFreezing) {
        if (G(FreezeIsActive) && !tu.game->FreezeIsActive()) tu_EndFreeze();
        else tu_TickFreeze(dt);
        return;
    }

    // FORCE WHEEL, NEXT WPN and SABER held still: the wheel opens to tap
    float wheelProgress = 0.0f, weaponProgress = 0.0f, saberProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!tu_IsWheelPress(s)) continue;
        if (known - s->tDown >= TU_WHEEL_HOLD) {
            tu_OpenWheelFrom(s, 1);
            wheelProgress = weaponProgress = saberProgress = 0.0f;
            break;
        }
        float progress = (float)TU_MIN((now - s->tDown) / TU_WHEEL_HOLD, 1.0);
        if (s->button == BTN_NEXTWPN) weaponProgress = progress;
        else if (s->button == BTN_SABER) saberProgress = progress;
        else wheelProgress = progress;
    }
    if (!tu.bWheelOpen) {
        tu.aRing[BTN_WHEEL] = wheelProgress;
        tu.aRing[BTN_NEXTWPN] = weaponProgress;
        tu.aRing[BTN_SABER] = saberProgress;
    }

    // JUMP, ACT (sithewok's flight, noclip) and the tray's LEVEL (its level
    // pick) held still. (DUCK has no hold: only ever a crouch.)
    float jumpProgress = 0.0f, actProgress = 0.0f, levelProgress = 0.0f;
    for (int i = 0; i < TU_MAX_TOUCHES; i++) {
        tuSlot* s = &tu.aSlots[i];
        if (!s->bUsed || s->role != ROLE_BUTTON || (s->button != BTN_JUMP && s->button != BTN_ACT && s->button != BTN_TRAYLEVEL) || s->bFlyDone)
            continue;
        if (hypotf(s->lx - s->ox, s->ly - s->oy) >= TU_FLY_STILL) {
            s->bFlyDone = 1;
            tu_EndLevelPick(s);
            continue;
        }
        if (s->button == BTN_TRAYLEVEL) {
            const int numLevels = (bChat || !tu.bTrayOpen || !tu.bTrayLevel || !G(NumPickableLevels)) ? 0 : tu.game->NumPickableLevels();
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
            levelProgress = TU_MAX(levelProgress, (float)TU_MIN((now - s->tDown) / TU_FLY_HOLD, 1.0));
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
    tu.aRing[BTN_TRAYLEVEL] = levelProgress;

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
        else if (tu_IsRowOpener(s->button) && !s->bFired && tu.bTrayOpen && (s->button != BTN_TRAYGYRO || tu_GyroAvailable())) {
            // The tray's camera switch or GYRO held still: its row opens (the
            // lift then doesn't switch it)
            if (known - s->tDown >= TU_MENU_HOLD) {
                s->bFired = 1;
                tu.aRing[s->button] = 0.0f;
                tu_SetOptRow(s->button == BTN_TRAYGYRO ? TU_ROW_GYRO : TU_ROW_CAMERA);
            }
            else tu.aRing[s->button] = (float)TU_MIN((now - s->tDown) / TU_MENU_HOLD, 1.0);
        }
        else if (s->button == BTN_CAMERA && !s->bFired && !s->bSwipe && !bChat) {
            // Held still: the ring fills, then the selfie (the lift takes no photos)
            if (known - s->tDown >= TU_SELFIE_HOLD) {
                s->bFired = 1;
                tu.aRing[BTN_CAMERA] = 0.0f;
                tu_StartSelfie();
                if (tu.bSelfie) return; // everything else waits for the selfie to end
                continue;
            }
            tu.aRing[BTN_CAMERA] = (float)TU_MIN((now - s->tDown) / TU_SELFIE_HOLD, 1.0);
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
    tu_TickBurst(now);

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
    tu.bAlwaysRun = -1;
    snprintf(tu.fpsText, sizeof(tu.fpsText), "-- FPS");
    if (pSettingsPath) snprintf(tu.settingsPath, sizeof(tu.settingsPath), "%s", pSettingsPath);
    tu_LoadSettings();
    touchUi_ShareSaberTarget();
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
    // (photos asked for and not taken yet are dropped once the overlay goes:
    // a menu, a cutscene)
    if (!bShown && tu.tCameraAsked > 0.0) {
        if (G(CaptureCancel)) tu.game->CaptureCancel();
        tu.tCameraAsked = 0.0;
    }
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
void touchUi_SetFpsShown(int bShow)
{
    if ((bShow != 0) != tu.bShowFps) tu_SetShowFps(bShow);
}

int touchUi_GetTopTextRect(float* pTop, float* pLeft, float* pRight)
{
    if (!tu.bTopText) return 0;
    const float* pText = tu.aTopText;
    if (tu.bTrayOpen) pText = tu.aTopTextTray[tu.optRow == TU_ROW_CAMERA ? (tu.bKindsOpen ? 3 : 2) : tu.optRow == TU_ROW_GYRO ? 1 : 0];
    *pTop = pText[0];
    *pLeft = pText[1];
    *pRight = pText[2];
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
int touchUi_IsCameraShown(void) { return tu.bShowCamera; }
int touchUi_IsSaberShown(void) { return tu.bShowSaber; }
int touchUi_GetOptRow(void) { return tu.optRow; }
int touchUi_IsKindsRowOpen(void) { return tu.bKindsOpen; }
int touchUi_GetCameraKinds(void) { return tu.cameraKinds; }
int touchUi_GetSelfieShots(void) { return tu.selfieShots; }
int touchUi_IsFreezeOn(void) { return tu.bFreezeOn; }
int touchUi_GetBurstShots(void) { return tu.bBurst ? tu.burstShots : -1; }
int touchUi_IsSelfieUp(void) { return tu.bSelfie; }
int touchUi_IsFreezing(void) { return tu.bFreezing; }
int touchUi_IsCinemaUp(void) { return tu.bCinema; }
void touchUi_ShareSaberTarget(void)
{
    inputWheel_SetSaberTarget(tu_SaberTargetToWheel(tu.saberTarget));
}
int touchUi_GetSaberTarget(void) { return tu_SaberTargetFromWheel(inputWheel_GetSaberTarget()); }
int touchUi_GetSaberChip(int chip, float* pX, float* pY)
{
    float x = 0.0f, y = 0.0f;
    const int bShown = tu.bWheelOpen && inputWheel_GetChip(chip, &x, &y);
    if (pX) *pX = x;
    if (pY) *pY = y;
    return bShown;
}
void touchUi_GetFreezeClose(float* pX, float* pY)
{
    *pX = tu.freezeX;
    *pY = tu.freezeY;
}

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

// The camera's picture filled (the CAMERA button)
static void tu_DrawCameraFilled(float cx, float cy, touchUiColor c)
{
    touchUiDraw_RRect(&tu_draw, cx - 10.0f, cy - 5.5f, 20.0f, 13.5f, 3.0f, c, 0.0f, touchUi_Clear());
    touchUiDraw_RRect(&tu_draw, cx - 4.5f, cy - 8.5f, 9.0f, 4.0f, 1.2f, c, 0.0f, touchUi_Clear());
    touchUiDraw_Circle(&tu_draw, cx, cy + 1.2f, 4.6f, touchUi_Black(0.75f), 0.0f, touchUi_Clear());
    touchUiDraw_Circle(&tu_draw, cx, cy + 1.2f, 2.8f, c, 0.0f, touchUi_Clear());
}

// A saber's picture (iosTouch.m's IOSTouch_AddSaberIcon): a short hilt and
// the blade up to the right (bMirror: to the left) in the blade's colour,
// glowing, a white core along it
static void tu_DrawSaberIcon(float cx, float cy, float r, int bMirror, unsigned char cr, unsigned char cg, unsigned char cb)
{
    const float sx = bMirror ? -1.0f : 1.0f;
    const float h0x = cx + sx * r * -0.42f, h0y = cy + r * 0.42f, h1x = cx + sx * r * -0.18f, h1y = cy + r * 0.18f;
    const float tx = cx + sx * r * 0.46f, ty = cy - r * 0.46f;
    const touchUiColor blade = touchUi_Rgba(cr / 255.0f, cg / 255.0f, cb / 255.0f, 1.0f);
    touchUiColor glow = blade;
    glow.a = 0.35f;
    const float bw = TU_MAX(r * 0.17f, 3.0f);
    touchUiDraw_Line(&tu_draw, h1x, h1y, tx, ty, bw + 4.0f, glow);
    touchUiDraw_Line(&tu_draw, h1x, h1y, tx, ty, bw, blade);
    touchUiDraw_Line(&tu_draw, h1x, h1y, tx, ty, TU_MAX(r * 0.06f, 1.2f), touchUi_White(0.9f));
    touchUiDraw_Line(&tu_draw, h0x, h0y, h1x, h1y, TU_MAX(r * 0.2f, 3.5f), touchUi_Rgba(0.78f, 0.78f, 0.78f, 1.0f));
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
                    || (i == BTN_TRAYGYRO && bGyro && tu.gyroMode != TOUCHUI_GYRO_OFF)
                    || (i == BTN_TRAYCAM && tu.bShowCamera)
                    || (tu_IsCamOptButton(i) && (tu.cameraKinds & tu_aButtons[i].bin))
                    || (i == BTN_CAMROW_PHOTO && tu.bKindsOpen)
                    || (i == BTN_CAMROW_FREEZE && tu.bFreezeOn)
                    || (i == BTN_CAMERA && tu.bBurst);
    const int bTray = tu_IsTrayButton(i) || tu_IsOptButton(i);
    float alpha = (bHeld || bOn || bWheel || bTray) ? 1.0f : TU_IDLE_ALPHA;
    if (i == BTN_FORCE && tu.bForceNameSet && !tu.forceName) alpha *= 0.45f;
    if (i == BTN_TRAYGYRO && !bGyro) alpha *= 0.45f;
    if (i == BTN_TRAYLEVEL && !(G(NumPickableLevels) && tu.game->NumPickableLevels() > 0)) alpha *= 0.45f; // (no level can be picked now)
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
    else if (i == BTN_TRAYLEVEL) {
        // (the count on a second line while LEVEL is held)
        for (int t = 0; t < TU_MAX_TOUCHES; t++) {
            const tuSlot* s = &tu.aSlots[t];
            if (s->bUsed && s->role == ROLE_BUTTON && s->button == BTN_TRAYLEVEL && s->levelPick > 0) {
                snprintf(buf, sizeof(buf), "LEVEL\n%d", s->levelPick);
                label = buf;
            }
        }
        size = tu_FontSize("GYRO\nALWAYS", b->radius);
        xscale = TU_WHEEL_XSCALE;
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
    else if (i == BTN_CAMROW_SELFIES || i == BTN_CAMROW_FREEZE) {
        if (i == BTN_CAMROW_SELFIES) snprintf(buf, sizeof(buf), "SELFIES\n%d", tu.selfieShots);
        else snprintf(buf, sizeof(buf), "FREEZE\n%s", tu.bFreezeOn ? "ON" : "OFF");
        label = buf;
        size = tu_FontSize("GYRO\nALWAYS", b->radius);
        xscale = TU_WHEEL_XSCALE;
    }
    if (i == BTN_CHEAT) {
        tu_DrawSithEmblem(b->x, b->y, b->radius * 0.64f, tu.bCheatOn ? touchUi_Rgba(0.9f, 0.32f, 0.28f, 0.95f) : text);
    }
    else if (i == BTN_TRAYKEYS) tu_DrawKeyboardIcon(b->x, b->y, text);
    else if (i == BTN_TRAYCAM) tu_DrawCameraIcon(b->x, b->y, text);
    else if (i == BTN_CAMERA) tu_DrawCameraFilled(b->x, b->y, text);
    else if (i == BTN_SABER) {
        // in the blades' colours, as picked (the game's own: its colour now)
        const int st = tu_SaberStyle();
        unsigned char aRgb[2][3] = { { 160, 160, 160 }, { 160, 160, 160 } };
        for (int k = 0; k < 2 && G(SaberColourRGB) && G(SaberGetColour); k++)
            tu.game->SaberColourRGB(tu.game->SaberGetColour(st, k), &aRgb[k][0], &aRgb[k][1], &aRgb[k][2]);
        if (tu_SaberNumBlades(st) >= 2) tu_DrawSaberIcon(b->x, b->y, b->radius, 1, aRgb[1][0], aRgb[1][1], aRgb[1][2]);
        tu_DrawSaberIcon(b->x, b->y, b->radius, 0, aRgb[0][0], aRgb[0][1], aRgb[0][2]);
    }
    else if (label[0]) tu_DrawLabel(label, b->x, b->y, size, xscale, 2.0f * b->radius, text);

    // sithewok's own uses of JUMP and ACT, named under their labels
    if (tu.bCheatOn && (i == BTN_JUMP || i == BTN_ACT)) {
        const char* sub = i == BTN_JUMP ? "FLY" : "NOCLIP";
        touchUiDraw_Text(&tu_draw, sub, b->x, b->y + b->radius * 0.5f, 7.5f, 1.0f, touchUi_Rgba(1.0f, 0.58f, 0.52f, 0.95f));
    }
    // the hold rings
    if (i == BTN_QUICKSAVE || i == BTN_QUICKLOAD || i == BTN_MENU || i == BTN_WHEEL || i == BTN_NEXTWPN || i == BTN_JUMP
        || i == BTN_ACT || i == BTN_TRAYLEVEL || i == BTN_CHEAT || i == BTN_BACTA || i == BTN_TRAYGYRO || i == BTN_TRAYCAM
        || i == BTN_CAMERA || i == BTN_SABER)
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

// The open wheel (inputWheel.c's), over the game dimmed, laid out clear of
// the buttons showing
static void tu_DrawWheel(void)
{
    if (!tu.bWheelOpen) return;
    tu_WheelTouched();
    tu_LayoutWheel();
    inputWheelScreen sc;
    tu_WheelScreen(&sc);
    inputWheel_Draw(&tu_draw, &sc);
}

static void tu_DrawFps(void)
{
    if (!tu.bShowFps) return;
    touchUiDraw_RRect(&tu_draw, tu.fpsX - TU_FPS_LABEL_HALF, tu.fpsY - 9.0f, 2.0f * TU_FPS_LABEL_HALF, 18.0f, 9.0f,
                      touchUi_Black(0.35f), 0.0f, touchUi_Clear());
    touchUiDraw_Text(&tu_draw, tu.fpsText, tu.fpsX, tu.fpsY, 11.0f, 1.0f, touchUi_White(0.85f));
}

// The camera's flash, over everything, fading (a burst's lighter and quicker)
static void tu_DrawFlash(double now)
{
    if (tu.tFlash <= 0.0) return;
    const double len = tu.bFlashLight ? 0.12 : 0.3;
    const double age = now - tu.tFlash;
    if (age >= len || age < -0.5) {
        tu.tFlash = 0.0;
        return;
    }
    const float k = (float)(1.0 - (age > 0.0 ? age : 0.0) / len);
    const float a = (tu.bFlashLight ? 0.35f : 0.85f) * k * (2.0f - k); // (eased out)
    touchUiDraw_RRect(&tu_draw, 0.0f, 0.0f, tu.W, tu.H, 0.0f, touchUi_White(a), 0.0f, touchUi_Clear());
}

const touchUiDrawList* touchUi_Draw(double now)
{
    touchUiDraw_Begin(&tu_draw);
    if (!tu.bShown || !tu.bLaidOut) return &tu_draw;
    if (tu_OtherPhotoModeUp()) return &tu_draw; // (the keys' or a pad's: no buttons to touch over it)
    // A selfie and the cinematic burst show nothing but the flash; Action
    // Freeze only CAMERA and its X
    if (tu.bSelfie || tu.bCinema) {
        tu_DrawFlash(now);
        return &tu_draw;
    }
    if (tu.bFreezing) {
        if (!tu_Hidden(BTN_CAMERA)) tu_DrawButton(BTN_CAMERA);
        touchUiDraw_Circle(&tu_draw, tu.freezeX, tu.freezeY, TU_FREEZE_CLOSE_R, touchUi_Black(0.45f), 1.5f, touchUi_White(0.6f));
        const float d = 6.5f;
        touchUiDraw_Line(&tu_draw, tu.freezeX - d, tu.freezeY - d, tu.freezeX + d, tu.freezeY + d, 2.5f, touchUi_White(1.0f));
        touchUiDraw_Line(&tu_draw, tu.freezeX - d, tu.freezeY + d, tu.freezeX + d, tu.freezeY - d, 2.5f, touchUi_White(1.0f));
        tu_DrawFlash(now);
        return &tu_draw;
    }
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
        for (int i = BTN_TRAY_FIRST; i <= BTN_TRAY_LAST; i++) if (!tu_Hidden(i)) tu_DrawButton(i);
    }
    {
        const int aFirst[3] = { BTN_GYROOPT_FIRST, BTN_CAMROW_FIRST, BTN_CAMOPT_FIRST };
        const int aLast[3] = { BTN_GYROOPT_LAST, BTN_CAMROW_LAST, BTN_CAMOPT_LAST };
        for (int k = 0; k < 3; k++) {
            if (tu_Hidden(aFirst[k])) continue;
            const float* r = tu.aRowBack[k];
            touchUiDraw_RRect(&tu_draw, r[0], r[1], r[2], r[3], 26.0f, touchUi_Black(0.55f), 1.5f, touchUi_White(0.4f));
            for (int i = aFirst[k]; i <= aLast[k]; i++) tu_DrawButton(i);
        }
    }
    // the burst's count, beside CAMERA (or under it, where SABER needs the room)
    if (tu.bBurst && !tu_Hidden(BTN_CAMERA)) {
        const tuButton* c = &tu_aButtons[BTN_CAMERA];
        const float bx = tu.bBurstUnder ? c->x : c->x + c->radius + 20.0f, by = tu.bBurstUnder ? c->y + c->radius + 12.0f : c->y;
        char num[8];
        snprintf(num, sizeof(num), "%d", tu.burstShots);
        touchUiDraw_RRect(&tu_draw, bx - TU_BURST_LABEL_HALF, by - 9.0f, 2.0f * TU_BURST_LABEL_HALF, 18.0f, 9.0f, touchUi_Black(0.45f), 0.0f,
                          touchUi_Clear());
        touchUiDraw_Text(&tu_draw, num, bx, by, 12.0f, 1.0f, TU_GOLD(0.95f));
    }
    tu_DrawFps();
    tu_DrawStick();
    if (tu.bWheelOpen) {
        tu_DrawWheel();
        tu_DrawButton(tu.wheelButton); // (lit, above the dimmed screen)
    }
    tu_DrawFlash(now);
    return &tu_draw;
}

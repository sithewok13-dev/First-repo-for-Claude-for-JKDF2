// Added: entire file. See iosPad.h.

#include "Platform/iOS/iosPad.h"

#ifdef IOSPAD_ENABLED

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "globals.h"
#include "Platform/stdControl.h"
#include "Devices/sithControl.h"
#include "Gui/jkGUIRend.h"
#include "Main/jkQuakeConsole.h"
#include "World/sithWorld.h"
#include "World/sithThing.h"
#include "Enhanced/iosGame.h"
#include "Platform/Input/inputUi.h" // (iOS: the touch overlay and iosPadPanel; elsewhere: inputOverlay.c)

extern int stdControl_bControllerEscapeKey;
SDL_Gamepad* stdControl_GetSdlGamepad(int idx);

// Sticks: under IOSPAD_STICK_INNER of a full push is nothing (round, so no
// direction is favoured); the rest, to IOSPAD_STICK_OUTER, is rescaled to
// 0..1, so a stick that never quite reaches its rim still gets to full.
#define IOSPAD_STICK_INNER 0.15f
#define IOSPAD_STICK_OUTER 0.95f
// Look acceleration (ACCEL on): once the look stick has been pushed past
// IOSPAD_ACCEL_PUSH for IOSPAD_ACCEL_DELAY seconds, turning speeds up by as
// much as IOSPAD_ACCEL_GAIN more over IOSPAD_ACCEL_RAMP seconds
#define IOSPAD_ACCEL_PUSH 0.9f
#define IOSPAD_ACCEL_DELAY 0.2f
#define IOSPAD_ACCEL_RAMP 0.4f
#define IOSPAD_ACCEL_GAIN 0.8f
// The wheels: their button held this long opens its wheel (as the touch
// buttons' 0.3 s); the right stick pushed this far (while it is down) opens
// it at once. Open, the stick points at a slice once pushed past
// IOSPAD_WHEEL_AIM and keeps pointing there down to IOSPAD_WHEEL_LETGO; back
// under that for IOSPAD_WHEEL_GRACE seconds it points at nothing (so a stick
// let go as the button is let go still picks)
#define IOSPAD_WHEEL_HOLD 0.3
#define IOSPAD_WHEEL_PUSH 0.55f
#define IOSPAD_WHEEL_AIM 0.5f
#define IOSPAD_WHEEL_LETGO 0.3f
#define IOSPAD_WHEEL_GRACE 0.25
// Back held this long quick saves (with D-pad down, quick loads), as QUICK
// SAVE and QUICK LOAD; let go sooner than IOSPAD_MAP_TAP it is a tap. Its
// ring shows once it has been held IOSPAD_RING_SHOW (not for every tap)
#define IOSPAD_SAVE_HOLD 0.3
#define IOSPAD_LOAD_HOLD 0.3
#define IOSPAD_MAP_TAP 0.2
#define IOSPAD_RING_SHOW 0.1
// Start held this long opens the controller options (MENU's tray: 0.45 s)
#define IOSPAD_MENU_HOLD 0.45
// A press the pad makes for the game (a tap's) is held down for this many
// of the game's control reads, then let go for one; Escape for this many frames
#define IOSPAD_PULSE_READS 2
#define IOSPAD_ESCAPE_FRAMES 2
// What counts as playing with the pad (hiding the touch controls): a press,
// or a stick or trigger pushed this far
#define IOSPAD_ACTIVE_PUSH 0.3f
// Running toggled on the run button stops once the move stick has rested
// this long
#define IOSPAD_RUN_REST 0.5
// The options: the stick moves the focus or changes a value once pushed
// this far (and again once it has come back under half of it)
#define IOSPAD_PANEL_PUSH 0.6f

// The pad's buttons and triggers, as this file numbers them
enum {
    PB_A = 0, PB_B, PB_X, PB_Y, PB_BACK, PB_GUIDE, PB_START, PB_LS, PB_RS, PB_LB, PB_RB,
    PB_LT, PB_RT, PB_DLEFT, PB_DUP, PB_DRIGHT, PB_DDOWN, PB_NUM
};
static const int iosPad_aSdlButton[PB_NUM] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    -1, -1, // (the triggers: axes)
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
};
// (a trigger is down past this, as stdControl has always had it)
#define IOSPAD_TRIGGER_DOWN 0x2666

// The game's key for a pad button: KEY_JOY1_B1.. (B1-B8 then the extended
// B9.., per pad), the hat for the D-pad
static int iosPad_KeyFor(int idx, int pb)
{
    static const int aButtonNum[PB_NUM] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 17, -1, -1, -1, -1 };
    if (pb >= PB_DLEFT) return KEY_JOY1_HLEFT + (pb - PB_DLEFT) + JK_JOYSTICK_BUTTON_STRIDE * idx;
    int n = aButtonNum[pb];
    if (n <= JK_NUM_JOY_BUTTONS) return KEY_JOY1_B1 + (n - 1) + JK_JOYSTICK_BUTTON_STRIDE * idx;
    return KEY_JOY1_B9 + (n - 9) + JK_JOYSTICK_EXT_BUTTON_STRIDE * idx;
}

// ------------------------------------------------------------ settings

static const float iosPad_aSens[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f };
#define IOSPAD_NUM_SENS ((int)(sizeof(iosPad_aSens) / sizeof(iosPad_aSens[0])))
#define IOSPAD_SENS_DEFAULT 2 // 1.0x
static const float iosPad_aCurve[] = { 1.0f, 1.6f, 2.4f };
static const char* const iosPad_aCurveName[] = { "LINEAR", "SMOOTH", "PRECISE" };
#define IOSPAD_NUM_CURVES 3
#define IOSPAD_CURVE_DEFAULT 1

typedef struct {
    int sensX, sensY, curve, bInvertY, bAccel, bRumble;
} iosPadSettings;
static iosPadSettings iosPad_set = { IOSPAD_SENS_DEFAULT, IOSPAD_SENS_DEFAULT, IOSPAD_CURVE_DEFAULT, 0, 0, 1 };
static int iosPad_bSettingsLoaded = 0;

static int iosPad_Clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void iosPad_LoadSettings(void)
{
    iosPad_bSettingsLoaded = 1;
    iosPad_set.sensX = iosPad_Clampi(inputUi_LoadInt("iosPadLookX", IOSPAD_SENS_DEFAULT), 0, IOSPAD_NUM_SENS - 1);
    iosPad_set.sensY = iosPad_Clampi(inputUi_LoadInt("iosPadLookY", IOSPAD_SENS_DEFAULT), 0, IOSPAD_NUM_SENS - 1);
    iosPad_set.curve = iosPad_Clampi(inputUi_LoadInt("iosPadCurve", IOSPAD_CURVE_DEFAULT), 0, IOSPAD_NUM_CURVES - 1);
    iosPad_set.bInvertY = inputUi_LoadInt("iosPadInvertY", 0) != 0;
    iosPad_set.bAccel = inputUi_LoadInt("iosPadAccel", 0) != 0;
    iosPad_set.bRumble = inputUi_LoadInt("iosPadRumble", 1) != 0;
}

static void iosPad_SaveSettings(void)
{
    inputUi_SaveInt("iosPadLookX", iosPad_set.sensX);
    inputUi_SaveInt("iosPadLookY", iosPad_set.sensY);
    inputUi_SaveInt("iosPadCurve", iosPad_set.curve);
    inputUi_SaveInt("iosPadInvertY", iosPad_set.bInvertY);
    inputUi_SaveInt("iosPadAccel", iosPad_set.bAccel);
    inputUi_SaveInt("iosPadRumble", iosPad_set.bRumble);
}

void iosPad_ReloadSettings(void)
{
    iosPad_LoadSettings();
}

float iosPad_GetLookSensX(void) { return iosPad_aSens[iosPad_set.sensX]; }
float iosPad_GetLookSensY(void) { return iosPad_aSens[iosPad_set.sensY]; }

// ------------------------------------------------------------ state

enum { IOSPAD_WS_NONE = 0, IOSPAD_WS_PENDING, IOSPAD_WS_HELD, IOSPAD_WS_TAP };

static struct {
    SDL_Gamepad* pGamepad;    // pad 0, as stdControl has it open (NULL: none)...
    SDL_JoystickID padId;     // ...and its SDL id (0: none)
    int bPhoto;               // a photo mode is up: the pad does nothing
    int bOtherWheel;          // a wheel a finger or the keys opened is up: likewise
    int bRumbleCap;           // it can rumble
    double now, last;         // this frame's time and the last's (seconds)
    int aDown[PB_NUM];        // held this frame
    int aPrev[PB_NUM];        // ...and last frame
    int aStale[PB_NUM];       // held since before the game listened: ignored until let go
    int aEventDown[PB_NUM];   // pressed since last frame, by the events (a press shorter than a frame still counts)
    int aEngine[PB_NUM];      // what the game is to see held
    unsigned char aPulseQueue[PB_NUM], aPulseReads[PB_NUM], aPulseGap[PB_NUM]; // taps for the game (as iosTouch's)
    float lx, ly, rx, ry;     // the sticks this frame, through the round dead zone (y down)
    float rmPrev;             // the right stick's push last frame
    int aStickStale[2];       // the left, right stick: still pushed from a wheel or the options; nothing to the game until it rests
    float lt, rt;             // the triggers, 0..1
    int bGame;                // gameplay is listening
    int bHudHidden;           // the touch controls are hidden for the pad
    // the wheel being opened or open
    int wheelState, wheelBtn, wheelKind, bWheelHeldOpen, bWheelAimed, bWheelAimOn, bWheelStickFree;
    double tWheelDown, tWheelLetGo;
    // Start and Back
    int startState; double tStart;
    int selState, ringKind; double tSel, tRing;
    // running toggled on
    int runBtn, bRunOn; double tRunRest;
    // the options
    int bPanel, panelFocus, bPanelStickX, bPanelStickY;
    // Escape for the game menu, frames left; quick save's key, reads left
    int escFrames;
    unsigned char saveQueue, saveReads, saveGap;
    // look acceleration
    float accelT, accelMult;
    // the player's health as last seen (vibration when hurt)
    SithThing* pHealthThing; float lastHealth;
} iosPad_s;

static int iosPad_IsGameplay(void)
{
    if ((sithWeapon_controlOptions & 0x20) != 0) return 0; // the game's "Disable joystick"
    return stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive() && !jkQuakeConsole_bOpen
           && sithWorld_g_pCurrentWorld && sithPlayer_g_pLocalPlayerThing;
}

int iosPad_OwnsSystemButtons(int sdlJoystickId)
{
    return iosPad_s.pGamepad && iosPad_s.bGame && (SDL_JoystickID)sdlJoystickId == iosPad_s.padId;
}

int iosPad_IsHudHidden(void)
{
    return iosPad_s.bHudHidden;
}

static void iosPad_SetHudHidden(int bHidden)
{
    if (iosPad_s.bHudHidden == bHidden) return;
    iosPad_s.bHudHidden = bHidden;
    inputUi_SetPadHud(bHidden);
}

void iosPad_NoteTouch(void)
{
    // (the overlay shows itself again; this only keeps count)
    iosPad_s.bHudHidden = 0;
}

static void iosPad_Rumble(float low, float high, int ms)
{
    if (!iosPad_s.pGamepad || !iosPad_s.bRumbleCap || !iosPad_set.bRumble) return;
    if (low > 1.0f) low = 1.0f;
    if (high > 1.0f) high = 1.0f;
    SDL_RumbleGamepad(iosPad_s.pGamepad, (Uint16)(low * 65535.0f), (Uint16)(high * 65535.0f), (Uint32)ms);
}

void iosPad_OnWheelSlice(void)
{
    iosPad_Rumble(0.0f, 0.25f, 30);
}

// Every input held now is ignored until it is let go
static void iosPad_MarkAllStale(void)
{
    for (int i = 0; i < PB_NUM; i++) {
        iosPad_s.aStale[i] = iosPad_s.aDown[i];
        iosPad_s.aEngine[i] = 0;
    }
}

static int iosPad_Pressed(int pb)
{
    return iosPad_s.aDown[pb] && !iosPad_s.aPrev[pb] && !iosPad_s.aStale[pb];
}

static void iosPad_Pulse(int pb)
{
    if (iosPad_s.aPulseQueue[pb] < 255) iosPad_s.aPulseQueue[pb]++;
}

// The function a key is bound to (the first; a key has one), or -1
static int iosPad_FuncOfKey(int key)
{
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            const stdControlKeyInfoEntry* e = &k->aEntries[j];
            if ((e->flags & INPUT_MAPPING_FLAG_AXIS) == 0 && e->dxKeyNum == key) return f;
        }
    }
    return -1;
}

// Whether one of the axes axis, axis + 1 is bound to turning or looking up and down
static int iosPad_IsLookStick(int axis)
{
    static const int aFunc[2] = { INPUT_FUNC_TURN, INPUT_FUNC_PITCH };
    for (int i = 0; i < 2; i++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[aFunc[i]];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            const stdControlKeyInfoEntry* e = &k->aEntries[j];
            if ((e->flags & INPUT_MAPPING_FLAG_AXIS) && (e->dxKeyNum == axis || e->dxKeyNum == axis + 1)) return 1;
        }
    }
    return 0;
}

// A stick through the round dead zone: *pX, *pY scaled so the push past the
// dead zone runs 0..1; returns that
static float iosPad_Radial(int rawX, int rawY, float* pX, float* pY)
{
    float x = rawX / 32767.0f, y = rawY / 32767.0f;
    float m = sqrtf(x * x + y * y);
    if (m <= IOSPAD_STICK_INNER) {
        *pX = *pY = 0.0f;
        return 0.0f;
    }
    float k = (m - IOSPAD_STICK_INNER) / (IOSPAD_STICK_OUTER - IOSPAD_STICK_INNER);
    if (k > 1.0f) k = 1.0f;
    *pX = x / m * k;
    *pY = y / m * k;
    return k;
}

// ------------------------------------------------------------ names

static const char* iosPad_FaceName(SDL_GamepadButtonLabel label, int bAscii)
{
    switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return bAscii ? "Cross" : "\xE2\x9C\x95";      // ✕
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return bAscii ? "Circle" : "\xE2\x97\x8B";    // ○
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return bAscii ? "Square" : "\xE2\x96\xA1";    // □
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return bAscii ? "Triangle" : "\xE2\x96\xB3"; // △
        default: return NULL;
    }
}

const char* iosPad_ButtonName(int sdlButton, int bAscii)
{
    SDL_Gamepad* g = iosPad_s.pGamepad;
    SDL_GamepadType t = g ? SDL_GetGamepadType(g) : SDL_GAMEPAD_TYPE_UNKNOWN;
    const int bPs = t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
    const int bNintendo = t >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && t <= SDL_GAMEPAD_TYPE_GAMECUBE;
    if (sdlButton >= 0 && sdlButton < SDL_GAMEPAD_BUTTON_COUNT && sdlButton <= SDL_GAMEPAD_BUTTON_NORTH) {
        SDL_GamepadButtonLabel l = g ? SDL_GetGamepadButtonLabel(g, (SDL_GamepadButton)sdlButton) : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        if (l == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN) {
            static const SDL_GamepadButtonLabel aXbox[4] = { SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_LABEL_Y };
            l = aXbox[sdlButton];
        }
        return iosPad_FaceName(l, bAscii);
    }
    switch (sdlButton) {
        case SDL_GAMEPAD_BUTTON_BACK: return bPs ? (t == SDL_GAMEPAD_TYPE_PS5 ? "Create" : "Share") : bNintendo ? "-" : "View";
        case SDL_GAMEPAD_BUTTON_GUIDE: return "Home";
        case SDL_GAMEPAD_BUTTON_START: return bPs ? "Options" : bNintendo ? "+" : "Menu";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return bPs ? "L1" : bNintendo ? "L" : "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return bPs ? "R1" : bNintendo ? "R" : "RB";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return bAscii ? "D-pad up" : "D-pad \xE2\x86\x91";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return bAscii ? "D-pad down" : "D-pad \xE2\x86\x93";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return bAscii ? "D-pad left" : "D-pad \xE2\x86\x90";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return bAscii ? "D-pad right" : "D-pad \xE2\x86\x92";
        case SDL_GAMEPAD_BUTTON_COUNT + 0: return bPs ? "L2" : bNintendo ? "ZL" : "LT";
        case SDL_GAMEPAD_BUTTON_COUNT + 1: return bPs ? "R2" : bNintendo ? "ZR" : "RT";
        default: return "?";
    }
}

static const char* iosPad_PbName(int pb, int bAscii)
{
    if (pb == PB_LT) return iosPad_ButtonName(SDL_GAMEPAD_BUTTON_COUNT + 0, bAscii);
    if (pb == PB_RT) return iosPad_ButtonName(SDL_GAMEPAD_BUTTON_COUNT + 1, bAscii);
    return iosPad_ButtonName(iosPad_aSdlButton[pb], bAscii);
}

const char* iosPad_KeyDisplayName(int dxKeyNum, int bAxis)
{
    static char aBuf[4][32];
    static int next = 0;
    char* b = aBuf[next++ & 3];
    if (bAxis) {
        static const char* const aAxis[JK_JOYSTICK_AXIS_STRIDE] = { "left stick X", "left stick Y", "right stick X", "right stick Y", NULL, NULL };
        if (dxKeyNum < AXIS_JOY1_X || dxKeyNum >= AXIS_JOY1_X + JK_JOYSTICK_AXIS_STRIDE * JK_NUM_JOYSTICKS) return NULL;
        const int idx = (dxKeyNum - AXIS_JOY1_X) / JK_JOYSTICK_AXIS_STRIDE, a = (dxKeyNum - AXIS_JOY1_X) % JK_JOYSTICK_AXIS_STRIDE;
        const char* n = aAxis[a] ? aAxis[a] : iosPad_PbName(a == 4 ? PB_LT : PB_RT, 1);
        snprintf(b, sizeof(aBuf[0]), idx ? "Pad 2 %s" : "Pad %s", n);
        return b;
    }
    for (int idx = 0; idx < JK_NUM_JOYSTICKS; idx++) {
        for (int pb = 0; pb < PB_NUM; pb++) {
            if (iosPad_KeyFor(idx, pb) != dxKeyNum) continue;
            snprintf(b, sizeof(aBuf[0]), idx ? "Pad 2 %s" : "Pad %s", iosPad_PbName(pb, 1));
            return b;
        }
    }
    return NULL;
}

// ------------------------------------------------------------ prompts

static void iosPad_SetWheelPrompt(void)
{
    char t[256];
    const char* a = iosPad_ButtonName(SDL_GAMEPAD_BUTTON_SOUTH, 0);
    const char* bb = iosPad_ButtonName(SDL_GAMEPAD_BUTTON_EAST, 0);
    if (iosPad_s.wheelKind == IOSPAD_WHEEL_SABER) {
        snprintf(t, sizeof(t), "Right stick: colour   %s: set   %s: one or two sabers   %s: which blade   R3: take the saber   %s: close",
                 a, iosPad_ButtonName(SDL_GAMEPAD_BUTTON_WEST, 0), iosPad_ButtonName(SDL_GAMEPAD_BUTTON_NORTH, 0), bb);
    }
    else if (iosPad_s.wheelState == IOSPAD_WS_HELD) {
        snprintf(t, sizeof(t), "Right stick: point   Let go of %s: pick   %s: cancel", iosPad_PbName(iosPad_s.wheelBtn, 0), bb);
    }
    else {
        snprintf(t, sizeof(t), "Right stick or D-pad: point   %s: pick   %s: cancel", a, bb);
    }
    inputUi_SetPrompt(t);
}

const char* iosPad_WheelHint(int bHot, int bCancel)
{
    static char t[64];
    if (iosPad_s.wheelState != IOSPAD_WS_HELD && iosPad_s.wheelState != IOSPAD_WS_TAP) return NULL;
    const int bHeld = iosPad_s.wheelState == IOSPAD_WS_HELD;
    if (bCancel) snprintf(t, sizeof(t), bHeld ? "let go to cancel" : "%s to close", iosPad_ButtonName(SDL_GAMEPAD_BUTTON_EAST, 0));
    else if (bHot) {
        if (bHeld) snprintf(t, sizeof(t), "let go to pick");
        else snprintf(t, sizeof(t), "%s to %s", iosPad_ButtonName(SDL_GAMEPAD_BUTTON_SOUTH, 0), iosPad_s.wheelKind == IOSPAD_WHEEL_SABER ? "set" : "pick");
    }
    else snprintf(t, sizeof(t), "point the right stick");
    return t;
}

// ------------------------------------------------------------ the wheels

static void iosPad_WheelReset(void)
{
    iosPad_s.wheelState = IOSPAD_WS_NONE;
    iosPad_s.wheelBtn = -1;
    iosPad_s.bWheelAimOn = 0;
    inputUi_SetPrompt(NULL);
}

// The wheel a press of pb would open (IOSPAD_WHEEL_*), or -1
static int iosPad_WheelKindFor(int pb)
{
    if (pb == PB_START || pb == PB_BACK || pb == PB_GUIDE) return -1;
    int f = iosPad_FuncOfKey(iosPad_KeyFor(0, pb));
    if (f == INPUT_FUNC_NEXTSKILL || f == INPUT_FUNC_PREVSKILL) return IOSPAD_WHEEL_FORCE;
    if (f == INPUT_FUNC_NEXTWEAPON || f == INPUT_FUNC_PREVWEAPON) return IOSPAD_WHEEL_WEAPONS;
    if (pb == PB_DLEFT && f < 0 && iosGame_IsSaberButtonShown()) return IOSPAD_WHEEL_SABER;
    return -1;
}

static void iosPad_WheelOpen(int bHeld)
{
    iosPad_s.wheelState = IOSPAD_WS_HELD; // (first: the wheel asks for its hint as it opens)
    if (!inputUi_PadWheelOpen(iosPad_s.wheelKind, 0)) {
        // (it can't: the press is spent)
        iosPad_s.wheelState = IOSPAD_WS_NONE;
        iosPad_s.aStale[iosPad_s.wheelBtn] = iosPad_s.aDown[iosPad_s.wheelBtn];
        iosPad_s.wheelBtn = -1;
        return;
    }
    iosPad_s.bWheelHeldOpen = bHeld;
    iosPad_s.bWheelAimed = 0;
    iosPad_s.bWheelAimOn = 0;
    // every other input lets go (as every other finger is dropped)
    const int btn = iosPad_s.wheelBtn;
    iosPad_MarkAllStale();
    iosPad_s.aStale[btn] = 0;
    iosPad_SetWheelPrompt();
}

static void iosPad_WheelClosed(void)
{
    iosPad_WheelReset();
    iosPad_MarkAllStale();
    iosPad_s.aStickStale[1] = 1; // (the stick that pointed doesn't turn the view on its way back)
}

// The right stick on the open wheel
static void iosPad_WheelAimFromStick(void)
{
    float m = sqrtf(iosPad_s.rx * iosPad_s.rx + iosPad_s.ry * iosPad_s.ry);
    if (!iosPad_s.bWheelStickFree) {
        // (a stick still turning the view as the wheel opened points at
        // nothing until it has come back)
        if (m >= IOSPAD_WHEEL_LETGO) return;
        iosPad_s.bWheelStickFree = 1;
    }
    if (m >= IOSPAD_WHEEL_AIM || (iosPad_s.bWheelAimOn && m >= IOSPAD_WHEEL_LETGO)) {
        float deg = atan2f(-iosPad_s.ry, iosPad_s.rx) * (180.0f / (float)M_PI);
        inputUi_WheelAim(1, deg);
        iosPad_s.bWheelAimOn = 1;
        iosPad_s.bWheelAimed = 1;
        iosPad_s.tWheelLetGo = 0.0;
    }
    else if (iosPad_s.bWheelAimOn) {
        if (iosPad_s.tWheelLetGo == 0.0) iosPad_s.tWheelLetGo = iosPad_s.now;
        else if (iosPad_s.now - iosPad_s.tWheelLetGo >= IOSPAD_WHEEL_GRACE) {
            inputUi_WheelAim(0, 0.0f);
            iosPad_s.bWheelAimOn = 0;
        }
    }
}

static void iosPad_WheelCloseWith(int bSelect)
{
    iosPad_s.wheelState = IOSPAD_WS_TAP; // (as it would be if it stays open: its hint is asked for)
    if (inputUi_WheelClose(bSelect)) {
        // the saber wheel, still open on its colour: on to pick more with A
        iosPad_s.bWheelAimOn = 0;
        iosPad_SetWheelPrompt();
        return;
    }
    iosPad_WheelClosed();
}

static void iosPad_WheelInput(void)
{
    if (!inputUi_PadWheelIsOpen()) {
        // closed some other way (a touch, the overlay going)
        iosPad_WheelClosed();
        return;
    }
    const int btn = iosPad_s.wheelBtn;
    const int bSaber = iosPad_s.wheelKind == IOSPAD_WHEEL_SABER;
    iosPad_WheelAimFromStick();
    if (iosPad_Pressed(PB_DLEFT) && btn != PB_DLEFT) { inputUi_WheelStep(-1); iosPad_s.bWheelAimed = 1; }
    if (iosPad_Pressed(PB_DRIGHT)) { inputUi_WheelStep(1); iosPad_s.bWheelAimed = 1; }
    if (bSaber) {
        if (iosPad_Pressed(PB_X)) inputUi_SaberChip(IOSPAD_CHIP_STYLE);
        if (iosPad_Pressed(PB_Y)) inputUi_SaberChip(IOSPAD_CHIP_TARGET);
        if (iosPad_Pressed(PB_RS)) {
            inputUi_SaberChip(IOSPAD_CHIP_EQUIP);
            if (!inputUi_PadWheelIsOpen()) {
                iosPad_WheelClosed();
                return;
            }
        }
    }
    if (iosPad_Pressed(PB_B) || iosPad_Pressed(PB_START)) {
        iosPad_WheelCloseWith(0);
        if (iosPad_s.wheelState == IOSPAD_WS_TAP) iosPad_WheelCloseWith(0); // (B always closes, the saber wheel too)
        return;
    }
    int bPickable = 0;
    const int bHot = inputUi_WheelHot(&bPickable);
    if (iosPad_s.wheelState == IOSPAD_WS_HELD) {
        if (!iosPad_s.aDown[btn]) {
            // the button let go: picks what is pointed at; pointing at nothing
            // after a hold that never pointed, it stays open to pick with A
            if (bHot) iosPad_WheelCloseWith(1);
            else if (!iosPad_s.bWheelAimed && (iosPad_s.bWheelHeldOpen || bSaber)) {
                iosPad_s.wheelState = IOSPAD_WS_TAP;
                inputUi_WheelSetTapMode();
                iosPad_SetWheelPrompt();
            }
            else iosPad_WheelCloseWith(0);
        }
        return;
    }
    // open to pick with A (or the wheel's own button again)
    if (iosPad_Pressed(PB_A) || iosPad_Pressed(btn)) {
        if (bHot) iosPad_WheelCloseWith(1);
        else if (iosPad_Pressed(btn)) iosPad_WheelCloseWith(0);
        if (bSaber && iosPad_Pressed(btn) && iosPad_s.wheelState == IOSPAD_WS_TAP) iosPad_WheelCloseWith(0);
    }
}

// ------------------------------------------------------------ the options

static void iosPad_PanelOpen(int bOpen)
{
    if (iosPad_s.bPanel == bOpen) return;
    iosPad_s.bPanel = bOpen;
    iosPad_s.bPanelStickX = iosPad_s.bPanelStickY = 1; // (a stick already pushed waits to come back)
    iosGame_SetHoldReason(IOSGAME_HOLD_PADMENU, bOpen);
    iosPad_MarkAllStale();
    iosPad_s.aStickStale[0] = iosPad_s.aStickStale[1] = 1;
    inputUi_PanelShow(bOpen);
    inputUi_SetPrompt(NULL);
}

int iosPad_PanelIsOpen(void)
{
    return iosPad_s.bPanel;
}

int iosPad_PanelFocus(void)
{
    return iosPad_s.panelFocus;
}

const char* iosPad_PanelTitle(void)
{
    static char t[96];
    const char* name = iosPad_s.pGamepad ? SDL_GetGamepadName(iosPad_s.pGamepad) : NULL;
    snprintf(t, sizeof(t), "CONTROLLER%s%s", name ? " \xE2\x80\x94 " : "", name ? name : ""); // (an em dash)
    return t;
}

// A sensitivity as the touch controls' SENS shows one: 1.0x, 1.5x, 0.75x
static void iosPad_SensText(char* p, int size, float v)
{
    snprintf(p, size, (fabsf(v * 10.0f - roundf(v * 10.0f)) < 0.01f) ? "%.1fx" : "%.2fx", (double)v);
}

void iosPad_PanelRow(int row, char* pLabel, int labelSize, char* pValue, int valueSize)
{
    static const char* const aLabel[IOSPAD_NUM_ROWS] = { "LOOK X", "LOOK Y", "CURVE", "INVERT LOOK", "ACCELERATION", "VIBRATION", "FPS", "TYPE" };
    pLabel[0] = pValue[0] = 0;
    if (row < 0 || row >= IOSPAD_NUM_ROWS) return;
    snprintf(pLabel, labelSize, "%s", aLabel[row]);
    switch (row) {
        case IOSPAD_ROW_SENSX: iosPad_SensText(pValue, valueSize, iosPad_aSens[iosPad_set.sensX]); break;
        case IOSPAD_ROW_SENSY: iosPad_SensText(pValue, valueSize, iosPad_aSens[iosPad_set.sensY]); break;
        case IOSPAD_ROW_CURVE: snprintf(pValue, valueSize, "%s", iosPad_aCurveName[iosPad_set.curve]); break;
        case IOSPAD_ROW_INVERT: snprintf(pValue, valueSize, "%s", iosPad_set.bInvertY ? "ON" : "OFF"); break;
        case IOSPAD_ROW_ACCEL: snprintf(pValue, valueSize, "%s", iosPad_set.bAccel ? "ON" : "OFF"); break;
        case IOSPAD_ROW_RUMBLE: snprintf(pValue, valueSize, "%s", !iosPad_s.bRumbleCap ? "NOT ON THIS PAD" : iosPad_set.bRumble ? "ON" : "OFF"); break;
        case IOSPAD_ROW_FPS: snprintf(pValue, valueSize, "%s", inputUi_IsFpsShown() ? "ON" : "OFF"); break;
        case IOSPAD_ROW_TYPE: snprintf(pValue, valueSize, "keyboard for cheats"); break;
    }
}

const char* iosPad_PanelHint(void)
{
    static char t[128];
    snprintf(t, sizeof(t), "D-pad: choose   %s: change   %s: close",
             iosPad_ButtonName(SDL_GAMEPAD_BUTTON_SOUTH, 0), iosPad_ButtonName(SDL_GAMEPAD_BUTTON_EAST, 0));
    return t;
}

// The focused row's value one step on (dir +1) or back (-1); A is +1
static void iosPad_PanelChange(int row, int dir)
{
    switch (row) {
        case IOSPAD_ROW_SENSX: iosPad_set.sensX = iosPad_Clampi(iosPad_set.sensX + dir, 0, IOSPAD_NUM_SENS - 1); break;
        case IOSPAD_ROW_SENSY: iosPad_set.sensY = iosPad_Clampi(iosPad_set.sensY + dir, 0, IOSPAD_NUM_SENS - 1); break;
        case IOSPAD_ROW_CURVE: iosPad_set.curve = (iosPad_set.curve + dir + IOSPAD_NUM_CURVES) % IOSPAD_NUM_CURVES; break;
        case IOSPAD_ROW_INVERT: iosPad_set.bInvertY = !iosPad_set.bInvertY; break;
        case IOSPAD_ROW_ACCEL: iosPad_set.bAccel = !iosPad_set.bAccel; break;
        case IOSPAD_ROW_RUMBLE:
            iosPad_set.bRumble = !iosPad_set.bRumble;
            iosPad_Rumble(0.5f, 0.5f, 150); // (what it feels like)
            break;
        case IOSPAD_ROW_FPS: inputUi_SetFpsShown(!inputUi_IsFpsShown()); break;
        case IOSPAD_ROW_TYPE:
            // the typing line, for cheats (the options close: the keyboard needs the screen)
            iosPad_PanelOpen(0);
            if (!jkHud_bChatOpen) iosGame_ToggleChat();
            return;
    }
    iosPad_SaveSettings();
    inputUi_PanelRefresh();
}

void iosPad_PanelTap(int row)
{
    if (!iosPad_s.bPanel) return;
    if (row < 0 || row >= IOSPAD_NUM_ROWS) {
        iosPad_PanelOpen(0);
        return;
    }
    iosPad_s.panelFocus = row;
    iosPad_PanelChange(row, 1);
    inputUi_PanelRefresh();
}

static void iosPad_PanelInput(void)
{
    if (iosPad_Pressed(PB_B) || iosPad_Pressed(PB_START)) {
        iosPad_PanelOpen(0);
        return;
    }
    int dy = 0, dx = 0;
    if (iosPad_Pressed(PB_DUP)) dy = -1;
    if (iosPad_Pressed(PB_DDOWN)) dy = 1;
    if (iosPad_Pressed(PB_DLEFT)) dx = -1;
    if (iosPad_Pressed(PB_DRIGHT)) dx = 1;
    // the left stick as a D-pad: once each push
    if (fabsf(iosPad_s.ly) >= IOSPAD_PANEL_PUSH && !iosPad_s.bPanelStickY) { dy = iosPad_s.ly > 0 ? 1 : -1; iosPad_s.bPanelStickY = 1; }
    else if (fabsf(iosPad_s.ly) < IOSPAD_PANEL_PUSH * 0.5f) iosPad_s.bPanelStickY = 0;
    if (fabsf(iosPad_s.lx) >= IOSPAD_PANEL_PUSH && !iosPad_s.bPanelStickX) { dx = iosPad_s.lx > 0 ? 1 : -1; iosPad_s.bPanelStickX = 1; }
    else if (fabsf(iosPad_s.lx) < IOSPAD_PANEL_PUSH * 0.5f) iosPad_s.bPanelStickX = 0;
    if (dy) {
        iosPad_s.panelFocus = (iosPad_s.panelFocus + dy + IOSPAD_NUM_ROWS) % IOSPAD_NUM_ROWS;
        inputUi_PanelRefresh();
        iosPad_Rumble(0.0f, 0.15f, 20);
    }
    const int row = iosPad_s.panelFocus;
    if (dx && row != IOSPAD_ROW_TYPE) iosPad_PanelChange(row, dx);
    else if (iosPad_Pressed(PB_A)) iosPad_PanelChange(row, 1);
}

// ------------------------------------------------------------ Start, Back

static void iosPad_MenuTap(void)
{
    // as MENU: the typing line closes if it is open, else the game menu
    if (jkHud_bChatOpen) iosGame_ToggleChat();
    else {
        // (down now, this frame's Window_SdlUpdate acts on it; let go after)
        stdControl_bControllerEscapeKey = 1;
        iosPad_s.escFrames = IOSPAD_ESCAPE_FRAMES - 1;
    }
}

static void iosPad_SetRing(int kind, float progress);

static void iosPad_StartInput(void)
{
    if (iosPad_Pressed(PB_START) && iosPad_s.aDown[PB_BACK] && !iosPad_s.aStale[PB_BACK]) {
        // Back + Start: the options too (for a pad whose Start can't be held:
        // iOS gives some only a short press), Back's save or map dropped
        iosPad_s.selState = 2;
        iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
        iosPad_s.aStale[PB_START] = 1;
        iosPad_PanelOpen(1);
        return;
    }
    if (iosPad_Pressed(PB_START)) {
        iosPad_s.startState = 1;
        iosPad_s.tStart = iosPad_s.now;
    }
    if (iosPad_s.startState != 1) return;
    if (!iosPad_s.aDown[PB_START]) {
        iosPad_s.startState = 0;
        iosPad_MenuTap();
    }
    else if (iosPad_s.now - iosPad_s.tStart >= IOSPAD_MENU_HOLD) {
        iosPad_s.startState = 0;
        iosPad_PanelOpen(1);
    }
}

static void iosPad_SetRing(int kind, float progress)
{
    iosPad_s.ringKind = kind;
    inputUi_HoldRing(kind, progress);
}

static void iosPad_SelectInput(void)
{
    if (iosPad_Pressed(PB_BACK)) {
        iosPad_s.selState = 1;
        iosPad_s.tSel = iosPad_s.tRing = iosPad_s.now;
        iosPad_s.ringKind = IOSPAD_RING_NONE;
    }
    if (iosPad_s.selState == 0) return;
    if (!iosPad_s.aDown[PB_BACK]) {
        // let go: before the ring filled, nothing -- unless it was a quick
        // tap with D-pad down never touched, which is Back's own binding
        if (iosPad_s.selState == 1 && iosPad_s.now - iosPad_s.tSel < IOSPAD_MAP_TAP && iosPad_s.ringKind != IOSPAD_RING_LOAD)
            iosPad_Pulse(PB_BACK);
        iosPad_s.selState = 0;
        iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
        return;
    }
    if (iosPad_s.selState != 1) return; // (went off: nothing more on this press)
    if (jkHud_bChatOpen) {
        // (not while typing)
        iosPad_s.selState = 2;
        iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
        return;
    }
    // D-pad down held with it: quick load (its ring starts again); let go, back to quick save
    const int kind = iosPad_s.aDown[PB_DDOWN] ? IOSPAD_RING_LOAD : IOSPAD_RING_SAVE;
    if (kind != iosPad_s.ringKind && iosPad_s.ringKind != IOSPAD_RING_NONE) iosPad_s.tRing = iosPad_s.now;
    const double hold = kind == IOSPAD_RING_LOAD ? IOSPAD_LOAD_HOLD : IOSPAD_SAVE_HOLD;
    const double t = iosPad_s.now - iosPad_s.tRing;
    if (t >= hold) {
        iosPad_s.selState = 2;
        iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
        if (kind == IOSPAD_RING_LOAD) iosGame_QuickLoad();
        else if (iosPad_s.saveQueue < 255) iosPad_s.saveQueue++; // (quick save's key, as QUICK SAVE presses it)
        iosPad_Rumble(0.6f, 0.3f, 120);
        return;
    }
    if (iosPad_s.now - iosPad_s.tSel >= IOSPAD_RING_SHOW || kind == IOSPAD_RING_LOAD) iosPad_SetRing(kind, (float)(t / hold));
    else iosPad_s.ringKind = kind; // (as good as shown: the ring's start is kept)
}

// ------------------------------------------------------------ the frame

static void iosPad_LeaveGame(void)
{
    if (iosPad_s.wheelState >= IOSPAD_WS_HELD && inputUi_PadWheelIsOpen()) inputUi_WheelClose(0);
    iosPad_WheelReset();
    iosPad_PanelOpen(0);
    iosPad_s.startState = iosPad_s.selState = 0;
    iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
    iosPad_s.bRunOn = 0;
    memset(iosPad_s.aPulseQueue, 0, sizeof(iosPad_s.aPulseQueue));
    memset(iosPad_s.aPulseReads, 0, sizeof(iosPad_s.aPulseReads));
    memset(iosPad_s.aPulseGap, 0, sizeof(iosPad_s.aPulseGap));
    memset(iosPad_s.aEngine, 0, sizeof(iosPad_s.aEngine));
    iosPad_s.saveQueue = iosPad_s.saveReads = iosPad_s.saveGap = 0;
    iosPad_s.accelT = 0.0f;
    iosPad_s.accelMult = 1.0f;
}

static void iosPad_Feedback(void)
{
    // hurt: a rumble as hard and long as the hit
    SithThing* p = sithPlayer_g_pLocalPlayerThing;
    float h = p ? (float)p->actorParams.health : 0.0f;
    if (p && p == iosPad_s.pHealthThing && !iosGame_IsHolding()) {
        float d = iosPad_s.lastHealth - h;
        if (d >= 1.0f && !(p->flags & SITH_TF_DEAD)) {
            float k = d / 30.0f;
            iosPad_Rumble(0.35f + 0.65f * k, 0.25f + 0.5f * k, 100 + (int)(fminf(d, 60.0f) * 3.0f));
        }
    }
    iosPad_s.pHealthThing = p;
    iosPad_s.lastHealth = h;
}

void iosPad_HandleEvent(const void* pSdlEvent)
{
    const SDL_Event* e = (const SDL_Event*)pSdlEvent;
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && iosPad_s.pGamepad && iosPad_s.padId == e->gbutton.which) {
        for (int i = 0; i < PB_NUM; i++) {
            if (iosPad_aSdlButton[i] == e->gbutton.button) iosPad_s.aEventDown[i] = 1;
        }
    }
    if (e->type == SDL_EVENT_GAMEPAD_REMOVED && iosPad_s.pGamepad && iosPad_s.padId == e->gdevice.which) {
        // (stdControl lets go of it next; the touch controls come back now)
        iosPad_SetHudHidden(0);
    }
}

void iosPad_Update(void)
{
    if (!iosPad_bSettingsLoaded) {
        iosPad_LoadSettings();
        iosPad_s.wheelBtn = iosPad_s.runBtn = -1;
        iosPad_s.accelMult = 1.0f;
    }
    const double now = (double)SDL_GetTicksNS() * 1e-9;
    double dt = now - iosPad_s.now;
    if (dt < 0.0 || dt > 0.1) dt = 0.1;
    iosPad_s.last = iosPad_s.now;
    iosPad_s.now = now;

    // Escape for a Start tap, for a couple of frames (Window_SdlUpdate acts on it once)
    if (iosPad_s.escFrames > 0) {
        stdControl_bControllerEscapeKey = 1;
        if (--iosPad_s.escFrames == 0) iosPad_s.escFrames = -1;
    }
    else if (iosPad_s.escFrames < 0) {
        stdControl_bControllerEscapeKey = 0;
        iosPad_s.escFrames = 0;
    }

    SDL_Gamepad* g = stdControl_GetSdlGamepad(0);
    const SDL_JoystickID padId = g ? SDL_GetGamepadID(g) : 0;
    iosPad_s.pGamepad = g; // (the same pad can be opened again elsewhere: the id says whether it changed)
    if (padId != iosPad_s.padId) {
        // a pad came, went or changed: start over
        if (iosPad_s.bGame) iosPad_LeaveGame();
        iosPad_s.bGame = 0;
        iosPad_s.padId = padId;
        iosPad_s.bRumbleCap = g ? SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false) : 0;
        memset(iosPad_s.aPrev, 0, sizeof(iosPad_s.aPrev));
        for (int i = 0; i < PB_NUM; i++) iosPad_s.aStale[i] = 1; // (whatever is held as it comes)
        if (!g) iosPad_SetHudHidden(0);
    }
    if (!g) return;

    for (int i = 0; i < PB_NUM; i++) {
        if (iosPad_aSdlButton[i] >= 0) iosPad_s.aDown[i] = SDL_GetGamepadButton(g, (SDL_GamepadButton)iosPad_aSdlButton[i]) || iosPad_s.aEventDown[i];
        iosPad_s.aEventDown[i] = 0;
    }
    const int lt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    iosPad_s.aDown[PB_LT] = lt > IOSPAD_TRIGGER_DOWN;
    iosPad_s.aDown[PB_RT] = rt > IOSPAD_TRIGGER_DOWN;
    iosPad_s.lt = lt / 32767.0f;
    iosPad_s.rt = rt / 32767.0f;
    const float lm = iosPad_Radial(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY), &iosPad_s.lx, &iosPad_s.ly);
    const float rm = iosPad_Radial(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY), &iosPad_s.rx, &iosPad_s.ry);
    for (int i = 0; i < PB_NUM; i++) {
        if (!iosPad_s.aDown[i]) iosPad_s.aStale[i] = 0;
    }
    if (lm == 0.0f) iosPad_s.aStickStale[0] = 0;
    if (rm == 0.0f) iosPad_s.aStickStale[1] = 0;

    const int bGame = iosPad_IsGameplay();
    if (bGame != iosPad_s.bGame) {
        if (bGame) iosPad_MarkAllStale();
        else iosPad_LeaveGame();
        iosPad_s.bGame = bGame;
    }
    if (!bGame) {
        memcpy(iosPad_s.aPrev, iosPad_s.aDown, sizeof(iosPad_s.aPrev));
        iosPad_s.rmPrev = rm;
        return;
    }

    // A photo mode (selfie, Action Freeze, the cinematic burst, the Photos
    // prompt): the pad does nothing -- no saves or loads under the photo
    // camera, no menu, no wheels -- and what is held then is ignored after
    const int bPhoto = inputUi_IsPhotoModeUp();
    if (bPhoto) {
        if (!iosPad_s.bPhoto) {
            iosPad_s.startState = iosPad_s.selState = 0;
            iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
            if (iosPad_s.wheelState == IOSPAD_WS_PENDING) iosPad_WheelReset();
            iosPad_s.bRunOn = 0;
            memset(iosPad_s.aPulseQueue, 0, sizeof(iosPad_s.aPulseQueue));
            iosPad_s.saveQueue = 0;
        }
        iosPad_s.bPhoto = 1;
        iosPad_MarkAllStale();
        iosPad_s.aStickStale[0] = lm > 0.0f;
        iosPad_s.aStickStale[1] = rm > 0.0f;
        memcpy(iosPad_s.aPrev, iosPad_s.aDown, sizeof(iosPad_s.aPrev));
        iosPad_s.rmPrev = rm;
        return;
    }
    iosPad_s.bPhoto = 0;

    // Played with: the touch controls go. A stick or trigger counts when it is pushed past the mark,
    // not while it stays there, so a touch that brings the controls back (iosPad_NoteTouch) isn't
    // undone on the next frame by a stick still held
    static int aPushed[4];
    const float aLevel[4] = { lm, rm, iosPad_s.lt, iosPad_s.rt };
    int bActive = 0;
    for (int i = 0; i < 4; i++) {
        const int bNow = aLevel[i] >= IOSPAD_ACTIVE_PUSH;
        if (bNow && !aPushed[i]) bActive = 1;
        aPushed[i] = bNow;
    }
    for (int i = 0; i < PB_NUM; i++) {
        if (iosPad_Pressed(i)) bActive = 1;
    }
    if (bActive) iosPad_SetHudHidden(1);

    // Look acceleration (on the look stick, whichever that is)
    {
        const float m = iosPad_IsLookStick(AXIS_JOY1_X) && !iosPad_IsLookStick(AXIS_JOY1_Z) ? lm : rm;
        if (iosPad_set.bAccel && m >= IOSPAD_ACCEL_PUSH) iosPad_s.accelT += (float)dt;
        else iosPad_s.accelT = 0.0f;
        float k = (iosPad_s.accelT - IOSPAD_ACCEL_DELAY) / IOSPAD_ACCEL_RAMP;
        k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
        iosPad_s.accelMult = 1.0f + IOSPAD_ACCEL_GAIN * k;
    }

    // A wheel a finger or the keyboard opened: the pad does nothing meanwhile
    // -- no tap of a wheel's button waiting to fire once it closes, no menu
    // or options over it, no save or load -- and what is held then is
    // ignored after
    if (inputUi_IsWheelOpen() && !inputUi_PadWheelIsOpen()) {
        if (!iosPad_s.bOtherWheel) {
            iosPad_s.startState = iosPad_s.selState = 0;
            iosPad_SetRing(IOSPAD_RING_NONE, 0.0f);
            if (iosPad_s.wheelState == IOSPAD_WS_PENDING) iosPad_WheelReset(); // (no pulse: not a tap)
            memset(iosPad_s.aPulseQueue, 0, sizeof(iosPad_s.aPulseQueue));
            iosPad_s.saveQueue = 0;
        }
        iosPad_s.bOtherWheel = 1;
        iosPad_MarkAllStale();
        memcpy(iosPad_s.aPrev, iosPad_s.aDown, sizeof(iosPad_s.aPrev));
        iosPad_s.rmPrev = rm;
        return;
    }
    iosPad_s.bOtherWheel = 0;

    if (iosPad_s.bPanel) iosPad_PanelInput();
    else if (iosPad_s.wheelState >= IOSPAD_WS_HELD) iosPad_WheelInput();
    else {
        iosPad_StartInput();
        if (!iosPad_s.bPanel) iosPad_SelectInput();

        // A wheel's button: waiting to see a tap, a hold or the stick
        if (iosPad_s.wheelState == IOSPAD_WS_NONE && !iosPad_s.bPanel) {
            for (int i = 0; i < PB_NUM; i++) {
                if (!iosPad_Pressed(i)) continue;
                if (i == PB_DDOWN && iosPad_s.aDown[PB_BACK]) continue; // (quick load's)
                const int kind = iosPad_WheelKindFor(i);
                if (kind < 0) continue;
                iosPad_s.wheelState = IOSPAD_WS_PENDING;
                iosPad_s.wheelBtn = i;
                iosPad_s.wheelKind = kind;
                iosPad_s.tWheelDown = now;
                // (the stick opens it only pushed with the button or after
                // it: one already turning the view as it is tapped goes on
                // turning)
                iosPad_s.bWheelStickFree = iosPad_s.rmPrev < IOSPAD_WHEEL_LETGO;
                if (kind == IOSPAD_WHEEL_SABER) iosPad_WheelOpen(0); // (SABER has nothing to tap for: it opens at once)
                break;
            }
        }
        else if (iosPad_s.wheelState == IOSPAD_WS_PENDING) {
            const int btn = iosPad_s.wheelBtn;
            for (int i = 0; i < PB_NUM; i++) {
                // (the other wheel's button meanwhile: nothing, as a second finger)
                if (i != btn && iosPad_Pressed(i) && iosPad_WheelKindFor(i) >= 0) iosPad_s.aStale[i] = 1;
            }
            if (!iosPad_s.aDown[btn]) {
                // a tap: what the button is bound to (the next power, the next weapon)
                iosPad_Pulse(btn);
                iosPad_WheelReset();
            }
            else if (rm >= IOSPAD_WHEEL_PUSH && iosPad_s.bWheelStickFree) {
                iosPad_WheelOpen(0);
                if (iosPad_s.wheelState == IOSPAD_WS_HELD) iosPad_WheelAimFromStick();
            }
            else if (now - iosPad_s.tWheelDown >= IOSPAD_WHEEL_HOLD) {
                iosPad_WheelOpen(1);
            }
            if (iosPad_s.wheelState == IOSPAD_WS_PENDING && rm < IOSPAD_WHEEL_LETGO) iosPad_s.bWheelStickFree = 1;
        }

        // Running, toggled on the button bound to it
        iosPad_s.runBtn = -1;
        for (int i = 0; i < PB_NUM; i++) {
            if (i != PB_START && i != PB_BACK && iosPad_FuncOfKey(iosPad_KeyFor(0, i)) == INPUT_FUNC_FAST) {
                iosPad_s.runBtn = i;
                break;
            }
        }
        if (iosPad_s.runBtn >= 0) {
            if (iosPad_Pressed(iosPad_s.runBtn)) {
                iosPad_s.bRunOn = !iosPad_s.bRunOn;
                iosPad_s.tRunRest = 0.0;
            }
            if (lm > 0.0f) iosPad_s.tRunRest = 0.0;
            else if (iosPad_s.bRunOn) {
                if (iosPad_s.tRunRest == 0.0) iosPad_s.tRunRest = now;
                else if (now - iosPad_s.tRunRest >= IOSPAD_RUN_REST) iosPad_s.bRunOn = 0;
            }
        }
        else iosPad_s.bRunOn = 0;
    }

    // What the game sees held: nothing while a wheel or the options are up;
    // else what is held, but for what the pad keeps for itself
    const int bSuppress = iosPad_s.bPanel || iosPad_s.wheelState >= IOSPAD_WS_HELD;
    for (int i = 0; i < PB_NUM; i++) {
        int b = iosPad_s.aDown[i] && !iosPad_s.aStale[i] && !bSuppress;
        if (i == PB_START || i == PB_BACK) b = 0;
        if (i == iosPad_s.wheelBtn && iosPad_s.wheelState != IOSPAD_WS_NONE) b = 0;
        if (i == PB_DDOWN && iosPad_s.aDown[PB_BACK]) b = 0;
        if (i == iosPad_s.runBtn) b = iosPad_s.bRunOn && !bSuppress;
        iosPad_s.aEngine[i] = b;
    }

    iosPad_Feedback();
    memcpy(iosPad_s.aPrev, iosPad_s.aDown, sizeof(iosPad_s.aPrev));
    iosPad_s.rmPrev = rm;
}

// ------------------------------------------------------------ the game's read

// A press the pad makes for the game: held for IOSPAD_PULSE_READS reads,
// then let go for one (so two in a row are two presses)
static int iosPad_PulseRead(unsigned char* pQueue, unsigned char* pReads, unsigned char* pGap, int bHeld)
{
    if (*pReads) {
        if (--*pReads == 0) *pGap = 1;
        return 1;
    }
    if (*pGap) {
        *pGap = 0;
        return bHeld;
    }
    if (*pQueue) {
        (*pQueue)--;
        *pReads = IOSPAD_PULSE_READS - 1;
        if (!*pReads) *pGap = 1;
        return 1;
    }
    return bHeld;
}

int iosPad_IsScancodeDown(int scancode)
{
    if (scancode != SDL_SCANCODE_F9) return 0;
    return iosPad_PulseRead(&iosPad_s.saveQueue, &iosPad_s.saveReads, &iosPad_s.saveGap, 0);
}

int iosPad_ReadGamepad(int idx, void* pSdlGamepad)
{
    SDL_Gamepad* g = (SDL_Gamepad*)pSdlGamepad;
    if (!g) return 0;
    const int a0 = JK_JOYSTICK_AXIS_STRIDE * idx;
    const int bMine = idx == 0 && iosPad_s.padId && SDL_GetGamepadID(g) == iosPad_s.padId && iosPad_s.bGame;
    const int bSuppress = bMine && (iosPad_s.bPanel || iosPad_s.wheelState >= IOSPAD_WS_HELD || iosPad_s.bPhoto);

    // (as stdControl has always done: a pad's axes are read whether bound or not)
    stdControl_aAxisEnabled[idx] = 1;
    for (int j = 0; j < 4; j++) stdControl_aAxes[a0 + j].flags |= 2;

    // The sticks: through the round dead zone; the look stick, in gameplay,
    // through the curve, its sensitivity, inversion and acceleration
    for (int s = 0; s < 2; s++) {
        float x, y;
        const int axis = a0 + 2 * s;
        const float m = iosPad_Radial(SDL_GetGamepadAxis(g, s ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX),
                                      SDL_GetGamepadAxis(g, s ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY), &x, &y);
        if (bSuppress || (bMine && iosPad_s.aStickStale[s])) x = y = 0.0f;
        else if (bMine && m > 0.0f && iosPad_IsLookStick(axis)) {
            const float k = powf(m, iosPad_aCurve[iosPad_set.curve]) / m;
            x *= k * iosPad_aSens[iosPad_set.sensX] * iosPad_s.accelMult;
            y *= k * iosPad_aSens[iosPad_set.sensY] * (iosPad_set.bInvertY ? -1.0f : 1.0f);
        }
        stdControl_aAxisStates[axis] = (int)lroundf(x * 32767.0f);
        stdControl_aAxisStates[axis + 1] = (int)lroundf(y * 32767.0f);
    }
    const int lt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    stdControl_aAxisStates[a0 + 4] = bSuppress ? 0 : lt;
    stdControl_aAxisStates[a0 + 5] = bSuppress ? 0 : rt;

    for (int i = 0; i < PB_NUM; i++) {
        int b;
        if (bMine) {
            b = iosPad_PulseRead(&iosPad_s.aPulseQueue[i], &iosPad_s.aPulseReads[i], &iosPad_s.aPulseGap[i], iosPad_s.aEngine[i]);
        }
        else if (i == PB_LT) b = lt > IOSPAD_TRIGGER_DOWN;
        else if (i == PB_RT) b = rt > IOSPAD_TRIGGER_DOWN;
        else b = SDL_GetGamepadButton(g, (SDL_GamepadButton)iosPad_aSdlButton[i]) ? 1 : 0;
        stdControl_UpdateKeyState(iosPad_KeyFor(idx, i), b, stdControl_curReadTime);
    }
    return 1;
}

#endif // IOSPAD_ENABLED

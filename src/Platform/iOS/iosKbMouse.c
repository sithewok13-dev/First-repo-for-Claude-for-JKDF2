// Added: entire file. See iosKbMouse.h.

#include "Platform/iOS/iosKbMouse.h"

#ifdef IOSKBM_ENABLED

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
#include "stdPlatform.h"
#include "Enhanced/iosGame.h"
#include "Platform/iOS/iosPad.h"
#include "Platform/Input/inputUi.h" // (iOS: the touch overlay and iosPadPanel; elsewhere: inputOverlay.c)

extern SDL_Window* displayWindow;
extern int stdControl_bControllerEscapeKey;
extern const uint8_t stdControl_aSdlToDik[256];
void sithControl_DefaultHelper(int funcIdx, int dxKeyNum, int flags);

// A wheel's key held this long opens its wheel (as FORCE WHEEL and NEXT WPN
// held: 0.3 s). (Not the mouse moved while it is down, as a slide or the
// pad's stick would: a key tapped while turning is a tap.)
#define IOSKBM_WHEEL_HOLD 0.3
// The mouse on an open wheel is a stick: its movement adds up, held at
// IOSKBM_AIM_RIM; past IOSKBM_AIM_ON from the middle it points (the way it
// is), back under IOSKBM_AIM_OFF it points at nothing
#define IOSKBM_AIM_RIM 120.0f
#define IOSKBM_AIM_ON 40.0f
#define IOSKBM_AIM_OFF 15.0f
// P held this long: the selfie (as CAMERA held: 0.5 s)
#define IOSKBM_SELFIE_HOLD 0.5
// F10 held this long quick loads (as QUICK LOAD: 0.3 s)
#define IOSKBM_LOAD_HOLD 0.3
// A press made for the game (a wheel key's tap) is held down for this many
// of the game's control reads, then let go for one (as the pad's)
#define IOSKBM_PULSE_READS 2
#define IOSKBM_ESCAPE_FRAMES 2
// What counts as playing with the mouse (hiding the touch controls): a
// click, the wheel, or this much movement (counts) in play
#define IOSKBM_ACTIVE_MOVE 12.0f
// The wheel: this much of it is a weapon step (a mouse's notch); at most one
// step a frame, the rest of a big scroll let go
#define IOSKBM_WHEEL_STEP 1.0f

#define IOSKBM_NUM_KEYS 256
#define IOSKBM_NUM_BUTTONS 5

// The keyboard's own shortcuts (only where nothing is bound to the key)
#define IOSKBM_KEY_VIEW SDL_SCANCODE_H
#define IOSKBM_KEY_SABER SDL_SCANCODE_L
#define IOSKBM_KEY_PHOTO SDL_SCANCODE_P
#define IOSKBM_KEY_FREEZE SDL_SCANCODE_O
#define IOSKBM_KEY_LOAD SDL_SCANCODE_F10

enum { IOSKBM_WS_NONE = 0, IOSKBM_WS_PENDING, IOSKBM_WS_HELD, IOSKBM_WS_TAP };

static struct {
    int bGame;                            // gameplay is listening
    int bHudHidden;                       // the touch controls are hidden for the keyboard and mouse
    int bActive;                          // played with since the last frame (events)
    double now;
    unsigned char aDown[IOSKBM_NUM_KEYS], aPrev[IOSKBM_NUM_KEYS];
    unsigned char aEventDown[IOSKBM_NUM_KEYS]; // pressed since last frame, by the events (a press shorter than a frame)
    unsigned char aStale[IOSKBM_NUM_KEYS];     // held while the game wasn't to see it: ignored until let go
    unsigned char aPulseQueue[IOSKBM_NUM_KEYS], aPulseReads[IOSKBM_NUM_KEYS], aPulseGap[IOSKBM_NUM_KEYS];
    int aBtnDown[IOSKBM_NUM_BUTTONS], aBtnStale[IOSKBM_NUM_BUTTONS];
    int aBtnClick[IOSKBM_NUM_BUTTONS];    // pressed since last frame (events)
    // the mouse's movement: fractions carried over; on a wheel, the stick
    float restX, restY;
    float aimX, aimY;
    int bAimOn, bAimMoved;
    float activeMove;
    // the scroll wheel: what is left of a step; the steps of this frame's
    // events, and those the game can take now (until the next frame)
    float scroll;
    int scrollSteps, wheelStepsNew, wheelStepsReady;
    // the wheel being opened or open from the keyboard
    int wheelState, wheelKey, wheelKind;
    double tWheelDown;
    // P (a photo, held: the selfie), F10 (quick load)
    int camState; double tCam;
    int loadState; double tLoad;
    // Escape for Cmd-., frames left
    int escFrames;
    // what the readout shows
    unsigned nRel, nAbs, nClicks, nWheel, nKeys;
    float relSum, lastWheel;
    int lastTouch; // 0 none yet, 1 a pointer click, 2 a finger
} iosKbMouse_s = { .wheelKey = -1 };

// ------------------------------------------------------------ the game

static int iosKbMouse_IsGameplay(void)
{
    return stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive() && !jkQuakeConsole_bOpen
           && sithWorld_g_pCurrentWorld && sithPlayer_g_pLocalPlayerThing;
}

// The function a key (SDL scancode) is bound to (the first; a key has one), or -1
static int iosKbMouse_FuncOfScancode(int sc)
{
    if (sc <= 0 || sc >= IOSKBM_NUM_KEYS || !stdControl_aSdlToDik[sc]) return -1;
    const int dik = stdControl_aSdlToDik[sc];
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            const stdControlKeyInfoEntry* e = &k->aEntries[j];
            if ((e->flags & INPUT_MAPPING_FLAG_AXIS) == 0 && e->dxKeyNum == dik) return f;
        }
    }
    return -1;
}

// Whether a shortcut's key is free for it (nothing in Setup > Keyboard on it)
static int iosKbMouse_IsFree(int sc)
{
    return iosKbMouse_FuncOfScancode(sc) < 0;
}

// Whether anything is bound to the mouse wheel (Setup > Mouse)
static int iosKbMouse_IsScrollBound(void)
{
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            if ((k->aEntries[j].flags & INPUT_MAPPING_FLAG_AXIS) && k->aEntries[j].dxKeyNum == AXIS_MOUSE_Z) return 1;
        }
    }
    return 0;
}

static int iosKbMouse_IsRealMouse(SDL_MouseID which)
{
    return which != SDL_TOUCH_MOUSEID && which != SDL_PEN_MOUSEID;
}

// ------------------------------------------------------------ the touch controls

static void iosKbMouse_SetHudHidden(int bHidden)
{
    if (iosKbMouse_s.bHudHidden == bHidden) return;
    iosKbMouse_s.bHudHidden = bHidden;
    inputUi_SetKbmHud(bHidden);
}

void iosKbMouse_NoteTouch(void)
{
    // (the overlay shows itself again; this only keeps count)
    iosKbMouse_s.bHudHidden = 0;
}

void iosKbMouse_NoteTouchKind(int bPointer)
{
    iosKbMouse_s.lastTouch = bPointer ? 1 : 2;
}

int iosKbMouse_IsPointerFree(void)
{
    return inputUi_WantsPointer() || iosPad_PanelIsOpen();
}

// ------------------------------------------------------------ the wheels

static const char* iosKbMouse_KeyName(int sc)
{
    const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
    return (n && n[0]) ? n : "the key";
}

static void iosKbMouse_SetWheelPrompt(void)
{
    char t[256];
    if (iosKbMouse_s.wheelKind == IOSPAD_WHEEL_SABER)
        snprintf(t, sizeof(t), "Mouse: colour   Click: set   1: one or two sabers   2: which blade   3: take the saber   Esc: close");
    else if (iosKbMouse_s.wheelState == IOSKBM_WS_HELD)
        snprintf(t, sizeof(t), "Mouse: point   Let go of %s: pick   Right click or Esc: cancel", iosKbMouse_KeyName(iosKbMouse_s.wheelKey));
    else
        snprintf(t, sizeof(t), "Mouse or scroll wheel: point   Click: pick   Right click or Esc: close");
    inputUi_SetPrompt(t);
}

const char* iosKbMouse_WheelHint(int bHot, int bCancel)
{
    static char t[64];
    const int st = iosKbMouse_s.wheelState;
    if (st != IOSKBM_WS_HELD && st != IOSKBM_WS_TAP) return NULL;
    if (bCancel) snprintf(t, sizeof(t), st == IOSKBM_WS_HELD ? "let go to cancel" : "Esc to close");
    else if (bHot) snprintf(t, sizeof(t), st == IOSKBM_WS_HELD ? "let go to pick" : iosKbMouse_s.wheelKind == IOSPAD_WHEEL_SABER ? "click to set" : "click to pick");
    else snprintf(t, sizeof(t), "move the mouse");
    return t;
}

// The wheel a key's press would open (IOSPAD_WHEEL_*), or -1
static int iosKbMouse_WheelKindFor(int sc)
{
    const int f = iosKbMouse_FuncOfScancode(sc);
    if (f == INPUT_FUNC_NEXTSKILL || f == INPUT_FUNC_PREVSKILL) return IOSPAD_WHEEL_FORCE;
    if (f == INPUT_FUNC_NEXTWEAPON || f == INPUT_FUNC_PREVWEAPON) return IOSPAD_WHEEL_WEAPONS;
    if (sc == IOSKBM_KEY_SABER && f < 0 && iosGame_IsSaberButtonShown()) return IOSPAD_WHEEL_SABER;
    return -1;
}

static void iosKbMouse_WheelReset(void)
{
    if (iosKbMouse_s.wheelState >= IOSKBM_WS_HELD) inputUi_SetPrompt(NULL);
    // (its key, still held, doesn't reach the game either)
    const int key = iosKbMouse_s.wheelKey;
    if (key >= 0 && iosKbMouse_s.aDown[key]) iosKbMouse_s.aStale[key] = 1;
    iosKbMouse_s.wheelState = IOSKBM_WS_NONE;
    iosKbMouse_s.wheelKey = -1;
    iosKbMouse_s.aimX = iosKbMouse_s.aimY = 0.0f;
    iosKbMouse_s.bAimOn = iosKbMouse_s.bAimMoved = 0;
}

static void iosKbMouse_WheelOpen(int bTap)
{
    iosKbMouse_s.wheelState = bTap ? IOSKBM_WS_TAP : IOSKBM_WS_HELD; // (first: the wheel asks for its hint as it opens)
    iosKbMouse_s.aimX = iosKbMouse_s.aimY = 0.0f;
    iosKbMouse_s.bAimOn = iosKbMouse_s.bAimMoved = 0;
    if (!inputUi_KbmWheelOpen(iosKbMouse_s.wheelKind, bTap)) {
        // (it can't now: the press is spent)
        iosKbMouse_s.wheelState = IOSKBM_WS_PENDING; // (no prompt to take away)
        iosKbMouse_WheelReset();
        return;
    }
    iosKbMouse_SetWheelPrompt();
}

static void iosKbMouse_WheelClose(int bSelect)
{
    if (inputUi_KbmWheelIsOpen() && inputUi_WheelClose(bSelect)) {
        // a saber colour set: the wheel stays open, to tap
        iosKbMouse_s.wheelState = IOSKBM_WS_TAP;
        iosKbMouse_SetWheelPrompt();
        return;
    }
    iosKbMouse_WheelReset();
}

// The mouse's movement on the wheel (or while its key is down)
static void iosKbMouse_Aim(float dx, float dy)
{
    float x = iosKbMouse_s.aimX + dx, y = iosKbMouse_s.aimY + dy;
    const float m = sqrtf(x * x + y * y);
    if (m > IOSKBM_AIM_RIM) {
        x *= IOSKBM_AIM_RIM / m;
        y *= IOSKBM_AIM_RIM / m;
    }
    iosKbMouse_s.aimX = x;
    iosKbMouse_s.aimY = y;
    iosKbMouse_s.bAimMoved = 1;
}

static void iosKbMouse_ApplyAim(void)
{
    if (!iosKbMouse_s.bAimMoved) return;
    iosKbMouse_s.bAimMoved = 0;
    const float m = sqrtf(iosKbMouse_s.aimX * iosKbMouse_s.aimX + iosKbMouse_s.aimY * iosKbMouse_s.aimY);
    if (m >= IOSKBM_AIM_ON) iosKbMouse_s.bAimOn = 1;
    else if (m < IOSKBM_AIM_OFF) iosKbMouse_s.bAimOn = 0;
    if (!iosKbMouse_s.bAimOn) {
        inputUi_WheelAim(0, 0.0f);
        return;
    }
    // (degrees counter-clockwise from pointing right, as the slices are; the mouse's y is down)
    inputUi_WheelAim(1, atan2f(-iosKbMouse_s.aimY, iosKbMouse_s.aimX) * (float)(180.0 / M_PI));
}

static int iosKbMouse_Pressed(int sc)
{
    return iosKbMouse_s.aDown[sc] && (!iosKbMouse_s.aPrev[sc] || iosKbMouse_s.aEventDown[sc]) && !iosKbMouse_s.aStale[sc];
}

static void iosKbMouse_Pulse(int sc)
{
    if (iosKbMouse_s.aPulseQueue[sc] < 255) iosKbMouse_s.aPulseQueue[sc]++;
}

// The open wheel (or one being opened) this frame
static void iosKbMouse_WheelInput(void)
{
    const int st = iosKbMouse_s.wheelState;
    const int key = iosKbMouse_s.wheelKey;

    if (st == IOSKBM_WS_PENDING) {
        if (!iosKbMouse_s.aDown[key]) {
            // a tap: what the key is bound to (the next power, the next weapon)
            iosKbMouse_Pulse(key);
            iosKbMouse_WheelReset();
        }
        else if (iosKbMouse_s.now - iosKbMouse_s.tWheelDown >= IOSKBM_WHEEL_HOLD) {
            iosKbMouse_WheelOpen(0);
        }
        return;
    }

    if (!inputUi_KbmWheelIsOpen()) {
        // closed by a touch (or the overlay went)
        iosKbMouse_WheelReset();
        return;
    }
    iosKbMouse_ApplyAim();
    while (iosKbMouse_s.scrollSteps) {
        inputUi_WheelStep(iosKbMouse_s.scrollSteps > 0 ? 1 : -1);
        iosKbMouse_s.scrollSteps += iosKbMouse_s.scrollSteps > 0 ? -1 : 1;
        iosKbMouse_s.bAimOn = 0; // (the scroll wheel points now, until the mouse moves on)
        iosKbMouse_s.aimX = iosKbMouse_s.aimY = 0.0f;
    }

    if (iosKbMouse_s.aBtnClick[1]) {
        iosKbMouse_WheelClose(0);
        return;
    }
    if (iosKbMouse_s.aBtnClick[0] || iosKbMouse_Pressed(SDL_SCANCODE_RETURN) || iosKbMouse_Pressed(SDL_SCANCODE_KP_ENTER)) {
        iosKbMouse_WheelClose(1);
        return;
    }
    if (iosKbMouse_s.wheelKind == IOSPAD_WHEEL_SABER) {
        if (iosKbMouse_Pressed(SDL_SCANCODE_1)) inputUi_SaberChip(IOSPAD_CHIP_STYLE);
        if (iosKbMouse_Pressed(SDL_SCANCODE_2)) inputUi_SaberChip(IOSPAD_CHIP_TARGET);
        if (iosKbMouse_Pressed(SDL_SCANCODE_3)) inputUi_SaberChip(IOSPAD_CHIP_EQUIP);
        if (!inputUi_KbmWheelIsOpen()) {
            iosKbMouse_WheelReset();
            return;
        }
    }
    if (st == IOSKBM_WS_HELD && key >= 0 && !iosKbMouse_s.aDown[key]) {
        // let go: what it points at; nothing pointed at, it stays open to click
        if (inputUi_WheelHot(NULL)) iosKbMouse_WheelClose(1);
        else {
            iosKbMouse_s.wheelState = IOSKBM_WS_TAP;
            inputUi_WheelSetTapMode();
            iosKbMouse_SetWheelPrompt();
        }
        return;
    }
    if (st == IOSKBM_WS_TAP && key >= 0 && iosKbMouse_Pressed(key)) {
        iosKbMouse_WheelClose(0); // (its key again)
    }
}

// ------------------------------------------------------------ the frame

static void iosKbMouse_SetRing(int kind, float progress)
{
    inputUi_HoldRing(kind, progress);
}

static void iosKbMouse_LeaveGame(void)
{
    if (iosKbMouse_s.wheelState >= IOSKBM_WS_HELD && inputUi_KbmWheelIsOpen()) inputUi_WheelClose(0);
    iosKbMouse_WheelReset();
    if (iosKbMouse_s.loadState == 1) iosKbMouse_SetRing(IOSPAD_RING_NONE, 0.0f);
    iosKbMouse_s.loadState = iosKbMouse_s.camState = 0;
    memset(iosKbMouse_s.aPulseQueue, 0, sizeof(iosKbMouse_s.aPulseQueue));
    memset(iosKbMouse_s.aPulseReads, 0, sizeof(iosKbMouse_s.aPulseReads));
    memset(iosKbMouse_s.aPulseGap, 0, sizeof(iosKbMouse_s.aPulseGap));
    iosKbMouse_s.restX = iosKbMouse_s.restY = 0.0f;
    iosKbMouse_s.scroll = 0.0f;
    iosKbMouse_s.scrollSteps = iosKbMouse_s.wheelStepsNew = iosKbMouse_s.wheelStepsReady = 0;
}

static void iosKbMouse_Escape(void)
{
    // as the pad's Start tap: down now, this frame's Window_SdlUpdate acts on it; let go after
    stdControl_bControllerEscapeKey = 1;
    iosKbMouse_s.escFrames = IOSKBM_ESCAPE_FRAMES - 1;
}

// The keyboard's shortcuts, in play
static void iosKbMouse_Shortcuts(void)
{
    // H: first or third person (as VIEW)
    if (iosKbMouse_Pressed(IOSKBM_KEY_VIEW) && iosKbMouse_IsFree(IOSKBM_KEY_VIEW))
        iosGame_SetThirdPerson(!iosGame_IsThirdPerson());

    // P: a photo (as CAMERA tapped); held, the selfie
    if (iosKbMouse_Pressed(IOSKBM_KEY_PHOTO) && iosKbMouse_IsFree(IOSKBM_KEY_PHOTO)) {
        iosKbMouse_s.camState = 1;
        iosKbMouse_s.tCam = iosKbMouse_s.now;
    }
    if (iosKbMouse_s.camState == 1) {
        if (!iosKbMouse_s.aDown[IOSKBM_KEY_PHOTO]) {
            iosKbMouse_s.camState = 0;
            inputUi_Camera(INPUTUI_CAM_PHOTO);
        }
        else if (iosKbMouse_s.now - iosKbMouse_s.tCam >= IOSKBM_SELFIE_HOLD) {
            iosKbMouse_s.camState = 0;
            inputUi_Camera(INPUTUI_CAM_SELFIE);
        }
    }

    // O: Action Freeze
    if (iosKbMouse_Pressed(IOSKBM_KEY_FREEZE) && iosKbMouse_IsFree(IOSKBM_KEY_FREEZE))
        inputUi_Camera(INPUTUI_CAM_FREEZE);

    // F10 held: quick load, its ring filling meanwhile (as QUICK LOAD)
    if (iosKbMouse_Pressed(IOSKBM_KEY_LOAD) && iosKbMouse_IsFree(IOSKBM_KEY_LOAD)) {
        iosKbMouse_s.loadState = 1;
        iosKbMouse_s.tLoad = iosKbMouse_s.now;
    }
    if (iosKbMouse_s.loadState == 1) {
        const double t = iosKbMouse_s.now - iosKbMouse_s.tLoad;
        if (!iosKbMouse_s.aDown[IOSKBM_KEY_LOAD]) {
            iosKbMouse_s.loadState = 0; // (let go sooner: nothing)
            iosKbMouse_SetRing(IOSPAD_RING_NONE, 0.0f);
        }
        else if (t >= IOSKBM_LOAD_HOLD) {
            iosKbMouse_s.loadState = 2; // (nothing more on this press)
            iosKbMouse_SetRing(IOSPAD_RING_NONE, 0.0f);
            iosGame_QuickLoad();
        }
        else iosKbMouse_SetRing(IOSPAD_RING_LOAD, (float)(t / IOSKBM_LOAD_HOLD));
    }
    else if (iosKbMouse_s.loadState == 2 && !iosKbMouse_s.aDown[IOSKBM_KEY_LOAD]) iosKbMouse_s.loadState = 0;
}

void iosKbMouse_Update(void)
{
    iosKbMouse_s.now = (double)SDL_GetTicksNS() * 1e-9;

    // Escape for Cmd-., for a couple of frames (Window_SdlUpdate acts on it once)
    if (iosKbMouse_s.escFrames > 0) {
        stdControl_bControllerEscapeKey = 1;
        if (--iosKbMouse_s.escFrames == 0) iosKbMouse_s.escFrames = -1;
    }
    else if (iosKbMouse_s.escFrames < 0) {
        stdControl_bControllerEscapeKey = 0;
        iosKbMouse_s.escFrames = 0;
    }

    // The mouse wheel's weapon steps: this frame's for the game to take, until the next
    iosKbMouse_s.wheelStepsReady = iosKbMouse_s.wheelStepsNew;
    iosKbMouse_s.wheelStepsNew = 0;

    const bool* ks = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < IOSKBM_NUM_KEYS; i++) {
        iosKbMouse_s.aDown[i] = (ks && ks[i]) || iosKbMouse_s.aEventDown[i];
        if (!iosKbMouse_s.aDown[i]) iosKbMouse_s.aStale[i] = 0;
    }
    const SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL);
    static const int aMask[IOSKBM_NUM_BUTTONS] = { SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK, SDL_BUTTON_X1MASK, SDL_BUTTON_X2MASK };
    for (int i = 0; i < IOSKBM_NUM_BUTTONS; i++) {
        iosKbMouse_s.aBtnDown[i] = (mb & aMask[i]) != 0 || iosKbMouse_s.aBtnClick[i];
        if (!iosKbMouse_s.aBtnDown[i]) iosKbMouse_s.aBtnStale[i] = 0;
    }

    const int bGame = iosKbMouse_IsGameplay();
    if (bGame != iosKbMouse_s.bGame) {
        if (!bGame) iosKbMouse_LeaveGame();
        iosKbMouse_s.bGame = bGame;
    }

    if (bGame) {
        const int bWheelBefore = iosKbMouse_s.wheelState >= IOSKBM_WS_HELD;
        const int bCmd = iosKbMouse_s.aDown[SDL_SCANCODE_LGUI] || iosKbMouse_s.aDown[SDL_SCANCODE_RGUI];
        const int bPhoto = inputUi_IsPhotoModeUp();
        const int bOtherWheel = inputUi_IsWheelOpen() && !inputUi_KbmWheelIsOpen();
        const int bTyping = jkHud_bChatOpen || stdControl_bDisableKeyboard;

        if (iosKbMouse_s.bActive && !bPhoto) iosKbMouse_SetHudHidden(1);

        if (bPhoto) {
            // a photo mode: P takes Action Freeze's photos, O ends it (Escape: iosKbMouse_HandleEvent)
            if (iosKbMouse_s.wheelState == IOSKBM_WS_PENDING) iosKbMouse_WheelReset();
            iosKbMouse_s.camState = 0;
            if (iosKbMouse_s.loadState == 1) iosKbMouse_SetRing(IOSPAD_RING_NONE, 0.0f);
            iosKbMouse_s.loadState = 0;
            if (!bCmd && iosKbMouse_Pressed(IOSKBM_KEY_PHOTO) && iosKbMouse_IsFree(IOSKBM_KEY_PHOTO)) inputUi_Camera(INPUTUI_CAM_PHOTO);
            if (!bCmd && iosKbMouse_Pressed(IOSKBM_KEY_FREEZE) && iosKbMouse_IsFree(IOSKBM_KEY_FREEZE)) inputUi_Camera(INPUTUI_CAM_FREEZE);
        }
        else if (bTyping || bOtherWheel || iosPad_PanelIsOpen() || bCmd) {
            // typing (the keys are the typing line's), a wheel a finger or the
            // pad opened, the pad's options, Cmd's shortcuts: nothing of ours
            if (iosKbMouse_s.wheelState == IOSKBM_WS_PENDING) iosKbMouse_WheelReset();
            else if (iosKbMouse_s.wheelState >= IOSKBM_WS_HELD) iosKbMouse_WheelInput();
            iosKbMouse_s.camState = 0;
            if (iosKbMouse_s.loadState == 1) iosKbMouse_SetRing(IOSPAD_RING_NONE, 0.0f);
            iosKbMouse_s.loadState = 0;
        }
        else if (iosKbMouse_s.wheelState != IOSKBM_WS_NONE) {
            iosKbMouse_WheelInput();
        }
        else {
            // a wheel's key: waiting to see a tap, a hold or the mouse (L: the saber wheel, open at once)
            for (int i = 0; i < IOSKBM_NUM_KEYS; i++) {
                if (!iosKbMouse_Pressed(i)) continue;
                const int kind = iosKbMouse_WheelKindFor(i);
                if (kind < 0) continue;
                iosKbMouse_s.wheelKey = i;
                iosKbMouse_s.wheelKind = kind;
                iosKbMouse_s.tWheelDown = iosKbMouse_s.now;
                iosKbMouse_s.aimX = iosKbMouse_s.aimY = 0.0f;
                iosKbMouse_s.bAimOn = 0;
                if (kind == IOSPAD_WHEEL_SABER) iosKbMouse_WheelOpen(1);
                else iosKbMouse_s.wheelState = IOSKBM_WS_PENDING;
                break;
            }
            if (iosKbMouse_s.wheelState == IOSKBM_WS_NONE) iosKbMouse_Shortcuts();
        }

        // What the game isn't to see: anything held while a wheel, a photo
        // mode, the pad's options or Cmd has the keyboard and mouse is
        // ignored until let go
        const int bBlocked = bPhoto || bOtherWheel || iosPad_PanelIsOpen() || bCmd || bWheelBefore || iosKbMouse_s.wheelState >= IOSKBM_WS_HELD;
        if (bBlocked) {
            for (int i = 0; i < IOSKBM_NUM_KEYS; i++) {
                if (iosKbMouse_s.aDown[i] && i != iosKbMouse_s.wheelKey) iosKbMouse_s.aStale[i] = 1;
            }
            for (int i = 0; i < IOSKBM_NUM_BUTTONS; i++) iosKbMouse_s.aBtnStale[i] = iosKbMouse_s.aBtnDown[i];
            if (bCmd || bPhoto) {
                memset(iosKbMouse_s.aPulseQueue, 0, sizeof(iosKbMouse_s.aPulseQueue));
            }
        }
        else if (iosKbMouse_IsPointerFree()) {
            // (MENU's tray, the pad's options: a click there works them, it doesn't fire)
            for (int i = 0; i < IOSKBM_NUM_BUTTONS; i++) iosKbMouse_s.aBtnStale[i] = iosKbMouse_s.aBtnDown[i];
        }
    }
    iosKbMouse_s.scrollSteps = 0;
    iosKbMouse_s.bActive = 0;
    memcpy(iosKbMouse_s.aPrev, iosKbMouse_s.aDown, sizeof(iosKbMouse_s.aPrev));
    memset(iosKbMouse_s.aEventDown, 0, sizeof(iosKbMouse_s.aEventDown));
    memset(iosKbMouse_s.aBtnClick, 0, sizeof(iosKbMouse_s.aBtnClick));

    inputUi_KbmViewTick();
}

// ------------------------------------------------------------ events

static void iosKbMouse_CheckDevicesGone(void)
{
    // the last keyboard and mouse gone: the touch controls come back
    int n = 0;
    SDL_free(SDL_GetMice(&n));
    int k = 0;
    SDL_free(SDL_GetKeyboards(&k));
    if (n == 0 && k == 0) iosKbMouse_SetHudHidden(0);
}

int iosKbMouse_HandleEvent(void* pSdlEvent)
{
    SDL_Event* e = (SDL_Event*)pSdlEvent;
    const int bGame = iosKbMouse_s.bGame && iosKbMouse_IsGameplay();
    const int bWheel = iosKbMouse_s.wheelState >= IOSKBM_WS_HELD; // (the keyboard's wheel open)

    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        if (!iosKbMouse_IsRealMouse(e->motion.which)) return 0;
        const int bRel = displayWindow && SDL_GetWindowRelativeMouseMode(displayWindow);
        if (bRel) {
            iosKbMouse_s.nRel++;
            iosKbMouse_s.relSum += fabsf(e->motion.xrel) + fabsf(e->motion.yrel);
        }
        else iosKbMouse_s.nAbs++;
        if (!bGame) {
            iosKbMouse_s.restX = iosKbMouse_s.restY = 0.0f;
            return 0;
        }
        if (bWheel) {
            iosKbMouse_Aim(e->motion.xrel, e->motion.yrel);
            return 1; // (the wheel's, not the view's)
        }
        if (iosKbMouse_IsPointerFree() || inputUi_IsPhotoModeUp() || inputUi_IsWheelOpen()) {
            // the pointer let go (a photo mode, a panel) or something else has the game: not the view's
            iosKbMouse_s.restX = iosKbMouse_s.restY = 0.0f;
            return 1;
        }
        if (!bRel) return 0; // (as it always was)
        iosKbMouse_s.activeMove += fabsf(e->motion.xrel) + fabsf(e->motion.yrel);
        if (iosKbMouse_s.activeMove >= IOSKBM_ACTIVE_MOVE) {
            iosKbMouse_s.activeMove = 0.0f;
            iosKbMouse_s.bActive = 1;
        }
        // the whole movement, fractions carried over (Window_HandleMouseMove keeps whole counts)
        const float x = e->motion.xrel + iosKbMouse_s.restX, y = e->motion.yrel + iosKbMouse_s.restY;
        e->motion.xrel = truncf(x);
        e->motion.yrel = truncf(y);
        iosKbMouse_s.restX = x - e->motion.xrel;
        iosKbMouse_s.restY = y - e->motion.yrel;
        return 0;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!iosKbMouse_IsRealMouse(e->button.which)) return 0;
        const int b = e->button.button - 1;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            iosKbMouse_s.nClicks++;
            if (b >= 0 && b < IOSKBM_NUM_BUTTONS) iosKbMouse_s.aBtnClick[b] = 1;
            if (bGame && !iosKbMouse_IsPointerFree()) iosKbMouse_s.bActive = 1;
            // (a click on the wheel is the wheel's; a button let go always reaches the game)
            if (bGame && iosKbMouse_s.wheelState >= IOSKBM_WS_HELD) return 1;
        }
        return 0;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!iosKbMouse_IsRealMouse(e->wheel.which)) return 0;
        iosKbMouse_s.nWheel++;
        iosKbMouse_s.lastWheel = e->wheel.y;
        if (!bGame) return 0;
        if (iosKbMouse_IsPointerFree() || inputUi_IsPhotoModeUp() || (inputUi_IsWheelOpen() && !bWheel)) return 1;
        if (!bWheel && iosKbMouse_IsScrollBound()) {
            iosKbMouse_s.bActive = 1;
            return 0; // (the player's own binding)
        }
        // the way the wheel turned (down: the next), the system's natural scrolling undone
        float y = e->wheel.y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        iosKbMouse_s.scroll -= y;
        int step = 0;
        if (iosKbMouse_s.scroll >= IOSKBM_WHEEL_STEP) step = 1;
        else if (iosKbMouse_s.scroll <= -IOSKBM_WHEEL_STEP) step = -1;
        if (step) {
            iosKbMouse_s.scroll = 0.0f; // (one step a notch, however big)
            if (bWheel) iosKbMouse_s.scrollSteps += step;
            else if (iosKbMouse_s.wheelStepsNew == 0) iosKbMouse_s.wheelStepsNew = step;
        }
        iosKbMouse_s.bActive = 1;
        return 1;
    }
    case SDL_EVENT_KEY_DOWN: {
        const int sc = (int)e->key.scancode;
        // A key pressed while typing (the typing line, cheats) is the typing
        // line's: never a new press for us -- not even one let go in the
        // frame the line closes (Return), nor while still held after
        const int bTyping = jkHud_bChatOpen || stdControl_bDisableKeyboard;
        if (sc > 0 && sc < IOSKBM_NUM_KEYS) {
            if (bTyping) iosKbMouse_s.aStale[sc] = 1;
            else if (!e->key.repeat) iosKbMouse_s.aEventDown[sc] = 1;
        }
        iosKbMouse_s.nKeys++;
        // Cmd-. is Escape, for keyboards without one: whatever Escape does
        const int bCmdDot = sc == SDL_SCANCODE_PERIOD && (e->key.mod & SDL_KMOD_GUI) != 0;
        if (!bGame) {
            if (bCmdDot && !e->key.repeat) iosKbMouse_Escape();
            return bCmdDot;
        }
        // (a new press only, never a key still held -- its repeats neither --
        // so a touch's showing the controls again isn't undone; typing, the
        // on-screen keyboard's keys among them, isn't playing)
#ifdef TARGET_IOS
        if (!e->key.repeat && !bTyping && !iosKbMouse_IsPointerFree()) iosKbMouse_s.bActive = 1;
#else
        // (the phone's own keys -- Android's Back, the volume, media and
        // power keys -- aren't a keyboard being played)
        const int bDeviceKey = sc == SDL_SCANCODE_AC_BACK || sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_VOLUMEDOWN
                               || sc == SDL_SCANCODE_MUTE || sc == SDL_SCANCODE_MEDIA_PLAY || sc == SDL_SCANCODE_MEDIA_PAUSE
                               || sc == SDL_SCANCODE_MEDIA_PLAY_PAUSE || sc == SDL_SCANCODE_POWER || sc == SDL_SCANCODE_UNKNOWN;
        if (!e->key.repeat && !bTyping && !bDeviceKey && !iosKbMouse_IsPointerFree()) iosKbMouse_s.bActive = 1;
#endif
        if (sc == SDL_SCANCODE_ESCAPE || bCmdDot) {
            if (iosKbMouse_s.wheelState >= IOSKBM_WS_HELD) {
                // Escape on the keyboard's wheel: it closes, nothing picked (not the menu)
                if (!e->key.repeat) iosKbMouse_WheelClose(0);
                return 1;
            }
            if (inputUi_IsPhotoModeUp()) {
                // Action Freeze: back to the game; the others go on their own (not the menu over them)
                if (!e->key.repeat) inputUi_Camera(INPUTUI_CAM_FREEZE_END);
                return 1;
            }
            if (bCmdDot) {
                // (the game's own Escape, as the window gives it for the Escape key)
                if (!e->key.repeat) iosKbMouse_Escape();
                return 1;
            }
        }
        return 0;
    }
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_KEYBOARD_REMOVED:
        iosKbMouse_CheckDevicesGone();
        return 0;
    default:
        return 0;
    }
}

// ------------------------------------------------------------ the game's read

int iosKbMouse_FilterKey(int scancode, int bDown)
{
    if (scancode < 0 || scancode >= IOSKBM_NUM_KEYS || !iosKbMouse_s.bGame) return bDown;
    int b = bDown && !iosKbMouse_s.aStale[scancode];
    if (scancode == iosKbMouse_s.wheelKey && iosKbMouse_s.wheelState != IOSKBM_WS_NONE) b = 0; // (a wheel's key: the wheel's until it is a tap)
    // a press made for the game: held for IOSKBM_PULSE_READS reads, then let go for one
    unsigned char* pReads = &iosKbMouse_s.aPulseReads[scancode];
    unsigned char* pGap = &iosKbMouse_s.aPulseGap[scancode];
    unsigned char* pQueue = &iosKbMouse_s.aPulseQueue[scancode];
    if (*pReads) {
        if (--*pReads == 0) *pGap = 1;
        return 1;
    }
    if (*pGap) {
        *pGap = 0;
        return b;
    }
    if (*pQueue) {
        (*pQueue)--;
        *pReads = IOSKBM_PULSE_READS - 1;
        if (!*pReads) *pGap = 1;
        return 1;
    }
    return b;
}

int iosKbMouse_FilterMouseButton(int button, int bDown)
{
    if (button < 0 || button >= IOSKBM_NUM_BUTTONS || !iosKbMouse_s.bGame) return bDown;
    return bDown && !iosKbMouse_s.aBtnStale[button] && iosKbMouse_s.wheelState < IOSKBM_WS_HELD;
}

int iosKbMouse_TakeWheelSteps(int dir)
{
    const int s = iosKbMouse_s.wheelStepsReady;
    if (!s || (s > 0) != (dir > 0)) return 0;
    iosKbMouse_s.wheelStepsReady = 0;
    return s > 0 ? s : -s;
}

// ------------------------------------------------------------ the readout

int iosKbMouse_SawPointerClick(void)
{
    return iosKbMouse_s.lastTouch == 1;
}

void iosKbMouse_DiagText(char* pBuf, int size)
{
    const char* t = iosKbMouse_s.lastTouch == 1 ? "POINTER" : iosKbMouse_s.lastTouch == 2 ? "FINGER" : "-";
    snprintf(pBuf, (size_t)size, "REL %u (%.0f)  ABS %u  CLICK %u  WHEEL %u (%.2f)  KEY %u  TOUCH %s",
             iosKbMouse_s.nRel, (double)iosKbMouse_s.relSum, iosKbMouse_s.nAbs, iosKbMouse_s.nClicks, iosKbMouse_s.nWheel,
             (double)iosKbMouse_s.lastWheel, iosKbMouse_s.nKeys, t);
}

// ------------------------------------------------------------ bindings

void iosKbMouse_MapMouseDefaults(void)
{
    sithControl_DefaultHelper(INPUT_FUNC_FIRE1, KEY_MOUSE_B1, 2);
    sithControl_DefaultHelper(INPUT_FUNC_FIRE2, KEY_MOUSE_B2, 2);
    sithControl_DefaultHelper(INPUT_FUNC_JUMP, KEY_MOUSE_B3, 2);
}

// The functions a mouse button is bound to: how many, and the first
static int iosKbMouse_ButtonBinds(int key, int* pFunc)
{
    int n = 0;
    *pFunc = -1;
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            const stdControlKeyInfoEntry* e = &k->aEntries[j];
            if ((e->flags & INPUT_MAPPING_FLAG_AXIS) == 0 && e->dxKeyNum == key) {
                if (n++ == 0) *pFunc = f;
            }
        }
    }
    return n;
}

static void iosKbMouse_RebindButton(int fromFunc, int toFunc, int key)
{
    stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[fromFunc];
    uint32_t n = 0;
    for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
        const stdControlKeyInfoEntry* e = &k->aEntries[j];
        if ((e->flags & INPUT_MAPPING_FLAG_AXIS) == 0 && e->dxKeyNum == key) continue;
        k->aEntries[n++] = k->aEntries[j];
    }
    k->numEntries = n;
    sithControl_DefaultHelper(toFunc, key, 2);
}

int iosKbMouse_MigrateBinds(int savedVersion)
{
    if (savedVersion >= IOSKBMBINDS_VERSION) return 0;

    // the game's own wheel binding (looking up and down, as it made it): taken
    // away, for the weapons
    int numWheel = 0;
    stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[INPUT_FUNC_PITCH];
    uint32_t n = 0;
    for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
        const stdControlKeyInfoEntry* e = &k->aEntries[j];
        if ((e->flags & INPUT_MAPPING_FLAG_AXIS) && e->dxKeyNum == AXIS_MOUSE_Z
            && !(e->flags & (INPUT_MAPPING_FLAG_AXIS_REVERSED | INPUT_MAPPING_FLAG_RAW_AXIS)) && fabsf((float)e->binaryAxisVal - 4.0f) < 0.01f) {
            numWheel++;
            continue;
        }
        k->aEntries[n++] = k->aEntries[j];
    }
    k->numEntries = n;

    // the right and middle buttons as the game made them (jump, alt fire): swapped
    int f2, f3, bSwapped = 0;
    if (iosKbMouse_ButtonBinds(KEY_MOUSE_B2, &f2) == 1 && f2 == INPUT_FUNC_JUMP
        && iosKbMouse_ButtonBinds(KEY_MOUSE_B3, &f3) == 1 && f3 == INPUT_FUNC_FIRE2) {
        iosKbMouse_RebindButton(INPUT_FUNC_JUMP, INPUT_FUNC_FIRE2, KEY_MOUSE_B2);
        iosKbMouse_RebindButton(INPUT_FUNC_FIRE2, INPUT_FUNC_JUMP, KEY_MOUSE_B3);
        bSwapped = 1;
    }
    stdPlatform_Printf("iosKbMouse: mouse bindings to version %d (wheel freed: %d, right and middle buttons swapped: %d)\n",
                       IOSKBMBINDS_VERSION, numWheel, bSwapped);
    return 1;
}

#endif // IOSKBM_ENABLED

// Added: entire file. See inputOverlay.h; inputUi.h for what each inputUi_
// call does (the iOS overlay's own, iosTouch.m and iosPadPanel.m, did them
// first: the same rules, sizes and wording here).

#include "Platform/Input/inputOverlay.h"

#ifdef ENH_INPUT

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "globals.h"
#include "Gui/jkGUIRend.h"
#include "Main/sithCvar.h"
#include "Platform/stdControl.h"
#include "Win95/Window.h"
#include "Platform/Input/inputUi.h"
#include "Platform/Input/inputWheel.h"
#include "Platform/Input/inputPhoto.h"
#include "Platform/Touch/touchUiDraw.h"
#include "Platform/Touch/touchUiRender_gl.h"
#include "Platform/iOS/iosPad.h"
#include "Platform/iOS/iosKbMouse.h"
#include "Enhanced/iosGame.h"
#include "Enhanced/iosCapture.h"
#include "Enhanced/iosSaber.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern SDL_Window* displayWindow;
extern int std3D_windowFbo;

// The screen is at least this many points tall: what is drawn is the iOS
// overlay's size on a phone of that height, scaled with the window
#define INPUTOVERLAY_PT_H 480.0f
// The options panel (iosPadPanel.m's): a card at most this wide, rows this
// tall at most, the title and the hint, the padding
#define INPUTOVERLAY_PANEL_W 440.0f
#define INPUTOVERLAY_ROW_H 30.0f
#define INPUTOVERLAY_TITLE_H 34.0f
#define INPUTOVERLAY_HINT_H 28.0f
#define INPUTOVERLAY_PAD 10.0f
// The quick save / quick load ring: its radius, its top this far under the
// top of the screen
#define INPUTOVERLAY_RING_R 26.0f
#define INPUTOVERLAY_RING_TOP 64.0f
// The prompts: this tall, this far over the bottom
#define INPUTOVERLAY_PROMPT_H 28.0f
#define INPUTOVERLAY_PROMPT_GAP 6.0f
// The FPS readout counts frames over at least this long
#define INPUTOVERLAY_FPS_PERIOD 0.5

#define INPUTOVERLAY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define INPUTOVERLAY_MAX(a, b) ((a) > (b) ? (a) : (b))

// ------------------------------------------------------------ settings (cvars)

typedef struct {
    const char* pKey;  // as iosPad asks for it
    const char* pCvar;
    int32_t val;
    int32_t seen;      // the value iosPad last had
} inputOverlaySetting;

static inputOverlaySetting inputOverlay_aSetting[] = {
    { "iosPadLookX", "enh_padLookX", 2, 2 },
    { "iosPadLookY", "enh_padLookY", 2, 2 },
    { "iosPadCurve", "enh_padCurve", 1, 1 },
    { "iosPadInvertY", "enh_padInvertY", 0, 0 },
    { "iosPadAccel", "enh_padAccel", 0, 0 },
    { "iosPadRumble", "enh_padRumble", 1, 1 },
};
#define INPUTOVERLAY_NUM_SETTINGS ((int)(sizeof(inputOverlay_aSetting) / sizeof(inputOverlay_aSetting[0])))

static int32_t inputOverlay_bShowFps = 0;
static int32_t inputOverlay_saberTarget = INPUTWHEEL_SABER_BOTH;
static int32_t inputOverlay_selfieShots = 3;
static int32_t inputOverlay_photoKinds = IOSCAPTURE_CLEAN;
static int inputOverlay_bCvars = 0;
static int inputOverlay_saberTargetSeen = -1;

void inputOverlay_StartupCvars(void)
{
    for (int i = 0; i < INPUTOVERLAY_NUM_SETTINGS; i++) {
        inputOverlaySetting* s = &inputOverlay_aSetting[i];
        sithCvar_RegisterInt(s->pCvar, s->val, &s->val, CVARFLAG_GLOBAL);
    }
    sithCvar_RegisterBool("enh_showFps", 0, &inputOverlay_bShowFps, CVARFLAG_GLOBAL);
    sithCvar_RegisterInt("enh_saberTarget", INPUTWHEEL_SABER_BOTH, &inputOverlay_saberTarget, CVARFLAG_GLOBAL);
    sithCvar_RegisterInt("enh_selfieShots", 3, &inputOverlay_selfieShots, CVARFLAG_GLOBAL);
    sithCvar_RegisterInt("enh_photoKinds", IOSCAPTURE_CLEAN, &inputOverlay_photoKinds, CVARFLAG_GLOBAL);
    inputOverlay_bCvars = 1;
}

static inputOverlaySetting* inputOverlay_FindSetting(const char* pKey)
{
    for (int i = 0; i < INPUTOVERLAY_NUM_SETTINGS; i++) {
        if (!strcmp(inputOverlay_aSetting[i].pKey, pKey)) return &inputOverlay_aSetting[i];
    }
    return NULL;
}

int inputUi_LoadInt(const char* pKey, int valDefault)
{
    inputOverlaySetting* s = inputOverlay_FindSetting(pKey);
    if (!s) return valDefault;
    s->seen = s->val;
    return (int)s->val;
}

void inputUi_SaveInt(const char* pKey, int val)
{
    inputOverlaySetting* s = inputOverlay_FindSetting(pKey);
    if (!s) return;
    if (inputOverlay_bCvars) sithCvar_SetInt(s->pCvar, val);
    s->val = s->seen = val;
}

// Once a frame: a setting changed from the console goes to the pad; the
// saber wheel's blade choice both ways; the photo choices
static void inputOverlay_SyncSettings(void)
{
    int bChanged = 0;
    for (int i = 0; i < INPUTOVERLAY_NUM_SETTINGS; i++) {
        if (inputOverlay_aSetting[i].val != inputOverlay_aSetting[i].seen) bChanged = 1;
    }
    if (bChanged) iosPad_ReloadSettings(); // (reads them again: inputUi_LoadInt)

    if (inputOverlay_saberTarget != inputOverlay_saberTargetSeen) {
        inputWheel_SetSaberTarget(inputOverlay_saberTarget);
        inputOverlay_saberTargetSeen = inputOverlay_saberTarget = inputWheel_GetSaberTarget();
    }
    else if (inputWheel_GetSaberTarget() != inputOverlay_saberTarget) {
        inputOverlay_saberTargetSeen = inputOverlay_saberTarget = inputWheel_GetSaberTarget();
        if (inputOverlay_bCvars) sithCvar_SetInt("enh_saberTarget", inputOverlay_saberTarget);
    }
    inputPhoto_SetChoices(inputOverlay_photoKinds, inputOverlay_selfieShots);
}

// ------------------------------------------------------------ state

enum { INPUTOVERLAY_BY_NONE = 0, INPUTOVERLAY_BY_PAD, INPUTOVERLAY_BY_KBM };

static struct {
    int bPadHud, bKbmHud;      // the touch controls hidden for a pad, the keyboard
    int wheelBy;               // who opened the wheel open now (INPUTOVERLAY_BY_*)
    int bPanel;                // the controller options are up
    int ringKind;              // IOSPAD_RING_*
    float ringProgress;
    char prompt[256];          // (empty: none)
    int bGameplay;
    // the FPS readout
    double fpsT0;
    unsigned int fpsFrames0;
    char fpsText[16];
    // the screen, as last drawn (points), and the window's pixels a point
    float W, H, inL, inT, inR, inB;
    float pxPerPt, winToPx;
} inputOverlay_s;

static touchUiDrawList inputOverlay_draw;

static double inputOverlay_Now(void)
{
    return (double)SDL_GetTicksNS() * 1e-9;
}

int inputOverlay_IsTouchHudHidden(void)
{
    return inputOverlay_s.bPadHud || inputOverlay_s.bKbmHud;
}

void inputOverlay_NoteTouch(void)
{
    inputOverlay_s.bPadHud = inputOverlay_s.bKbmHud = 0;
    iosPad_NoteTouch();
    iosKbMouse_NoteTouch();
}

void inputUi_SetPadHud(int bHidden) { inputOverlay_s.bPadHud = bHidden != 0; }
void inputUi_SetKbmHud(int bHidden) { inputOverlay_s.bKbmHud = bHidden != 0; }

// ------------------------------------------------------------ the wheels

static int inputOverlay_OutsideWheelIsOpen(void)
{
    if (!inputWheel_IsOpen()) inputOverlay_s.wheelBy = INPUTOVERLAY_BY_NONE;
    return inputOverlay_s.wheelBy != INPUTOVERLAY_BY_NONE;
}

static int inputOverlay_OutsideWheelOpen(int kind, int bTapMode, int by)
{
    if (kind < 0 || kind >= INPUTWHEEL_NUM_KINDS) return 0;
    if (inputWheel_IsOpen() || inputPhoto_IsUp()) return 0;
    if (kind == INPUTWHEEL_SABER && !iosGame_IsSaberButtonShown()) return 0;
    if (jkHud_bChatOpen) iosGame_ToggleChat(); // (its keyboard, where there is one, would cover the wheel)
    if (!inputWheel_Open(kind, bTapMode)) return 0;
    inputOverlay_s.wheelBy = by;
    // a tick in the hand for each slice; on a pad, a short rumble too
    inputWheel_SetSliceHook(by == INPUTOVERLAY_BY_PAD ? iosPad_OnWheelSlice : NULL);
    inputWheel_SetHintFn(by == INPUTOVERLAY_BY_KBM ? iosKbMouse_WheelHint : iosPad_WheelHint);
    return 1;
}

int inputUi_PadWheelOpen(int kind, int bTapMode) { return inputOverlay_OutsideWheelOpen(kind, bTapMode, INPUTOVERLAY_BY_PAD); }
int inputUi_KbmWheelOpen(int kind, int bTapMode) { return inputOverlay_OutsideWheelOpen(kind, bTapMode, INPUTOVERLAY_BY_KBM); }
int inputUi_PadWheelIsOpen(void) { return inputOverlay_OutsideWheelIsOpen() && inputOverlay_s.wheelBy == INPUTOVERLAY_BY_PAD; }
int inputUi_KbmWheelIsOpen(void) { return inputOverlay_OutsideWheelIsOpen() && inputOverlay_s.wheelBy == INPUTOVERLAY_BY_KBM; }
int inputUi_IsWheelOpen(void) { return inputWheel_IsOpen(); }

void inputUi_WheelSetTapMode(void)
{
    if (inputOverlay_OutsideWheelIsOpen()) inputWheel_SetTapMode(1);
}

void inputUi_WheelAim(int bAim, float deg)
{
    if (inputOverlay_OutsideWheelIsOpen()) inputWheel_Aim(bAim, deg);
}

void inputUi_WheelStep(int dir)
{
    if (inputOverlay_OutsideWheelIsOpen()) inputWheel_Step(dir);
}

int inputUi_WheelHot(int* pbPickable)
{
    const int hot = inputOverlay_OutsideWheelIsOpen() ? inputWheel_GetHot() : -1;
    const int bHot = hot >= 0;
    if (pbPickable) *pbPickable = bHot && inputWheel_IsPickable(hot);
    return bHot;
}

int inputUi_WheelClose(int bSelect)
{
    if (!inputOverlay_OutsideWheelIsOpen()) return 0;
    const int slice = (bSelect && inputUi_WheelHot(NULL)) ? inputWheel_GetHot() : -1;
    inputWheel_Close(slice); // (the saber wheel: a colour set and still open; else closed)
    return inputOverlay_OutsideWheelIsOpen();
}

void inputUi_SaberChip(int chip)
{
    if (!inputOverlay_OutsideWheelIsOpen() || inputWheel_GetKind() != INPUTWHEEL_SABER) return;
    if (chip == IOSPAD_CHIP_STYLE) {
        const int bDual = iosSaber_GetStyle() == IOSSABER_STYLE_DUAL;
        inputWheel_SaberChip(bDual ? INPUTWHEEL_CHIP_SINGLE : INPUTWHEEL_CHIP_DUAL);
    }
    else if (chip == IOSPAD_CHIP_TARGET) {
        // LEFT, BOTH, RIGHT, as they sit
        const int t = inputWheel_GetSaberTarget();
        inputWheel_SaberChip(t == INPUTWHEEL_SABER_LEFT ? INPUTWHEEL_CHIP_BOTH : t == INPUTWHEEL_SABER_BOTH ? INPUTWHEEL_CHIP_RIGHT : INPUTWHEEL_CHIP_LEFT);
    }
    else if (chip == IOSPAD_CHIP_EQUIP) {
        inputWheel_SaberChip(INPUTWHEEL_CHIP_EQUIP);
    }
    if (inputWheel_IsOpen()) inputWheel_Point(-1, 0, 0.0f);
    inputOverlay_OutsideWheelIsOpen();
}

// ------------------------------------------------------------ photo modes, the pointer, FPS

int inputUi_IsPhotoModeUp(void)
{
    return inputPhoto_IsUp();
}

int inputUi_WantsPointer(void)
{
    // (no buttons on the screen to click in a photo mode here: the mouse
    // keeps turning Action Freeze's camera. The controller options let it
    // go: iosKbMouse_IsPointerFree.)
    return 0;
}

int inputUi_Camera(int action)
{
    if (inputWheel_IsOpen() && action != INPUTUI_CAM_FREEZE_END) return 0;
    return inputPhoto_Camera(action);
}

int inputUi_IsFpsShown(void)
{
    return inputOverlay_bShowFps != 0;
}

void inputUi_SetFpsShown(int bShow)
{
    inputOverlay_bShowFps = bShow != 0;
    if (inputOverlay_bCvars) sithCvar_SetBool("enh_showFps", inputOverlay_bShowFps);
    inputOverlay_s.fpsT0 = 0.0;
    inputOverlay_s.fpsText[0] = 0;
}

// ------------------------------------------------------------ the options, the ring, the prompts

void inputUi_PanelShow(int bShow)
{
    if (inputOverlay_s.bPanel == (bShow != 0)) return;
    inputOverlay_s.bPanel = bShow != 0;
    // (the mouse's buttons start over: none held into or out of the options)
    Window_bMouseLeft = Window_bMouseRight = 0;
}
void inputUi_PanelRefresh(void) {} // (drawn as they are, every frame)

void inputUi_HoldRing(int kind, float progress)
{
    inputOverlay_s.ringKind = kind;
    inputOverlay_s.ringProgress = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
}

void inputUi_SetPrompt(const char* pText)
{
    snprintf(inputOverlay_s.prompt, sizeof(inputOverlay_s.prompt), "%s", pText ? pText : "");
}

void inputUi_KbmViewTick(void) {}

// ------------------------------------------------------------ the screen

// Text at a size, made smaller (to minScale) to fit maxW; drawn with its
// left edge, middle or right edge (align -1, 0, 1) at x
static void inputOverlay_Text(const char* pText, float x, float y, float size, float maxW, float minScale, int align, touchUiColor c)
{
    char t[256];
    inputWheel_Ascii(pText, t, sizeof(t));
    float w = 0.0f;
    touchUiDraw_MeasureText(t, size, 1.0f, &w, NULL);
    if (maxW > 0.0f && w > maxW) {
        const float k = INPUTOVERLAY_MAX(minScale, maxW / w);
        size *= k;
        w *= k;
    }
    const float cx = align < 0 ? x + w * 0.5f : align > 0 ? x - w * 0.5f : x;
    touchUiDraw_Text(&inputOverlay_draw, t, cx, y, size, 1.0f, c);
}

static touchUiColor inputOverlay_Gold(float a)
{
    return touchUi_Rgba(1.0f, 0.84f, 0.47f, a);
}

static void inputOverlay_Bounds(float* pLeft, float* pTop, float* pRight, float* pBottom)
{
    *pLeft = INPUTOVERLAY_MAX(inputOverlay_s.inL, 8.0f);
    *pRight = inputOverlay_s.W - INPUTOVERLAY_MAX(inputOverlay_s.inR, 8.0f);
    *pTop = INPUTOVERLAY_MAX(inputOverlay_s.inT, 8.0f);
    *pBottom = inputOverlay_s.H - INPUTOVERLAY_MAX(inputOverlay_s.inB, 8.0f);
}

// The options' card (points) and its rows' height
static void inputOverlay_PanelRect(float* pX, float* pY, float* pW, float* pH, float* pRowH)
{
    float left, top, right, bottom;
    inputOverlay_Bounds(&left, &top, &right, &bottom);
    const float w = INPUTOVERLAY_MIN(INPUTOVERLAY_PANEL_W, right - left);
    const float chrome = INPUTOVERLAY_TITLE_H + INPUTOVERLAY_HINT_H + 2.0f * INPUTOVERLAY_PAD;
    const float rowH = INPUTOVERLAY_MIN(INPUTOVERLAY_ROW_H, floorf((bottom - top - chrome) / IOSPAD_NUM_ROWS));
    const float h = chrome + rowH * IOSPAD_NUM_ROWS;
    *pX = roundf((left + right - w) * 0.5f);
    *pY = roundf((top + bottom - h) * 0.5f);
    *pW = w;
    *pH = h;
    *pRowH = rowH;
}

static void inputOverlay_DrawPanel(void)
{
    touchUiDrawList* l = &inputOverlay_draw;
    float x, y, w, h, rowH;
    inputOverlay_PanelRect(&x, &y, &w, &h, &rowH);
    touchUiDraw_RRect(l, 0.0f, 0.0f, inputOverlay_s.W, inputOverlay_s.H, 0.0f, touchUi_Black(0.45f), 0.0f, touchUi_Clear());
    touchUiDraw_RRect(l, x, y, w, h, 16.0f, touchUi_Rgba(0.06f, 0.06f, 0.06f, 0.92f), 1.5f, touchUi_White(0.35f));
    inputOverlay_Text(iosPad_PanelTitle(), x + w * 0.5f, y + INPUTOVERLAY_PAD + (INPUTOVERLAY_TITLE_H - 6.0f) * 0.5f, 15.0f,
                      w - 2.0f * INPUTOVERLAY_PAD, 0.6f, 0, inputOverlay_Gold(1.0f));
    const int focus = iosPad_PanelFocus();
    for (int i = 0; i < IOSPAD_NUM_ROWS; i++) {
        char label[64], value[64];
        iosPad_PanelRow(i, label, sizeof(label), value, sizeof(value));
        const float ry = y + INPUTOVERLAY_PAD + INPUTOVERLAY_TITLE_H + rowH * i;
        const int bFocus = i == focus;
        if (bFocus)
            touchUiDraw_RRect(l, x + INPUTOVERLAY_PAD, ry + 1.0f, w - 2.0f * INPUTOVERLAY_PAD, rowH - 2.0f, 8.0f, touchUi_White(0.12f),
                              2.0f, inputOverlay_Gold(0.95f));
        const float my = ry + rowH * 0.5f;
        inputOverlay_Text(label, x + INPUTOVERLAY_PAD + 10.0f, my, 14.0f, w * 0.45f - INPUTOVERLAY_PAD - 10.0f, 0.6f, -1, touchUi_White(1.0f));
        inputOverlay_Text(value, x + w - INPUTOVERLAY_PAD - 10.0f, my, 14.0f, w * 0.55f - INPUTOVERLAY_PAD - 10.0f, 0.6f, 1,
                          bFocus ? inputOverlay_Gold(1.0f) : touchUi_Rgba(0.85f, 0.85f, 0.85f, 1.0f));
    }
    inputOverlay_Text(iosPad_PanelHint(), x + w * 0.5f, y + h - INPUTOVERLAY_PAD - INPUTOVERLAY_HINT_H * 0.5f, 12.0f,
                      w - 2.0f * INPUTOVERLAY_PAD, 0.6f, 0, touchUi_Rgba(0.75f, 0.75f, 0.75f, 1.0f));
}

// The row of the options under a point: a row, -1 off the card, -2 on the
// card but no row (the title, the hint)
static int inputOverlay_PanelRowAt(float px, float py)
{
    float x, y, w, h, rowH;
    inputOverlay_PanelRect(&x, &y, &w, &h, &rowH);
    if (px < x || px > x + w || py < y || py > y + h) return -1;
    const float ry = py - y - INPUTOVERLAY_PAD - INPUTOVERLAY_TITLE_H;
    if (ry < 0.0f || rowH <= 0.0f) return -2;
    const int row = (int)(ry / rowH);
    return row < IOSPAD_NUM_ROWS ? row : -2;
}

int inputOverlay_PanelFinger(float fx, float fy)
{
    if (!iosPad_PanelIsOpen()) return 0;
    if (inputOverlay_s.W > 0.0f && inputOverlay_s.H > 0.0f) {
        const int row = inputOverlay_PanelRowAt(fx * inputOverlay_s.W, fy * inputOverlay_s.H);
        if (row != -2) iosPad_PanelTap(row);
    }
    return 1;
}

static void inputOverlay_DrawRing(void)
{
    touchUiDrawList* l = &inputOverlay_draw;
    float left, top, right, bottom;
    inputOverlay_Bounds(&left, &top, &right, &bottom);
    const float r = INPUTOVERLAY_RING_R;
    const float x = inputOverlay_s.W * 0.5f, y = top + INPUTOVERLAY_RING_TOP + r;
    touchUiDraw_Circle(l, x, y, r + 1.5f, touchUi_Black(0.5f), 3.0f, touchUi_White(0.35f));
    if (inputOverlay_s.ringProgress > 0.0f)
        touchUiDraw_Arc(l, x, y, r, 4.0f, 0.0f, 2.0f * (float)M_PI * inputOverlay_s.ringProgress, 1, inputOverlay_Gold(1.0f));
    touchUiDraw_Text(l, inputOverlay_s.ringKind == IOSPAD_RING_LOAD ? "QUICK\nLOAD" : "QUICK\nSAVE", x, y, 10.0f, 1.0f, touchUi_White(1.0f));
}

static void inputOverlay_DrawPrompt(void)
{
    float left, top, right, bottom;
    inputOverlay_Bounds(&left, &top, &right, &bottom);
    char t[256];
    inputWheel_Ascii(inputOverlay_s.prompt, t, sizeof(t));
    float tw = 0.0f, size = 13.0f;
    touchUiDraw_MeasureText(t, size, 1.0f, &tw, NULL);
    const float maxW = right - left - 28.0f;
    if (tw > maxW) {
        size *= INPUTOVERLAY_MAX(0.6f, maxW / tw);
        tw = INPUTOVERLAY_MIN(tw * size / 13.0f, maxW);
    }
    const float w = INPUTOVERLAY_MIN(right - left, tw + 28.0f);
    const float y = bottom - INPUTOVERLAY_PROMPT_GAP - INPUTOVERLAY_PROMPT_H;
    touchUiDraw_RRect(&inputOverlay_draw, roundf((left + right - w) * 0.5f), y, w, INPUTOVERLAY_PROMPT_H, INPUTOVERLAY_PROMPT_H * 0.5f,
                      touchUi_Black(0.55f), 0.0f, touchUi_Clear());
    touchUiDraw_Text(&inputOverlay_draw, t, (left + right) * 0.5f, y + INPUTOVERLAY_PROMPT_H * 0.5f, size, 1.0f, touchUi_White(1.0f));
}

static void inputOverlay_DrawFps(void)
{
    if (!inputOverlay_s.fpsText[0]) return;
    float left, top, right, bottom;
    inputOverlay_Bounds(&left, &top, &right, &bottom);
    const float w = 54.0f, h = 20.0f;
    touchUiDraw_RRect(&inputOverlay_draw, right - w, top, w, h, 6.0f, touchUi_Black(0.45f), 0.0f, touchUi_Clear());
    touchUiDraw_Text(&inputOverlay_draw, inputOverlay_s.fpsText, right - w * 0.5f, top + h * 0.5f, 12.0f, 1.0f, touchUi_White(0.9f));
}

static int inputOverlay_bFpsElsewhere = 0;
void inputOverlay_SetFpsElsewhere(int bElsewhere) { inputOverlay_bFpsElsewhere = bElsewhere != 0; }

static void inputOverlay_TickFps(double now)
{
    if (!inputOverlay_bShowFps || !inputOverlay_s.bGameplay || inputOverlay_bFpsElsewhere) {
        inputOverlay_s.fpsT0 = 0.0;
        inputOverlay_s.fpsText[0] = 0;
        return;
    }
    const unsigned int frames = iosGame_GetFrameCount();
    if (inputOverlay_s.fpsT0 == 0.0 || frames < inputOverlay_s.fpsFrames0) {
        inputOverlay_s.fpsT0 = now;
        inputOverlay_s.fpsFrames0 = frames;
        return;
    }
    const double dt = now - inputOverlay_s.fpsT0;
    if (dt < INPUTOVERLAY_FPS_PERIOD) return;
    snprintf(inputOverlay_s.fpsText, sizeof(inputOverlay_s.fpsText), "%.0f FPS", (double)(frames - inputOverlay_s.fpsFrames0) / dt);
    inputOverlay_s.fpsT0 = now;
    inputOverlay_s.fpsFrames0 = frames;
}

// The window in points (y down), and its safe area
static int inputOverlay_Measure(int* pPxW, int* pPxH)
{
    int w = 0, h = 0, ww = 0, wh = 0;
    if (!displayWindow) return 0;
    SDL_GetWindowSizeInPixels(displayWindow, &w, &h);
    SDL_GetWindowSize(displayWindow, &ww, &wh);
    if (w <= 0 || h <= 0) return 0;
    inputOverlay_s.winToPx = ww > 0 ? (float)w / (float)ww : 1.0f;
    inputOverlay_s.pxPerPt = INPUTOVERLAY_MAX(1.0f, (float)h / INPUTOVERLAY_PT_H);
    inputOverlay_s.W = (float)w / inputOverlay_s.pxPerPt;
    inputOverlay_s.H = (float)h / inputOverlay_s.pxPerPt;
    inputOverlay_s.inL = inputOverlay_s.inT = inputOverlay_s.inR = inputOverlay_s.inB = 0.0f;
    SDL_Rect safe;
    if (ww > 0 && wh > 0 && SDL_GetWindowSafeArea(displayWindow, &safe)) {
        const float k = inputOverlay_s.winToPx / inputOverlay_s.pxPerPt;
        inputOverlay_s.inL = INPUTOVERLAY_MAX(0.0f, (float)safe.x * k);
        inputOverlay_s.inT = INPUTOVERLAY_MAX(0.0f, (float)safe.y * k);
        inputOverlay_s.inR = INPUTOVERLAY_MAX(0.0f, (float)(ww - (safe.x + safe.w)) * k);
        inputOverlay_s.inB = INPUTOVERLAY_MAX(0.0f, (float)(wh - (safe.y + safe.h)) * k);
    }
    *pPxW = w;
    *pPxH = h;
    return 1;
}

void inputOverlay_Render(void)
{
    const double now = inputOverlay_Now();
    const float flash = inputPhoto_FlashAlpha(now);
    // (a wheel a finger opened: the touch overlay draws it, with its buttons)
    const int bWheel = inputOverlay_OutsideWheelIsOpen();
    const int bRing = inputOverlay_s.ringKind != IOSPAD_RING_NONE;
    const int bPrompt = inputOverlay_s.prompt[0] != 0;
    const int bFps = inputOverlay_s.fpsText[0] != 0;
    if (!bWheel && !bRing && !bPrompt && !inputOverlay_s.bPanel && !bFps && flash <= 0.0f) return;
    int pxW, pxH;
    if (!inputOverlay_Measure(&pxW, &pxH)) return;

    touchUiDrawList* l = &inputOverlay_draw;
    touchUiDraw_Begin(l);
    if (flash > 0.0f) touchUiDraw_RRect(l, 0.0f, 0.0f, inputOverlay_s.W, inputOverlay_s.H, 0.0f, touchUi_White(flash), 0.0f, touchUi_Clear());
    if (bFps) inputOverlay_DrawFps();
    if (bWheel) {
        const inputWheelScreen s = { inputOverlay_s.W, inputOverlay_s.H, inputOverlay_s.inL, inputOverlay_s.inT, inputOverlay_s.inR, inputOverlay_s.inB };
        inputWheel_Draw(l, &s);
    }
    if (bRing) inputOverlay_DrawRing();
    if (bPrompt) inputOverlay_DrawPrompt();
    if (inputOverlay_s.bPanel) inputOverlay_DrawPanel();
    touchUiRender_Draw(l, (unsigned int)std3D_windowFbo, pxW, pxH, inputOverlay_s.pxPerPt);
}

void inputOverlay_ContextLost(void)
{
    touchUiRender_ContextLost();
}

// ------------------------------------------------------------ the frame

int inputOverlay_HandleEvent(void* pSdlEvent)
{
    SDL_Event* e = (SDL_Event*)pSdlEvent;
    switch (e->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // the controller options: a click on a row changes it (as A does);
        // off the card, they close. Every click is theirs while they are up.
        // (a button let go always reaches the window, so the game never keeps one held)
        if (!iosPad_PanelIsOpen() || e->button.which == SDL_TOUCH_MOUSEID || e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return 0;
        if (e->button.button == SDL_BUTTON_LEFT && inputOverlay_s.pxPerPt > 0.0f) {
            const float k = inputOverlay_s.winToPx / inputOverlay_s.pxPerPt;
            const int row = inputOverlay_PanelRowAt(e->button.x * k, e->button.y * k);
            if (row != -2) iosPad_PanelTap(row);
        }
        return 1;
    case SDL_EVENT_MOUSE_MOTION:
        // Action Freeze's camera follows the mouse (the view doesn't turn:
        // iosKbMouse keeps the movement from it in a photo mode)
        if (inputPhoto_IsFreezing() && e->motion.which != SDL_TOUCH_MOUSEID) inputPhoto_FreezeMouse(e->motion.xrel, e->motion.yrel);
        return 0;
    case SDL_EVENT_MOUSE_WHEEL:
        if (inputPhoto_IsFreezing() && e->wheel.which != SDL_TOUCH_MOUSEID) {
            float y = e->wheel.y;
            if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
            inputPhoto_FreezeWheel(y);
        }
        return 0;
    default:
        return 0;
    }
}

void inputOverlay_Update(void)
{
    const double now = inputOverlay_Now();
    inputOverlay_SyncSettings();
    inputOverlay_s.bGameplay = stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive();
    inputPhoto_Tick(inputOverlay_s.bGameplay, now);
    if (!inputOverlay_s.bGameplay) {
        // (the pad and the keys close theirs as the game is left; this is
        // only in case: never a wheel over a menu, the game held)
        if (inputOverlay_OutsideWheelIsOpen()) {
            inputWheel_Close(-1);
            inputOverlay_s.wheelBy = INPUTOVERLAY_BY_NONE;
        }
        inputOverlay_s.prompt[0] = 0;
        inputOverlay_s.ringKind = IOSPAD_RING_NONE;
    }
    inputOverlay_TickFps(now);
}

#endif // ENH_INPUT

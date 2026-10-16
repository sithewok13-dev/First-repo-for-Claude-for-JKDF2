// Added: entire file. See touchUiSdl.h.

#include "Platform/Touch/touchUiSdl.h"

#ifdef TOUCHUI_ENABLED

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Platform/Touch/touchUi.h"
#include "Platform/Touch/touchUiRender_gl.h"
#include "stdPlatform.h"

extern SDL_Window* displayWindow;
extern int Window_lastXRel;
extern int Window_lastYRel;
extern int Window_bMouseLeft;
extern int Window_bMouseRight;
extern int stdControl_bControlsActive;
extern int stdControl_bControllerEscapeKey;
extern int jkCutscene_isRendering;
extern int jkHud_bChatOpen;
extern int jkGuiRend_IsMenuActive(void);
extern int std3D_windowFbo;
extern const touchUiGame touchUiGame_engine; // touchUiGame.c

// Sizes: points are density-independent pixels (Android's dp: 160 a
// physical inch, about an iPhone's point), times a little more on a tablet
// -- from 1.0 with a short side of TOUCHUISDL_TABLET_FROM points to
// TOUCHUISDL_TABLET_SCALE at TOUCHUISDL_TABLET_TO and over -- so the buttons
// keep their size under the thumbs and a big screen isn't all empty middle.
#define TOUCHUISDL_TABLET_FROM 480.0f
#define TOUCHUISDL_TABLET_TO 720.0f
#define TOUCHUISDL_TABLET_SCALE 1.2f
// The accelerometer's reading is smoothed to tell which way is up (seconds)
#define TOUCHUISDL_GRAVITY_SMOOTH 0.1
// A game controller's stick counts as in use past this far
#define TOUCHUISDL_PAD_AXIS 16000

static struct {
    int bInit;
    double known;
    int bShown;
    int bEscWas;
    int bPhotoModeWas; // (a selfie, Action Freeze or the cinematic burst up at the last frame)
    // the window, as last measured
    int pxW, pxH;
    float pxPerPt;   // pixels per point
    float winToPx;   // pixels per window unit
    // the gyro and the accelerometer
    int bHaveGyro;
    SDL_SensorID gyroId, accelId;
    SDL_Sensor* pGyro;
    SDL_Sensor* pAccel;
    int rotDeg;      // the screen turned this far counter-clockwise from the device's natural way up
    int bGravity;
    double gx, gy, gz;
    double lastAccelT;
} ts;

static double touchUiSdl_Now(void)
{
    return (double)SDL_GetTicksNS() / 1.0e9;
}

// ------------------------------------------------------------ sizes

static void touchUiSdl_Measure(float* pW, float* pH, float aInset[4])
{
    int w = 0, h = 0, ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(displayWindow, &w, &h);
    SDL_GetWindowSize(displayWindow, &ww, &wh);
    ts.pxW = w;
    ts.pxH = h;
    ts.winToPx = (ww > 0) ? (float)w / (float)ww : 1.0f;
    float scale = SDL_GetWindowDisplayScale(displayWindow);
    if (!(scale > 0.1f)) scale = 1.0f;
    aInset[0] = aInset[1] = aInset[2] = aInset[3] = 0.0f;
    SDL_Rect safe;
    if (ww > 0 && wh > 0 && SDL_GetWindowSafeArea(displayWindow, &safe)) {
        aInset[0] = (float)safe.x;
        aInset[1] = (float)safe.y;
        aInset[2] = (float)(ww - (safe.x + safe.w));
        aInset[3] = (float)(wh - (safe.y + safe.h));
        for (int i = 0; i < 4; i++) aInset[i] = aInset[i] > 0.0f ? aInset[i] * ts.winToPx : 0.0f; // (pixels)
    }
#ifndef TARGET_ANDROID
    // (the test rig: a phone's or a tablet's density, and its safe area, in points)
    const char* pScale = getenv("OPENJKDF2_TOUCH_SCALE");
    if (pScale && atof(pScale) > 0.1) scale = (float)atof(pScale);
    const char* pIns = getenv("OPENJKDF2_TOUCH_INSETS");
    float l, t, r, b;
    if (pIns && sscanf(pIns, "%f,%f,%f,%f", &l, &t, &r, &b) == 4) {
        aInset[0] = l * scale; aInset[1] = t * scale; aInset[2] = r * scale; aInset[3] = b * scale;
    }
#endif
    float shortPt = (float)(w < h ? w : h) / scale;
    float k = (shortPt - TOUCHUISDL_TABLET_FROM) / (TOUCHUISDL_TABLET_TO - TOUCHUISDL_TABLET_FROM);
    k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    ts.pxPerPt = scale * (1.0f + (TOUCHUISDL_TABLET_SCALE - 1.0f) * k);
    *pW = (float)w / ts.pxPerPt;
    *pH = (float)h / ts.pxPerPt;
    for (int i = 0; i < 4; i++) aInset[i] /= ts.pxPerPt;
}

float touchUiSdl_WinToPt(void)
{
    return ts.pxPerPt > 0.0f ? ts.winToPx / ts.pxPerPt : 1.0f;
}

// ------------------------------------------------------------ the gyro

// How far the screen is turned from the device's natural way up (degrees,
// counter-clockwise, as the device turned), and which landscape side that is
static int touchUiSdl_ScreenTurn(int* pSide)
{
    static const int aDeg[] = { 0, 90, 270, 0, 180 }; // SDL_ORIENTATION_UNKNOWN, LANDSCAPE, LANDSCAPE_FLIPPED, PORTRAIT, PORTRAIT_FLIPPED
    SDL_DisplayID d = SDL_GetDisplayForWindow(displayWindow);
    SDL_DisplayOrientation cur = SDL_GetCurrentDisplayOrientation(d);
    SDL_DisplayOrientation nat = SDL_GetNaturalDisplayOrientation(d);
    *pSide = (cur == SDL_ORIENTATION_LANDSCAPE) ? -1 : ((cur == SDL_ORIENTATION_LANDSCAPE_FLIPPED) ? 1 : 0);
    if ((int)cur < 0 || (int)cur > 4 || (int)nat < 0 || (int)nat > 4) return 0;
    if (nat == SDL_ORIENTATION_UNKNOWN) nat = SDL_ORIENTATION_PORTRAIT;
    return ((aDeg[cur] - aDeg[nat]) % 360 + 360) % 360;
}

// A vector in the device's own axes to the screen's (x right, y up, z out
// of the screen), with the screen turned rotDeg from the device's natural way up
static void touchUiSdl_ToScreen(const float* v, double* pX, double* pY, double* pZ)
{
    double c = 1.0, s = 0.0;
    switch (ts.rotDeg) {
        case 90: c = 0.0; s = 1.0; break;
        case 180: c = -1.0; s = 0.0; break;
        case 270: c = 0.0; s = -1.0; break;
        default: break;
    }
    *pX = c * v[0] - s * v[1];
    *pY = s * v[0] + c * v[1];
    *pZ = v[2];
}

static void touchUiSdl_FindSensors(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) return;
    int n = 0;
    SDL_SensorID* aIds = SDL_GetSensors(&n);
    for (int i = 0; aIds && i < n; i++) {
        SDL_SensorType type = SDL_GetSensorTypeForID(aIds[i]);
        if (type == SDL_SENSOR_GYRO && !ts.gyroId) ts.gyroId = aIds[i];
        if (type == SDL_SENSOR_ACCEL && !ts.accelId) ts.accelId = aIds[i];
    }
    SDL_free(aIds);
    ts.bHaveGyro = ts.gyroId && ts.accelId;
    stdPlatform_Printf("OpenJKDF2: touch overlay: gyro %s\n", ts.bHaveGyro ? "found" : "not found");
}

// The sensors run only while gyro aiming could use them
static void touchUiSdl_RunSensors(int bRun)
{
    if (bRun && !ts.pGyro && ts.bHaveGyro) {
        ts.pGyro = SDL_OpenSensor(ts.gyroId);
        ts.pAccel = SDL_OpenSensor(ts.accelId);
        ts.bGravity = 0;
    }
    else if (!bRun && ts.pGyro) {
        SDL_CloseSensor(ts.pGyro);
        if (ts.pAccel) SDL_CloseSensor(ts.pAccel);
        ts.pGyro = ts.pAccel = NULL;
    }
}

static void touchUiSdl_SensorEvent(const SDL_SensorEvent* e)
{
    double t = e->sensor_timestamp ? (double)e->sensor_timestamp / 1.0e9 : (double)e->timestamp / 1.0e9;
    double x, y, z;
    touchUiSdl_ToScreen(e->data, &x, &y, &z);
    if (e->which == ts.accelId) {
        // at rest it reads straight up: smoothed, it is which way up is
        double k = ts.bGravity && t > ts.lastAccelT ? (t - ts.lastAccelT) / TOUCHUISDL_GRAVITY_SMOOTH : 1.0;
        if (k > 1.0) k = 1.0;
        ts.gx += (x - ts.gx) * k;
        ts.gy += (y - ts.gy) * k;
        ts.gz += (z - ts.gz) * k;
        ts.bGravity = 1;
        ts.lastAccelT = t;
    }
    else if (e->which == ts.gyroId && ts.bGravity) {
        double n = sqrt(ts.gx * ts.gx + ts.gy * ts.gy + ts.gz * ts.gz);
        if (n < 1.0) return;
        touchUi_GyroSample(x, y, z, ts.gx / n, ts.gy / n, ts.gz / n, t);
    }
}

// ------------------------------------------------------------ the frame

static void touchUiSdl_Init(void)
{
    ts.bInit = 1;
    char path[1024] = "";
    char* pPref = SDL_GetPrefPath("OpenJKDF2", "OpenJKDF2");
    if (pPref) {
        snprintf(path, sizeof(path), "%stouch_controls.txt", pPref);
        SDL_free(pPref);
    }
    touchUi_Init(&touchUiGame_engine, path[0] ? path : NULL);
    touchUiSdl_FindSensors();
    touchUi_SetGyroAvailable(ts.bHaveGyro);
}

void touchUiSdl_BeforeEvents(void)
{
    if (!ts.bInit) touchUiSdl_Init();
    ts.known = touchUiSdl_Now();
}

static int touchUiSdl_IsOwnKey(SDL_Scancode sc)
{
    // keys a keyboard sends that aren't someone playing on it
    return sc == SDL_SCANCODE_AC_BACK || sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_VOLUMEDOWN
           || sc == SDL_SCANCODE_MUTE || sc == SDL_SCANCODE_MEDIA_PLAY || sc == SDL_SCANCODE_MEDIA_PAUSE
           || sc == SDL_SCANCODE_MEDIA_PLAY_PAUSE || sc == SDL_SCANCODE_POWER || sc == SDL_SCANCODE_UNKNOWN;
}

int touchUiSdl_HandleEvent(const union SDL_Event* pEvent)
{
    const SDL_Event* e = (const SDL_Event*)pEvent;
    if (!ts.bInit) touchUiSdl_Init();
    // SDL makes every finger a mouse too (its touch mouse, left on all the
    // time, so a finger down in a menu and up in a level always ends): while
    // the controls are up those clicks and moves aren't the game's -- the
    // fingers are the controls'. In menus and cutscenes they are clicks.
    if (ts.bShown) {
        if ((e->type == SDL_EVENT_MOUSE_MOTION && e->motion.which == SDL_TOUCH_MOUSEID)
            || ((e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) && e->button.which == SDL_TOUCH_MOUSEID)
            || (e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.which == SDL_TOUCH_MOUSEID))
            return 1;
    }
    switch (e->type) {
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (e->tfinger.touchID == SDL_MOUSE_TOUCHID || ts.pxPerPt <= 0.0f) break;
            const int phase = e->type == SDL_EVENT_FINGER_DOWN ? TOUCHUI_PHASE_DOWN
                            : e->type == SDL_EVENT_FINGER_MOTION ? TOUCHUI_PHASE_MOVE
                            : e->type == SDL_EVENT_FINGER_UP ? TOUCHUI_PHASE_UP : TOUCHUI_PHASE_CANCEL;
            const float x = e->tfinger.x * (float)ts.pxW / ts.pxPerPt;
            const float y = e->tfinger.y * (float)ts.pxH / ts.pxPerPt;
            const long long id = (long long)e->tfinger.fingerID ^ ((long long)e->tfinger.touchID << 24);
            const double t = e->tfinger.timestamp ? (double)e->tfinger.timestamp / 1.0e9 : touchUiSdl_Now();
            touchUi_Finger(id, phase, x, y, t);
            break;
        }
        case SDL_EVENT_SENSOR_UPDATE:
            touchUiSdl_SensorEvent(&e->sensor);
            break;
        // Another input in use: the touch controls hide until the next touch
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            touchUi_SetOtherInput(1);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (e->gaxis.value > TOUCHUISDL_PAD_AXIS || e->gaxis.value < -TOUCHUISDL_PAD_AXIS) touchUi_SetOtherInput(1);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!e->key.repeat && !jkHud_bChatOpen && !SDL_TextInputActive(displayWindow) && !touchUiSdl_IsOwnKey(e->key.scancode))
                touchUi_SetOtherInput(1);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e->motion.which != SDL_TOUCH_MOUSEID && e->motion.which != SDL_PEN_MOUSEID && (e->motion.xrel != 0.0f || e->motion.yrel != 0.0f))
                touchUi_SetOtherInput(1);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e->button.which != SDL_TOUCH_MOUSEID && e->button.which != SDL_PEN_MOUSEID) touchUi_SetOtherInput(1);
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            // every finger let go, the sensors off
            touchUi_Reset();
            touchUiSdl_RunSensors(0);
            break;
        default:
            break;
    }
    return 0;
}

void touchUiSdl_AfterEvents(void)
{
    if (!ts.bInit) touchUiSdl_Init();
    if (!displayWindow) return;
    const double now = touchUiSdl_Now();

    // Shown while gameplay's controls are on: no cutscene, no menu (the
    // objectives at a level's start wait for Ok with the controls on)
    const int bWant = stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive();
    touchUi_SetShown(bWant);
    // The controls appearing: a mouse button a finger pressed before (a menu's
    // Ok, a loading screen) lets go now -- its lift, now the controls', won't
    // reach the game, and a held left button would fire on and on
    if (bWant && !ts.bShown) {
        Window_bMouseLeft = 0;
        Window_bMouseRight = 0;
    }

    float w, h, aInset[4];
    touchUiSdl_Measure(&w, &h, aInset);
    if (!bWant) {
        touchUiSdl_RunSensors(0);
        ts.bShown = 0;
        if (ts.bEscWas) {
            stdControl_bControllerEscapeKey = 0;
            ts.bEscWas = 0;
        }
        return;
    }
    ts.bShown = 1;
    touchUi_SetScreen(w, h, aInset[0], aInset[1], aInset[2], aInset[3]);

    int side = 0;
    ts.rotDeg = touchUiSdl_ScreenTurn(&side);
    touchUi_GyroSetSide(side, now);
    touchUiSdl_RunSensors(touchUi_IsGyroWanted());

    touchUi_Tick(ts.known, now);

    // A photo mode just ended: a mouse or trackpad moved meanwhile (the game
    // held) doesn't turn the view now (iOS: showAfterPhotos)
    const int bPhotoMode = touchUi_IsPhotoModeUp();
    if (ts.bPhotoModeWas && !bPhotoMode) {
        Window_lastXRel = 0;
        Window_lastYRel = 0;
    }
    ts.bPhotoModeWas = bPhotoMode;

    int dx, dy, gx, gy;
    touchUi_TakeLook(&dx, &dy);
    touchUi_GyroFrame(now, &gx, &gy);
    Window_lastXRel += dx + gx;
    Window_lastYRel += dy + gy;

    // MENU's tap: Escape, held for two frames (the game acts on it once); only
    // ever let go of by this, so a controller's Start is left alone
    const int bEsc = touchUi_TakeEscape();
    if (bEsc) stdControl_bControllerEscapeKey = 1;
    else if (ts.bEscWas) stdControl_bControllerEscapeKey = 0;
    ts.bEscWas = bEsc;

    const int haptics = touchUi_TakeHaptics();
    if (haptics) touchUiPlat_Haptic(haptics);
}

void touchUiSdl_Render(void)
{
    if (!ts.bShown || !touchUi_IsShown() || ts.pxW <= 0) {
        platServicesTouch_OverlayDrawn(0);
        return;
    }
    touchUiRender_Draw(touchUi_Draw(touchUiSdl_Now()), (unsigned int)std3D_windowFbo, ts.pxW, ts.pxH, ts.pxPerPt);
    platServicesTouch_OverlayDrawn(1);
}

void touchUiSdl_ContextLost(void)
{
    touchUiRender_ContextLost();
}

int touchUiSdl_IsScancodeDown(int scancode)
{
    return touchUi_IsScancodeDown(scancode);
}

int touchUiSdl_GetTopTextRectWin(float* pTop, float* pLeft, float* pRight)
{
    float t, l, r;
    if (!touchUi_GetTopTextRect(&t, &l, &r) || ts.winToPx <= 0.0f) return 0;
    const float k = ts.pxPerPt / ts.winToPx;
    *pTop = t * k;
    *pLeft = l * k;
    *pRight = r * k;
    return 1;
}

#ifndef TARGET_ANDROID
// No haptics here (the test rig says when it would have ticked)
void touchUiPlat_Haptic(int kinds)
{
    if (getenv("OPENJKDF2_TOUCH_DEBUG")) stdPlatform_Printf("OpenJKDF2: touch overlay: haptic %d\n", kinds);
}
#endif

#endif // TOUCHUI_ENABLED

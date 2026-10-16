// Added: entire file. See inputPhoto.h. What the iOS overlay's CAMERA does
// (iosTouch.m: takePhotos, startSelfie, startFreeze, endFreeze,
// takeFreezePhoto, tickFreeze), with keys and a mouse.

#include "Platform/Input/inputPhoto.h"

#ifdef ENH_INPUT

#include <math.h>
#include <string.h>

#include "Platform/Input/inputUi.h"
#include "Enhanced/iosGame.h"
#include "Enhanced/iosCapture.h"
#include "Enhanced/iosCinema.h"
#include "Enhanced/iosFreeze.h"
#include "Enhanced/iosSelfieShots.h"
#include "Enhanced/enhTick.h"
#include "Win95/Window.h"
#include "globals.h"

// A photo asked for that no frame takes this soon is dropped
#define INPUTPHOTO_TIMEOUT 0.5
// Action Freeze's camera: degrees round per count of mouse movement, the
// mouse's moves followed over about this long, the lens zoomed this much a
// wheel notch
#define INPUTPHOTO_ORBIT 0.25f
#define INPUTPHOTO_SMOOTH 0.03f
#define INPUTPHOTO_ZOOM_NOTCH 1.12f
// The flash: this bright, gone after this long
#define INPUTPHOTO_FLASH 0.6f
#define INPUTPHOTO_FLASH_SECS 0.25

enum { INPUTPHOTO_YAW = 0, INPUTPHOTO_PITCH, INPUTPHOTO_ZOOM, INPUTPHOTO_NUM };

static struct {
    int bFreezing;      // Action Freeze is up, started here
    int bSelfie;        // a selfie started here is going
    double tAsked;      // when photos were asked for (0: not waiting)
    double tFlash;      // when the last ones were taken (the flash)
    double lastNow;
    int kinds, selfieShots;
    float aMove[INPUTPHOTO_NUM], aMid[INPUTPHOTO_NUM];
} inputPhoto_s = { .kinds = IOSCAPTURE_CLEAN, .selfieShots = 3, .tFlash = -1.0 };

int inputPhoto_IsUp(void)
{
    return inputPhoto_s.bFreezing || iosSelfieShots_IsActive() || iosCinema_IsActive() || iosFreeze_IsActive();
}

int inputPhoto_IsFreezing(void)
{
    return inputPhoto_s.bFreezing;
}

void inputPhoto_SetChoices(int kinds, int selfieShots)
{
    kinds &= IOSCAPTURE_CLEAN | IOSCAPTURE_HUD; // (no touch buttons to show here)
    inputPhoto_s.kinds = kinds ? kinds : IOSCAPTURE_CLEAN;
    inputPhoto_s.selfieShots = selfieShots < 1 ? 1 : selfieShots > IOSSELFIESHOTS_MAX ? IOSSELFIESHOTS_MAX : selfieShots;
}

static void inputPhoto_Asked(void)
{
    inputPhoto_s.tAsked = enh_Now();
}

static void inputPhoto_EndFreeze(void)
{
    if (!inputPhoto_s.bFreezing) return;
    inputPhoto_s.bFreezing = 0;
    // (a photo asked for and not taken yet would be of the game's own view)
    if (inputPhoto_s.tAsked != 0.0) {
        iosCapture_Cancel();
        inputPhoto_s.tAsked = 0.0;
    }
    iosFreeze_End(); // (all as it was: the game goes on from the same moment)
    // (the mouse moved meanwhile doesn't turn the view afterwards)
    Window_lastXRel = 0;
    Window_lastYRel = 0;
}

static int inputPhoto_StartFreeze(void)
{
    if (inputPhoto_IsUp()) return 0;
    if (!iosFreeze_Begin()) {
        iosGame_ShowMessage("Can't freeze here");
        return 0;
    }
    inputPhoto_s.bFreezing = 1;
    memset(inputPhoto_s.aMove, 0, sizeof(inputPhoto_s.aMove));
    memset(inputPhoto_s.aMid, 0, sizeof(inputPhoto_s.aMid));
    iosFreeze_SetSmooth(1);
    return 1;
}

static int inputPhoto_StartSelfie(void)
{
    if (inputPhoto_IsUp()) return 0;
    int numPeople = 0;
    const int r = iosSelfieShots_Start(inputPhoto_s.selfieShots, enh_Now(), &numPeople);
    if (r != IOSSELFIESHOTS_STARTED) {
        iosGame_ShowMessage(r == IOSSELFIESHOTS_BUSY ? "Still saving the last photo" : "Can't take a selfie here");
        return 0;
    }
    inputPhoto_s.bSelfie = 1;
    return 1;
}

int inputPhoto_Camera(int action)
{
    if (jkHud_bChatOpen) return 0;
    switch (action) {
    case INPUTUI_CAM_PHOTO:
        if (inputPhoto_s.bFreezing) {
            // Action Freeze's photo: one clean one of the frozen moment
            if (!iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_PHOTO)) {
                iosGame_ShowMessage("Still saving the last photo");
                return 0;
            }
            inputPhoto_Asked();
            return 1;
        }
        if (inputPhoto_IsUp()) return 0;
        if (!iosCapture_Request(inputPhoto_s.kinds, IOSCAPTURE_MODE_PHOTO)) return 0; // (the last ones are still being saved)
        inputPhoto_Asked();
        return 1;
    case INPUTUI_CAM_SELFIE:
        return inputPhoto_StartSelfie();
    case INPUTUI_CAM_FREEZE:
        if (inputPhoto_s.bFreezing) {
            inputPhoto_EndFreeze();
            return 1;
        }
        return inputPhoto_StartFreeze();
    case INPUTUI_CAM_FREEZE_END:
        if (!inputPhoto_s.bFreezing) return 0;
        inputPhoto_EndFreeze();
        return 1;
    default:
        return 0;
    }
}

void inputPhoto_FreezeMouse(float dx, float dy)
{
    if (!inputPhoto_s.bFreezing) return;
    // (as a finger: what is on screen follows the mouse)
    inputPhoto_s.aMove[INPUTPHOTO_YAW] -= dx * INPUTPHOTO_ORBIT;
    inputPhoto_s.aMove[INPUTPHOTO_PITCH] -= dy * INPUTPHOTO_ORBIT;
}

void inputPhoto_FreezeWheel(float notches)
{
    if (!inputPhoto_s.bFreezing) return;
    inputPhoto_s.aMove[INPUTPHOTO_ZOOM] += notches * logf(INPUTPHOTO_ZOOM_NOTCH);
}

// The camera follows the mouse's moves so far, part of the way (the rest
// next frame), so it glides (as iosTouch.m's tickFreeze)
static void inputPhoto_TickFreeze(double dt)
{
    if (dt > 0.1) dt = 0.1;
    const float k = (float)(1.0 - exp(-(dt > 0.0 ? dt : 0.0) / INPUTPHOTO_SMOOTH));
    float a[INPUTPHOTO_NUM];
    for (int i = 0; i < INPUTPHOTO_NUM; i++) {
        const float m = inputPhoto_s.aMove[i] * k;
        inputPhoto_s.aMove[i] -= m;
        inputPhoto_s.aMid[i] += m;
        a[i] = inputPhoto_s.aMid[i] * k;
        inputPhoto_s.aMid[i] -= a[i];
        if (fabsf(inputPhoto_s.aMove[i]) + fabsf(inputPhoto_s.aMid[i]) < 1e-5f) inputPhoto_s.aMove[i] = inputPhoto_s.aMid[i] = 0.0f;
    }
    if ((a[INPUTPHOTO_YAW] != 0.0f || a[INPUTPHOTO_PITCH] != 0.0f) && !iosFreeze_Orbit(a[INPUTPHOTO_YAW], a[INPUTPHOTO_PITCH])) {
        // (a wall that way: the rest is dropped)
        inputPhoto_s.aMove[INPUTPHOTO_YAW] = inputPhoto_s.aMid[INPUTPHOTO_YAW] = 0.0f;
        inputPhoto_s.aMove[INPUTPHOTO_PITCH] = inputPhoto_s.aMid[INPUTPHOTO_PITCH] = 0.0f;
    }
    if (a[INPUTPHOTO_ZOOM] != 0.0f) iosFreeze_Zoom(expf(a[INPUTPHOTO_ZOOM]));
}

void inputPhoto_Tick(int bGameplay, double now)
{
    double dt = now - inputPhoto_s.lastNow;
    inputPhoto_s.lastNow = now;

    // Photos asked for: taken (the flash), or dropped once the game is left
    // or no frame took them in time
    if (inputPhoto_s.tAsked != 0.0) {
        if (!iosCapture_IsPending()) {
            inputPhoto_s.tAsked = 0.0;
            inputPhoto_s.tFlash = now;
        }
        else if (!bGameplay || now - inputPhoto_s.tAsked > INPUTPHOTO_TIMEOUT) {
            iosCapture_Cancel();
            inputPhoto_s.tAsked = 0.0;
        }
    }

    if (inputPhoto_s.bFreezing && !iosFreeze_IsActive()) inputPhoto_s.bFreezing = 0; // (ended elsewhere)
    if (inputPhoto_s.bSelfie && !iosSelfieShots_IsActive()) inputPhoto_s.bSelfie = 0;
    if (!bGameplay) {
        // the game left (a menu, a cutscene, a level's end): the photo modes end
        inputPhoto_EndFreeze();
        if (inputPhoto_s.bSelfie) {
            iosSelfieShots_Stop();
            inputPhoto_s.bSelfie = 0;
        }
        return;
    }
    if (inputPhoto_s.bFreezing) inputPhoto_TickFreeze(dt);
}

float inputPhoto_FlashAlpha(double now)
{
    if (inputPhoto_s.tFlash < 0.0) return 0.0f;
    const double t = now - inputPhoto_s.tFlash;
    if (t < 0.0 || t >= INPUTPHOTO_FLASH_SECS) return 0.0f;
    return INPUTPHOTO_FLASH * (float)(1.0 - t / INPUTPHOTO_FLASH_SECS);
}

#endif // ENH_INPUT

#include "Platform/iOS/iosCinema.h"

#ifdef TARGET_IOS

#include <stdio.h>

#include "Platform/iOS/iosFreeze.h"
#include "Platform/iOS/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosGame.h"

// The arc: up to this many degrees round, rising this much over it; tried
// shorter (IOSCINEMA_SWEEP_STEP less each time) where there isn't room for
// the camera at IOSCINEMA_ROOM of its distance all the way round
#define IOSCINEMA_SWEEP_MAX  60.0f
#define IOSCINEMA_SWEEP_MIN  20.0f
#define IOSCINEMA_SWEEP_STEP 20.0f
#define IOSCINEMA_RISE       -8.0f  // (iosFreeze's pitch is the way the camera looks: less is a camera higher up, looking down)
#define IOSCINEMA_ROOM        0.6f
// The first photo waits this long (the buttons hidden, a frame drawn
// frozen); the others are taken a frame apart; the last stays on screen
// IOSCINEMA_SHOW; a photo not taken IOSCINEMA_GIVEUP after it was due ends it
#define IOSCINEMA_SETTLE 0.1
#define IOSCINEMA_SHOW   0.5
#define IOSCINEMA_GIVEUP 3.0

static int iosCinema_state = IOSCINEMA_IDLE;
static int iosCinema_taken = 0;
static int iosCinema_bAsked = 0;
static float iosCinema_sweep = 0.0f, iosCinema_rise = 0.0f;
static int iosCinema_dir = 1;
static float iosCinema_yawDone = 0.0f, iosCinema_pitchDone = 0.0f; // turned so far
static double iosCinema_tStart = 0, iosCinema_tStage = 0, iosCinema_tLast = 0;

// How much room the camera has all along an arc: the least of its distance
// at each photo's place over the distance it is wanted at (1: all open; -1:
// somewhere on it the camera can't be at all)
static float iosCinema_Room(int dir, float sweep, float rise)
{
    const float want = iosFreeze_GetWantedDist();
    float least = 1.0f;
    for (int k = 1; k < IOSCINEMA_SHOTS; k++) {
        const float t = (float)k / (IOSCINEMA_SHOTS - 1);
        const float d = iosFreeze_Probe(dir * sweep * t, rise * t);
        if (d < 0.0f) return -1.0f;
        const float room = want > 1e-4f ? d / want : 1.0f;
        if (room < least) least = room;
    }
    return least;
}

// Which way, and how far round: the side with more room, as far as there
// is room for (else the roomiest arc tried; else none, the photos from
// where it is)
static void iosCinema_ChooseArc(void)
{
    float bestRoom = -1.0f, bestSweep = 0.0f;
    int bestDir = 1;
    for (float sweep = IOSCINEMA_SWEEP_MAX; sweep >= IOSCINEMA_SWEEP_MIN - 0.01f; sweep -= IOSCINEMA_SWEEP_STEP) {
        for (int dir = 1; dir >= -1; dir -= 2) {
            const float room = iosCinema_Room(dir, sweep, IOSCINEMA_RISE);
            if (room > bestRoom + 0.01f) {
                bestRoom = room;
                bestSweep = sweep;
                bestDir = dir;
            }
        }
        if (bestRoom >= IOSCINEMA_ROOM) break;
    }
    iosCinema_sweep = bestRoom > 0.0f ? bestSweep : 0.0f;
    iosCinema_rise = bestRoom > 0.0f ? IOSCINEMA_RISE : 0.0f;
    iosCinema_dir = bestDir;
}

int iosCinema_Start(double now)
{
    if (iosCinema_state != IOSCINEMA_IDLE) return IOSCINEMA_CANT;
    // (room for all five in the capture budget first: it never waits frozen)
    if (iosCapture_IsPending() || !iosCapture_HasRoom(IOSCAPTURE_CLEAN, IOSCINEMA_SHOTS)) return IOSCINEMA_BUSY;
    if (!iosFreeze_Begin()) return IOSCINEMA_CANT;
    iosCinema_ChooseArc();
    iosCinema_taken = 0;
    iosCinema_bAsked = 0;
    iosCinema_yawDone = iosCinema_pitchDone = 0.0f;
    iosCinema_tStart = iosCinema_tStage = iosCinema_tLast = now;
    iosCinema_state = IOSCINEMA_TAKING;
    iosPhotos_SetBatch(1); // (one message for them all, numbered names)
    return IOSCINEMA_STARTED;
}

// The camera to photo k's place on the arc
static void iosCinema_MoveTo(int k)
{
    const float t = (float)k / (IOSCINEMA_SHOTS - 1);
    const float yaw = iosCinema_dir * iosCinema_sweep * t, pitch = iosCinema_rise * t;
    // (a step it can't make, it doesn't: that photo is from the last place,
    // and the next step is from there)
    if (iosFreeze_Orbit(yaw - iosCinema_yawDone, pitch - iosCinema_pitchDone)) {
        iosCinema_yawDone = yaw;
        iosCinema_pitchDone = pitch;
    }
}

int iosCinema_Tick(double now)
{
    if (iosCinema_state == IOSCINEMA_IDLE) return IOSCINEMA_IDLE;
    iosCinema_tLast = now;
    if (!iosFreeze_IsActive()) {
        iosCinema_Stop(); // (ended from elsewhere)
        return iosCinema_state;
    }
    if (iosCinema_state == IOSCINEMA_SHOWING) {
        if (now - iosCinema_tStage >= IOSCINEMA_SHOW) iosCinema_Stop();
        return iosCinema_state;
    }
    // Taken (a frame read it), or given up on by the capture
    if (iosCinema_bAsked && !iosCapture_IsPending()) {
        iosCinema_bAsked = 0;
        iosCinema_taken++;
        iosCinema_tStage = now;
        if (iosCinema_taken >= IOSCINEMA_SHOTS) {
            iosCinema_state = IOSCINEMA_SHOWING;
            return iosCinema_state;
        }
    }
    if (!iosCinema_bAsked && (iosCinema_taken > 0 || now - iosCinema_tStage >= IOSCINEMA_SETTLE)) {
        // (moved, then asked for: the frame drawn next is from the new place)
        if (iosCinema_taken > 0) iosCinema_MoveTo(iosCinema_taken);
        if (iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_BURST)) {
            iosCinema_bAsked = 1;
            iosCinema_tStage = now;
        }
    }
    if (now - iosCinema_tStage >= IOSCINEMA_GIVEUP + (iosCinema_taken ? 0.0 : IOSCINEMA_SETTLE)) {
        const int taken = iosCinema_taken;
        iosCinema_Stop();
        char msg[64];
        if (taken) snprintf(msg, sizeof(msg), "Took %d of %d photos", taken, IOSCINEMA_SHOTS);
        else snprintf(msg, sizeof(msg), "Couldn't take the photos");
        iosGame_ShowMessage(msg);
    }
    return iosCinema_state;
}

void iosCinema_Stop(void)
{
    if (iosCinema_state == IOSCINEMA_IDLE) return;
    if (iosCinema_bAsked && iosCapture_IsPending()) iosCapture_Cancel();
    iosCinema_bAsked = 0;
    iosCinema_state = IOSCINEMA_IDLE;
    iosFreeze_End(); // (the game goes on from the same moment)
    // (the game held from the start to now, the last photo's showing too)
    iosCapture_NoteFreeze(iosCinema_tLast - iosCinema_tStart);
    iosPhotos_SetBatch(0);
}

int iosCinema_IsActive(void)
{
    return iosCinema_state != IOSCINEMA_IDLE;
}

int iosCinema_NumTaken(void)
{
    return iosCinema_taken;
}

void iosCinema_GetArc(float* pSweep, int* pDir)
{
    if (pSweep) *pSweep = iosCinema_sweep;
    if (pDir) *pDir = iosCinema_dir;
}

#endif // TARGET_IOS

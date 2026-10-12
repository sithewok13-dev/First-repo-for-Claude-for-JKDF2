#include "Platform/iOS/iosSelfieShots.h"

#ifdef TARGET_IOS

#include "Platform/iOS/iosSelfie.h"
#include "Platform/iOS/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosGame.h"

#include <stdio.h>

// The first photo waits this long after posing (a frame or two drawn posed,
// and the moment the buttons take to go); the others are taken as soon as
// the one before is (one frame each). The last stays on screen
// IOSSELFIESHOTS_SHOW. A photo that hasn't been taken IOSSELFIESHOTS_GIVEUP
// after it was due (the frame read failed, or no room to save it) ends it.
#define IOSSELFIESHOTS_SETTLE 0.35
#define IOSSELFIESHOTS_SHOW   0.9
#define IOSSELFIESHOTS_GIVEUP 3.0

static int iosSelfieShots_state = IOSSELFIESHOTS_IDLE;
static int iosSelfieShots_num = 0;     // photos wanted
static int iosSelfieShots_taken = 0;   // photos taken
static int iosSelfieShots_bAsked = 0;  // the current one is asked for
static double iosSelfieShots_tStart = 0, iosSelfieShots_tStage = 0;
static double iosSelfieShots_tLast = 0; // the last tick (how long the game was held)

int iosSelfieShots_Start(int numShots, double now, int* pNumPeople)
{
    if (pNumPeople) *pNumPeople = 0;
    if (iosSelfieShots_state != IOSSELFIESHOTS_IDLE) return IOSSELFIESHOTS_CANT;
    if (numShots < 1) numShots = 1;
    if (numShots > IOSSELFIESHOTS_MAX) numShots = IOSSELFIESHOTS_MAX;
    // Room for all of them in the capture budget before starting, so a
    // selfie never waits frozen for the saving to catch up
    if (iosCapture_IsPending() || !iosCapture_HasRoom(IOSCAPTURE_CLEAN, numShots)) return IOSSELFIESHOTS_BUSY;
    if (!iosSelfie_Begin(IOSSELFIE_POSE_RANDOM, pNumPeople)) return IOSSELFIESHOTS_CANT;
    iosSelfieShots_num = numShots;
    iosSelfieShots_taken = 0;
    iosSelfieShots_bAsked = 0;
    iosSelfieShots_tStart = iosSelfieShots_tStage = iosSelfieShots_tLast = now;
    iosSelfieShots_state = IOSSELFIESHOTS_TAKING;
    // (several: one message for them all, and numbered names)
    if (numShots > 1) iosPhotos_SetBatch(1);
    return IOSSELFIESHOTS_STARTED;
}

int iosSelfieShots_Tick(double now)
{
    if (iosSelfieShots_state == IOSSELFIESHOTS_IDLE) return IOSSELFIESHOTS_IDLE;
    iosSelfieShots_tLast = now;
    // (the selfie ended from elsewhere: no more photos of the game's own view)
    if (!iosSelfie_IsActive()) {
        iosSelfieShots_Stop();
        return iosSelfieShots_state;
    }
    iosSelfie_Frame();
    if (iosSelfieShots_state == IOSSELFIESHOTS_SHOWING) {
        if (now - iosSelfieShots_tStage >= IOSSELFIESHOTS_SHOW) iosSelfieShots_Stop();
        return iosSelfieShots_state;
    }
    // Taken (a frame read it), or given up on by the capture (it says why)
    if (iosSelfieShots_bAsked && !iosCapture_IsPending()) {
        iosSelfieShots_bAsked = 0;
        iosSelfieShots_taken++;
        iosSelfieShots_tStage = now;
        if (iosSelfieShots_taken >= iosSelfieShots_num) {
            iosSelfieShots_state = IOSSELFIESHOTS_SHOWING;
            return iosSelfieShots_state;
        }
        iosSelfie_Repose(); // the next one, posed afresh (drawn so in the next frame)
    }
    if (!iosSelfieShots_bAsked) {
        const int bDue = iosSelfieShots_taken > 0 || now - iosSelfieShots_tStage >= IOSSELFIESHOTS_SETTLE;
        if (bDue && iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_SELFIE)) {
            iosSelfieShots_bAsked = 1;
            iosSelfieShots_tStage = now;
        }
    }
    if (now - iosSelfieShots_tStage >= IOSSELFIESHOTS_GIVEUP + (iosSelfieShots_taken ? 0.0 : IOSSELFIESHOTS_SETTLE)) {
        const int taken = iosSelfieShots_taken, num = iosSelfieShots_num;
        iosSelfieShots_Stop();
        // (the saving says what it saved; this, that the rest weren't taken)
        char msg[64];
        if (taken) snprintf(msg, sizeof(msg), "Took %d of %d selfie photos", taken, num);
        else snprintf(msg, sizeof(msg), "Couldn't take the selfie");
        iosGame_ShowMessage(msg);
    }
    return iosSelfieShots_state;
}

void iosSelfieShots_Stop(void)
{
    if (iosSelfieShots_state == IOSSELFIESHOTS_IDLE) return;
    if (iosSelfieShots_bAsked && iosCapture_IsPending()) iosCapture_Cancel();
    iosSelfieShots_bAsked = 0;
    iosSelfieShots_state = IOSSELFIESHOTS_IDLE;
    iosSelfie_End();
    // (the game held from the start to now, the last photo's showing too)
    iosCapture_NoteFreeze(iosSelfieShots_tLast - iosSelfieShots_tStart);
    if (iosSelfieShots_num > 1) iosPhotos_SetBatch(0);
}

int iosSelfieShots_IsActive(void)
{
    return iosSelfieShots_state != IOSSELFIESHOTS_IDLE;
}

int iosSelfieShots_NumTaken(void)
{
    return iosSelfieShots_taken;
}

#endif // TARGET_IOS

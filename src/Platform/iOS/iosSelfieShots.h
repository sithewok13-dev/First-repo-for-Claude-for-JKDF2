#ifndef _PLATFORM_IOS_IOSSELFIESHOTS_H
#define _PLATFORM_IOS_IOSSELFIESHOTS_H

// A selfie of one, three or five photos: the selfie (iosSelfie) set up once,
// then for each photo everyone posed afresh at random and one clean capture
// (iosCapture), as fast as frames are drawn and the capture budget allows.
// The game stays held from the first photo to the last, so nothing in the
// level moves between them (no time passes for it: iosGame's hold). The
// last photo stays on screen a moment, then everything is put back and the
// game goes on. The touch overlay starts it, ticks it once per frame drawn
// and hides its buttons while it runs; the Linux rig drives the same code.

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

#define IOSSELFIESHOTS_MAX 5

// iosSelfieShots_Start's answers
#define IOSSELFIESHOTS_STARTED  1
#define IOSSELFIESHOTS_CANT     0  // no selfie here (no room for the camera, a cutscene...)
#define IOSSELFIESHOTS_BUSY    -1  // the last photos are still being saved

// What it is doing (iosSelfieShots_Tick)
#define IOSSELFIESHOTS_IDLE    0
#define IOSSELFIESHOTS_TAKING  1   // posing and taking the photos
#define IOSSELFIESHOTS_SHOWING 2   // the last photo, on screen a moment

// Starts a selfie of numShots photos (clamped to 1..IOSSELFIESHOTS_MAX), at
// now (seconds, any steady clock). *pNumPeople: who is in it besides the
// player.
int iosSelfieShots_Start(int numShots, double now, int* pNumPeople);

// Once per frame drawn, before it is drawn. Returns what it is doing; IDLE
// once all is put back (or it gave up).
int iosSelfieShots_Tick(double now);

// Ends it at once, everything put back (the app going to the background, a
// menu, the overlay going). Safe to call when idle.
void iosSelfieShots_Stop(void);

int iosSelfieShots_IsActive(void);

// Photos taken so far in this one (the overlay's counter)
int iosSelfieShots_NumTaken(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSSELFIESHOTS_H

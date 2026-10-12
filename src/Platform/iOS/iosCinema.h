#ifndef _PLATFORM_IOS_IOSCINEMA_H
#define _PLATFORM_IOS_IOSCINEMA_H

// The cinematic burst: the action frozen (iosFreeze) and five photos taken
// one frame apart as the camera sweeps round it in an arc, then the game
// goes on from the same moment. The arc starts from the player's view and
// turns towards whichever side has more room (the walls checked first:
// iosFreeze_Probe), rising a little; in a cramped place it sweeps less. No
// game time passes between the photos (iosGame's hold), and they are only
// started if there is room in the capture budget for all five.

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

#define IOSCINEMA_SHOTS 5

// iosCinema_Start's answers (as iosSelfieShots')
#define IOSCINEMA_STARTED  1
#define IOSCINEMA_CANT     0  // no Action Freeze here (a cutscene, multiplayer...)
#define IOSCINEMA_BUSY    -1  // the last photos are still being saved

// What it is doing (iosCinema_Tick)
#define IOSCINEMA_IDLE    0
#define IOSCINEMA_TAKING  1
#define IOSCINEMA_SHOWING 2   // the last photo, on screen a moment

int iosCinema_Start(double now);

// Once per frame drawn, before it is drawn; IDLE once the game goes on.
int iosCinema_Tick(double now);

// Ends it at once, the game going on. Safe to call when idle.
void iosCinema_Stop(void);

int iosCinema_IsActive(void);
int iosCinema_NumTaken(void);

// The arc chosen (degrees round, and which way: +1 or -1), for the rig
void iosCinema_GetArc(float* pSweep, int* pDir);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSCINEMA_H

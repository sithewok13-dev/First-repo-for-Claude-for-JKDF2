#ifndef _PLATFORM_IOS_IOSCINEMA_H
#define _PLATFORM_IOS_IOSCINEMA_H

// The cinematic burst: the action frozen (iosFreeze) and five photos taken
// one frame apart as the camera moves round it -- a true orbit: the camera
// itself moves along an arc, always aimed at what it turns round -- then
// the game goes on from the same moment. The first photo is from the
// player's own view; what it turns round, how far out and which way are
// worked out once at the start (see iosCinema.c). No game time passes
// between the photos (iosGame's hold), and they are only started if there
// is room in the capture budget for all five.

#ifdef JK_ENHANCED

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

// Once per frame drawn, before it is drawn (enh_Tick, with enh_Now); IDLE
// once the game goes on.
int iosCinema_Tick(double now);

// Ends it at once, the game going on. Safe to call when idle.
void iosCinema_Stop(void);

int iosCinema_IsActive(void);
int iosCinema_GetState(void); // (IOSCINEMA_IDLE, _TAKING, _SHOWING)
int iosCinema_NumTaken(void);

// The arc chosen (degrees round, and which way: +1 or -1), for the rig
void iosCinema_GetArc(float* pSweep, int* pDir);

// What it turns round (IOSCINEMA_SUBJECT_*), how much the arc rises (as
// iosFreeze's pitch), its distance from it at the first and last photos,
// and whether it fell back to zooming from where it is, for the rig
#define IOSCINEMA_SUBJECT_KEPT 0  // (as Action Freeze picked: a point ahead)
#define IOSCINEMA_SUBJECT_ONE  1  // (someone or something in view)
#define IOSCINEMA_SUBJECT_PAIR 2  // (someone close to the player: both)
#define IOSCINEMA_SUBJECT_HERO 3  // (nothing in view: the player)
void iosCinema_GetPlan(float* pRise, int* pSubject, float* pDist0, float* pDist1, int* pbZoomOnly);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _PLATFORM_IOS_IOSCINEMA_H

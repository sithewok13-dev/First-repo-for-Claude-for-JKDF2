#ifndef _PLATFORM_IOS_IOSCAPTURE_H
#define _PLATFORM_IOS_IOSCAPTURE_H

// The CAMERA button's photos, read back from one drawn game frame: a clean
// one (the world and the weapon in hand, before the HUD is drawn on top),
// one with the HUD (everything the game draws), and that one again with the
// touch overlay's buttons on top (added by iosPhotos, which saves them all).

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Which photos a capture takes (any mix)
#define IOSCAPTURE_CLEAN    1
#define IOSCAPTURE_HUD      2
#define IOSCAPTURE_HUDBTNS  4
#define IOSCAPTURE_ALL      7

// At most this many captures are being saved at once; another waits
#define IOSCAPTURE_MAX_SAVING 2

// Takes the photos at the next game frame drawn (bBurst: one shot of a
// burst; only IOSCAPTURE_CLEAN). 0 if a capture is already waiting, or too
// many are still being saved.
int iosCapture_Request(int kinds, int bBurst);

// Gives up on a requested capture that no frame has taken yet.
void iosCapture_Cancel(void);

// Whether a capture is requested, or photos are still being saved.
int iosCapture_IsBusy(void);

// iosPhotos: one capture's photos are saved (or failed).
void iosCapture_Done(void);

// std3D_DrawMenu: the world is drawn into the window, the HUD isn't yet.
void iosCapture_AfterScene(void);

// std3D_DrawMenu: everything the game draws is in the window.
void iosCapture_AfterHud(void);

// iosPhotos.m: takes the images (RGBA, top row first, opaque, w x h pixels,
// malloc'd, NULL if not taken; it frees them), adds the touch overlay for
// IOSCAPTURE_HUDBTNS, saves the kinds asked for, then calls iosCapture_Done.
void iosPhotos_SaveCapture(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int bBurst);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSCAPTURE_H

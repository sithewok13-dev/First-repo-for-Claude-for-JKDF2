#ifndef _PLATFORM_IOS_IOSCAPTURE_H
#define _PLATFORM_IOS_IOSCAPTURE_H

// The CAMERA button's photos, read back from one drawn game frame: a clean
// one (the world and the weapon in hand, before the HUD is drawn on top),
// one with the HUD (everything the game draws), and that one again with the
// touch overlay's buttons on top (added by iosPhotos, which saves them all).
// They are handed over to be saved through platServices_SavePhotos.

#ifdef JK_ENHANCED

#ifdef __cplusplus
extern "C" {
#endif

// Which photos a capture takes (any mix)
#define IOSCAPTURE_CLEAN    1
#define IOSCAPTURE_HUD      2
#define IOSCAPTURE_HUDBTNS  4
#define IOSCAPTURE_ALL      7

// The captures waiting to be saved hold at most this many full-screen
// images between them (each is the window's width x height x 4 bytes: about
// 12 MB on a 2556 x 1179 iPhone screen, so some 72 MB in all); a capture that
// would go over waits. Bounds the memory however fast photos are asked for.
#define IOSCAPTURE_BUDGET_IMAGES 6

// What a capture is for: the photos asked for, one shot of a burst, or a
// selfie (both only IOSCAPTURE_CLEAN)
#define IOSCAPTURE_MODE_PHOTO  0
#define IOSCAPTURE_MODE_BURST  1
#define IOSCAPTURE_MODE_SELFIE 2

// Takes the photos at the next game frame drawn. 0 if a capture is already
// waiting, or the ones still being saved leave no room for it (see
// IOSCAPTURE_BUDGET_IMAGES).
int iosCapture_Request(int kinds, int mode);

// Whether n more captures of these kinds would fit in the memory budget now
// (a multi-shot selfie checks before it starts).
int iosCapture_HasRoom(int kinds, int n);

// Whether a capture is requested and no frame has taken it yet.
int iosCapture_IsPending(void);

// Gives up on a requested capture that no frame has taken yet.
void iosCapture_Cancel(void);

// Whether a capture is requested, or photos are still being saved.
int iosCapture_IsBusy(void);

// iosPhotos: one capture's photos are saved (or failed), freeing this many
// bytes of the budget; saveSeconds: from the hand-over to saved, including
// any wait behind earlier captures on the save queue.
void iosCapture_Done(long bytes, double saveSeconds);

// Measurements for the FPS readout's photo line (see iosPhotos): the last
// frame's read-back time, the last save time, the most memory the waiting
// captures held at once (bytes) since the last call, which resets it; and how
// long the game was held for the last selfie, from its start until it let the
// game go (0: none since the last call).
typedef struct {
    double readSeconds;
    double saveSeconds;
    long peakBytes;
    double freezeSeconds;
} iosCaptureStats;
void iosCapture_TakeStats(iosCaptureStats* pStats);
void iosCapture_NoteFreeze(double seconds);

// std3D_DrawMenu: the world is drawn into the window, the HUD isn't yet.
void iosCapture_AfterScene(void);

// std3D_DrawMenu: everything the game draws is in the window.
void iosCapture_AfterHud(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _PLATFORM_IOS_IOSCAPTURE_H

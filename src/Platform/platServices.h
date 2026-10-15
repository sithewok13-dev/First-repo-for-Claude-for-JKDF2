#ifndef _PLATFORM_PLATSERVICES_H
#define _PLATFORM_PLATSERVICES_H

// Added: entire file. What the enhanced gameplay (src/Enhanced, JK_ENHANCED)
// needs from the platform it runs on, in one small interface, so that the
// gameplay itself is the same everywhere:
//   - iOS: src/Platform/iOS/platServices_ios.m (the Photos library, UIKit
//     haptics, the safe area and the touch overlay's top row)
//   - everything else: src/Platform/SDL2/platServices_sdl.c (PNG files in the
//     Pictures folder, no haptics, no margins)

#ifdef JK_ENHANCED

#ifdef __cplusplus
extern "C" {
#endif

// Saves one capture's photos (Enhanced/iosCapture: IOSCAPTURE_* kinds and
// modes; images RGBA, top row first, w x h, malloc'd, NULL if not taken; this
// frees them), then calls iosCapture_Done (possibly a few frames later).
void platServices_SavePhotos(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode);

// While a burst or a multi-shot selfie is going (1): the photos are numbered,
// and the message saying what was saved waits for its end.
void platServices_SetPhotoBatch(int bOn);

// A light tap felt in the hand, where the device has that (none elsewhere).
#define PLATSERVICES_HAPTIC_TICK   0  // a selection moving on (a wheel's slice)
#define PLATSERVICES_HAPTIC_TAP    1  // something done (a photo taken)
void platServices_Haptic(int kind);

// Margins for HUD elements anchored to the left, right and bottom edges of a
// videoW x videoH HUD (rounded corners, a home bar): all 0 where there are none.
void platServices_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom);

// Where the game's top message text goes, in HUD units (the left edge and
// width its lines are wrapped and centred in, the top of the first line), if
// somewhere else than the game's own place: 1 if so, 0 to keep the game's own.
int platServices_GetTopTextRect(int videoW, int videoH, int* pX, int* pY, int* pW);

// Whether the game is played by touch (on-screen controls).
int platServices_IsTouchDevice(void);

// A photo mode that went on by itself (enh_Tick) has ended: the several-photo
// selfie or the cinematic burst, all its photos taken (or given up on). What
// started it puts back what it hid (iOS: the touch overlay's buttons).
#define PLATSERVICES_MODE_SELFIE 1
#define PLATSERVICES_MODE_CINEMA 2
void platServices_PhotoModeEnded(int mode);

// Once a frame (from enh_Tick): what the platform finishes on the game's
// thread (the desktop's photos, written on a thread of their own, handed back
// to the capture as saved).
void platServices_Tick(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _PLATFORM_PLATSERVICES_H

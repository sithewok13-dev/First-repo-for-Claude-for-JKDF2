// Added: entire file. Safe-area margins for the in-game HUD on iOS.
//
// The HUD gauges sit flush against the screen corners, which on a modern
// iPhone are rounded (and the bottom edge has the home indicator), so the
// health/shield and ammo/force gauges got clipped. This turns the window's
// safe area into margins, in the HUD's own (Video_format) pixel units.

#ifndef _PLATFORM_IOS_IOSSAFEAREA_H
#define _PLATFORM_IOS_IOSSAFEAREA_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Margins for HUD elements anchored to the left, right and bottom edges of a
// videoW x videoH HUD. All 0 if the window or its safe area isn't known yet.
void iosSafeArea_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSSAFEAREA_H

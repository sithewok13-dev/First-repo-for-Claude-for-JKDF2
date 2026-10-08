// Added: entire file. Safe-area margins for the in-game HUD on iOS.
//
// The HUD is laid out for a rectangular screen, but a modern iPhone's corners
// are rounded and its bottom edge has the home indicator, so HUD art in the
// corners got clipped. This turns the window's safe area into margins, in the
// HUD's own (Video_format) pixel units: the corner gauges are lifted by the
// bottom one (jkHud.c), the inventory's columns pulled in by the side ones
// (jkHudInv.c).

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

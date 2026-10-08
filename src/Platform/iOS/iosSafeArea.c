// Added: entire file. See iosSafeArea.h.

#include "Platform/iOS/iosSafeArea.h"

#ifdef TARGET_IOS

#include <SDL3/SDL.h>

extern SDL_Window* displayWindow;

// In landscape the side insets (~60pt on notched/Dynamic Island iPhones) are
// sized to clear the camera cutout, which is far more than a corner gauge
// needs to clear the rounded corner, so only part of them is used. The bottom
// inset (~21pt) is the home indicator strip, which also clears the corner
// curve, so it is used in full.
#define IOSSAFEAREA_SIDE_FRACTION 0.5f
#define IOSSAFEAREA_BOTTOM_FRACTION 1.0f

void iosSafeArea_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom)
{
    *pLeft = 0;
    *pRight = 0;
    *pBottom = 0;

    if (!displayWindow || videoW <= 0 || videoH <= 0) return;

    int winW = 0, winH = 0;
    SDL_Rect safe;
    if (!SDL_GetWindowSize(displayWindow, &winW, &winH) || winW <= 0 || winH <= 0) return;
    if (!SDL_GetWindowSafeArea(displayWindow, &safe)) return;

    // Window points -> HUD pixels. Video_format is the drawable size, so this
    // is the display scale (3x on most iPhones), but going by the ratio keeps
    // it right whatever size the HUD is laid out at.
    float scaleX = (float)videoW / (float)winW;
    float scaleY = (float)videoH / (float)winH;

    int insetLeft = safe.x;
    int insetRight = winW - (safe.x + safe.w);
    int insetBottom = winH - (safe.y + safe.h);

    if (insetLeft > 0)   *pLeft   = (int)(insetLeft   * IOSSAFEAREA_SIDE_FRACTION   * scaleX);
    if (insetRight > 0)  *pRight  = (int)(insetRight  * IOSSAFEAREA_SIDE_FRACTION   * scaleX);
    if (insetBottom > 0) *pBottom = (int)(insetBottom * IOSSAFEAREA_BOTTOM_FRACTION * scaleY);
}

#endif // TARGET_IOS

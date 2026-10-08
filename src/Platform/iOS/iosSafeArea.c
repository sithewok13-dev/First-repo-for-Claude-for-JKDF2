// Added: entire file. See iosSafeArea.h.

#include "Platform/iOS/iosSafeArea.h"

#ifdef TARGET_IOS

#include <SDL3/SDL.h>

extern SDL_Window* displayWindow;

// In landscape the side insets (~60pt on notched/Dynamic Island iPhones) are
// sized to clear the camera cutout, which is far more than HUD art near a
// side edge needs to clear the rounded corner, so only part of them is used
// (the inventory's active-items column, jkHudInv.c). The bottom one (~21pt,
// the home indicator strip) is used in full: it lifts the corner gauges
// (jkHud.c), which then need no side margin -- lifted that far, a ~55-62pt
// corner radius only trims the ends of their straps.
#define IOSSAFEAREA_SIDE_FRACTION 0.3f
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

    // Window points -> HUD units. In-game, Video_format is a canvas about 960
    // units tall (see jkMain_FixRes), not the drawable, so this is ~2.4 units
    // per point on a phone -- go by the ratio rather than the display scale.
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

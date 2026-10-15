// Added: entire file. The platform services (Platform/platServices.h) on iOS:
// what was already here, behind the shared interface.

#include "Platform/platServices.h"

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>

#include "Enhanced/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosSafeArea.h"

void platServices_SavePhotos(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode)
{
    iosPhotos_SaveCapture(pClean, pHud, w, h, kinds, mode);
}

void platServices_SetPhotoBatch(int bOn)
{
    iosPhotos_SetBatch(bOn);
}

void platServices_Haptic(int kind)
{
    // (main thread, as the game's loop is; the touch overlay keeps its own)
    static UISelectionFeedbackGenerator* tick = nil;
    static UIImpactFeedbackGenerator* tap = nil;
    if (kind == PLATSERVICES_HAPTIC_TICK) {
        if (!tick) tick = [[UISelectionFeedbackGenerator alloc] init];
        [tick selectionChanged];
    }
    else {
        if (!tap) tap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        [tap impactOccurred];
    }
}

void platServices_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom)
{
    iosSafeArea_GetHudMargins(videoW, videoH, pLeft, pRight, pBottom);
}

int platServices_GetTopTextRect(int videoW, int videoH, int* pX, int* pY, int* pW)
{
    iosSafeArea_GetTopTextRect(videoW, videoH, pX, pY, pW);
    return 1;
}

int platServices_IsTouchDevice(void)
{
    return 1;
}

#endif // TARGET_IOS

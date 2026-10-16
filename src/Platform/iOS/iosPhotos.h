#ifndef _PLATFORM_IOS_IOSPHOTOS_H
#define _PLATFORM_IOS_IOSPHOTOS_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Asks for add-only Photos access if it was never asked: 1 if the system's
// question is now up (done is called on the main thread once it is
// answered), 0 if it was answered before (done isn't called).
int iosPhotos_AskAccess(void (*done)(void));

// While a burst is going (1): the message saying what was saved waits for
// its end, so it comes once for the whole burst.
void iosPhotos_SetBatch(int bOn);

// Takes a capture's images (RGBA, top row first, opaque, w x h pixels,
// malloc'd, NULL if not taken; it frees them), adds the touch overlay for
// IOSCAPTURE_HUDBTNS, saves the kinds asked for, then calls iosCapture_Done.
// (Enhanced/iosCapture hands them over through platServices_SavePhotos.)
void iosPhotos_SaveCapture(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSPHOTOS_H

#ifndef _PLATFORM_IOS_IOSPHOTOS_H
#define _PLATFORM_IOS_IOSPHOTOS_H

#ifdef JK_ENHANCED

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

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _PLATFORM_IOS_IOSPHOTOS_H

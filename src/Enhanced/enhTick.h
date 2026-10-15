#ifndef _ENHANCED_ENHTICK_H
#define _ENHANCED_ENHTICK_H

// Added: entire file. The enhanced gameplay's own clock and its once-a-frame
// work, run by the engine (Window_SdlUpdate) on every platform: the photo
// modes go on by themselves once started, whatever starts them (the iOS touch
// overlay, a key...), and say when they end (platServices_PhotoModeEnded).

#ifdef JK_ENHANCED

#ifdef __cplusplus
extern "C" {
#endif

// The clock the photo modes run on (seconds; SDL_GetTicksNS): what their
// Start functions take as `now` (iosSelfieShots_Start, iosCinema_Start)
double enh_Now(void);

// Once a frame (Window_SdlUpdate, just after the touch overlay's update): the
// several-photo selfie (iosSelfieShots), else the cinematic burst
// (iosCinema), else Action Freeze's camera (iosFreeze_Frame), whichever is
// going; then the platform's own (platServices_Tick); away from iOS, the
// choices kept as cvars (enhSettings_Sync)
void enh_Tick(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _ENHANCED_ENHTICK_H

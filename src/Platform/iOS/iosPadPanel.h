// Added: entire file. What a game controller (iosPad) shows on the screen
// besides the wheels: its options panel, the quick save / quick load hold
// ring and a line of button prompts -- each above the touch overlay, so they
// show while the touch controls are hidden for the pad -- and where its
// settings are kept (the app's settings, as the touch controls' are).

#ifndef _PLATFORM_IOS_IOSPADPANEL_H
#define _PLATFORM_IOS_IOSPADPANEL_H

#include "Platform/iOS/iosPad.h"

#ifdef IOSPAD_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

// The controller options (their rows, focus and wording come from iosPad_Panel*):
// shown or hidden, or drawn again after a change
void iosPadPanel_Show(int bShow);
void iosPadPanel_Refresh(void);

// The hold ring for quick save or quick load (IOSPAD_RING_*), filled this far
// (0..1); IOSPAD_RING_NONE hides it
void iosPadPanel_HoldRing(int kind, float progress);

// A line of button prompts along the bottom of the screen (NULL: none)
void iosPadPanel_SetPrompt(const char* pText);

// Whether a view is one of these (void*: a UIView*), and those shown put
// back on top of the overlay (which keeps itself above what SDL adds)
int iosPadPanel_IsOwnView(const void* pView);
void iosPadPanel_KeepOnTop(void);

// The settings, kept between launches
int iosPadPanel_LoadInt(const char* pKey, int valDefault);
void iosPadPanel_SaveInt(const char* pKey, int val);

#ifdef __cplusplus
}
#endif

#endif // IOSPAD_ENABLED

#endif // _PLATFORM_IOS_IOSPADPANEL_H

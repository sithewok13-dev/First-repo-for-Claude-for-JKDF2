// Added: entire file. On-screen touch controls for iOS.
//
// A UIKit overlay sits on top of the game view while gameplay controls are
// active (stdControl_bControlsActive, and no cutscene playing). It turns
// touches into:
//   - a floating move stick on the left half (W/A/S/D, + Shift when pushed all the way)
//   - drag-to-look anywhere else on the right (fed in as mouse movement)
//   - buttons that hold down the default keyboard keys for fire, jump, etc.
// In menus and cutscenes the overlay hides, so touches reach SDL as mouse
// clicks like before.

#ifndef _PLATFORM_IOS_IOSTOUCH_H
#define _PLATFORM_IOS_IOSTOUCH_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Once per frame, before events are polled: shows/hides the overlay and
// hands accumulated look movement to the mouse axes.
void iosTouch_Update(void);

// Whether the overlay is holding down this SDL scancode.
int iosTouch_IsScancodeDown(int scancode);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSTOUCH_H

// Added: entire file. On-screen touch controls for iOS.
//
// A UIKit overlay sits on top of the game view while gameplay controls are
// active (stdControl_bControlsActive, and no cutscene playing). It turns
// touches into:
//   - a floating move stick on the left (W/A/S/D, + Shift when pushed all the way)
//   - drag-to-look anywhere else on the right (fed in as mouse movement)
//   - buttons on the default keyboard keys. Bottom right: FIRE, with DUCK /
//     ACT / JUMP on an arc around it, ALT above the ammo gauge (dragging on
//     any of these also looks) and FORCE just outside the arc (tap or hold to
//     use the power it shows, slide sideways to pick another). Top left:
//     weapon, force (NEXT/USE FORCE, for using a power while jumping) and
//     inventory. Top right: quick save, quick load (hold) and menu.
// In menus and cutscenes the overlay hides, so touches reach SDL as mouse
// clicks like before. If SDL's window is recreated the overlay follows it.

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

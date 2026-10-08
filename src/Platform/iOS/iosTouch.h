// Added: entire file. On-screen touch controls for iOS.
//
// A UIKit overlay sits on top of the game view while gameplay controls are
// active (stdControl_bControlsActive, and no cutscene playing). It turns
// touches into:
//   - a floating move stick on the left (W/A/S/D), which walks. While the
//     game's Always Run option is off, a run marker (a small "^^" circle,
//     joined to the stick's ring by a line) shows above it, as in Alien:
//     Isolation: push the thumb up past the ring onto it to run straight
//     ahead (W + Shift; the marker lights and the knob sits on it), slide
//     back down to walk again. It comes down toward the stick where the top
//     row is in the way; a thumb down right under it gets no marker. With
//     Always Run on there is no marker: the game runs anyway.
//   - drag-to-look anywhere else on the right (fed in as mouse movement)
//   - buttons on the default keyboard keys. Bottom right: FIRE, with DUCK /
//     ACT / JUMP on an arc around it, ALT above the ammo gauge and FORCE
//     beside JUMP (tap or hold to use the power it shows; a ring round it is
//     the force meter, glowing when full); dragging on any of these also
//     looks. Top left: next weapon, FORCE WHEEL, and a button for each
//     usable item the player has (field light, IR goggles, bacta). Top
//     right: quick save, quick load (hold) and menu.
//   - the force wheel, opened by FORCE WHEEL; the game holds still while it
//     is open. Every power has a fixed slice, learned or not (those are
//     dimmed). Jedi Knight: light side down the left (blue), dark side down
//     the right (red), neutral across the top (gold). Mysteries of the Sith:
//     its four tiers (its Force screen's columns) round from the bottom left.
//     The gap at the bottom cancels. Slide the thumb from FORCE WHEEL toward
//     a power, or all the way onto it -- it pops out, its name shows in the
//     middle -- and lift to select it; lift in the gap, in the middle, or
//     back where it started, for no change. A quick tap instead leaves the
//     wheel open to tap a power.
//   - MENU: a tap opens the menu (or closes the typing line). Held, it opens
//     a tray just under it: the keyboard, to type cheats, and FPS, which
//     shows a frame rate readout left of QUICK SAVE (remembered between
//     launches). The tray closes after a choice, or at a touch elsewhere.
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

// Added: entire file. On-screen touch controls for iOS.
//
// A UIKit overlay sits on top of the game view while gameplay controls are
// active (stdControl_bControlsActive, and no cutscene playing). It turns
// touches (and the gyro) into:
//   - a floating move stick on the left (W/A/S/D), which walks. While the
//     game's Always Run option is off, a run marker (a small "^^" circle,
//     joined to the stick's ring by a line) shows above it, as in Alien:
//     Isolation: push the thumb up past the ring onto it to run straight
//     ahead (W + Shift; the marker lights and the knob sits on it), slide
//     back down to walk again. It comes down toward the stick where the top
//     row is in the way; a thumb down right under it gets no marker. With
//     Always Run on there is no marker: the game runs anyway.
//   - drag-to-look anywhere else on the right (fed in as mouse movement)
//   - gyro aiming: turning the phone turns the view, on top of what the
//     drag does. It starts OFF, until it is switched on in MENU's tray.
//     TOUCH aims while either thumb is down on the game: the left one on the
//     move stick (pushed or resting, or on the run marker), or the right one
//     where it aims -- on the look area (resting or dragging), or on FIRE,
//     ALT, DUCK, ACT, JUMP or FORCE (the buttons a drag on also looks). So
//     with the left thumb on the stick, the right one can hop from the look
//     area onto FIRE without the aiming stopping. The top row, the wheel and
//     the tray don't count. Lifting both thumbs is like lifting a mouse: the
//     view freezes where it is and nothing springs back, and touching again
//     carries on from there, however the phone is held by then. ALWAYS aims
//     with no thumb down; OFF never. Turning is measured about the way up, so
//     it works however far back the phone is tipped, to lying flat; with the
//     screen facing down (lying on your back under it) or the phone rolled
//     right over (lying on your side) it is about the screen's own up axis
//     instead, as if looking through it. Tilting the top of the phone toward
//     you looks up. At 1.0x sensitivity the view turns as far as the phone
//     does. It goes in as mouse movement scaled to the game's mouse look
//     settings, so changing those (sensitivity, reverse) leaves it as it is.
//     It never aims while a wheel, the typing line or MENU's tray is
//     open, nor for a moment after the screen turns round to the other
//     landscape side (turning the phone round while it aims can still turn
//     the view before that, if the phone is tipped back), and the gyro is
//     only read while the overlay is up, the app in front and gyro aiming on.
//     Should iOS ever refuse the motion data, the tray's GYRO says NO GYRO.
//   - buttons on the default keyboard keys. Bottom right: FIRE, with DUCK /
//     ACT / JUMP on an arc around it, ALT above the ammo gauge and FORCE
//     beside JUMP (tap or hold to use the power it shows; a ring round it is
//     the force meter, glowing when full); dragging on any of these also
//     looks. Top left: NEXT WPN, FORCE WHEEL, and a button for each usable
//     item the player has (field light, IR goggles, bacta); under NEXT WPN,
//     VIEW, which switches the camera view (first / third person, the
//     game's F1 key). Top right: quick save, quick load and menu.
//   - QUICK SAVE and QUICK LOAD only go off when held: a ring round the
//     button fills while it is held (0.3 s), and once it is full the game
//     saves or loads -- once, with the finger still down. Lifting sooner
//     does nothing; so does sliding off first (even off and back on between
//     two frames), or holding it while the typing line is open. Two fingers
//     on it save or load once, and QUICK SAVE and QUICK LOAD held together
//     do whichever fills first.
//   - FORCE WHEEL and NEXT WPN take three gestures each. A quick tap (lifted
//     within 0.3 s, without sliding) is the next learned power (the game's
//     own key for it, E: FORCE shows which) or the next weapon (G), as the
//     game's own keys do. Touching and sliding opens the wheel straight away,
//     to slide to a slice; touching and holding still for 0.3 s (a ring
//     fills round the button) opens it to tap one -- and sliding the held
//     thumb out from there picks by sliding again. Either wheel holds the
//     game still while it is open (single player).
//   - the force wheel (FORCE WHEEL). Every power has a fixed slice, learned
//     or not (those are dimmed). Jedi Knight: light side down the left
//     (blue), dark side down the right (red), neutral across the top (gold).
//     Mysteries of the Sith: its four tiers (its Force screen's columns)
//     round from the bottom left. The gap at the bottom cancels. Slide the
//     thumb from FORCE WHEEL toward a power, or all the way onto it -- it
//     pops out, its name shows in the middle -- and lift to select it; lift
//     in the gap, in the middle, or back where it started, for no change. A
//     slide too short to point anywhere leaves the wheel open to tap a power.
//     A learned power's level shows under its name: 0-4 of four stars filled.
//   - the weapon wheel (NEXT WPN), picked from the same way: every weapon of
//     the game in a fixed slot, in its number key order round from the
//     bottom left (Mysteries of the Sith: each key's two weapons side by
//     side). A weapon not found yet is dimmed; one the player has shows its
//     ammo count under its name (what the HUD shows with it in hand; none
//     for the fists and the lightsaber), and with no ammo to fire it is
//     greyed, its count 0 (the bowcaster and the concussion rifle need more
//     than 1 and 7 power cells), and can't be picked. A pick made while a
//     weapon is still being switched to waits until the game would take it.
//   - MENU: a tap opens the menu (or closes the typing line). Held, it opens
//     a tray just under it, left to right: SENS and GYRO, gyro aiming's
//     sensitivity (1.0x, 1.5x -- the default --, 2.0x, 3.0x) and mode
//     (OFF -- the default --, TOUCH, ALWAYS), each tap going on to the next;
//     FPS, which shows a frame rate readout left of QUICK SAVE; and the
//     keyboard, to type cheats. All three settings are remembered between
//     launches. A touch anywhere on the tray's backing is on the nearest of
//     its buttons.
//     The tray closes after a choice (SENS and GYRO leave it open for another
//     tap), or at a touch off it.
// In menus and cutscenes the overlay hides, so touches reach SDL as mouse
// clicks like before. If SDL's window is recreated the overlay follows it.

#ifndef _PLATFORM_IOS_IOSTOUCH_H
#define _PLATFORM_IOS_IOSTOUCH_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Once per frame, before events are polled: shows/hides the overlay and
// hands accumulated look movement (the drag) to the mouse axes.
void iosTouch_Update(void);

// Once per frame, after events are polled -- so it knows which touches are
// down by now: hands what the phone turned since the last frame (gyro
// aiming) to the mouse axes.
void iosTouch_UpdateGyro(void);

// Whether the overlay is holding down this SDL scancode.
int iosTouch_IsScancodeDown(int scancode);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSTOUCH_H

#ifndef _PLATFORM_IOS_IOSFREEZE_H
#define _PLATFORM_IOS_IOSFREEZE_H

// Action Freeze: the game stopped exactly where it is (iosGame's hold: no
// world update, no game time, sounds paused) and the photo camera
// (iosPhotoCam) free to move round it, for photos. It turns round a focus
// point -- at first what the player is looking at -- with one finger, zooms
// with a pinch (the lens) and moves the focus with two fingers; walls stop
// it. Ending it puts everything back as it was (iosPhotoCam_End), and the
// game goes on from that same moment.
//
// Confirmed on the Linux rig with the real engine (six levels, Jedi Knight
// and Mysteries of the Sith): after it the world, the cameras, the animation
// tracks, the game clock (no time passes during it), the sounds and the hold
// are exactly as they were. While it is up, photos seconds apart from the
// same place are pixel for pixel the same, a thermal detonator's blast
// included (its light on the room and its flying debris stay put).
// Not confirmed: an explosion's own fireball sprite (it was out of sight in
// those tests), and anything on an iPhone.

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// Starts it from the view the player has now (the first frame drawn looks
// the same). 0, and nothing changed, if it can't now (as iosPhotoCam_Begin:
// multiplayer, a cutscene, a scope, already up...).
int iosFreeze_Begin(void);

// One finger: turns the camera round the focus, by these degrees (yaw:
// round the up axis; pitch: up and down, kept short of straight up or down).
void iosFreeze_Orbit(float yawDegrees, float pitchDegrees);

// Two fingers: moves the focus (and the camera with it) across the view, by
// these fractions of the screen's height (right, down), so what is on screen
// follows the fingers.
void iosFreeze_Pan(float right, float down);

// A pinch: the lens zoomed by this factor (2: twice as close).
void iosFreeze_Zoom(float factor);

// Puts everything back; the game goes on. Safe to call when not up.
void iosFreeze_End(void);

int iosFreeze_IsActive(void);

// Where the camera is (distance from the focus, lens in degrees top to
// bottom), for the rig's checks
void iosFreeze_GetState(float* pDist, float* pFov);

// sithRender: whether to leave this thing out of the view -- the player's
// own body round the camera, when Action Freeze started from first person,
// until the camera first moves (out of it)
struct SithThing;
int iosFreeze_HidesThing(struct SithThing* pThing);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSFREEZE_H

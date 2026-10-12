#ifndef _PLATFORM_IOS_IOSPHOTOCAM_H
#define _PLATFORM_IOS_IOSPHOTOCAM_H

// The photo camera: a camera of its own for the touch overlay's photo modes
// (the selfie now; a frozen-action camera is planned), shared by them all. While it is
// up the game holds still (iosGame's hold), the view is drawn from it instead
// of the player's eyes, and that view only looks: it doesn't sight things or
// sectors for the game (no AI woken, no level script run, no map marked), and
// the HUD isn't drawn. Everything it borrows is put back exactly by
// iosPhotoCam_End: the engine's spare camera (slot 2), the camera the game
// was using, the camera shake, and the sectors' "seen" map flags.
//
// Confirmed in the code: slot 2 is the engine's "focus" camera (sithCamera),
// unused while the player's first or third person view (slot 0 or 1) is
// current; the hold stops the world update and the game clock, so nothing in
// the level moves while the camera is up.

#ifdef TARGET_IOS

// (No engine headers: the touch overlay, UIKit code, includes this too)
struct rdCamera;
struct rdVector3;
struct SithSector;

#ifdef __cplusplus
extern "C" {
#endif

// The lens by default (degrees top to bottom; the game's own is 90, some 130
// across on a wide phone screen)
#define IOSPHOTOCAM_FOV_DEFAULT 70.0f
#define IOSPHOTOCAM_FOV_MIN 20.0f
#define IOSPHOTOCAM_FOV_MAX 100.0f

// Takes the camera over (holding the game with this IOSGAME_HOLD_* reason).
// 0, and nothing changed, if it can't now: already up, multiplayer, no level,
// or the view isn't the player's own (a cutscene, a scope, a security camera).
int iosPhotoCam_Begin(int holdReason);

// Where the camera is and what it looks at, tilted this many degrees round
// its line of sight; in sector (the one pos is in).
void iosPhotoCam_Place(const struct rdVector3* pos, const struct rdVector3* look, float tilt, struct SithSector* sector);

// The lens (degrees top to bottom, clamped to IOSPHOTOCAM_FOV_MIN..MAX)
void iosPhotoCam_SetFov(float fov);
float iosPhotoCam_GetFov(void);

// Where it is now (only while up)
void iosPhotoCam_GetPos(struct rdVector3* pPos, struct rdVector3* pLook);

// Puts everything back as it was. Safe to call when not up.
void iosPhotoCam_End(void);

int iosPhotoCam_IsActive(void);

// The engine, after setting a view's lens each frame: the photo camera's own
// lens instead, while it is up. 0 (nothing changed) for any other camera.
int iosPhotoCam_SetLens(struct rdCamera* pCamera);

// Whether the line from a to b is clear of walls (people and things in the
// way don't count; openings between sectors are seen through), up to `slack`
// short of b; *pEnd: the sector b is in, following the line.
int iosPhotoCam_Clear(struct SithSector* from, const struct rdVector3* a, const struct rdVector3* b, float slack, struct SithSector** pEnd);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSPHOTOCAM_H

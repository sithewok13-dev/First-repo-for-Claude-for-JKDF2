#ifndef _PLATFORM_IOS_IOSSELFIE_H
#define _PLATFORM_IOS_IOSSELFIE_H

// The selfie: the CAMERA button held still. The whole game holds still; up to
// IOSSELFIE_MAX_PEOPLE characters near the player (bosses too) turn to face a
// camera held out at arm's length and strike a pose; the player holsters for
// the photo and holds an arm out as if holding the phone. Then everything is
// put back exactly as it was and the game goes on. Nothing of it is saved:
// it only changes what is drawn (facing, the frame of each animation, joint
// bends), and only while it lasts.

#ifdef TARGET_IOS

// (No engine headers: the touch overlay, UIKit code, includes this too)
struct rdThing;
struct rdMatrix34;
struct SithThing;
struct rdCamera;

#ifdef __cplusplus
extern "C" {
#endif

#define IOSSELFIE_MAX_PEOPLE 8

// The poses (IOSSELFIE_POSE_RANDOM: one each, at random)
enum {
    IOSSELFIE_POSE_RANDOM = -1,
    IOSSELFIE_POSE_WAVE = 0,
    IOSSELFIE_POSE_FIST,
    IOSSELFIE_POSE_HIPS,
    IOSSELFIE_POSE_CROSSED,
    IOSSELFIE_POSE_NUM_PEOPLE,                 // (the ones above are for anyone)
    IOSSELFIE_POSE_PHONE = IOSSELFIE_POSE_NUM_PEOPLE, // the player's: an arm out to the camera
    IOSSELFIE_POSE_NUM
};

// The parts of a body a pose bends
enum {
    IOSSELFIE_PART_RUPPER = 0, IOSSELFIE_PART_RFORE, IOSSELFIE_PART_RHAND,
    IOSSELFIE_PART_LUPPER, IOSSELFIE_PART_LFORE, IOSSELFIE_PART_LHAND,
    IOSSELFIE_PART_NECK, IOSSELFIE_PART_HEAD, IOSSELFIE_PART_TORSO,
    IOSSELFIE_PART_RTHIGH, IOSSELFIE_PART_RCALF, IOSSELFIE_PART_RFOOT,
    IOSSELFIE_PART_LTHIGH, IOSSELFIE_PART_LCALF, IOSSELFIE_PART_LFOOT,
    IOSSELFIE_PART_NUM
};

// Starts the selfie: who is in it, where the camera goes, the poses (pose:
// for everyone, or IOSSELFIE_POSE_RANDOM). 0 (and nothing changed) if it
// can't be taken here: no player, multiplayer, already going, or no room for
// the camera. *pNumPeople: how many besides the player are in it.
int iosSelfie_Begin(int pose, int* pNumPeople);

// Puts everything back exactly as it was. Safe to call when not going.
void iosSelfie_End(void);

int iosSelfie_IsActive(void);

// The renderer, while a selfie is up: whether to leave this thing out (a
// shot or blast frozen in front of someone's face).
int iosSelfie_HidesThing(struct SithThing* pThing);

// The saber drawing, while a selfie is up: whether to leave out a blade
// (from m's place along its forward axis, this long) that passes right by
// the camera, across the whole photo.
int iosSelfie_HidesBlade(const struct rdMatrix34* m, float length);

// Each frame, after the game sets the view's lens: the selfie camera's own
// lens instead, while a selfie is up. 0 (nothing changed) for any other.
int iosSelfie_SetLens(struct rdCamera* pCamera);

// Each drawn frame while it's going: has everyone in it drawn afresh.
void iosSelfie_Frame(void);

// rdPuppet, building the joints of someone held in their rest pose while a
// selfie is going: adds the pose's turn for this joint, if any.
void iosSelfie_PoseNode(struct rdThing* pThing, int node, struct rdMatrix34* m);

// Tuning (the rig): which way one part of one pose points, in the person's
// own frame (right, forwards to the camera, up).
void iosSelfie_SetPosePart(int pose, int part, float right, float forward, float up);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSSELFIE_H

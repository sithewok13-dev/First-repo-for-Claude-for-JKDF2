#include "Platform/iOS/iosFreeze.h"

#ifdef TARGET_IOS

#include <math.h>
#include <string.h>

#include "Platform/iOS/iosGame.h"
#include "Platform/iOS/iosPhotoCam.h"
#include "World/sithThing.h"
#include "Gameplay/sithPlayer.h"
#include "World/sithWorld.h"
#include "World/sithSector.h"
#include "Engine/sithCamera.h"
#include "Engine/sithIntersect.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdVector.h"
#include "jk.h"

// The focus at first: the nearest person or thing the player is looking
// straight at, within this far (world units; Kyle is some 0.18 tall), or,
// with none, a point this far ahead (nearer if a wall is)
#define IOSFREEZE_FOCUS_MAX   1.5f
#define IOSFREEZE_FOCUS_AHEAD 0.4f
#define IOSFREEZE_FOCUS_MIN   0.08f  // (not the player's own head, in first person)
// The camera stays this far off walls (the near clip shows through nearer),
// and between these distances from the focus
#define IOSFREEZE_WALL_GAP    0.02f
#define IOSFREEZE_DIST_MIN    0.03f
// ...and with this much room round it inside its sector: nearer a wall or an
// opening than this, the view past it can come out black (as the selfie's
// camera, IOSSELFIE_CAM_CLEARANCE). Where it can't be, it comes in towards
// the focus, in this many steps, until it can.
#define IOSFREEZE_CAM_ROOM    0.012f
#define IOSFREEZE_STEPS_IN    12
#define IOSFREEZE_FIT_HALVINGS 7
// With the camera moved by a finger (iosFreeze_SetSmooth): back out from a
// wall, it covers this part of the way each frame
#define IOSFREEZE_EASE_OUT    0.25f
// ...once it has been this many frames without being pushed in (sliding
// along a wall, the room for it can come and go from one step to the next:
// it would go in and out)
#define IOSFREEZE_HOLD_IN     12
// The player is left out of the view with the camera this near their head
#define IOSFREEZE_NEAR_HEAD   0.07f
// Up and down, kept this short of straight up or down (degrees)
#define IOSFREEZE_PITCH_MAX   80.0f

static int iosFreeze_bActive = 0;
static rdVector3 iosFreeze_focus;          // what it turns round
static SithSector* iosFreeze_pFocusSector = NULL;
static float iosFreeze_yaw = 0, iosFreeze_pitch = 0; // the direction from the camera to the focus (degrees)
static float iosFreeze_dist = 0;           // from the focus, as wanted (walls may bring the camera nearer)
static float iosFreeze_shownDist = 0;      // ...and as it is
static int iosFreeze_bInsideMe = 0;        // started from first person, the camera not moved yet
static float iosFreeze_targetDist = 0;     // where the walls let it be (it may still be gliding out to it)
static int iosFreeze_bSmooth = 0;          // moved by a finger: glides back out from walls
static int iosFreeze_holdIn = 0;           // frames to wait before gliding out

static void iosFreeze_Dir(rdVector3* pDir)
{
    const float y = iosFreeze_yaw * (float)M_PI / 180.0f, p = iosFreeze_pitch * (float)M_PI / 180.0f;
    pDir->x = cosf(p) * cosf(y);
    pDir->y = cosf(p) * sinf(y);
    pDir->z = sinf(p);
}

// Whether the camera fits this far back from the focus (along -dir): in a
// sector, with room round it there; and where that is
static int iosFreeze_FitsAt(const rdVector3* dir, float d, rdVector3* pPos, SithSector** pSector)
{
    rdVector3 pos = iosFreeze_focus;
    rdVector_ScaleAdd3Acc(&pos, dir, -d);
    SithSector* sector = NULL;
    iosPhotoCam_ClearDist(iosFreeze_pFocusSector, &iosFreeze_focus, &pos, &sector);
    if (!sector || !sithIntersect_IsSphereInSector(&pos, IOSFREEZE_CAM_ROOM, sector)) return 0;
    *pPos = pos;
    *pSector = sector;
    return 1;
}

// Where the camera can be, as near where it is wanted as the walls let it
// (coming in towards the focus): put there if bApply, and how far it is
// from the focus in *pDist; 0, and nothing changed, if it can't be anywhere
// on that line. Found to the nearest IOSFREEZE_FIT_HALVINGS halving of a
// step, not just the step: sliding along a wall, coarse steps made the
// camera jump in and out (seen as jitter on an iPhone).
static int iosFreeze_Find(int bApply, float* pDist, rdVector3* pPosOut, SithSector** pSectorOut)
{
    rdVector3 dir, want, pos;
    SithSector* sector = NULL;
    iosFreeze_Dir(&dir);
    want = iosFreeze_focus;
    rdVector_ScaleAdd3Acc(&want, &dir, -iosFreeze_dist);
    float d0 = iosFreeze_dist;
    const float clear = iosPhotoCam_ClearDist(iosFreeze_pFocusSector, &iosFreeze_focus, &want, NULL);
    if (clear < d0) d0 = clear - IOSFREEZE_WALL_GAP;
    if (d0 < 0.001f) d0 = 0.001f;
    float d = d0;
    if (!iosFreeze_FitsAt(&dir, d0, &pos, &sector)) {
        // The first step in that fits, then the farthest between it and the
        // step before
        float lo = -1.0f, hi = d0;
        for (int k = 1; k <= IOSFREEZE_STEPS_IN; k++) {
            float t = d0 * (1.0f - (float)k / IOSFREEZE_STEPS_IN);
            if (t < 0.001f) t = 0.001f;
            if (iosFreeze_FitsAt(&dir, t, &pos, &sector)) { lo = t; break; }
            hi = t;
        }
        if (lo < 0.0f) return 0;
        for (int h = 0; h < IOSFREEZE_FIT_HALVINGS; h++) {
            rdVector3 p2;
            SithSector* s2 = NULL;
            const float mid = (lo + hi) * 0.5f;
            if (iosFreeze_FitsAt(&dir, mid, &p2, &s2)) { lo = mid; pos = p2; sector = s2; }
            else hi = mid;
        }
        d = lo;
    }
    if (pDist) *pDist = d;
    if (pPosOut) *pPosOut = pos;
    if (pSectorOut) *pSectorOut = sector;
    if (!bApply) return 1;
    iosFreeze_targetDist = d;
    // Moved by a finger, back out from a wall: glides out (in, it goes at
    // once: nearer is where it fits)
    if (iosFreeze_bSmooth && d > iosFreeze_shownDist + 0.0005f) {
        // (where it is, if that still fits, until the wait is over)
        rdVector3 p2;
        SithSector* s2 = NULL;
        const float e = iosFreeze_holdIn > 0 ? iosFreeze_shownDist
                                             : iosFreeze_shownDist + (d - iosFreeze_shownDist) * IOSFREEZE_EASE_OUT;
        if (iosFreeze_FitsAt(&dir, e, &p2, &s2)) { d = e; pos = p2; sector = s2; }
    }
    else if (iosFreeze_bSmooth && d < iosFreeze_shownDist - 0.0005f) {
        iosFreeze_holdIn = IOSFREEZE_HOLD_IN; // (pushed in)
    }
    // (looking at the focus; from right on it, along the direction)
    rdVector3 look = iosFreeze_focus;
    if (d < 0.01f) rdVector_ScaleAdd3Acc(&look, &dir, 0.05f);
    iosFreeze_shownDist = d;
    iosPhotoCam_Place(&pos, &look, 0.0f, sector);
    return 1;
}

static int iosFreeze_Place(void)
{
    return iosFreeze_Find(1, NULL, NULL, NULL);
}

float iosFreeze_Probe(float yawDegrees, float pitchDegrees)
{
    if (!iosFreeze_bActive) return -1.0f;
    const float yaw = iosFreeze_yaw, pitch = iosFreeze_pitch;
    iosFreeze_yaw = fmodf(iosFreeze_yaw + yawDegrees, 360.0f);
    iosFreeze_pitch += pitchDegrees;
    if (iosFreeze_pitch > IOSFREEZE_PITCH_MAX) iosFreeze_pitch = IOSFREEZE_PITCH_MAX;
    if (iosFreeze_pitch < -IOSFREEZE_PITCH_MAX) iosFreeze_pitch = -IOSFREEZE_PITCH_MAX;
    float d = -1.0f;
    if (!iosFreeze_Find(0, &d, NULL, NULL)) d = -1.0f;
    iosFreeze_yaw = yaw;
    iosFreeze_pitch = pitch;
    return d;
}

float iosFreeze_ProbeAt(float yawDegrees, float pitchDegrees, float dist, struct rdVector3* pPos, struct SithSector** pSector)
{
    if (!iosFreeze_bActive) return -1.0f;
    const float yaw = iosFreeze_yaw, pitch = iosFreeze_pitch, want = iosFreeze_dist;
    iosFreeze_yaw = fmodf(iosFreeze_yaw + yawDegrees, 360.0f);
    iosFreeze_pitch += pitchDegrees;
    if (iosFreeze_pitch > IOSFREEZE_PITCH_MAX) iosFreeze_pitch = IOSFREEZE_PITCH_MAX;
    if (iosFreeze_pitch < -IOSFREEZE_PITCH_MAX) iosFreeze_pitch = -IOSFREEZE_PITCH_MAX;
    if (dist > 0.0f) iosFreeze_dist = dist;
    float d = -1.0f;
    if (!iosFreeze_Find(0, &d, pPos, pSector)) d = -1.0f;
    iosFreeze_yaw = yaw;
    iosFreeze_pitch = pitch;
    iosFreeze_dist = want;
    return d;
}

void iosFreeze_GetOrbit(float* pYaw, float* pPitch, float* pDist)
{
    if (pYaw) *pYaw = iosFreeze_yaw;
    if (pPitch) *pPitch = iosFreeze_pitch;
    if (pDist) *pDist = iosFreeze_dist;
}

int iosFreeze_GetInsideMe(void)
{
    return iosFreeze_bInsideMe;
}

void iosFreeze_SetInsideMe(int bInside)
{
    iosFreeze_bInsideMe = bInside != 0;
}

void iosFreeze_SetWantedDist(float dist)
{
    if (dist > 0.0f) iosFreeze_dist = dist;
}

void iosFreeze_GetFocus(struct rdVector3* pFocus, struct SithSector** pSector)
{
    if (pFocus) *pFocus = iosFreeze_focus;
    if (pSector) *pSector = iosFreeze_pFocusSector;
}

int iosFreeze_SetOrbit(const struct rdVector3* focus, float yawDegrees, float pitchDegrees, float dist)
{
    if (!iosFreeze_bActive || !focus || dist <= 0.0f) return 0;
    rdVector3 cam, v;
    SithSector* camSector = sithCamera_g_aCameras[2].sector;
    iosPhotoCam_GetPos(&cam, NULL);
    // (the new focus must be in sight of where the camera is, through no
    // wall: its sector is found along that line)
    rdVector_Sub3(&v, focus, &cam);
    const float len = rdVector_Len3(&v);
    SithSector* sector = camSector;
    if (!camSector) return 0;
    if (len > 0.001f && (iosPhotoCam_ClearDist(camSector, &cam, focus, &sector) < len - 0.005f || !sector)) return 0;
    const rdVector3 oldFocus = iosFreeze_focus;
    SithSector* oldSector = iosFreeze_pFocusSector;
    const float yaw = iosFreeze_yaw, pitch = iosFreeze_pitch, want = iosFreeze_dist;
    iosFreeze_focus = *focus;
    iosFreeze_pFocusSector = sector;
    iosFreeze_yaw = fmodf(yawDegrees, 360.0f);
    iosFreeze_pitch = pitchDegrees;
    if (iosFreeze_pitch > IOSFREEZE_PITCH_MAX) iosFreeze_pitch = IOSFREEZE_PITCH_MAX;
    if (iosFreeze_pitch < -IOSFREEZE_PITCH_MAX) iosFreeze_pitch = -IOSFREEZE_PITCH_MAX;
    iosFreeze_dist = dist;
    if (iosFreeze_Place()) {
        iosFreeze_bInsideMe = 0; // (moved: out of the player)
        return 1;
    }
    iosFreeze_focus = oldFocus;
    iosFreeze_pFocusSector = oldSector;
    iosFreeze_yaw = yaw;
    iosFreeze_pitch = pitch;
    iosFreeze_dist = want;
    iosFreeze_Place();
    return 0;
}

float iosFreeze_GetWantedDist(void)
{
    return iosFreeze_dist;
}

// The nearest thing straight ahead (a person, a thrown or fired thing, an
// explosion...), not past the first wall: how far along the view it is, 0 if
// none
static float iosFreeze_ThingAhead(const rdVector3* eye, const rdVector3* fwd, float maxDist)
{
    SithWorld* w = sithWorld_g_pCurrentWorld;
    float best = 0.0f;
    if (!w) return 0.0f;
    for (int i = 0; i < w->numThings; i++) {
        SithThing* t = &w->aThings[i];
        if (t->type != SITH_THING_ACTOR && t->type != SITH_THING_PLAYER && t->type != SITH_THING_WEAPON
            && t->type != SITH_THING_EXPLOSION && t->type != SITH_THING_ITEM && t->type != SITH_THING_CORPSE)
            continue;
        if (t->flags & (SITH_TF_DISABLED | SITH_TF_DESTROYED)) continue; // (not there)
        rdVector3 v;
        rdVector_Sub3(&v, &t->position, eye);
        const float along = rdVector_Dot3(&v, fwd);
        if (along < IOSFREEZE_FOCUS_MIN || along > maxDist || (best > 0.0f && along >= best)) continue;
        rdVector3 off = v;
        rdVector_ScaleAdd3Acc(&off, fwd, -along);
        float r = t->collideSize > 0.02f ? t->collideSize : 0.02f;
        if (rdVector_Len3(&off) <= r * 1.5f) best = along;
    }
    return best;
}

int iosFreeze_Begin(void)
{
    if (iosFreeze_bActive) return 0;
    SithCamera* cur = sithCamera_g_pCurCamera;
    if (!cur || !cur->sector) return 0;
    // (as the player sees it now, shake and all)
    const rdVector3 eye = cur->orient.scale;
    rdVector3 fwd = cur->orient.lvec;
    if (rdVector_Normalize3Acc(&fwd) < 1e-4f) return 0;
    SithSector* eyeSector = cur->sector;

    // The focus: what is being looked at
    rdVector3 farPoint = eye;
    rdVector_ScaleAdd3Acc(&farPoint, &fwd, IOSFREEZE_FOCUS_MAX);
    float wall = iosPhotoCam_ClearDist(eyeSector, &eye, &farPoint, NULL);
    float d = iosFreeze_ThingAhead(&eye, &fwd, wall - IOSFREEZE_WALL_GAP);
    if (d <= 0.0f) d = IOSFREEZE_FOCUS_AHEAD;
    if (d > wall - IOSFREEZE_WALL_GAP) d = wall - IOSFREEZE_WALL_GAP;
    // (right up against something: the focus stays on this side of it, and
    // the camera turns nearly where it is)
    if (d < IOSFREEZE_DIST_MIN) d = wall * 0.5f < IOSFREEZE_DIST_MIN ? wall * 0.5f : IOSFREEZE_DIST_MIN;
    if (d < 0.001f) return 0; // (no room at all)

    if (!iosPhotoCam_Begin(IOSGAME_HOLD_FREEZE)) return 0;
    iosFreeze_focus = eye;
    rdVector_ScaleAdd3Acc(&iosFreeze_focus, &fwd, d);
    iosFreeze_pFocusSector = NULL;
    iosPhotoCam_ClearDist(eyeSector, &eye, &iosFreeze_focus, &iosFreeze_pFocusSector);
    if (!iosFreeze_pFocusSector) iosFreeze_pFocusSector = eyeSector;
    iosFreeze_dist = d;
    iosFreeze_yaw = atan2f(fwd.y, fwd.x) * 180.0f / (float)M_PI;
    iosFreeze_pitch = asinf(fwd.z > 1.0f ? 1.0f : (fwd.z < -1.0f ? -1.0f : fwd.z)) * 180.0f / (float)M_PI;
    if (iosFreeze_pitch > IOSFREEZE_PITCH_MAX) iosFreeze_pitch = IOSFREEZE_PITCH_MAX;
    if (iosFreeze_pitch < -IOSFREEZE_PITCH_MAX) iosFreeze_pitch = -IOSFREEZE_PITCH_MAX;
    // (the lens the player's own view has, so nothing jumps at the start;
    // the photo camera keeps it as degrees top to bottom)
    float fov = IOSPHOTOCAM_FOV_DEFAULT;
    if (cur->rdCamera.fov > 1.0f) {
        const float across = cur->rdCamera.fov * (float)M_PI / 180.0f;
        fov = 2.0f * atanf(tanf(across * 0.5f) * cur->rdCamera.aspectRatio) * 180.0f / (float)M_PI;
    }
    iosFreeze_bActive = 1;
    iosFreeze_bSmooth = 0; // (placed at once; the touch overlay turns it on)
    iosFreeze_holdIn = 0;
    iosFreeze_bInsideMe = cur == &sithCamera_g_aCameras[0];
    iosPhotoCam_SetFov(fov);
    if (!iosFreeze_Place()) {
        // (right up against a wall: from the player's own eye, as the game
        // was drawing it, until it moves somewhere with room)
        iosFreeze_shownDist = d;
        iosPhotoCam_Place(&eye, &iosFreeze_focus, 0.0f, eyeSector);
    }
    return 1;
}

int iosFreeze_Orbit(float yawDegrees, float pitchDegrees)
{
    if (!iosFreeze_bActive) return 0;
    const float yaw = iosFreeze_yaw, pitch = iosFreeze_pitch;
    iosFreeze_yaw = fmodf(iosFreeze_yaw + yawDegrees, 360.0f);
    iosFreeze_pitch += pitchDegrees;
    if (iosFreeze_pitch > IOSFREEZE_PITCH_MAX) iosFreeze_pitch = IOSFREEZE_PITCH_MAX;
    if (iosFreeze_pitch < -IOSFREEZE_PITCH_MAX) iosFreeze_pitch = -IOSFREEZE_PITCH_MAX;
    if (!iosFreeze_Place()) {
        // (nowhere for it that way: it stays)
        iosFreeze_yaw = yaw;
        iosFreeze_pitch = pitch;
        return 0;
    }
    if (yawDegrees != 0.0f || pitchDegrees != 0.0f) iosFreeze_bInsideMe = 0; // (moved out of the player)
    return 1;
}

void iosFreeze_Pan(float right, float down)
{
    if (!iosFreeze_bActive) return;
    // The screen's height at the focus, in the world
    const float tall = 2.0f * (iosFreeze_shownDist > IOSFREEZE_DIST_MIN ? iosFreeze_shownDist : IOSFREEZE_DIST_MIN)
                     * tanf(iosPhotoCam_GetFov() * 0.5f * (float)M_PI / 180.0f);
    rdVector3 dir, r, u, zUp = { 0, 0, 1 };
    iosFreeze_Dir(&dir);
    rdVector_Cross3(&r, &dir, &zUp);
    if (rdVector_Normalize3Acc(&r) < 1e-4f) return;
    rdVector_Cross3(&u, &r, &dir);
    // (what is on screen follows the fingers: the focus goes the other way)
    rdVector3 want = iosFreeze_focus;
    rdVector_ScaleAdd3Acc(&want, &r, -right * tall);
    rdVector_ScaleAdd3Acc(&want, &u, down * tall);
    SithSector* sector = NULL;
    rdVector3 step;
    rdVector_Sub3(&step, &want, &iosFreeze_focus);
    const float len = rdVector_Len3(&step);
    if (len < 1e-6f) return;
    float clear = iosPhotoCam_ClearDist(iosFreeze_pFocusSector, &iosFreeze_focus, &want, &sector);
    if (clear < len) {
        // (up to the wall, short of it)
        float k = (clear - IOSFREEZE_WALL_GAP) / len;
        if (k <= 0.0f) return;
        want = iosFreeze_focus;
        rdVector_ScaleAdd3Acc(&want, &step, k);
        sector = NULL;
        iosPhotoCam_ClearDist(iosFreeze_pFocusSector, &iosFreeze_focus, &want, &sector);
    }
    const rdVector3 focus = iosFreeze_focus;
    SithSector* focusSector = iosFreeze_pFocusSector;
    iosFreeze_focus = want;
    if (sector) iosFreeze_pFocusSector = sector;
    if (!iosFreeze_Place()) {
        // (nowhere for the camera there: it stays)
        iosFreeze_focus = focus;
        iosFreeze_pFocusSector = focusSector;
    }
    else iosFreeze_bInsideMe = 0;
}

void iosFreeze_Zoom(float factor)
{
    if (!iosFreeze_bActive || factor <= 0.0f) return;
    const float half = iosPhotoCam_GetFov() * 0.5f * (float)M_PI / 180.0f;
    iosPhotoCam_SetFov(2.0f * atanf(tanf(half) / factor) * 180.0f / (float)M_PI);
}

void iosFreeze_SetSmooth(int bSmooth)
{
    iosFreeze_bSmooth = bSmooth != 0;
}

void iosFreeze_Frame(void)
{
    // (still gliding back out from a wall, after the wait)
    if (iosFreeze_holdIn > 0) iosFreeze_holdIn--;
    if (iosFreeze_bActive && iosFreeze_bSmooth && iosFreeze_holdIn == 0 && iosFreeze_shownDist < iosFreeze_targetDist - 0.0005f) iosFreeze_Place();
}

void iosFreeze_End(void)
{
    if (!iosFreeze_bActive) return;
    iosFreeze_bActive = 0;
    iosFreeze_bSmooth = 0;
    iosPhotoCam_End();
}

int iosFreeze_HidesThing(SithThing* pThing)
{
    if (!iosFreeze_bActive || !pThing || pThing != sithPlayer_g_pLocalPlayerThing) return 0;
    rdVector3 pos, v;
    iosPhotoCam_GetPos(&pos, NULL);
    rdVector_Sub3(&v, &pos, &pThing->position);
    // Started from first person, until the camera moves: within its height
    // of its middle, inside its width (the head and shoulders round the eye)
    const float r = pThing->collideSize > 0.02f ? pThing->collideSize : 0.02f;
    if (iosFreeze_bInsideMe && rdVector_Len3(&v) < r * 2.0f) return 1;
    // Any time: the camera right by the player's head (a cut-off half face
    // filling the edge of the photo, the near clip through it)
    v.z -= pThing->actorParams.eyeOffset.z > 0.0f ? pThing->actorParams.eyeOffset.z : 0.03f;
    return rdVector_Len3(&v) < IOSFREEZE_NEAR_HEAD;
}

int iosFreeze_IsActive(void)
{
    return iosFreeze_bActive;
}

void iosFreeze_GetState(float* pDist, float* pFov)
{
    if (pDist) *pDist = iosFreeze_shownDist;
    if (pFov) *pFov = iosPhotoCam_GetFov();
}

#endif // TARGET_IOS

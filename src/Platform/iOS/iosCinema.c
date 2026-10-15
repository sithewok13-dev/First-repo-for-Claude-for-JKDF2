#include "Platform/iOS/iosCinema.h"

#ifdef JK_ENHANCED

#include <stdio.h>

#include "Platform/iOS/iosFreeze.h"
#include "Platform/iOS/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosGame.h"
#include "Platform/iOS/iosPhotoCam.h"
#include "World/sithThing.h"
#include "World/sithWorld.h"
#include "Gameplay/sithPlayer.h"
#include "Engine/sithCamera.h"
#include "Primitives/rdVector.h"
#include "jk.h"

#include <math.h>

// The rig's log (not on the device)
#ifdef IOSCINEMA_DEBUG
#define IOSCINEMA_LOG(...) fprintf(stderr, "CINEMA: " __VA_ARGS__)
#else
#define IOSCINEMA_LOG(...) do {} while (0)
#endif

// The plan, worked out once when it starts (all geometry, no extra frames
// drawn; 0.1 to 0.5 ms on the Linux rig in the scenes tried; at most some
// 400 camera places tried, each a few raycasts, if every plan is cramped):
//
// What it turns round. The most interesting thing the player faces: a
// person, a shot in flight or an explosion, near the way they face, in
// their sight (no wall between) and not too far (IOSCINEMA_SUBJECT_*) --
// the way the player faces, not the camera: in third person the camera
// looks at the player, and what they face is well off its middle. Someone close to
// the player: both of them, round the middle between them (a duel, a saber
// clash). Nothing: the player (a hero shot). The player out of sight too:
// as Action Freeze picks (a point straight ahead), as before.
//
// The photos. The first is exactly what the player saw. The other four go
// round what it turns round, from far enough out to frame it, looking down
// on it a little (IOSCINEMA_PITCH_*): one thing, starting from the player's
// side of it; two people, round the side view of them (from the player's
// side), so both stay in the photo; the player alone, round their front.
//
// Which way, how far round. Arcs from the widest (iosCinema_aSweep) down,
// each way, rising a little or more: the widest where the camera has room
// at each photo's place (walls, closed doors: iosFreeze) and a clear view
// of what it turns round from each (no wall, nobody else in the way). None
// like that: the best of them if it is good enough, else the five from
// where it is, zooming in a little each time.
#define IOSCINEMA_SUBJECT_RANGE  2.5f   // (world units; Kyle is some 0.18 tall)
#define IOSCINEMA_SUBJECT_ANGLE 50.0f   // (degrees off the way the player faces)
#define IOSCINEMA_PAIR_RANGE     1.0f   // (both in the photo, nearer than this)
#define IOSCINEMA_DIST_MIN       0.3f   // (how far out it draws back, at least...)
#define IOSCINEMA_DIST_MAX       0.8f   // (...and at most)
#define IOSCINEMA_HERO_DIST      0.3f
#define IOSCINEMA_PITCH_HIGH   -30.0f   // (iosFreeze's pitch, at the start: looking down at most this much...)
#define IOSCINEMA_PITCH_LOW      5.0f   // (...and looking up at most this much)
#define IOSCINEMA_PITCH_END    -40.0f   // (and with the rise, looking down at most this much at the end)
#define IOSCINEMA_HERO_PITCH   -12.0f
static const float iosCinema_aSweep[] = { 90.0f, 70.0f, 50.0f, 35.0f, 20.0f };
static const float iosCinema_aRise[] = { -14.0f, -6.0f }; // (iosFreeze's pitch: less is a camera higher up, looking down)
#define IOSCINEMA_ROOM        0.7f  // (at each place, at least this part of the distance wanted)
#define IOSCINEMA_ROOM_LEAST  0.4f  // (the best of the rest, used if at least this...)
#define IOSCINEMA_SEEN_LEAST  3     // (...and in clear view from this many of the four places)
#define IOSCINEMA_ZOOM_STEP   1.15f // (the fallback: zoomed in this much more each photo)
#define IOSCINEMA_NEAR_HEAD   0.07f // (as iosFreeze's IOSFREEZE_NEAR_HEAD)
// The first photo waits this long (the buttons hidden, a frame drawn
// frozen); the others are taken a frame apart; the last stays on screen
// IOSCINEMA_SHOW; a photo not taken IOSCINEMA_GIVEUP after it was due ends it
#define IOSCINEMA_SETTLE 0.1
#define IOSCINEMA_SHOW   0.5
#define IOSCINEMA_GIVEUP 3.0

static int iosCinema_state = IOSCINEMA_IDLE;
static int iosCinema_taken = 0;
static int iosCinema_bAsked = 0;
static float iosCinema_sweep = 0.0f, iosCinema_rise = 0.0f;
static int iosCinema_dir = 1;
static int iosCinema_subject = IOSCINEMA_SUBJECT_KEPT;
static int iosCinema_bZoomOnly = 0;          // the fallback
static float iosCinema_dist0 = 0.0f, iosCinema_dist1 = 0.0f; // from the focus: at the first photo, at the last
static float iosCinema_yawDone = 0.0f, iosCinema_pitchDone = 0.0f; // turned so far
static double iosCinema_tStart = 0, iosCinema_tStage = 0, iosCinema_tLast = 0;
// What it turns round, and who mustn't be counted as in the way of it
static rdVector3 iosCinema_focus;
static SithThing* iosCinema_aSubject[2];
// The first photo's place (the player's view), and where on the arc the
// others are: from its start (0) or round its middle (0.5, the player alone)
static rdVector3 iosCinema_eye, iosCinema_eyeLook;
static SithSector* iosCinema_pEyeSector = NULL;
static float iosCinema_arcMiddle = 0.0f;
static int iosCinema_bOrbit = 0;
static float iosCinema_startPitch = 0.0f;  // the four go round a new focus (else: on from the player's view, as Action Freeze picked)

// Photo k's place on the arc (0 to 1), k = 1..4
static float iosCinema_ArcAt(int k)
{
    return iosCinema_bOrbit ? (float)(k - 1) / (IOSCINEMA_SHOTS - 2) : (float)k / (IOSCINEMA_SHOTS - 1);
}

static float iosCinema_DistAt(float t)
{
    return iosCinema_dist0 + (iosCinema_dist1 - iosCinema_dist0) * t;
}

// Whether the focus is in clear view from pos (in sector): no wall or closed
// door, and nobody else (a person) across the line
static int iosCinema_Sees(const rdVector3* pos, SithSector* sector)
{
    rdVector3 v;
    rdVector_Sub3(&v, &iosCinema_focus, pos);
    const float len = rdVector_Len3(&v);
    if (len < 0.01f) return 1;
    if (iosPhotoCam_ClearDist(sector, pos, &iosCinema_focus, NULL) < len - 0.03f) return 0;
    rdVector_Scale3Acc(&v, 1.0f / len);
    SithWorld* w = sithWorld_g_pCurrentWorld;
    SithThing* me = sithPlayer_g_pLocalPlayerThing;
    for (int i = 0; w && i < w->numThings; i++) {
        SithThing* t = &w->aThings[i];
        if ((t->type != SITH_THING_ACTOR && t->type != SITH_THING_PLAYER) || (t->flags & (SITH_TF_DISABLED | SITH_TF_DESTROYED))) continue;
        rdVector3 d;
        rdVector_Sub3(&d, &t->position, pos);
        const float r = t->collideSize > 0.02f ? t->collideSize : 0.02f;
        if (t == me) {
            // (the player's body isn't drawn with the camera right by their
            // head (iosFreeze_HidesThing): from this place, not the camera's)
            rdVector3 h = d;
            h.z += me->actorParams.eyeOffset.z > 0.0f ? me->actorParams.eyeOffset.z : 0.03f;
            if (rdVector_Len3(&h) < IOSCINEMA_NEAR_HEAD) continue;
        }
        // (a camera inside someone -- the subjects too -- sees nothing)
        if (rdVector_Len3(&d) < r * 1.2f) return 0;
        if (t == iosCinema_aSubject[0] || t == iosCinema_aSubject[1]) continue;
        const float along = rdVector_Dot3(&d, &v);
        if (along < 0.03f || along > len - 0.03f) continue;
        rdVector_ScaleAdd3Acc(&d, &v, -along);
        if (rdVector_Len3(&d) < r * 1.1f) return 0;
    }
    return 1;
}

// The most interesting thing in view from eye (looking along fwd), and how
// far it is; NULL if none
static SithThing* iosCinema_PickSubject(const rdVector3* eye, const rdVector3* fwd, SithSector* eyeSector, float* pDist)
{
    SithWorld* w = sithWorld_g_pCurrentWorld;
    SithThing* me = sithPlayer_g_pLocalPlayerThing;
    SithThing* best = NULL;
    float bestScore = 0.0f;
    const float cosMax = cosf(IOSCINEMA_SUBJECT_ANGLE * (float)M_PI / 180.0f);
    for (int i = 0; w && i < w->numThings; i++) {
        SithThing* t = &w->aThings[i];
        if (t == me || (t->flags & (SITH_TF_DISABLED | SITH_TF_DESTROYED))) continue;
        float bonus;
        if (t->type == SITH_THING_ACTOR && t->actorParams.health > 0.0f) bonus = 0.2f;
        else if (t->type == SITH_THING_EXPLOSION) bonus = 0.25f;
        else if (t->type == SITH_THING_WEAPON) bonus = 0.15f; // (a shot or a grenade in flight)
        else continue;
        rdVector3 v;
        rdVector_Sub3(&v, &t->position, eye);
        const float dist = rdVector_Len3(&v);
        if (dist < 0.03f || dist > IOSCINEMA_SUBJECT_RANGE) continue;
        const float c = rdVector_Dot3(&v, fwd) / dist;
        IOSCINEMA_LOG("candidate #%d type %d at %.2f, %.0f degrees off%s\n", i, t->type, dist, acosf(c > 1.0f ? 1.0f : c) * 180.0f / (float)M_PI,
                      iosPhotoCam_ClearDist(eyeSector, eye, &t->position, NULL) < dist - 0.03f ? ", behind a wall" : "");
        if (c < cosMax) continue;
        if (iosPhotoCam_ClearDist(eyeSector, eye, &t->position, NULL) < dist - 0.03f) continue; // (behind a wall)
        const float ang = acosf(c > 1.0f ? 1.0f : c) * 180.0f / (float)M_PI;
        const float score = (1.0f - ang / IOSCINEMA_SUBJECT_ANGLE) * 0.55f + (1.0f - dist / IOSCINEMA_SUBJECT_RANGE) * 0.25f + bonus;
        if (score > bestScore) {
            bestScore = score;
            best = t;
            if (pDist) *pDist = dist;
        }
    }
    return best;
}

static void iosCinema_ChoosePath(void);

// The player's view, for the first photo (and where each plan is tried
// from), and what Action Freeze picked there (the plan of last resort)
static rdVector3 iosCinema_beginFocus;
static float iosCinema_beginYaw, iosCinema_beginPitch, iosCinema_beginDist;

static void iosCinema_BackToEye(void)
{
    if (iosCinema_pEyeSector) iosPhotoCam_Place(&iosCinema_eye, &iosCinema_eyeLook, 0.0f, iosCinema_pEyeSector);
}

// One plan: what it turns round (kind; t: the one picked), set up and its
// arc chosen; 1 if it has somewhere good to go (not just zooming)
static int iosCinema_TryPlan(int kind, SithThing* t, float tDist)
{
    SithThing* me = sithPlayer_g_pLocalPlayerThing;
    rdVector3 focus, fwd;
    float dist1, yaw, pitch;
    iosCinema_BackToEye();
    rdVector_Sub3(&fwd, &iosCinema_eyeLook, &iosCinema_eye);
    rdVector_Normalize3Acc(&fwd);
    iosCinema_aSubject[0] = iosCinema_aSubject[1] = NULL;
    iosCinema_arcMiddle = 0.0f;
    iosCinema_bOrbit = 1;
    if (kind == IOSCINEMA_SUBJECT_PAIR) {
        // Both: round the middle between them, far enough out to have them
        // both in the photo (across: the view is wider than tall), side on
        // to the line between them, on the player's view's side
        rdVector3 v, side, toEye;
        rdVector_Sub3(&v, &t->position, &me->position);
        const float apart = rdVector_Len3(&v);
        focus = me->position;
        rdVector_ScaleAdd3Acc(&focus, &v, 0.5f);
        const float tall = tanf(iosPhotoCam_GetFov() * 0.5f * (float)M_PI / 180.0f);
        dist1 = (apart * 0.5f + 0.08f) / (tall * 1.4f);
        side.x = -v.y; side.y = v.x; side.z = 0.0f;
        rdVector_Sub3(&toEye, &iosCinema_eye, &focus);
        if (rdVector_Dot3(&side, &toEye) < 0.0f) rdVector_Neg3Acc(&side);
        yaw = atan2f(-side.y, -side.x) * 180.0f / (float)M_PI;
        pitch = -10.0f;
        iosCinema_arcMiddle = 0.5f;
        iosCinema_aSubject[0] = t;
        iosCinema_aSubject[1] = me;
    }
    else if (kind == IOSCINEMA_SUBJECT_ONE) {
        // One: from the player's side of it
        rdVector3 v;
        focus = t->position;
        dist1 = tDist;
        rdVector_Sub3(&v, &focus, &iosCinema_eye);
        if (rdVector_Normalize3Acc(&v) < 1e-4f) v = fwd;
        yaw = atan2f(v.y, v.x) * 180.0f / (float)M_PI;
        pitch = asinf(v.z > 1.0f ? 1.0f : (v.z < -1.0f ? -1.0f : v.z)) * 180.0f / (float)M_PI;
        iosCinema_aSubject[0] = t;
    }
    else if (kind == IOSCINEMA_SUBJECT_HERO) {
        // The player: round their front
        rdVector3 f = me->orient.lvec;
        focus = me->position;
        dist1 = IOSCINEMA_HERO_DIST;
        yaw = atan2f(-f.y, -f.x) * 180.0f / (float)M_PI;
        pitch = IOSCINEMA_HERO_PITCH;
        iosCinema_arcMiddle = 0.5f;
        iosCinema_aSubject[0] = me;
    }
    else {
        // As Action Freeze picked, on from the player's view (as it was
        // before there were plans)
        focus = iosCinema_beginFocus;
        yaw = iosCinema_beginYaw;
        pitch = iosCinema_beginPitch;
        dist1 = iosCinema_beginDist;
        iosCinema_bOrbit = 0;
        // (whoever it picked, and the player, aren't in the way of it)
        iosCinema_aSubject[0] = t;
        iosCinema_aSubject[1] = me;
    }
    if (kind != IOSCINEMA_SUBJECT_KEPT) {
        if (dist1 < IOSCINEMA_DIST_MIN) dist1 = IOSCINEMA_DIST_MIN;
        if (dist1 > IOSCINEMA_DIST_MAX) dist1 = IOSCINEMA_DIST_MAX;
        focus.z += 0.01f; // (a little above the middle of them: the chest)
        if (pitch < IOSCINEMA_PITCH_HIGH) pitch = IOSCINEMA_PITCH_HIGH;
        if (pitch > IOSCINEMA_PITCH_LOW) pitch = IOSCINEMA_PITCH_LOW;
    }
    if (!iosFreeze_SetOrbit(&focus, yaw, pitch, dist1)) {
        IOSCINEMA_LOG("plan %d: no place to start\n", kind);
        return 0;
    }
    iosCinema_focus = focus;
    iosCinema_dist0 = iosCinema_dist1 = dist1;
    iosCinema_subject = kind;
    iosCinema_startPitch = pitch;
    iosCinema_ChoosePath();
    IOSCINEMA_LOG("plan %d: arc %.0f%s\n", kind, iosCinema_sweep, iosCinema_bZoomOnly ? ", nowhere good: zoom" : "");
    return !iosCinema_bZoomOnly;
}

// What it turns round and how: the best plan with somewhere good to go --
// both people, else the one, else the player, else as before -- else
// zooming in from the player's view
static void iosCinema_Plan(void)
{
    rdVector3 eye = iosCinema_eye, look = iosCinema_eyeLook, fwd;
    iosCinema_subject = IOSCINEMA_SUBJECT_KEPT;
    iosCinema_bZoomOnly = 0;
    iosFreeze_GetFocus(&iosCinema_beginFocus, NULL);
    iosFreeze_GetOrbit(&iosCinema_beginYaw, &iosCinema_beginPitch, &iosCinema_beginDist);
    iosCinema_focus = iosCinema_beginFocus;
    iosCinema_dist0 = iosCinema_dist1 = iosCinema_beginDist;
    rdVector_Sub3(&fwd, &look, &eye);
    rdVector_Normalize3Acc(&fwd);
    SithThing* me = sithPlayer_g_pLocalPlayerThing;
    float d = 0.0f;
    SithThing* t = NULL;
    if (me && me->sector) {
        // (from the player's eyes, the way they face)
        rdVector3 meEye = me->position, meFwd = me->orient.lvec;
        meEye.z += me->actorParams.eyeOffset.z > 0.0f ? me->actorParams.eyeOffset.z : 0.03f;
        SithSector* meSector = NULL;
        iosPhotoCam_ClearDist(me->sector, &me->position, &meEye, &meSector);
        if (rdVector_Normalize3Acc(&meFwd) > 1e-4f && meSector) t = iosCinema_PickSubject(&meEye, &meFwd, meSector, &d);
    }
    else if (iosCinema_pEyeSector) {
        t = iosCinema_PickSubject(&eye, &fwd, iosCinema_pEyeSector, &d);
    }
    int aKind[4], n = 0;
    if (t && me && t->type == SITH_THING_ACTOR) {
        rdVector3 v;
        rdVector_Sub3(&v, &t->position, &me->position);
        if (rdVector_Len3(&v) < IOSCINEMA_PAIR_RANGE) aKind[n++] = IOSCINEMA_SUBJECT_PAIR;
    }
    if (t) aKind[n++] = IOSCINEMA_SUBJECT_ONE;
    if (me) aKind[n++] = IOSCINEMA_SUBJECT_HERO;
    aKind[n++] = IOSCINEMA_SUBJECT_KEPT;
    for (int i = 0; i < n; i++) {
        if (iosCinema_TryPlan(aKind[i], t, d)) return;
    }
    // (nowhere good for any, the last one tried as before: zooming in from
    // the player's view)
    iosCinema_bZoomOnly = 1;
    iosCinema_sweep = iosCinema_rise = 0.0f;
}

// One arc, tried without moving: the least room at its four later places
// (-1: the camera can't be at one), and from how many of them the focus is
// in clear view
static float iosCinema_TryArc(int dir, float sweep, float rise, int* pSeen)
{
    float least = 1.0f;
    *pSeen = 0;
    for (int k = 1; k < IOSCINEMA_SHOTS; k++) {
        const float t = iosCinema_ArcAt(k);
        const float want = iosCinema_DistAt(t);
        rdVector3 pos;
        SithSector* sector = NULL;
        const float d = iosFreeze_ProbeAt(dir * sweep * (t - iosCinema_arcMiddle), rise * t, want, &pos, &sector);
        if (d < 0.0f || !sector) return -1.0f;
        const float room = d / want;
        if (room < least) least = room;
        if (iosCinema_Sees(&pos, sector)) (*pSeen)++;
    }
    return least;
}

static void iosCinema_ChoosePath(void)
{
    const int numSweeps = (int)(sizeof(iosCinema_aSweep) / sizeof(iosCinema_aSweep[0]));
    const int numRises = (int)(sizeof(iosCinema_aRise) / sizeof(iosCinema_aRise[0]));
    float bestScore = -1.0f, bestSweep = 0.0f, bestRise = 0.0f, bestRoom = -1.0f;
    int bestDir = 1, bestSeen = 0;
    for (int s = 0; s < numSweeps; s++) {
        // The widest arc where every place has room and a clear view...
        float okRoom = -1.0f, okRise = 0.0f;
        int okDir = 1;
        for (int r = 0; r < numRises; r++) {
            // (not looking down too steeply by the end)
            float rise = iosCinema_aRise[r];
            if (iosCinema_bOrbit && iosCinema_startPitch + rise < IOSCINEMA_PITCH_END) rise = IOSCINEMA_PITCH_END - iosCinema_startPitch;
            if (rise > 0.0f) rise = 0.0f;
            for (int dir = 1; dir >= -1; dir -= 2) {
                int seen = 0;
                const float room = iosCinema_TryArc(dir, iosCinema_aSweep[s], rise, &seen);
                if (room < 0.0f) continue;
                if (room >= IOSCINEMA_ROOM && seen == IOSCINEMA_SHOTS - 1 && room > okRoom + 0.05f) {
                    okRoom = room;
                    okRise = rise;
                    okDir = dir;
                }
                // (...else the best of the rest: in view first, then room,
                // then wider)
                const float score = seen * 0.5f + room + iosCinema_aSweep[s] / 90.0f * 0.3f;
                if (score > bestScore) {
                    bestScore = score;
                    bestSweep = iosCinema_aSweep[s];
                    bestRise = rise;
                    bestDir = dir;
                    bestRoom = room;
                    bestSeen = seen;
                }
            }
        }
        if (okRoom >= 0.0f) {
            iosCinema_sweep = iosCinema_aSweep[s];
            iosCinema_rise = okRise;
            iosCinema_dir = okDir;
            iosCinema_bZoomOnly = 0;
            return;
        }
    }
    if (bestRoom >= IOSCINEMA_ROOM_LEAST && bestSeen >= IOSCINEMA_SEEN_LEAST) {
        iosCinema_sweep = bestSweep;
        iosCinema_rise = bestRise;
        iosCinema_dir = bestDir;
        iosCinema_bZoomOnly = 0;
        return;
    }
    // Nowhere good to go: from the player's view, zooming in
    iosCinema_sweep = iosCinema_rise = 0.0f;
    iosCinema_dir = 1;
    iosCinema_dist1 = iosCinema_dist0;
    iosCinema_bZoomOnly = 1;
}

int iosCinema_Start(double now)
{
    if (iosCinema_state != IOSCINEMA_IDLE) return IOSCINEMA_CANT;
    // (room for all five in the capture budget first: it never waits frozen)
    if (iosCapture_IsPending() || !iosCapture_HasRoom(IOSCAPTURE_CLEAN, IOSCINEMA_SHOTS)) return IOSCINEMA_BUSY;
    // (the first photo: exactly what the player saw -- the camera as it is
    // before Action Freeze takes over, which may bring it off a wall)
    SithCamera* cur = sithCamera_g_pCurCamera;
    if (!cur || !cur->sector) return IOSCINEMA_CANT;
    iosCinema_eye = cur->orient.scale;
    iosCinema_eyeLook = cur->orient.scale;
    rdVector_Add3Acc(&iosCinema_eyeLook, &cur->orient.lvec);
    iosCinema_pEyeSector = cur->sector;
    if (!iosFreeze_Begin()) return IOSCINEMA_CANT;
    const int bInsideMe = iosFreeze_GetInsideMe();
    iosCinema_Plan();
    iosCinema_BackToEye();
    iosFreeze_SetInsideMe(bInsideMe); // (and the player's body left out again, if it was)
    iosCinema_taken = 0;
    iosCinema_bAsked = 0;
    iosCinema_yawDone = iosCinema_pitchDone = 0.0f;
    iosCinema_tStart = iosCinema_tStage = iosCinema_tLast = now;
    iosCinema_state = IOSCINEMA_TAKING;
    iosPhotos_SetBatch(1); // (one message for them all, numbered names)
    return IOSCINEMA_STARTED;
}

// The camera to photo k's place on the arc
static void iosCinema_MoveTo(int k)
{
    if (iosCinema_bZoomOnly) {
        iosFreeze_Zoom(IOSCINEMA_ZOOM_STEP);
        return;
    }
    iosFreeze_SetInsideMe(0); // (off the player's eye from now on)
    const float t = iosCinema_ArcAt(k);
    const float yaw = iosCinema_dir * iosCinema_sweep * (t - iosCinema_arcMiddle), pitch = iosCinema_rise * t;
    iosFreeze_SetWantedDist(iosCinema_DistAt(t));
    // (a step it can't make, it doesn't: that photo is from the last place,
    // and the next step is from there)
    if (iosFreeze_Orbit(yaw - iosCinema_yawDone, pitch - iosCinema_pitchDone)) {
        iosCinema_yawDone = yaw;
        iosCinema_pitchDone = pitch;
    }
}

int iosCinema_Tick(double now)
{
    if (iosCinema_state == IOSCINEMA_IDLE) return IOSCINEMA_IDLE;
    iosCinema_tLast = now;
    if (!iosFreeze_IsActive()) {
        iosCinema_Stop(); // (ended from elsewhere)
        return iosCinema_state;
    }
    if (iosCinema_state == IOSCINEMA_SHOWING) {
        if (now - iosCinema_tStage >= IOSCINEMA_SHOW) iosCinema_Stop();
        return iosCinema_state;
    }
    // Taken (a frame read it), or given up on by the capture
    if (iosCinema_bAsked && !iosCapture_IsPending()) {
        iosCinema_bAsked = 0;
        iosCinema_taken++;
        iosCinema_tStage = now;
        if (iosCinema_taken >= IOSCINEMA_SHOTS) {
            iosCinema_state = IOSCINEMA_SHOWING;
            return iosCinema_state;
        }
    }
    if (!iosCinema_bAsked && (iosCinema_taken > 0 || now - iosCinema_tStage >= IOSCINEMA_SETTLE)) {
        // (moved, then asked for: the frame drawn next is from the new place)
        if (iosCinema_taken > 0) iosCinema_MoveTo(iosCinema_taken);
        if (iosCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_BURST)) {
            iosCinema_bAsked = 1;
            iosCinema_tStage = now;
        }
    }
    if (now - iosCinema_tStage >= IOSCINEMA_GIVEUP + (iosCinema_taken ? 0.0 : IOSCINEMA_SETTLE)) {
        const int taken = iosCinema_taken;
        iosCinema_Stop();
        char msg[64];
        if (taken) snprintf(msg, sizeof(msg), "Took %d of %d photos", taken, IOSCINEMA_SHOTS);
        else snprintf(msg, sizeof(msg), "Couldn't take the photos");
        iosGame_ShowMessage(msg);
    }
    return iosCinema_state;
}

void iosCinema_Stop(void)
{
    if (iosCinema_state == IOSCINEMA_IDLE) return;
    if (iosCinema_bAsked && iosCapture_IsPending()) iosCapture_Cancel();
    iosCinema_bAsked = 0;
    iosCinema_state = IOSCINEMA_IDLE;
    iosFreeze_End(); // (the game goes on from the same moment)
    // (the game held from the start to now, the last photo's showing too)
    iosCapture_NoteFreeze(iosCinema_tLast - iosCinema_tStart);
    iosPhotos_SetBatch(0);
}

int iosCinema_IsActive(void)
{
    return iosCinema_state != IOSCINEMA_IDLE;
}

int iosCinema_NumTaken(void)
{
    return iosCinema_taken;
}

void iosCinema_GetArc(float* pSweep, int* pDir)
{
    if (pSweep) *pSweep = iosCinema_sweep;
    if (pDir) *pDir = iosCinema_dir;
}

void iosCinema_GetPlan(float* pRise, int* pSubject, float* pDist0, float* pDist1, int* pbZoomOnly)
{
    if (pRise) *pRise = iosCinema_rise;
    if (pSubject) *pSubject = iosCinema_subject;
    if (pDist0) *pDist0 = iosCinema_dist0;
    if (pDist1) *pDist1 = iosCinema_dist1;
    if (pbZoomOnly) *pbZoomOnly = iosCinema_bZoomOnly;
}

#endif // JK_ENHANCED

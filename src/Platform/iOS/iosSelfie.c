#include "Platform/iOS/iosSelfie.h"

#ifdef TARGET_IOS

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "Platform/iOS/iosGame.h"
#include "World/sithThing.h"
#include "World/sithWorld.h"
#include "World/sithSector.h"
#include "Engine/sithCamera.h"
#include "Engine/sithCollision.h"
#include "Engine/rdPuppet.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdVector.h"
#include "Dss/sithMulti.h"
#include "Win95/Window.h"
#include "Engine/rdCamera.h"
#include "stdPlatform.h"
#include "jk.h"

// How near (world units) characters must be to join
#define IOSSELFIE_RANGE 1.0f
// The camera: at least this far from the player's head (an arm's length),
// and no further than IOSSELFIE_CAM_MAX
#define IOSSELFIE_CAM_MIN 0.04f
#define IOSSELFIE_CAM_MAX 0.6f
#define IOSSELFIE_CAM_STEP 0.005f
#define IOSSELFIE_CAM_LIFT 0.008f
// ...or this much above the head, to see over shoulders
#define IOSSELFIE_CAM_LIFT_HIGH 0.035f
// Everyone's head inside this share of the view's half-width and
// half-height (near the middle, with room for the bodies below)
#define IOSSELFIE_FIT_X 0.45f
#define IOSSELFIE_FIT_Y 0.35f
// The selfie camera's lens (degrees top to bottom, as the game's own lens
// is set by default): narrower than the game's 90 (on a wide phone screen
// some 130 across), so faces and hands near it don't bulge as in a fisheye
#ifndef IOSSELFIE_FOV
#define IOSSELFIE_FOV 70.0f
#endif
// The flash: the player gives off this much light while it is up
#define IOSSELFIE_FLASH_LIGHT 1.6f
#define IOSSELFIE_NUM_DIRS 16
// Seen from the camera, a face is hidden behind someone nearer within their
// head (this much of their size: how wide they stand) or their chest and
// shoulders (this much), with this margin (radians) for the face itself
#define IOSSELFIE_HEAD_SIZE 0.6f
#define IOSSELFIE_CHEST_SIZE 1.1f
#define IOSSELFIE_FACE_MARGIN 0.05f
// A wall this close in front of a head doesn't hide it
#define IOSSELFIE_SIGHT_SLACK 0.03f
// Joints a pose turns, per person
#define IOSSELFIE_MAX_TURNS 8
// How far a head (neck and head together) turns from straight ahead to look
// at the camera (radians)
#define IOSSELFIE_NECK_MAX 0.95f

// The rig's log (not on the device)
#ifdef IOSSELFIE_DEBUG
#include <stdio.h>
#define IOSSELFIE_LOG(...) fprintf(stderr, "SELFIE: " __VA_ARGS__)
#else
#define IOSSELFIE_LOG(...) do {} while (0)
#endif

// One person in the selfie and what to put back
typedef struct {
    SithThing* thing;
    uint32_t signature;
    rdMatrix34 orient;      // as it was
    uint32_t flags;         // as they were
    int bPuppet;
    rdPuppet puppet;        // the animation tracks as they were
    int aPart[IOSSELFIE_PART_NUM]; // node index per part, -1: none
    int pose;               // -1: only turns to the camera; else posing...
    int armR, armL;         // ...these gestures (IOSSELFIE_ARM_*; armR -1: the phone, toward the camera)
    flex_t headTilt;        // ...and the head this much on one side (radians)
    flex_t light;           // the light it gave off, as it was
    rdVector3 head;         // where the head is (world)...
    SithSector* headSector; // ...and the sector it is in
    int numTurns;           // the pose: these joints turned this much (only drawn)
    int aTurnNode[IOSSELFIE_MAX_TURNS];
    rdMatrix34 aTurn[IOSSELFIE_MAX_TURNS];
} iosSelfiePerson;

static iosSelfiePerson iosSelfie_aPeople[IOSSELFIE_MAX_PEOPLE + 1]; // [0]: the player
static int iosSelfie_numPeople = 0;
static int iosSelfie_bActive = 0;
static SithCamera* iosSelfie_pPrevCamera = NULL;
// The camera slot the selfie borrows, and the camera shake, as they were
// (scripts can use slot 2; the shake winds down whenever a camera updates)
static SithCamera iosSelfie_savedCamera;
// Blaster bolts, sparks and blasts in front of someone's face: not drawn in
// the selfie (iosSelfie_HidesThing)
#define IOSSELFIE_MAX_HIDDEN 64
#define IOSSELFIE_FACE_RADIUS 0.028f
static SithThing* iosSelfie_apHidden[IOSSELFIE_MAX_HIDDEN];
static int iosSelfie_numHidden = 0;
static rdVector3 iosSelfie_savedShakePos, iosSelfie_savedShakeAngle;
static flex_t iosSelfie_savedShakePosDelta, iosSelfie_savedShakeAngleDelta;
// Which sectors the map had as seen (the selfie's camera looks round places
// the player may not have been yet: it doesn't count)
static unsigned char* iosSelfie_aSectorSeen = NULL;
static int iosSelfie_numSectorSeen = 0;

// ------------------------------------------------------------ the poses

// A pose is a gesture for each arm, picked at random. Each gesture says
// which way the upper arm and the forearm point, for the right arm, in the
// person's own frame (x: their right, y: forwards, to the camera, z: up);
// the left arm uses it mirrored. The joint turns are worked out per body
// from these (iosSelfie_SolvePose), so every gesture fits every humanoid,
// whatever its bones are called or how they are set up.
typedef struct {
    const char* name;
    rdVector3 upper, fore; // both zero: hanging down as in the rest pose
    int bPairOnly;         // only with the same gesture on the other arm
    int weight;            // how often it comes up (arms in the air less often)
    int bGroup;            // one a whole group can do together
} iosSelfieGesture;

enum {
    IOSSELFIE_ARM_DOWN = 0, IOSSELFIE_ARM_WAVE, IOSSELFIE_ARM_FIST, IOSSELFIE_ARM_HIP,
    IOSSELFIE_ARM_POINT, IOSSELFIE_ARM_FLEX, IOSSELFIE_ARM_SALUTE, IOSSELFIE_ARM_BEHINDHEAD,
    IOSSELFIE_ARM_OUT, IOSSELFIE_ARM_UP, IOSSELFIE_ARM_VICTORY, IOSSELFIE_ARM_PRESENT,
    IOSSELFIE_ARM_CROSS, IOSSELFIE_ARM_FISTFWD, IOSSELFIE_ARM_SHRUG, IOSSELFIE_ARM_CHIN,
    IOSSELFIE_ARM_HEART, IOSSELFIE_ARM_NEIGHBOUR, IOSSELFIE_ARM_NUM
};

// (the arm round a neighbour's shoulders is worked out from where the
// neighbour stands: iosSelfie_NeighbourDirs; without one, a hand on the hip)
static iosSelfieGesture iosSelfie_aGesture[IOSSELFIE_ARM_NUM] = {
    [IOSSELFIE_ARM_DOWN]       = { "down",          { 0 }, { 0 }, 0, 3 },
    [IOSSELFIE_ARM_WAVE]       = { "wave",          { 0.55f, 0.15f, 0.80f },  { -0.15f, 0.20f, 1.0f },  0, 2, 1 },
    [IOSSELFIE_ARM_FIST]       = { "fist up",       { 0.20f, 0.10f, 1.0f },   { 0.05f, 0.05f, 1.0f },   0, 1 },
    [IOSSELFIE_ARM_HIP]        = { "hand on hip",   { 0.75f, -0.10f, -0.65f }, { -0.70f, 0.15f, -0.55f }, 0, 4, 1 },
    [IOSSELFIE_ARM_POINT]      = { "point",         { 0.25f, 0.95f, 0.10f },  { 0.20f, 1.0f, 0.15f },   0, 3 },
    [IOSSELFIE_ARM_FLEX]       = { "flex",          { 1.0f, 0.05f, 0.05f },   { 0.0f, 0.15f, 1.0f },    0, 2, 1 },
    [IOSSELFIE_ARM_SALUTE]     = { "salute",        { 0.70f, 0.50f, 0.20f },  { -0.60f, 0.10f, 0.80f }, 0, 1, 1 },
    [IOSSELFIE_ARM_BEHINDHEAD] = { "behind head",   { 0.80f, 0.10f, 0.60f },  { -0.70f, -0.25f, 0.50f }, 0, 1 },
    [IOSSELFIE_ARM_OUT]        = { "arm out",       { 1.0f, 0.10f, -0.20f },  { 1.0f, 0.15f, -0.05f },  0, 2 },
    [IOSSELFIE_ARM_UP]         = { "hand up",       { 0.80f, 0.0f, 0.60f },   { 0.0f, 0.05f, 1.0f },    0, 1 },
    [IOSSELFIE_ARM_VICTORY]    = { "victory",       { 0.60f, 0.10f, 0.80f },  { 0.60f, 0.10f, 0.80f },  0, 1, 1 },
    [IOSSELFIE_ARM_PRESENT]    = { "present",       { 0.20f, 0.30f, -0.90f }, { 0.15f, 1.0f, 0.30f },   0, 2 },
    [IOSSELFIE_ARM_CROSS]      = { "arms crossed",  { 0.15f, 0.45f, -0.85f }, { -1.0f, 0.35f, 0.15f },  1, 3, 1 },
    [IOSSELFIE_ARM_FISTFWD]    = { "fist forward",  { 0.20f, 0.20f, -0.95f }, { 0.05f, 0.85f, 0.50f },  0, 3 },
    [IOSSELFIE_ARM_SHRUG]      = { "shrug",         { 0.55f, 0.0f, -0.85f },  { 0.70f, 0.45f, 0.55f },  0, 3, 1 },
    [IOSSELFIE_ARM_CHIN]       = { "hand on chin",  { 0.15f, 0.55f, -0.80f }, { -0.35f, 0.35f, 0.85f }, 0, 3 },
    [IOSSELFIE_ARM_HEART]      = { "hand on heart", { 0.25f, 0.25f, -0.95f }, { -0.85f, 0.35f, 0.35f }, 0, 2 },
    [IOSSELFIE_ARM_NEIGHBOUR]  = { "arm round",     { 0.75f, -0.10f, -0.65f }, { -0.70f, 0.15f, -0.55f }, 0, 4 },
};

// How far each direction is turned at random, so no two photos are quite
// the same
#define IOSSELFIE_JITTER 0.14f

// The selfie's own random numbers: the game's own (rand) must go on as if
// no selfie was taken
static uint32_t iosSelfie_seed = 0;

static uint32_t iosSelfie_Rand(void)
{
    if (!iosSelfie_seed) iosSelfie_seed = (uint32_t)stdPlatform_GetTimeMsec() * 2654435761u | 1u;
    // xorshift32
    iosSelfie_seed ^= iosSelfie_seed << 13;
    iosSelfie_seed ^= iosSelfie_seed >> 17;
    iosSelfie_seed ^= iosSelfie_seed << 5;
    return iosSelfie_seed;
}

static flex_t iosSelfie_Rand01(void)
{
    return (flex_t)(iosSelfie_Rand() & 0xFFFFFF) / (flex_t)0xFFFFFF;
}

// A gesture at random, by weight (not a paired one unless bPaired)
static int iosSelfie_RandGesture(int bPaired)
{
    int total = 0;
    for (int g = 0; g < IOSSELFIE_ARM_NUM; g++)
        if (bPaired || !iosSelfie_aGesture[g].bPairOnly) total += iosSelfie_aGesture[g].weight;
    int k = (int)(iosSelfie_Rand() % (uint32_t)total);
    for (int g = 0; g < IOSSELFIE_ARM_NUM; g++) {
        if (!bPaired && iosSelfie_aGesture[g].bPairOnly) continue;
        k -= iosSelfie_aGesture[g].weight;
        if (k < 0) return g;
    }
    return IOSSELFIE_ARM_HIP;
}

// A random pose: a gesture for each arm (not both hanging down; a paired
// gesture on both arms). `pose` >= 0 (tests): that gesture on both arms.
static void iosSelfie_PickGestures(int pose, int* pRight, int* pLeft)
{
    if (pose >= 0 && pose < IOSSELFIE_ARM_NUM) {
        *pRight = *pLeft = pose;
        return;
    }
    for (;;) {
        int r = iosSelfie_RandGesture(1);
        int l = iosSelfie_RandGesture(1);
        if (iosSelfie_aGesture[r].bPairOnly || iosSelfie_aGesture[l].bPairOnly) {
            int g = iosSelfie_aGesture[r].bPairOnly ? r : l;
            *pRight = *pLeft = g;
            return;
        }
        if (r == IOSSELFIE_ARM_DOWN && l == IOSSELFIE_ARM_DOWN) continue;
        *pRight = r;
        *pLeft = l;
        return;
    }
}

// For the player: the free arm's gesture (the right one holds the phone)
static int iosSelfie_PickFreeArm(void)
{
    return iosSelfie_RandGesture(0);
}

// Now and then the whole group does the same thing, like a team photo
#define IOSSELFIE_GROUP_CHANCE 5 // one selfie in this many (with 2 or more others)

static int iosSelfie_PickGroupGesture(void)
{
    for (;;) {
        int g = (int)(iosSelfie_Rand() % IOSSELFIE_ARM_NUM);
        if (iosSelfie_aGesture[g].bGroup) return g;
    }
}

// Tuning in the rig: one gesture's upper arm (part 0) or forearm (part 1)
void iosSelfie_SetPosePart(int pose, int part, float right, float forward, float up)
{
    if (pose < 0 || pose >= IOSSELFIE_ARM_NUM || part < 0 || part > 1) return;
    rdVector3 v = { right, forward, up };
    if (part == 0)
        iosSelfie_aGesture[pose].upper = v;
    else
        iosSelfie_aGesture[pose].fore = v;
}

// ------------------------------------------------------------ bodies

static int iosSelfie_EndsWith(const char* s, const char* end)
{
    size_t a = strlen(s), b = strlen(end);
    return a >= b && !strcmp(s + a - b, end);
}

// Which part of a body a node is, from its name (-1: none). The game's
// models name them in a few ways: k_rshouldr / strshouldr / rershouldr /
// rarm (upper arm), k_rforearm / r4arm (forearm), k_rhand / rhand / rwrist,
// k_head / sthead / head, k_neck / neck, k_torso / torso / chest.
static int iosSelfie_PartOf(const char* name)
{
    char n[64];
    int i = 0;
    for (; name[i] && i < (int)sizeof(n) - 1; i++) n[i] = (char)tolower((unsigned char)name[i]);
    n[i] = 0;
    for (int side = 0; side < 2; side++) {
        const char c = side ? 'l' : 'r';
        char up1[16], up2[16], fo1[16], fo2[16], ha1[16], ha2[16];
        snprintf(up1, sizeof(up1), "%cshouldr", c);
        snprintf(up2, sizeof(up2), "%carm", c);
        snprintf(fo1, sizeof(fo1), "%cforearm", c);
        snprintf(fo2, sizeof(fo2), "%c4arm", c);
        snprintf(ha1, sizeof(ha1), "%chand", c);
        snprintf(ha2, sizeof(ha2), "%cwrist", c);
        const int base = side ? IOSSELFIE_PART_LUPPER : IOSSELFIE_PART_RUPPER;
        if (iosSelfie_EndsWith(n, fo1) || iosSelfie_EndsWith(n, fo2)) return base + 1;
        if (iosSelfie_EndsWith(n, up1)) return base;
        if (iosSelfie_EndsWith(n, up2) && !iosSelfie_EndsWith(n, "earm")) return base; // "rarm", not "forearm"
        if (iosSelfie_EndsWith(n, ha1) || iosSelfie_EndsWith(n, ha2)) return base + 2;
    }
    if (iosSelfie_EndsWith(n, "head")) return IOSSELFIE_PART_HEAD;
    if (iosSelfie_EndsWith(n, "neck")) return IOSSELFIE_PART_NECK;
    if (iosSelfie_EndsWith(n, "torso") || iosSelfie_EndsWith(n, "chest")) return IOSSELFIE_PART_TORSO;
    return -1;
}

// A person can pose if both arms are found, upper arm, forearm and hand
// (droids, creatures and walkers only turn to face the camera)
static int iosSelfie_CanPose(const iosSelfiePerson* p)
{
    for (int part = IOSSELFIE_PART_RUPPER; part <= IOSSELFIE_PART_LHAND; part++)
        if (p->aPart[part] < 0) return 0;
    return 1;
}

static int iosSelfie_HasBody(SithThing* t)
{
    rdThing* r = &t->renderData;
    return r->type == RD_THING_MODEL3 && r->model3 && r->paJointMatrices && r->hierarchyNodes2
        && r->model3->numHNodes > 0;
}

static void iosSelfie_FindParts(iosSelfiePerson* p)
{
    rdModel3* m = p->thing->renderData.model3;
    for (int i = 0; i < IOSSELFIE_PART_NUM; i++) p->aPart[i] = -1;
    for (int i = 0; i < m->numHNodes; i++) {
        int part = iosSelfie_PartOf(m->aHierarchyNodes[i].name);
        if (part >= 0 && p->aPart[part] < 0) p->aPart[part] = i;
    }
#ifdef IOSSELFIE_DEBUG
    for (int i = 0; i < IOSSELFIE_PART_NUM; i++) {
        int n = p->aPart[i];
        if (n >= 0)
            IOSSELFIE_LOG("%s part %d = node %d %s (parent %s)\n", m->filename, i, n, m->aHierarchyNodes[n].name,
                          m->aHierarchyNodes[n].parent ? m->aHierarchyNodes[n].parent->name : "-");
    }
#endif
}

// Its joints, built for where it stands now (and drawn the same way).
// Building moves the animation tracks' place-keeping on; that is put back,
// so looking leaves no trace.
static void iosSelfie_Build(SithThing* t)
{
    rdMatrix34 place = t->orient;
    rdVector_Copy3(&place.scale, &t->position);
    rdPuppet* rp = t->renderData.puppet;
    rdPuppet saved;
    if (rp) saved = *rp;
    t->renderData.rdFrameNum = 0;
    rdPuppet_BuildJointMatrices(&t->renderData, &place);
    t->renderData.rdFrameNum = 0; // built again when drawn
    if (rp) *rp = saved;
}

static void iosSelfie_HeadSector(iosSelfiePerson* p)
{
    // from where it stands, up to the head (a sector of its own sometimes)
    rdVector3 head = p->head;
    p->headSector = p->thing->sector
        ? sithCollision_FindSectorInRadius(p->thing->sector, &p->thing->position, &head, 0.0f) : NULL;
    if (!p->headSector) p->headSector = p->thing->sector;
}

// Where its head is now: its head node (or its eyes, failing that)
static void iosSelfie_HeadOf(iosSelfiePerson* p)
{
    SithThing* t = p->thing;
    int head = p->aPart[IOSSELFIE_PART_HEAD];
    if (head >= 0) {
        iosSelfie_Build(t);
        rdVector_Copy3(&p->head, &t->renderData.paJointMatrices[head].scale);
    }
    else {
        rdVector_Copy3(&p->head, &t->position);
        p->head.z += t->actorParams.eyeOffset.z > 0 ? t->actorParams.eyeOffset.z : 0.03f;
    }
    iosSelfie_HeadSector(p);
}

// ------------------------------------------------------------ the camera

// Whether the line from a to b is clear of walls (people and things in
// the way don't count), up to `slack` short of b (a head can touch a sloping
// wall it stands under); *pEnd: the sector b is in, following the line
static int iosSelfie_Clear(SithSector* from, const rdVector3* a, const rdVector3* b, flex_t slack, SithSector** pEnd)
{
    rdVector3 dir, start = *a;
    rdVector_Sub3(&dir, b, a);
    flex_t dist = rdVector_Normalize3Acc(&dir);
    SithSector* s = from;
    int bClear = 1;
    if (pEnd) *pEnd = NULL;
    if (!from) return 0;
    sithCollision_SearchForCollisions(from, NULL, &start, &dir, dist, 0.0f, RAYCAST_2000 | RAYCAST_100 | RAYCAST_20);
    for (SithCollision* c = sithCollision_PopStack(); c; c = sithCollision_PopStack()) {
        // Through an opening between sectors, even one that can't be walked
        // through (bars, a force field): it can be seen through
        if (c->surface && c->surface->pAdjoin && (c->type & (SITHCOLLISION_ADJOINCROSS | SITHCOLLISION_THINGADJOINCROSS))) {
            if (c->type & SITHCOLLISION_ADJOINCROSS) s = c->surface->pAdjoin->sector;
        }
        else if ((c->type & SITHCOLLISION_WORLD) && c->distance < dist - slack) {
            IOSSELFIE_LOG("  blocked: type %x at %.3f of %.3f, thing %p surface %p adjoin %p\n", c->type, (double)c->distance, (double)dist,
                          (void*)c->pThingCollided, (void*)c->surface, c->surface ? (void*)c->surface->pAdjoin : NULL);
            if (c->surface)
                IOSSELFIE_LOG("  surface flags %x geo %d mat %p normal %.2f %.2f %.2f, sector %p (start %p)\n", c->surface->flags,
                              (int)c->surface->surfaceInfo.face.geometryMode, (void*)c->surface->surfaceInfo.face.material,
                              (double)c->surface->surfaceInfo.face.normal.x, (double)c->surface->surfaceInfo.face.normal.y,
                              (double)c->surface->surfaceInfo.face.normal.z, (void*)c->surface->pSector, (void*)from);
            bClear = 0;
            break;
        }
    }
    sithCollision_DecreaseStackLevel();
    if (pEnd) *pEnd = s;
    return bClear && s;
}

typedef struct {
    rdVector3 pos;
    rdVector3 look;
    SithSector* sector;
} iosSelfieCam;

// The camera spot for the people in it: out from the player's head, in the
// direction away from the others (or straight ahead, alone), turned a bit
// either way if that is blocked; as near as everyone fits in the frame.
// Whether, seen from the camera at pos, person i's face is behind someone
// nearer: their head, or the player's chest and shoulders below it
static int iosSelfie_Hidden(const rdVector3* pos, int i)
{
    rdVector3 v;
    rdVector_Sub3(&v, &iosSelfie_aPeople[i].head, pos);
    const flex_t depth = rdVector_Len3(&v);
    if (depth < 0.0001f) return 1;
    rdVector_InvScale3Acc(&v, depth);
    for (int j = 0; j < iosSelfie_numPeople; j++) {
        if (j == i) continue;
        SithThing* t = iosSelfie_aPeople[j].thing;
        flex_t size = t->collideSize > 0.005f ? t->collideSize : 0.02f;
        // (behind another of them in the group, a face only needs to
        // clear their head: peeking over a shoulder is fine)
        for (int k = 0; k < (j == 0 ? 2 : 1); k++) {
            // k 0: the head; 1: the chest, a body's width below it
            rdVector3 b = iosSelfie_aPeople[j].head;
            flex_t radius = size * IOSSELFIE_HEAD_SIZE;
            if (k == 1) {
                b.z -= size;
                radius = size * IOSSELFIE_CHEST_SIZE;
            }
            rdVector3 w;
            rdVector_Sub3(&w, &b, pos);
            flex_t d = rdVector_Len3(&w);
            if (d < 0.0001f || d >= depth) continue; // not in front
            rdVector_InvScale3Acc(&w, d);
            flex_t cover = atanf(radius / d) + IOSSELFIE_FACE_MARGIN;
            if (acosf(fmaxf(-1.0f, fminf(1.0f, rdVector_Dot3(&v, &w)))) < cover) return 1;
        }
    }
    return 0;
}

static int iosSelfie_PlaceCamera(iosSelfieCam* out)
{
    iosSelfiePerson* me = &iosSelfie_aPeople[0];
    rdVector3 mid = {0, 0, 0};
    for (int i = 0; i < iosSelfie_numPeople; i++) rdVector_Add3Acc(&mid, &iosSelfie_aPeople[i].head);
    rdVector_InvScale3Acc(&mid, (flex_t)iosSelfie_numPeople);

    // Away from the others; alone: where the player faces
    rdVector3 away;
    if (iosSelfie_numPeople > 1) {
        rdVector_Sub3(&away, &me->head, &mid);
    }
    else {
        away = me->thing->orient.lvec;
    }
    away.z = 0;
    if (rdVector_Normalize3Acc(&away) < 0.0001f) {
        away = me->thing->orient.lvec;
        away.z = 0;
        rdVector_Normalize3Acc(&away);
    }

    // How far off the middle a head may be, as a share of its distance
    const flex_t tanY = tanf(IOSSELFIE_FOV * 0.5f * (flex_t)M_PI / 180.0f);
    const flex_t tanX = (Window_xSize > 0 && Window_ySize > 0) ? tanY * Window_xSize / Window_ySize : tanY / 0.46f;
    const flex_t fitX = tanX * IOSSELFIE_FIT_X, fitY = tanY * IOSSELFIE_FIT_Y;
    int bestVisible = -1;
    flex_t bestDist = 0;
    // The directions tried, in steps of 360/IOSSELFIE_NUM_DIRS from straight
    // away from the others: a little to the side first (as a selfie is held:
    // the player at one side, the others beside), then straight, then further
    // round
    static const int aStep[IOSSELFIE_NUM_DIRS] = { 2, -2, 1, -1, 3, -3, 0, 4, -4, 5, -5, 6, -6, 7, -7, 8 };
    // Held up high (to see over shoulders) only if not everyone is seen
    // held as usual
    for (int high = 0; high < 2 && bestVisible < iosSelfie_numPeople; high++)
    for (int k = 0; k < IOSSELFIE_NUM_DIRS; k++) {
        int step = aStep[k];
        flex_t a = (flex_t)step * (flex_t)(2.0 * M_PI / IOSSELFIE_NUM_DIRS);
        rdVector3 dir = { away.x * cosf(a) - away.y * sinf(a), away.x * sinf(a) + away.y * cosf(a), 0 };

        for (flex_t D = IOSSELFIE_CAM_MIN; D <= IOSSELFIE_CAM_MAX; D += IOSSELFIE_CAM_STEP) {
            iosSelfieCam c;
            rdVector_Copy3(&c.pos, &me->head);
            rdVector_ScaleAdd3Acc(&c.pos, &dir, D);
            c.pos.z += high ? IOSSELFIE_CAM_LIFT_HIGH : IOSSELFIE_CAM_LIFT;
            c.look = mid;
            // Everyone inside the frame?
            rdVector3 f;
            rdVector_Sub3(&f, &c.look, &c.pos);
            rdVector_Normalize3Acc(&f);
            rdVector3 up = {0, 0, 1}, r, u;
            rdVector_Cross3(&r, &f, &up);
            if (rdVector_Normalize3Acc(&r) < 0.0001f) continue;
            rdVector_Cross3(&u, &r, &f);
            int bFits = 1;
            for (int i = 0; i < iosSelfie_numPeople && bFits; i++) {
                rdVector3 v;
                rdVector_Sub3(&v, &iosSelfie_aPeople[i].head, &c.pos);
                flex_t z = rdVector_Dot3(&v, &f);
                if (z < 0.03f) { bFits = 0; break; }
                if (fabsf(rdVector_Dot3(&v, &r)) / z > fitX) bFits = 0;
                // the head with some of the body below it
                if (fabsf(rdVector_Dot3(&v, &u) - 0.01f) / z > fitY) bFits = 0;
            }
            if (!bFits) continue;
            // Room for it: the player's arm reaches there through open space
            if (!iosSelfie_Clear(me->headSector, &me->head, &c.pos, 0.0f, &c.sector)) {
                IOSSELFIE_LOG("camera try: step %d, %.3f out: no room\n", step, (double)D);
                break; // further only hits it too
            }
            // Who is seen: not behind a wall, nor right behind the player
            int visible = 0;
            rdVector3 cf;
            rdVector_Sub3(&cf, &c.look, &c.pos);
            rdVector_Normalize3Acc(&cf);
            rdVector3 meV;
            rdVector_Sub3(&meV, &me->head, &c.pos);
            const flex_t meDepth = rdVector_Dot3(&meV, &cf);
            for (int i = 0; i < iosSelfie_numPeople; i++) {
                if (!iosSelfie_Clear(c.sector, &c.pos, &iosSelfie_aPeople[i].head, IOSSELFIE_SIGHT_SLACK, NULL)) continue;
                if (i > 0) {
                    // only counted behind the player: it's the player's
                    // selfie, nobody stands between them and the camera
                    rdVector3 hv;
                    rdVector_Sub3(&hv, &iosSelfie_aPeople[i].head, &c.pos);
                    if (rdVector_Dot3(&hv, &cf) < meDepth + 0.012f) continue;
                    if (iosSelfie_Hidden(&c.pos, i)) continue;
                }
                visible++;
            }
            IOSSELFIE_LOG("camera try: %sstep %d, %.3f out, %d of %d seen\n", high ? "high, " : "", step, (double)D, visible, iosSelfie_numPeople);
            if (visible > bestVisible || (visible == bestVisible && D < bestDist - 0.05f)) {
                bestVisible = visible;
                bestDist = D;
                *out = c;
            }
            break; // nearest that fits, in this direction
        }
        if (bestVisible == iosSelfie_numPeople) break; // everyone seen: good enough
    }
    IOSSELFIE_LOG("camera chosen: %.3f out, %d of %d seen\n", (double)bestDist, bestVisible, iosSelfie_numPeople);
    return bestVisible >= 1;
}

// ------------------------------------------------------------ posing

// Turns a person to face the point (round the vertical only)
static void iosSelfie_Face(SithThing* t, const rdVector3* at)
{
    rdVector3 l;
    rdVector_Sub3(&l, at, &t->position);
    l.z = 0;
    if (rdVector_Normalize3Acc(&l) < 0.0001f) return;
    rdVector3 up = {0, 0, 1};
    t->orient.lvec = l;
    t->orient.uvec = up;
    rdVector_Cross3(&t->orient.rvec, &l, &up);
    rdVector_Normalize3Acc(&t->orient.rvec);
}

// Puts someone who doesn't pose on their standing frame (the tracks are set
// directly, so no animation markers run; they are put back afterwards)
static void iosSelfie_Stand(iosSelfiePerson* p)
{
    SithThing* t = p->thing;
    rdPuppet* rp = t->renderData.puppet;
    if (!rp || !t->pPuppetClass || !t->puppet) return;
    int mode = t->puppet->majorMode;
    if (mode < 0 || mode >= 6) mode = 0;
    SithPuppetClassSubmode* sub = &t->pPuppetClass->modes[mode].keyframe[SITH_ANIM_STAND];
    if (!sub->keyframe) return;
    for (int i = 0; i < 4; i++) rp->aTracks[i].status = 0;
    rdPuppetTrack* tr = &rp->aTracks[0];
    memset(tr->aCurKfNodeEntryNums, 0, sizeof(tr->aCurKfNodeEntryNums));
    tr->keyframe = sub->keyframe;
    tr->status = 3;
    tr->lowPri = sub->lowPri;
    tr->highPri = sub->highPri;
    tr->speed = 0;
    tr->noise = 0;
    tr->playSpeed = 1.0f;
    tr->fadeSpeed = 0;
    tr->field_120 = 0;
    tr->field_124 = 0;
    tr->callback = NULL;
    rp->bPaused = 0;
}

// m's turn applied to a direction (no move)
static void iosSelfie_Rotate(rdVector3* out, const rdMatrix34* m, const rdVector3* v)
{
    rdVector3 r;
    r.x = m->rvec.x * v->x + m->lvec.x * v->y + m->uvec.x * v->z;
    r.y = m->rvec.y * v->x + m->lvec.y * v->y + m->uvec.y * v->z;
    r.z = m->rvec.z * v->x + m->lvec.z * v->y + m->uvec.z * v->z;
    *out = r;
}

// ...and back (m is a turn with no scaling)
static void iosSelfie_Unrotate(rdVector3* out, const rdMatrix34* m, const rdVector3* v)
{
    rdVector3 r;
    r.x = rdVector_Dot3(&m->rvec, v);
    r.y = rdVector_Dot3(&m->lvec, v);
    r.z = rdVector_Dot3(&m->uvec, v);
    *out = r;
}

// The shortest turn taking direction a onto direction b (both unit length)
static void iosSelfie_TurnBetween(rdMatrix34* out, const rdVector3* a, const rdVector3* b)
{
    rdVector3 axis;
    rdVector_Cross3(&axis, a, b);
    flex_t s = rdVector_Len3(&axis), c = rdVector_Dot3(a, b);
    rdMatrix_Identity34(out);
    if (s < 1e-5f) {
        if (c > 0) return;
        // opposite: half a turn round any axis square to a
        rdVector3 t = fabsf(a->x) < 0.9f ? (rdVector3){1, 0, 0} : (rdVector3){0, 1, 0};
        rdVector_Cross3(&axis, a, &t);
        s = rdVector_Normalize3Acc(&axis);
        s = 0;
        c = -1;
    }
    else {
        rdVector_InvScale3Acc(&axis, s);
    }
    // Rodrigues: R = I cos + (1 - cos) k k^T + sin [k]x; columns are R's images of x, y, z
    flex_t x = axis.x, y = axis.y, z = axis.z, k = 1 - c;
    out->rvec = (rdVector3){ c + x * x * k,     y * x * k + z * s, z * x * k - y * s };
    out->lvec = (rdVector3){ x * y * k - z * s, c + y * y * k,     z * y * k + x * s };
    out->uvec = (rdVector3){ x * z * k + y * s, y * z * k - x * s, c + z * z * k };
}

// A turn by angle (radians) round a unit axis
static void iosSelfie_AxisAngle(rdMatrix34* out, const rdVector3* axis, flex_t angle)
{
    flex_t c = cosf(angle), s = sinf(angle), k = 1 - c;
    flex_t x = axis->x, y = axis->y, z = axis->z;
    rdMatrix_Identity34(out);
    out->rvec = (rdVector3){ c + x * x * k,     y * x * k + z * s, z * x * k - y * s };
    out->lvec = (rdVector3){ x * y * k - z * s, c + y * y * k,     z * y * k + x * s };
    out->uvec = (rdVector3){ x * z * k + y * s, y * z * k - x * s, c + z * z * k };
}

// A joint's drawn matrix with the pose's turn for it, if any (rdPuppet,
// while a selfie is going: people who pose are drawn from their rest pose)
void iosSelfie_PoseNode(struct rdThing* pThing, int node, struct rdMatrix34* m)
{
    if (!iosSelfie_bActive || !pThing) return;
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[i];
        if (&p->thing->renderData != pThing) continue;
        for (int k = 0; k < p->numTurns; k++) {
            if (p->aTurnNode[k] != node) continue;
            rdMatrix34 r;
            r.scale = m->scale;
            iosSelfie_Rotate(&r.rvec, m, &p->aTurn[k].rvec);
            iosSelfie_Rotate(&r.lvec, m, &p->aTurn[k].lvec);
            iosSelfie_Rotate(&r.uvec, m, &p->aTurn[k].uvec);
            *m = r;
            return;
        }
        return;
    }
}

// Where a joint turns, in the world: its built matrix places the model's
// part, which is offset from the joint by the node's pivot
static void iosSelfie_JointAt(rdVector3* out, const rdThing* r, int node)
{
    const rdMatrix34* w = &r->paJointMatrices[node];
    rdVector3 off;
    iosSelfie_Rotate(&off, w, &r->model3->aHierarchyNodes[node].pivot);
    rdVector_Sub3(out, &w->scale, &off);
}

// Turns one joint so the bone from it to `toward` (the next joint) points
// along the wanted direction (world), then builds the body again
static void iosSelfie_Aim(iosSelfiePerson* p, int node, int toward, const rdVector3* want)
{
    if (node < 0 || toward < 0 || p->numTurns >= IOSSELFIE_MAX_TURNS) return;
    rdThing* r = &p->thing->renderData;
    const rdMatrix34* w = &r->paJointMatrices[node];
    rdVector3 bone, a, b, from, to;
    iosSelfie_JointAt(&from, r, node);
    iosSelfie_JointAt(&to, r, toward);
    rdVector_Sub3(&bone, &to, &from);
    if (rdVector_Normalize3Acc(&bone) < 1e-6f) return;
    // in the joint's own frame, where its turn is applied
    iosSelfie_Unrotate(&a, w, &bone);
    iosSelfie_Unrotate(&b, w, want);
    if (rdVector_Normalize3Acc(&a) < 1e-6f || rdVector_Normalize3Acc(&b) < 1e-6f) return;
    p->aTurnNode[p->numTurns] = node;
    iosSelfie_TurnBetween(&p->aTurn[p->numTurns], &a, &b);
    p->numTurns++;
    iosSelfie_Build(p->thing);
}

// A direction turned a little at random
static void iosSelfie_Jitter(rdVector3* v)
{
    rdVector3 r = { iosSelfie_Rand01() - 0.5f, iosSelfie_Rand01() - 0.5f, iosSelfie_Rand01() - 0.5f };
    rdVector_ScaleAdd3Acc(v, &r, 2.0f * IOSSELFIE_JITTER);
    rdVector_Normalize3Acc(v);
}

// One arm's gesture (side 0: right, 1: left, mirrored) as world directions;
// 0 if it hangs down
static int iosSelfie_GestureDirs(const iosSelfiePerson* p, int gesture, int side, rdVector3* pUpper, rdVector3* pFore)
{
    const iosSelfieGesture* g = &iosSelfie_aGesture[gesture];
    if (!g->upper.x && !g->upper.y && !g->upper.z) return 0;
    const SithThing* t = p->thing;
    const rdVector3* aDir[2] = { &g->upper, &g->fore };
    rdVector3* aOut[2] = { pUpper, pFore };
    for (int k = 0; k < 2; k++) {
        rdVector3 d = *aDir[k];
        if (side) d.x = -d.x;
        rdVector3 w = { 0, 0, 0 };
        rdVector_ScaleAdd3Acc(&w, &t->orient.rvec, d.x);
        rdVector_ScaleAdd3Acc(&w, &t->orient.lvec, d.y);
        rdVector_ScaleAdd3Acc(&w, &t->orient.uvec, d.z);
        if (rdVector_Normalize3Acc(&w) < 1e-6f) return 0;
        iosSelfie_Jitter(&w);
        *aOut[k] = w;
    }
    return 1;
}

// The arm round a neighbour's shoulders: the nearest other person on that
// side, close enough to reach; 0 if there is none
#define IOSSELFIE_NEIGHBOUR_REACH 0.09f
static int iosSelfie_NeighbourDirs(const iosSelfiePerson* p, int side, rdVector3* pUpper, rdVector3* pFore)
{
    const SithThing* t = p->thing;
    const iosSelfiePerson* best = NULL;
    flex_t bestDist = IOSSELFIE_NEIGHBOUR_REACH;
    for (int j = 0; j < iosSelfie_numPeople; j++) {
        const iosSelfiePerson* q = &iosSelfie_aPeople[j];
        if (q == p) continue;
        rdVector3 d;
        rdVector_Sub3(&d, &q->thing->position, &t->position);
        d.z = 0;
        flex_t across = rdVector_Dot3(&d, &t->orient.rvec);
        if (side ? across > -0.01f : across < 0.01f) continue; // not on that side
        flex_t dist = rdVector_Len3(&d);
        if (dist < bestDist) {
            bestDist = dist;
            best = q;
        }
    }
    if (!best) return 0;
    // the upper arm toward their shoulders, the forearm on round behind them
    rdVector3 shoulder, target;
    iosSelfie_JointAt(&shoulder, &t->renderData, p->aPart[side ? IOSSELFIE_PART_LUPPER : IOSSELFIE_PART_RUPPER]);
    target = best->head;
    target.z -= 0.012f;
    rdVector_Sub3(pUpper, &target, &shoulder);
    if (rdVector_Normalize3Acc(pUpper) < 1e-6f) return 0;
    *pFore = *pUpper;
    rdVector_ScaleAdd3Acc(pFore, &t->orient.lvec, -0.7f);
    pFore->z -= 0.2f;
    rdVector_Normalize3Acc(pFore);
    return 1;
}

// Works out the joint turns for the person's pose, from their rest pose:
// each arm, upper arm first; then the neck and head to look straight into
// the camera, the head tilted a little to one side
static void iosSelfie_SolvePose(iosSelfiePerson* p, const rdVector3* camPos)
{
    SithThing* t = p->thing;
    rdThing* r = &t->renderData;
    p->numTurns = 0;
    iosSelfie_Build(t);
    if (p->pose < 0) return;
    for (int side = 0; side < 2; side++) {
        const int partUp = side ? IOSSELFIE_PART_LUPPER : IOSSELFIE_PART_RUPPER;
        rdVector3 up, fore;
        if (!side && p->armR < 0) {
            // The phone: the whole arm reaching straight for the camera
            rdVector3 shoulder;
            iosSelfie_JointAt(&shoulder, r, p->aPart[partUp]);
            rdVector_Sub3(&up, camPos, &shoulder);
            if (rdVector_Normalize3Acc(&up) < 1e-6f) continue;
            fore = up;
        }
        else if ((side ? p->armL : p->armR) == IOSSELFIE_ARM_NEIGHBOUR && iosSelfie_NeighbourDirs(p, side, &up, &fore)) {
            // (round the neighbour's shoulders)
        }
        else if (!iosSelfie_GestureDirs(p, side ? p->armL : p->armR, side, &up, &fore)) {
            continue;
        }
        iosSelfie_Aim(p, p->aPart[partUp], p->aPart[partUp + 1], &up);
        iosSelfie_Aim(p, p->aPart[partUp + 1], p->aPart[partUp + 2], &fore);
    }

    // The face points along the body's forwards in the rest pose. The neck
    // turns it half way to the camera, the head the rest of the way.
    rdVector3 face = t->orient.lvec;
    const int aNode[2] = { p->aPart[IOSSELFIE_PART_NECK], p->aPart[IOSSELFIE_PART_HEAD] };
    // Where to look: the camera, but no further round from straight ahead
    // than a neck turns (the body is already turned to the camera)
    rdVector3 look = { 0, 0, 0 };
    {
        const int at = aNode[1] >= 0 ? aNode[1] : aNode[0];
        if (at >= 0) {
            rdVector3 from;
            iosSelfie_JointAt(&from, r, at);
            rdVector_Sub3(&look, camPos, &from);
            rdVector_Normalize3Acc(&look);
            flex_t ang = acosf(fmaxf(-1.0f, fminf(1.0f, rdVector_Dot3(&look, &face))));
            if (ang > IOSSELFIE_NECK_MAX) {
                // part way from straight ahead toward the camera
                rdVector3 axis;
                rdVector_Cross3(&axis, &face, &look);
                if (rdVector_Normalize3Acc(&axis) > 1e-6f) {
                    rdMatrix34 turn;
                    iosSelfie_AxisAngle(&turn, &axis, IOSSELFIE_NECK_MAX);
                    iosSelfie_Rotate(&look, &turn, &face);
                }
            }
        }
    }
    for (int k = 0; k < 2; k++) {
        const int node = aNode[k];
        if (node < 0 || p->numTurns >= IOSSELFIE_MAX_TURNS) continue;
        const rdMatrix34* w = &r->paJointMatrices[node];
        rdVector3 to = look, a, b;
        if (rdVector_Len3(&to) < 1e-6f) continue;
        iosSelfie_Unrotate(&a, w, &face);
        iosSelfie_Unrotate(&b, w, &to);
        if (rdVector_Normalize3Acc(&a) < 1e-6f || rdVector_Normalize3Acc(&b) < 1e-6f) continue;
        if (k == 0 && aNode[1] >= 0) {
            // the neck: half way (the head does the rest)
            rdVector_Add3Acc(&b, &a);
            if (rdVector_Normalize3Acc(&b) < 1e-6f) continue;
        }
        rdMatrix34 turn;
        iosSelfie_TurnBetween(&turn, &a, &b);
        if (k == 1 && p->headTilt != 0) {
            // the head tilted round its line of sight
            rdMatrix34 roll, both;
            iosSelfie_AxisAngle(&roll, &b, p->headTilt);
            iosSelfie_Rotate(&both.rvec, &roll, &turn.rvec);
            iosSelfie_Rotate(&both.lvec, &roll, &turn.lvec);
            iosSelfie_Rotate(&both.uvec, &roll, &turn.uvec);
            both.scale = turn.scale;
            turn = both;
        }
        // where the face now points, for the head after the neck
        rdVector3 faceNow;
        iosSelfie_Rotate(&faceNow, &turn, &a);
        iosSelfie_Rotate(&face, w, &faceNow);
        p->aTurnNode[p->numTurns] = node;
        p->aTurn[p->numTurns] = turn;
        p->numTurns++;
        iosSelfie_Build(t);
    }
}

// ------------------------------------------------------------ begin, end

static int iosSelfie_ByDistance(const void* a, const void* b)
{
    const flex_t* x = (const flex_t*)a;
    const flex_t* y = (const flex_t*)b;
    return (*x > *y) - (*x < *y);
}

int iosSelfie_Begin(int pose, int* pNumPeople)
{
    SithThing* me = sithPlayer_g_pLocalPlayerThing;
    SithWorld* w = sithWorld_g_pCurrentWorld;
    if (pNumPeople) *pNumPeople = 0;
    if (iosSelfie_bActive || !me || !w || sithNet_isMulti || !iosSelfie_HasBody(me)) return 0;
    if (me->flags & SITH_TF_DEAD) return 0;
    // Only from the player's own view, first or third person (not a
    // scripted camera, a scope, a security camera or the idle camera)
    if (sithCamera_g_pCurCamera != &sithCamera_g_aCameras[0] && sithCamera_g_pCurCamera != &sithCamera_g_aCameras[1])
        return 0;

    iosSelfiePerson meTmp;
    memset(&meTmp, 0, sizeof(meTmp));
    meTmp.thing = me;
    iosSelfie_FindParts(&meTmp);
    iosSelfie_HeadOf(&meTmp);

    // Who is near: alive, with a body, in sight (head to head, past walls
    // only); the nearest IOSSELFIE_MAX_PEOPLE
    struct { flex_t d; int idx; } aNear[256];
    int numNear = 0;
    for (int i = 0; i < w->numThings && numNear < 256; i++) {
        SithThing* t = &w->aThings[i];
        if (t == me || !t->type) continue;
        rdVector3 v;
        rdVector_Sub3(&v, &t->position, &me->position);
        flex_t d = rdVector_Len3(&v);
        if (d > IOSSELFIE_RANGE) continue;
        if (t->type != SITH_THING_ACTOR) {
            if (t->renderData.type == RD_THING_MODEL3 && t->renderData.model3 && d < 0.4f)
                IOSSELFIE_LOG("thing %d at %.3f: type %d (%s), not an actor\n", i, (double)d, t->type, t->renderData.model3->filename);
            continue;
        }
        if ((t->flags & (SITH_TF_DEAD | SITH_TF_DESTROYED | SITH_TF_DISABLED | SITH_TF_10))
            || t->actorParams.health <= 0 || !iosSelfie_HasBody(t) || !t->sector) {
            IOSSELFIE_LOG("thing %d at %.3f: left out (flags %x)\n", i, (double)d, t->flags);
            continue;
        }
        iosSelfiePerson tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.thing = t;
        iosSelfie_FindParts(&tmp);
        iosSelfie_HeadOf(&tmp);
        if (!iosSelfie_Clear(meTmp.headSector, &meTmp.head, &tmp.head, IOSSELFIE_SIGHT_SLACK, NULL)) {
            IOSSELFIE_LOG("thing %d at %.3f: not in sight\n", i, (double)d);
            continue;
        }
        IOSSELFIE_LOG("thing %d at %.3f: joins (%s)\n", i, (double)d, t->renderData.model3->filename);
        aNear[numNear].d = d;
        aNear[numNear].idx = i;
        numNear++;
    }
    qsort(aNear, numNear, sizeof(aNear[0]), iosSelfie_ByDistance);
    if (numNear > IOSSELFIE_MAX_PEOPLE) numNear = IOSSELFIE_MAX_PEOPLE;

    memset(iosSelfie_aPeople, 0, sizeof(iosSelfie_aPeople));
    iosSelfie_numPeople = 1 + numNear;
    iosSelfie_aPeople[0] = meTmp;
    for (int i = 0; i < numNear; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[1 + i];
        p->thing = &w->aThings[aNear[i].idx];
        iosSelfie_FindParts(p);
        iosSelfie_HeadOf(p);
    }

    iosSelfieCam cam;
    if (!iosSelfie_PlaceCamera(&cam)) {
        iosSelfie_numPeople = 0;
        return 0;
    }

    // From here on everything changed is saved first, and put back by End
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[i];
        SithThing* t = p->thing;
        p->signature = t->signature;
        p->orient = t->orient;
        p->flags = t->flags;
        p->bPuppet = t->renderData.puppet != NULL;
        if (p->bPuppet) p->puppet = *t->renderData.puppet;
    }
    free(iosSelfie_aSectorSeen);
    iosSelfie_numSectorSeen = w->numSectors;
    iosSelfie_aSectorSeen = (unsigned char*)malloc(w->numSectors > 0 ? w->numSectors : 1);
    for (int i = 0; iosSelfie_aSectorSeen && i < w->numSectors; i++)
        iosSelfie_aSectorSeen[i] = (w->aSectors[i].flags & SITH_SECTOR_SEEN) != 0;
    iosSelfie_bActive = 1;
    // Now and then everyone does the same, like a team photo
    int group = -1;
    if (pose < 0 && iosSelfie_numPeople >= 3 && iosSelfie_Rand() % IOSSELFIE_GROUP_CHANCE == 0)
        group = iosSelfie_PickGroupGesture();
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[i];
        SithThing* t = p->thing;
        p->light = t->light;
        iosSelfie_Face(t, &cam.pos);
        p->headTilt = (iosSelfie_Rand01() - 0.5f) * 0.35f;
        if (i == 0) {
            p->pose = 0;
            p->armR = -1; // the phone
            p->armL = pose >= 0 ? pose : iosSelfie_PickFreeArm();
            if (group >= 0) p->armL = iosSelfie_aGesture[group].bPairOnly ? IOSSELFIE_ARM_HIP : group;
            t->flags &= ~SITH_TF_RENDERWEAPON; // holstered, for the photo only
            // The flash: light from the phone in the player's hand
            t->flags |= SITH_TF_EMITLIGHT;
            if (t->light < IOSSELFIE_FLASH_LIGHT) t->light = IOSSELFIE_FLASH_LIGHT;
        }
        else {
            p->pose = 0;
            if (group >= 0)
                p->armR = p->armL = group;
            else
                iosSelfie_PickGestures(pose, &p->armR, &p->armL);
        }
        // Posed from the model's rest pose (standing, arms down: the same
        // start for every pose and every body); anyone who can't pose on
        // their standing frame
        if (!iosSelfie_CanPose(p) || !t->renderData.puppet) p->pose = -1;
        if (p->pose >= 0)
            t->renderData.puppet->bPaused = 1;
        else
            iosSelfie_Stand(p);
        iosSelfie_SolvePose(p, &cam.pos);
        IOSSELFIE_LOG("person %d (%s): %s / %s, %d joints turned\n", i, t->renderData.model3->filename,
                      p->pose < 0 ? "-" : (p->armR < 0 ? "phone" : iosSelfie_aGesture[p->armR].name),
                      p->pose < 0 ? "-" : iosSelfie_aGesture[p->armL].name, p->numTurns);
    }

    // Shots and blasts frozen in front of a face are left out of the photo
    // (the rest of the action stays in it)
    iosSelfie_numHidden = 0;
    {
        rdVector3 f;
        rdVector_Sub3(&f, &cam.look, &cam.pos);
        rdVector_Normalize3Acc(&f);
        for (int i = 0; i < w->numThings && iosSelfie_numHidden < IOSSELFIE_MAX_HIDDEN; i++) {
            SithThing* t = &w->aThings[i];
            if (t->type != SITH_THING_WEAPON && t->type != SITH_THING_EXPLOSION
                && t->type != SITH_THING_PARTICLE && t->type != SITH_THING_DEBRIS) continue;
            rdVector3 v;
            rdVector_Sub3(&v, &t->position, &cam.pos);
            flex_t z = rdVector_Dot3(&v, &f);
            if (z < 0.002f || z > IOSSELFIE_CAM_MAX + IOSSELFIE_RANGE) continue;
            rdVector_Normalize3Acc(&v);
            for (int k = 0; k < iosSelfie_numPeople; k++) {
                rdVector3 hv;
                rdVector_Sub3(&hv, &iosSelfie_aPeople[k].head, &cam.pos);
                flex_t zh = rdVector_Dot3(&hv, &f);
                if (zh <= z) continue; // behind the face
                flex_t dist = rdVector_Normalize3Acc(&hv);
                if (acosf(fminf(1.0f, rdVector_Dot3(&v, &hv))) < atanf(IOSSELFIE_FACE_RADIUS / dist)) {
                    iosSelfie_apHidden[iosSelfie_numHidden++] = t;
                    break;
                }
            }
        }
    }

    SithCamera* c = &sithCamera_g_aCameras[2];
    iosSelfie_pPrevCamera = sithCamera_g_pCurCamera;
    iosSelfie_savedCamera = *c;
    iosSelfie_savedShakePos = sithCamera_g_vecCameraPosOffset;
    iosSelfie_savedShakeAngle = sithCamera_g_vecCameraAngleOffset;
    iosSelfie_savedShakePosDelta = sithCamera_g_cameraPosDelta;
    iosSelfie_savedShakeAngleDelta = sithCamera_g_cameraAngleDelta;
    c->pSecondaryFocusThing = NULL;
    rdMatrix_LookAt(&c->orient, &cam.pos, &cam.look, 0.0f);
    c->sector = cam.sector;
    c->pPrimaryFocusThing = me;
    sithCamera_SetCurrentCamera(c);
    iosSelfie_SetLens(&c->rdCamera);

    iosGame_SetHoldReason(IOSGAME_HOLD_SELFIE, 1);
    if (pNumPeople) *pNumPeople = iosSelfie_numPeople - 1;
    return 1;
}

void iosSelfie_Frame(void)
{
    if (!iosSelfie_bActive) return;
    for (int i = 0; i < iosSelfie_numPeople; i++)
        iosSelfie_aPeople[i].thing->renderData.rdFrameNum = 0;
}

void iosSelfie_End(void)
{
    if (!iosSelfie_bActive) return;
    iosSelfie_bActive = 0; // (drawn without the poses from now on)
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[i];
        SithThing* t = p->thing;
        if (t->signature != p->signature) continue;
        t->orient = p->orient;
        t->flags = p->flags;
        t->light = p->light;
        if (p->bPuppet && t->renderData.puppet) *t->renderData.puppet = p->puppet;
        t->renderData.rdFrameNum = 0;
    }
    iosSelfie_numPeople = 0;
    iosSelfie_numHidden = 0;
    SithWorld* w = sithWorld_g_pCurrentWorld;
    if (w && iosSelfie_aSectorSeen && w->numSectors == iosSelfie_numSectorSeen) {
        for (int i = 0; i < w->numSectors; i++) {
            if (!iosSelfie_aSectorSeen[i]) w->aSectors[i].flags &= ~SITH_SECTOR_SEEN;
        }
    }
    free(iosSelfie_aSectorSeen);
    iosSelfie_aSectorSeen = NULL;
    sithCamera_g_aCameras[2] = iosSelfie_savedCamera;
    rdCamera_BuildFOV(&sithCamera_g_aCameras[2].rdCamera); // (its view shape, from the lens put back)
    if (iosSelfie_pPrevCamera) sithCamera_SetCurrentCamera(iosSelfie_pPrevCamera);
    iosSelfie_pPrevCamera = NULL;
    // (switching back updates the camera, which winds the shake down: put
    // it back after)
    sithCamera_g_vecCameraPosOffset = iosSelfie_savedShakePos;
    sithCamera_g_vecCameraAngleOffset = iosSelfie_savedShakeAngle;
    sithCamera_g_cameraPosDelta = iosSelfie_savedShakePosDelta;
    sithCamera_g_cameraAngleDelta = iosSelfie_savedShakeAngleDelta;
    iosGame_SetHoldReason(IOSGAME_HOLD_SELFIE, 0);
}

#ifdef IOSSELFIE_DEBUG
// The rig: everyone in the selfie drawn in their model's rest pose
void iosSelfie_DebugRest(void)
{
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfie_aPeople[i].numTurns = 0;
        rdPuppet* rp = iosSelfie_aPeople[i].thing->renderData.puppet;
        if (rp) rp->bPaused = 1;
        iosSelfie_aPeople[i].thing->renderData.rdFrameNum = 0;
    }
}

// The rig: works the poses out again (after posepart changes)
void iosSelfie_DebugRepose(int pose)
{
    SithCamera* c = &sithCamera_g_aCameras[2];
    for (int i = 0; i < iosSelfie_numPeople; i++) {
        iosSelfiePerson* p = &iosSelfie_aPeople[i];
        if (i > 0 && p->pose >= 0 && pose >= 0) p->pose = pose;
        iosSelfie_SolvePose(p, &c->orient.scale);
    }
}

// The rig: the camera close in front of one person in the selfie, to tune poses
void iosSelfie_DebugFocus(int person, float dist, float side)
{
    if (!iosSelfie_bActive || person < 0 || person >= iosSelfie_numPeople) return;
    iosSelfiePerson* p = &iosSelfie_aPeople[person];
    SithThing* t = p->thing;
    rdVector3 look = p->head;
    look.z -= 0.012f; // the chest
    rdVector3 pos = look;
    rdVector_ScaleAdd3Acc(&pos, &t->orient.lvec, dist);
    rdVector_ScaleAdd3Acc(&pos, &t->orient.rvec, side);
    SithCamera* c = &sithCamera_g_aCameras[2];
    SithSector* sec = NULL;
    iosSelfie_Clear(p->headSector, &look, &pos, 0.0f, &sec);
    rdMatrix_LookAt(&c->orient, &pos, &look, 0.0f);
    if (sec) c->sector = sec;
}
#endif

int iosSelfie_HidesThing(SithThing* pThing)
{
    if (!iosSelfie_bActive) return 0;
    for (int i = 0; i < iosSelfie_numHidden; i++)
        if (iosSelfie_apHidden[i] == pThing) return 1;
    return 0;
}

int iosSelfie_SetLens(struct rdCamera* pCamera)
{
    if (!iosSelfie_bActive || !pCamera || pCamera != &sithCamera_g_aCameras[2].rdCamera) return 0;
    // (a lens is kept as degrees across: from top to bottom, through the
    // view's shape, whichever way round the player's own lens is set)
    flex_t across = IOSSELFIE_FOV;
    if (pCamera->aspectRatio > 0.0001f)
        across = 2.0f * atanf(tanf(IOSSELFIE_FOV * 0.5f * (flex_t)M_PI / 180.0f) / pCamera->aspectRatio) * 180.0f / (flex_t)M_PI;
    pCamera->fov = across;
    rdCamera_BuildFOV(pCamera);
    return 1;
}

int iosSelfie_IsActive(void)
{
    return iosSelfie_bActive;
}

#endif // TARGET_IOS

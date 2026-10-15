#include "Platform/iOS/iosPhotoCam.h"

#ifdef JK_ENHANCED

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "Platform/iOS/iosGame.h"
#include "World/sithWorld.h"
#include "World/sithSector.h"
#include "Engine/sithCamera.h"
#include "Engine/sithCollision.h"
#include "Engine/rdCamera.h"
#include "Engine/rdPuppet.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdVector.h"
#include "Dss/sithMulti.h"
#include "jk.h"

static int iosPhotoCam_bActive = 0;
static int iosPhotoCam_holdReason = 0;
static float iosPhotoCam_fov = IOSPHOTOCAM_FOV_DEFAULT;
static rdVector3 iosPhotoCam_pos, iosPhotoCam_look;

// What it borrows, as it was
static SithCamera* iosPhotoCam_pPrevCamera = NULL;
static SithCamera iosPhotoCam_savedCamera;
static rdVector3 iosPhotoCam_savedShakePos, iosPhotoCam_savedShakeAngle;
static flex_t iosPhotoCam_savedShakePosDelta, iosPhotoCam_savedShakeAngleDelta;
// Each sector's "seen on the map" flag as it was (looking from the camera
// must not mark the map: see sithRender's SITHRENDER_PHOTO_LOOKING)
static unsigned char* iosPhotoCam_aSectorSeen = NULL;
static int iosPhotoCam_numSectorSeen = 0;
// Every animated thing's animation tracks as they were: drawing a thing
// works out its pose from them, and keeps in them where its keyframe search
// got to (rdPuppet_BuildJointMatrices); a thing first drawn by the photo
// camera (seen from where the player can't) would keep that afterwards.
// Harmless as far as the code shows (only where the next search starts),
// but put back all the same, so the game goes on exactly as it was.
typedef struct {
    rdPuppet* puppet;
    unsigned char aTracks[sizeof(((rdPuppet*)0)->aTracks)];
} iosPhotoCamTracks;
static iosPhotoCamTracks* iosPhotoCam_aTracks = NULL;
static int iosPhotoCam_numThingsSaved = 0;

int iosPhotoCam_Begin(int holdReason)
{
    SithWorld* w = sithWorld_g_pCurrentWorld;
    if (iosPhotoCam_bActive || !w || sithNet_isMulti) return 0;
    // Only from the player's own view, first or third person (not a
    // scripted camera, a scope, a security camera or the idle camera)
    if (sithCamera_g_pCurCamera != &sithCamera_g_aCameras[0] && sithCamera_g_pCurCamera != &sithCamera_g_aCameras[1])
        return 0;

    free(iosPhotoCam_aSectorSeen);
    iosPhotoCam_numSectorSeen = w->numSectors;
    iosPhotoCam_aSectorSeen = (unsigned char*)malloc(w->numSectors > 0 ? w->numSectors : 1);
    if (!iosPhotoCam_aSectorSeen) return 0;
    for (int i = 0; i < w->numSectors; i++)
        iosPhotoCam_aSectorSeen[i] = (w->aSectors[i].flags & SITH_SECTOR_SEEN) != 0;
    free(iosPhotoCam_aTracks);
    iosPhotoCam_numThingsSaved = w->numThings;
    iosPhotoCam_aTracks = (iosPhotoCamTracks*)calloc(w->numThings > 0 ? w->numThings : 1, sizeof(iosPhotoCamTracks));
    if (!iosPhotoCam_aTracks) {
        free(iosPhotoCam_aSectorSeen);
        iosPhotoCam_aSectorSeen = NULL;
        return 0;
    }
    for (int i = 0; i < w->numThings; i++) {
        rdPuppet* p = w->aThings[i].type ? w->aThings[i].renderData.puppet : NULL;
        iosPhotoCam_aTracks[i].puppet = p;
        if (p) memcpy(iosPhotoCam_aTracks[i].aTracks, p->aTracks, sizeof(p->aTracks));
    }

    SithCamera* c = &sithCamera_g_aCameras[2];
    iosPhotoCam_pPrevCamera = sithCamera_g_pCurCamera;
    iosPhotoCam_savedCamera = *c;
    iosPhotoCam_savedShakePos = sithCamera_g_vecCameraPosOffset;
    iosPhotoCam_savedShakeAngle = sithCamera_g_vecCameraAngleOffset;
    iosPhotoCam_savedShakePosDelta = sithCamera_g_cameraPosDelta;
    iosPhotoCam_savedShakeAngleDelta = sithCamera_g_cameraAngleDelta;
    iosPhotoCam_fov = IOSPHOTOCAM_FOV_DEFAULT;
    iosPhotoCam_holdReason = holdReason;
    iosPhotoCam_bActive = 1;
    iosGame_SetHoldReason(holdReason, 1);
    return 1;
}

void iosPhotoCam_Place(const rdVector3* pos, const rdVector3* look, float tilt, SithSector* sector)
{
    if (!iosPhotoCam_bActive || !pos || !look) return;
    SithCamera* c = &sithCamera_g_aCameras[2];
    c->pSecondaryFocusThing = NULL;
    rdMatrix_LookAt(&c->orient, pos, look, tilt);
    if (sector) c->sector = sector;
    // (the camera follows nothing; its focus is only what the engine
    // expects to find there)
    c->pPrimaryFocusThing = iosPhotoCam_savedCamera.pPrimaryFocusThing ? iosPhotoCam_savedCamera.pPrimaryFocusThing
                                                                        : sithCamera_g_aCameras[0].pPrimaryFocusThing;
    iosPhotoCam_pos = *pos;
    iosPhotoCam_look = *look;
    if (sithCamera_g_pCurCamera != c) sithCamera_SetCurrentCamera(c);
    iosPhotoCam_SetLens(&c->rdCamera);
}

void iosPhotoCam_SetFov(float fov)
{
    if (fov < IOSPHOTOCAM_FOV_MIN) fov = IOSPHOTOCAM_FOV_MIN;
    if (fov > IOSPHOTOCAM_FOV_MAX) fov = IOSPHOTOCAM_FOV_MAX;
    iosPhotoCam_fov = fov;
    if (iosPhotoCam_bActive) iosPhotoCam_SetLens(&sithCamera_g_aCameras[2].rdCamera);
}

float iosPhotoCam_GetFov(void)
{
    return iosPhotoCam_fov;
}

void iosPhotoCam_GetPos(rdVector3* pPos, rdVector3* pLook)
{
    if (pPos) *pPos = iosPhotoCam_pos;
    if (pLook) *pLook = iosPhotoCam_look;
}

void iosPhotoCam_End(void)
{
    if (!iosPhotoCam_bActive) return;
    iosPhotoCam_bActive = 0;
    SithWorld* w = sithWorld_g_pCurrentWorld;
    if (w && iosPhotoCam_aSectorSeen && w->numSectors == iosPhotoCam_numSectorSeen) {
        for (int i = 0; i < w->numSectors; i++) {
            if (!iosPhotoCam_aSectorSeen[i]) w->aSectors[i].flags &= ~SITH_SECTOR_SEEN;
        }
    }
    free(iosPhotoCam_aSectorSeen);
    iosPhotoCam_aSectorSeen = NULL;
    // (the same thing, with the same animation, as when saved: the game was
    // held, so none can have come or gone)
    if (w && iosPhotoCam_aTracks && w->numThings == iosPhotoCam_numThingsSaved) {
        for (int i = 0; i < w->numThings; i++) {
            rdPuppet* p = w->aThings[i].type ? w->aThings[i].renderData.puppet : NULL;
            if (p && p == iosPhotoCam_aTracks[i].puppet) memcpy(p->aTracks, iosPhotoCam_aTracks[i].aTracks, sizeof(p->aTracks));
        }
    }
    free(iosPhotoCam_aTracks);
    iosPhotoCam_aTracks = NULL;
    sithCamera_g_aCameras[2] = iosPhotoCam_savedCamera;
    rdCamera_BuildFOV(&sithCamera_g_aCameras[2].rdCamera); // (its view shape, from the lens put back)
    if (iosPhotoCam_pPrevCamera) sithCamera_SetCurrentCamera(iosPhotoCam_pPrevCamera);
    iosPhotoCam_pPrevCamera = NULL;
    // (switching back updates the camera, which winds the shake down: put
    // it back after)
    sithCamera_g_vecCameraPosOffset = iosPhotoCam_savedShakePos;
    sithCamera_g_vecCameraAngleOffset = iosPhotoCam_savedShakeAngle;
    sithCamera_g_cameraPosDelta = iosPhotoCam_savedShakePosDelta;
    sithCamera_g_cameraAngleDelta = iosPhotoCam_savedShakeAngleDelta;
    iosGame_SetHoldReason(iosPhotoCam_holdReason, 0);
    iosPhotoCam_holdReason = 0;
}

int iosPhotoCam_IsActive(void)
{
    return iosPhotoCam_bActive;
}

int iosPhotoCam_SetLens(struct rdCamera* pCamera)
{
    if (!iosPhotoCam_bActive || !pCamera || pCamera != &sithCamera_g_aCameras[2].rdCamera) return 0;
    // (a lens is kept as degrees across: from top to bottom, through the
    // view's shape, whichever way round the player's own lens is set)
    flex_t across = iosPhotoCam_fov;
    if (pCamera->aspectRatio > 0.0001f)
        across = 2.0f * atanf(tanf(iosPhotoCam_fov * 0.5f * (flex_t)M_PI / 180.0f) / pCamera->aspectRatio) * 180.0f / (flex_t)M_PI;
    pCamera->fov = across;
    rdCamera_BuildFOV(pCamera);
    return 1;
}

// A door, a lift, a moving wall: a level's moving parts are "cog" things,
// the only things these searches look for (RAYCAST_2000); they stop the line
// as a wall does
static int iosPhotoCam_IsDoor(const SithCollision* c)
{
    return (c->type & SITHCOLLISION_THING) && c->pThingCollided && c->pThingCollided->type == SITH_THING_COG;
}

int iosPhotoCam_Clear(SithSector* from, const rdVector3* a, const rdVector3* b, float slack, SithSector** pEnd)
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
        else if (((c->type & SITHCOLLISION_WORLD) || iosPhotoCam_IsDoor(c)) && c->distance < dist - slack) {
            bClear = 0;
            break;
        }
    }
    sithCollision_DecreaseStackLevel();
    if (pEnd) *pEnd = s;
    return bClear && s;
}

float iosPhotoCam_ClearDist(SithSector* from, const rdVector3* a, const rdVector3* b, SithSector** pEnd)
{
    rdVector3 dir, start = *a;
    rdVector_Sub3(&dir, b, a);
    const flex_t dist = rdVector_Normalize3Acc(&dir);
    SithSector* s = from;
    flex_t clear = dist;
    if (pEnd) *pEnd = from;
    if (!from || dist <= 0.0f) return 0.0f;
    sithCollision_SearchForCollisions(from, NULL, &start, &dir, dist, 0.0f, RAYCAST_2000 | RAYCAST_100 | RAYCAST_20);
    // (nearest first, as iosPhotoCam_Clear takes them)
    for (SithCollision* c = sithCollision_PopStack(); c; c = sithCollision_PopStack()) {
        if (c->surface && c->surface->pAdjoin && (c->type & (SITHCOLLISION_ADJOINCROSS | SITHCOLLISION_THINGADJOINCROSS))) {
            if (c->type & SITHCOLLISION_ADJOINCROSS) s = c->surface->pAdjoin->sector;
        }
        else if ((c->type & SITHCOLLISION_WORLD) || iosPhotoCam_IsDoor(c)) {
            clear = c->distance < dist ? c->distance : dist;
            break;
        }
    }
    sithCollision_DecreaseStackLevel();
    if (pEnd) *pEnd = s;
    return clear;
}

#endif // JK_ENHANCED

// Added: entire file. The lightsaber blade's shape (see iosSaberShape.h).

#include "Enhanced/iosSaberShape.h"

#include "types.h"

#ifdef JK_ENHANCED

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Engine/rdMaterial.h"
#include "Engine/rdCamera.h"
#include "Primitives/rdPolyline.h"
#include "Platform/std3D.h"
#include "General/stdMath.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdModel3.h"
#include "Raster/rdCache.h"
#include "Engine/rdroid.h"
#include "jk.h" // (the game's clock, sithTime_g_msecGameTime: CROSS's crackle)

// ------------------------------------------------------------ the choice

static int iosSaberShape_shape = IOSSABERSHAPE_DEFAULT;
static const char* const iosSaberShape_aName[IOSSABERSHAPE_NUM] = {
    [IOSSABERSHAPE_SPIKE] = "SPIKE", [IOSSABERSHAPE_BEAM] = "BEAM"
};

int iosSaberShape_Get(void)
{
    return iosSaberShape_shape;
}

void iosSaberShape_Set(int shape)
{
    if (shape >= 0 && shape < IOSSABERSHAPE_NUM) iosSaberShape_shape = shape;
}

void iosSaberShape_Toggle(void)
{
    iosSaberShape_shape = iosSaberShape_shape == IOSSABERSHAPE_BEAM ? IOSSABERSHAPE_SPIKE : IOSSABERSHAPE_BEAM;
}

const char* iosSaberShape_Name(int shape)
{
    return (shape >= 0 && shape < IOSSABERSHAPE_NUM) ? iosSaberShape_aName[shape] : NULL;
}

int iosSaberShape_ByName(const char* name)
{
    for (int i = 0; name && i < IOSSABERSHAPE_NUM; i++)
        if (!strcmp(iosSaberShape_aName[i], name)) return i;
    return -1;
}

// ------------------------------------------------------------ the look of it

// The beam's radius at the hilt, of the template's base radius (the spike's
// widest); the glow's, of the beam's (at each end; its texels glow out to
// IOSSABERSHAPE_GLOW_REACH of that: as wide as before the beam was narrowed);
// a thrown saber's, of its beam's
#define IOSSABERSHAPE_BEAM_WIDTH 0.62f
#define IOSSABERSHAPE_BEAM_GLOW_WIDTH 9.6f
#define IOSSABERSHAPE_GLOW_WIDTH 6.9f
// The tip as wide as the hilt in the world; but nearer the far end than the
// hilt is (in first person, about twice as far from the eye), it would look
// half as wide on screen and come to a point again. So it is made wider in
// the world as it is further off, to look this much of the hilt's width on
// screen, up to this many times the hilt's in the world.
#define IOSSABERSHAPE_TIP_ON_SCREEN 0.55f
#define IOSSABERSHAPE_TIP_MAX 2.6f
// The beam drawn this much longer than the game's blade (only drawn: the
// game's own length, which strikes, is left as it is), its last
// IOSSABERSHAPE_BEAM_END of it closing in a rounded end (in slices round a
// quarter of an ellipse) in its own colours, instead of the game's round tip
#define IOSSABERSHAPE_BEAM_LENGTH 1.15f
#define IOSSABERSHAPE_BEAM_END 0.07f
#define IOSSABERSHAPE_BEAM_END_SLICES 4
// (a thrown saber's beam, for its glow: of its blade's length, as a held one's)
#define IOSSABERSHAPE_THROWN_BEAM 0.018f
// How bright the glow is at the blade's middle (of its colour), and how much
// toward white it goes there
#define IOSSABERSHAPE_GLOW_PEAK 1.25f
#define IOSSABERSHAPE_GLOW_WHITE 70.0f
// (how the glow falls off out from the blade: a power of how far there is left to go)
#define IOSSABERSHAPE_GLOW_FALLOFF 1.7f
// The glow's set back behind the blade (along the line of sight, so it stays
// where it is on screen), as rdPolyline_Draw sets the tip faces back
#define IOSSABERSHAPE_SET_BACK 0.001f

// How strong each glow is: a held blade's, and a thrown saber's (its blade a model, a little brighter of its own);
// CROSS's, held and thrown
enum { IOSSABERSHAPE_GAIN_BLADE = 0, IOSSABERSHAPE_GAIN_THROWN, IOSSABERSHAPE_GAIN_CROSS, IOSSABERSHAPE_GAIN_CROSS_THROWN, IOSSABERSHAPE_NUM_GAINS };
static const float iosSaberShape_aGain[IOSSABERSHAPE_NUM_GAINS] = { 1.0f, 0.85f, 1.35f, 1.15f };
// (and how far toward white its middle goes: CROSS's stays its colour, stronger)
static const float iosSaberShape_aWhite[IOSSABERSHAPE_NUM_GAINS] = { IOSSABERSHAPE_GLOW_WHITE, IOSSABERSHAPE_GLOW_WHITE, 22.0f, 22.0f };

// ------------------------------------------------------------ the glow textures

// Each glow in use: made from the game's round blade tip (mat\saberblue0.mat,
// a 32x32 disc in both games, its texture's alpha on) with its texels a soft
// round glow in the colour, at its strength; loaded privately, freed with
// the level. The renderer blends them premultiplied (colour + what was there
// times one less their alpha), so the glow only adds light: its alpha is the
// least the renderer draws (IOSSABERSHAPE_ADD_ALPHA) wherever it glows at
// all, each colour at least that much, so nothing behind it is ever darker;
// and none at all (not drawn) from IOSSABERSHAPE_GLOW_REACH of the way out,
// well before the edges of its faces, so they never show.
// (room for every colour of both looks, the game's own blades' and the thrown
// ones', at all four strengths, on one level)
#define IOSSABERSHAPE_MAX_GLOWS 192
typedef struct {
    unsigned char rgb[3];
    int gain;
    int loaded; // 1: loaded; -1: tried, and couldn't be
    rdMaterial mat;
    flex_t w, h;
} iosSaberShapeGlow;
static iosSaberShapeGlow iosSaberShape_aGlow[IOSSABERSHAPE_MAX_GLOWS];
static int iosSaberShape_numGlows = 0;

// (a glow texel's alpha: just over the renderer's discard, 0.01 of 255; how far
// out, of the disc's radius, it glows at all; and the least it adds, under which
// it isn't drawn)
#define IOSSABERSHAPE_ADD_ALPHA 3
#define IOSSABERSHAPE_GLOW_REACH 0.8f
#define IOSSABERSHAPE_GLOW_LEAST 2.0f

static void iosSaberShape_MakeTexels(uint8_t* pOut, int w, int h, const unsigned char rgb[3], float gain, float white)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float dx = ((x + 0.5f) / w) * 2.0f - 1.0f, dy = ((y + 0.5f) / h) * 2.0f - 1.0f;
            const float t = sqrtf(dx * dx + dy * dy) / IOSSABERSHAPE_GLOW_REACH;
            uint8_t* o = &pOut[(y * w + x) * 4];
            o[0] = o[1] = o[2] = o[3] = 0;
            if (t >= 1.0f) continue;
            const float f = powf(1.0f - t, IOSSABERSHAPE_GLOW_FALLOFF) * IOSSABERSHAPE_GLOW_PEAK * gain;
            float c[3], m = 0.0f;
            for (int k = 0; k < 3; k++) {
                c[k] = rgb[k] * f + white * f * f;
                if (c[k] > 255.0f) c[k] = 255.0f;
                if (c[k] > m) m = c[k];
            }
            if (m < IOSSABERSHAPE_GLOW_LEAST) continue; // (too faint to add anything: not drawn)
            // (each colour at least the alpha, so what is behind gains at least what the
            // alpha takes from it, however bright it is)
            for (int k = 0; k < 3; k++)
                if (c[k] < IOSSABERSHAPE_ADD_ALPHA) c[k] = IOSSABERSHAPE_ADD_ALPHA;
            o[0] = (uint8_t)(c[2] + 0.5f); // (B, G, R, A: as TEX_MODE_16BPP reads them)
            o[1] = (uint8_t)(c[1] + 0.5f);
            o[2] = (uint8_t)(c[0] + 0.5f);
            o[3] = IOSSABERSHAPE_ADD_ALPHA;
        }
    }
}

static void iosSaberShape_FreeGlow(iosSaberShapeGlow* g)
{
    rdMaterial* pMat = &g->mat;
    for (uint32_t i = 0; i < pMat->num_texinfo; i++) {
        rdTexture* pTex = pMat->texinfos[i] ? pMat->texinfos[i]->texture_ptr : NULL;
        for (uint32_t j = 0; pTex && j < pTex->num_mipmaps; j++) {
            tVBuffer* v = pTex->texture_struct[j];
            if (v && v->pTrueColour) {
                free(v->pTrueColour);
                v->pTrueColour = NULL;
            }
        }
    }
    rdMaterial_FreeEntry(pMat);
    memset(pMat, 0, sizeof(*pMat));
}

static int iosSaberShape_MakeGlow(iosSaberShapeGlow* g)
{
    if (!rdMaterial_LoadEntry("mat\\saberblue0.mat", &g->mat, 0, 0)) return 0;
    rdMaterial* pMat = &g->mat;
    int bAny = 0;
    for (uint32_t i = 0; i < pMat->num_texinfo; i++) {
        rdTexture* pTex = pMat->texinfos[i] ? pMat->texinfos[i]->texture_ptr : NULL;
        for (uint32_t j = 0; pTex && j < pTex->num_mipmaps; j++) {
            tVBuffer* v = pTex->texture_struct[j];
            if (!v) continue;
            const int w = v->format.width, h = v->format.height;
            uint8_t* pOut = (uint8_t*)malloc((size_t)w * h * 4);
            if (!pOut) {
                iosSaberShape_FreeGlow(g);
                return 0;
            }
            iosSaberShape_MakeTexels(pOut, w, h, g->rgb, iosSaberShape_aGain[g->gain], iosSaberShape_aWhite[g->gain]);
            free(v->pTrueColour);
            v->pTrueColour = pOut;
            std3D_PurgeSurfaceRefs(&pTex->alphaMats[j]);
            std3D_PurgeSurfaceRefs(&pTex->opaqueMats[j]);
            pTex->alphaMats[j].texture_loaded = 0;
            pTex->opaqueMats[j].texture_loaded = 0;
            if (i == 0 && j == 0) {
                g->w = (flex_t)w;
                g->h = (flex_t)h;
            }
            bAny = 1;
        }
    }
    if (!bAny) {
        iosSaberShape_FreeGlow(g);
        return 0;
    }
    return 1;
}

// (the glow for this colour and gain, made if need be; NULL if it can't be)
static iosSaberShapeGlow* iosSaberShape_Glow(const unsigned char rgb[3], int gain)
{
    for (int i = 0; i < iosSaberShape_numGlows; i++) {
        iosSaberShapeGlow* g = &iosSaberShape_aGlow[i];
        if (g->gain == gain && !memcmp(g->rgb, rgb, 3)) return g->loaded > 0 ? g : NULL;
    }
    // (a slot of its own; one that couldn't be made gives its slot up when they run out)
    iosSaberShapeGlow* g = NULL;
    if (iosSaberShape_numGlows < IOSSABERSHAPE_MAX_GLOWS) g = &iosSaberShape_aGlow[iosSaberShape_numGlows++];
    for (int i = 0; !g && i < iosSaberShape_numGlows; i++)
        if (iosSaberShape_aGlow[i].loaded < 0) g = &iosSaberShape_aGlow[i];
    if (!g) return NULL;
    memset(g, 0, sizeof(*g));
    memcpy(g->rgb, rgb, 3);
    g->gain = gain;
    g->loaded = iosSaberShape_MakeGlow(g) ? 1 : -1;
    return g->loaded > 0 ? g : NULL;
}

void iosSaberShape_FreeWorld(void)
{
    for (int i = 0; i < iosSaberShape_numGlows; i++)
        if (iosSaberShape_aGlow[i].loaded > 0) iosSaberShape_FreeGlow(&iosSaberShape_aGlow[i]);
    iosSaberShape_numGlows = 0;
}

// ------------------------------------------------------------ drawing

// The glow's faces: quads, lit as they are, the blade's own geometry, light
// and texture modes
static int iosSaberShape_aIdx[4] = { 0, 1, 2, 3 };
static rdVector3 iosSaberShape_aVerts[4];
static rdVector2 iosSaberShape_aUVs[4];

static void iosSaberShape_DrawQuad(rdThing* pLine, rdMaterial* pMat, rdTexMode_t texMode)
{
    rdFace face;
    memset(&face, 0, sizeof(face));
    face.geometryMode = RD_GEOMETRY_FULL;
    face.lightingMode = RD_LIGHTMODE_FULLYLIT;
    face.textureMode = texMode;
    face.numVertices = 4;
    face.vertexPosIdx = iosSaberShape_aIdx;
    face.vertexUVIdx = iosSaberShape_aIdx;
    face.material = pMat;
    face.wallCel = 0xFFFFFFFF;
    rdMeshinfo info;
    memset(&info, 0, sizeof(info));
    info.numVertices = 4;
    info.aVertices = iosSaberShape_aVerts;
    info.aTexVerticies = iosSaberShape_aUVs;
    rdPolyline_DrawFace(pLine, &face, iosSaberShape_aVerts, &info);
}

// The glows drawn while the depth buffer is written (the things: third
// person, other players, thrown sabers) are kept until the scene is drawn
// (iosSaberShape_FlushGlows): drawn there and then, they are seen through
// what is drawn after them, but never hide it, as they would if they wrote
// their depth across their whole area (a band cut out round the blade from
// water, glass, the other blade or someone behind it). The first person
// blade's are drawn at once: that pass never writes depth. (The trade: the
// translucent surfaces write no depth either, so the glow of a blade behind
// water or glass is drawn over it at its full strength.)
#define IOSSABERSHAPE_MAX_QUEUED 512
typedef struct {
    rdVector3 aVerts[4];
    rdVector2 aUVs[4];
    rdMaterial* pMat;
    rdTexMode_t texMode;
    rdThing modes; // (the geometry, light and texture modes and cel it is drawn with)
} iosSaberShapeQueued;
static iosSaberShapeQueued iosSaberShape_aQueue[IOSSABERSHAPE_MAX_QUEUED];
static int iosSaberShape_numQueued = 0;

static void iosSaberShape_Quad(rdThing* pLine, rdMaterial* pMat, rdTexMode_t texMode)
{
    const int z = rdGetZBufferMethod();
    if (z != RD_ZBUFFER_READ_WRITE && z != RD_ZBUFFER_NOREAD_WRITE) {
        iosSaberShape_DrawQuad(pLine, pMat, texMode);
        return;
    }
    if (iosSaberShape_numQueued >= IOSSABERSHAPE_MAX_QUEUED) return;
    iosSaberShapeQueued* q = &iosSaberShape_aQueue[iosSaberShape_numQueued++];
    memcpy(q->aVerts, iosSaberShape_aVerts, sizeof(q->aVerts));
    memcpy(q->aUVs, iosSaberShape_aUVs, sizeof(q->aUVs));
    q->pMat = pMat;
    q->texMode = texMode;
    memset(&q->modes, 0, sizeof(q->modes));
    q->modes.curGeoMode = pLine->curGeoMode;
    q->modes.curLightMode = pLine->curLightMode;
    q->modes.curTexMode = pLine->curTexMode;
    q->modes.wallCel = pLine->wallCel;
}

void iosSaberShape_BeginScene(void)
{
    iosSaberShape_numQueued = 0;
}

void iosSaberShape_FlushGlows(void)
{
    if (!iosSaberShape_numQueued || !rdCamera_g_pCurCamera) {
        iosSaberShape_numQueued = 0;
        return;
    }
    // (what is drawn so far first, then the glows tested against its depth, writing none)
    rdCache_Flush();
    const int z = rdGetZBufferMethod();
    rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
    for (int i = 0; i < iosSaberShape_numQueued; i++) {
        iosSaberShapeQueued* q = &iosSaberShape_aQueue[i];
        memcpy(iosSaberShape_aVerts, q->aVerts, sizeof(q->aVerts));
        memcpy(iosSaberShape_aUVs, q->aUVs, sizeof(q->aUVs));
        iosSaberShape_DrawQuad(&q->modes, q->pMat, q->texMode);
    }
    rdCache_Flush();
    rdSetZBufferMethod(z);
    iosSaberShape_numQueued = 0;
}

// (a camera space point set back from the eye by IOSSABERSHAPE_SET_BACK
// along the line of sight: where it was on screen, just behind; CROSS's,
// in first person, further: see iosSaberShape_Cross)
static flex_t iosSaberShape_setBack = IOSSABERSHAPE_SET_BACK;
static void iosSaberShape_SetBack(rdVector3* p)
{
    if (rdCamera_g_pCurCamera->projectType == rdCameraProjectType_Ortho || p->y <= (flex_t)0.0001) {
        p->y += iosSaberShape_setBack;
        return;
    }
    const flex_t k = (p->y + iosSaberShape_setBack) / p->y;
    p->x *= k;
    p->y *= k;
    p->z *= k;
}

// Where the depth isn't written (first person), what is drawn is sorted by
// its nearest point, furthest first: a blade's glow is set back this far
// (more than a slice's or a side blade's own depth) so it comes before every
// part of the blade it lies under, and they are drawn over it. (Where depth
// is written, the glow is drawn after the scene, against it: just behind.)
#define IOSSABERSHAPE_FP_SET_BACK 0.02f
static void iosSaberShape_GlowSetBack(int bOn)
{
    const int z = rdGetZBufferMethod();
    iosSaberShape_setBack = (bOn && z != RD_ZBUFFER_READ_WRITE && z != RD_ZBUFFER_NOREAD_WRITE) ? IOSSABERSHAPE_FP_SET_BACK : IOSSABERSHAPE_SET_BACK;
}

static void iosSaberShape_Vert(int i, const rdVector3* c, flex_t ax, flex_t az, flex_t u, flex_t v)
{
    iosSaberShape_aVerts[i].x = c->x + ax;
    iosSaberShape_aVerts[i].y = c->y;
    iosSaberShape_aVerts[i].z = c->z + az;
    iosSaberShape_SetBack(&iosSaberShape_aVerts[i]);
    iosSaberShape_aUVs[i].x = u;
    iosSaberShape_aUVs[i].y = v;
}

// The blade being drawn with BEAM or CROSS (between BeginBlade and EndBlade),
// its widths as they were, and its glow; with CROSS, whose it is (for its own
// crackle)
static rdThing* iosSaberShape_pLine = NULL;
static flex_t iosSaberShape_savedBase, iosSaberShape_savedTip, iosSaberShape_savedLength;
static flex_t iosSaberShape_beamTip; // (BEAM: its tip's radius, fitted; the game's round tip face, drawn at the template's tip radius, left out)
static iosSaberShapeGlow* iosSaberShape_pGlow = NULL;
static int iosSaberShape_bCross = 0;
static uint32_t iosSaberShape_crossId = 0;

void iosSaberShape_BeginBlade(SithThing* pThing, rdThing* pLine, const unsigned char rgb[3], int form)
{
    iosSaberShape_EndBlade(); // (begun twice with no end between: put back first)
    if (!pLine || !pLine->polyline) return;
    if (form == IOSSABERSHAPE_FORM_CROSS) {
        // CROSS, whatever the shape chosen: its widths kept as the template's (its
        // own drawn from them, DrawBlade), but no round tip face: it comes to a point
        rdPolyline* l = pLine->polyline;
        iosSaberShape_pLine = pLine;
        iosSaberShape_savedBase = l->baseRadius;
        iosSaberShape_savedTip = l->tipRadius;
        l->tipRadius = 0;
        iosSaberShape_bCross = 1;
        iosSaberShape_crossId = pThing ? pThing->idx + 1 : 0;
        iosSaberShape_pGlow = rgb ? iosSaberShape_Glow(rgb, IOSSABERSHAPE_GAIN_CROSS) : NULL;
        return;
    }
    if (iosSaberShape_shape != IOSSABERSHAPE_BEAM) return;
    rdPolyline* l = pLine->polyline;
    iosSaberShape_pLine = pLine;
    iosSaberShape_savedBase = l->baseRadius;
    iosSaberShape_savedTip = l->tipRadius;
    iosSaberShape_savedLength = l->length;
    // (even: as wide at the tip as at the hilt, and closed by its own rounded end
    // (DrawBeam), not the round tip face, which is left out; drawn longer)
    l->baseRadius = l->baseRadius * IOSSABERSHAPE_BEAM_WIDTH;
    iosSaberShape_beamTip = l->baseRadius;
    l->tipRadius = 0;
    l->length = l->length * IOSSABERSHAPE_BEAM_LENGTH;
    iosSaberShape_pGlow = rgb ? iosSaberShape_Glow(rgb, IOSSABERSHAPE_GAIN_BLADE) : NULL;
}

void iosSaberShape_FitTip(rdThing* pLine, float tipDepth, float baseDepth)
{
    if (!pLine || pLine != iosSaberShape_pLine || !pLine->polyline || iosSaberShape_bCross) return;
    rdPolyline* l = pLine->polyline;
    float k = 1.0f;
    if (rdCamera_g_pCurCamera && rdCamera_g_pCurCamera->projectType != rdCameraProjectType_Ortho && baseDepth > 0.0001f && tipDepth > baseDepth)
        k = IOSSABERSHAPE_TIP_ON_SCREEN * tipDepth / baseDepth;
    if (k < 1.0f) k = 1.0f;
    if (k > IOSSABERSHAPE_TIP_MAX) k = IOSSABERSHAPE_TIP_MAX;
    iosSaberShape_beamTip = l->baseRadius * k;
}

void iosSaberShape_EndBlade(void)
{
    if (!iosSaberShape_pLine) return;
    if (iosSaberShape_pLine->polyline) {
        iosSaberShape_pLine->polyline->baseRadius = iosSaberShape_savedBase;
        iosSaberShape_pLine->polyline->tipRadius = iosSaberShape_savedTip;
        if (!iosSaberShape_bCross) iosSaberShape_pLine->polyline->length = iosSaberShape_savedLength;
    }
    iosSaberShape_pLine = NULL;
    iosSaberShape_pGlow = NULL;
    iosSaberShape_bCross = 0;
}

// The beam widens toward its tip (iosSaberShape_FitTip), so its side and the
// glow's length are not rectangles: drawn as one quad (two triangles), their
// texture's middle line bends off the blade's along the diagonal, to one side
// only. Each is drawn in this many slices along the blade instead, each its
// own width, so the middle stays on the middle (the beam's, from its rounded
// end down; its end in IOSSABERSHAPE_BEAM_END_SLICES more).
#define IOSSABERSHAPE_SLICES 8

// The glow: square to the blade on screen, the base's half disc, the length
// (in slices) and the tip's half disc (camera space: tip, base, the glow's
// radius at each, (cos, sin) across the blade on screen). The glow texture's
// middle row runs along the length (across it, the glow's profile) and its
// top and bottom halves make the round ends, so they meet with no seam and
// nothing is drawn twice.
// (ends: which round ends are drawn, IOSSABERSHAPE_END_*)
#define IOSSABERSHAPE_END_TIP 1
#define IOSSABERSHAPE_END_BASE 2
static void iosSaberShape_GlowQuads(rdThing* pLine, const iosSaberShapeGlow* g, rdTexMode_t texMode, const rdVector3* pTip,
                                    const rdVector3* pBase, flex_t rTip, flex_t rBase, flex_t angCos, flex_t angSin, int ends)
{
    // (across, and along from base to tip, at each end's radius)
    const flex_t tx = rTip * angCos, tz = rTip * angSin, bx = rBase * angCos, bz = rBase * angSin;
    const flex_t w = g->w - (flex_t)0.01, h = g->h - (flex_t)0.01, mid = g->h * (flex_t)0.5;
    rdMaterial* pMat = (rdMaterial*)&g->mat;
    rdVector3 tipOut = { pTip->x - tz, pTip->y, pTip->z + tx };
    rdVector3 baseOut = { pBase->x + bz, pBase->y, pBase->z - bx };

    // the length, from the tip down
    for (int i = 0; i < IOSSABERSHAPE_SLICES; i++) {
        const flex_t s0 = (flex_t)i / IOSSABERSHAPE_SLICES, s1 = (flex_t)(i + 1) / IOSSABERSHAPE_SLICES;
        const rdVector3 p0 = { pTip->x + (pBase->x - pTip->x) * s0, pTip->y + (pBase->y - pTip->y) * s0, pTip->z + (pBase->z - pTip->z) * s0 };
        const rdVector3 p1 = { pTip->x + (pBase->x - pTip->x) * s1, pTip->y + (pBase->y - pTip->y) * s1, pTip->z + (pBase->z - pTip->z) * s1 };
        const flex_t x0 = tx + (bx - tx) * s0, z0 = tz + (bz - tz) * s0, x1 = tx + (bx - tx) * s1, z1 = tz + (bz - tz) * s1;
        iosSaberShape_Vert(0, &p0, x0, z0, w, mid);
        iosSaberShape_Vert(1, &p0, -x0, -z0, 0, mid);
        iosSaberShape_Vert(2, &p1, -x1, -z1, 0, mid);
        iosSaberShape_Vert(3, &p1, x1, z1, w, mid);
        iosSaberShape_Quad(pLine, pMat, texMode);
    }
    // the tip's end
    if (ends & IOSSABERSHAPE_END_TIP) {
        iosSaberShape_Vert(0, &tipOut, tx, tz, w, 0);
        iosSaberShape_Vert(1, &tipOut, -tx, -tz, 0, 0);
        iosSaberShape_Vert(2, pTip, -tx, -tz, 0, mid);
        iosSaberShape_Vert(3, pTip, tx, tz, w, mid);
        iosSaberShape_Quad(pLine, pMat, texMode);
    }
    // the hilt's end
    if (ends & IOSSABERSHAPE_END_BASE) {
        iosSaberShape_Vert(0, pBase, bx, bz, w, mid);
        iosSaberShape_Vert(1, pBase, -bx, -bz, 0, mid);
        iosSaberShape_Vert(2, &baseOut, -bx, -bz, 0, h);
        iosSaberShape_Vert(3, &baseOut, bx, bz, w, h);
        iosSaberShape_Quad(pLine, pMat, texMode);
    }
}

static void iosSaberShape_DrawGlow(rdThing* pLine, const rdVector3* pTip, const rdVector3* pBase, float angCos, float angSin)
{
    if (!pLine || pLine != iosSaberShape_pLine || !iosSaberShape_pGlow || !pLine->polyline) return;
    // (its brightest end where the rounded end begins, under the beam, not out past it as a ball of light)
    const rdVector3 end = { pTip->x + (pBase->x - pTip->x) * IOSSABERSHAPE_BEAM_END, pTip->y + (pBase->y - pTip->y) * IOSSABERSHAPE_BEAM_END,
                            pTip->z + (pBase->z - pTip->z) * IOSSABERSHAPE_BEAM_END };
    iosSaberShape_GlowSetBack(1); // (under the beam's end too, in first person)
    iosSaberShape_GlowQuads(pLine, iosSaberShape_pGlow, pLine->polyline->face.textureMode, &end, pBase,
                            iosSaberShape_beamTip * IOSSABERSHAPE_BEAM_GLOW_WIDTH, pLine->polyline->baseRadius * IOSSABERSHAPE_BEAM_GLOW_WIDTH,
                            angCos, angSin, IOSSABERSHAPE_END_TIP | IOSSABERSHAPE_END_BASE);
    iosSaberShape_GlowSetBack(0);
}

// The beam's side, as rdPolyline_Draw lays the blade (its corners, its
// texture's corners), but in slices from the tip down, each its own width
static rdVector3 iosSaberShape_aSideVerts[4];
static rdVector2 iosSaberShape_aSideUVs[4];

static int iosSaberShape_DrawBeam(rdThing* pLine, const rdVector3* pTip, const rdVector3* pBase, float angCos, float angSin)
{
    rdPolyline* l = pLine->polyline;
    if (!l->extraUVTipMaybe || !l->face.vertexUVIdx) return 0; // (untextured: as the game draws it)
    const flex_t w = l->extraUVTipMaybe[0].x, h = l->extraUVTipMaybe[2].y;
    const flex_t tx = iosSaberShape_beamTip * angCos, tz = iosSaberShape_beamTip * angSin, bx = l->baseRadius * angCos, bz = l->baseRadius * angSin;
    rdMeshinfo info;
    memset(&info, 0, sizeof(info));
    info.numVertices = 4;
    info.aVertices = iosSaberShape_aSideVerts;
    info.aTexVerticies = iosSaberShape_aSideUVs;
    for (int i = 0; i < IOSSABERSHAPE_BEAM_END_SLICES + IOSSABERSHAPE_SLICES; i++) {
        // (along it from the tip, 0, to the hilt, 1; and how much of its width there: the
        // rounded end's slices round a quarter ellipse, the rest the full width)
        flex_t s[2], k[2] = { 1, 1 };
        for (int e = 0; e < 2; e++) {
            const int j = i + e;
            if (j <= IOSSABERSHAPE_BEAM_END_SLICES) {
                const float th = (float)j / IOSSABERSHAPE_BEAM_END_SLICES * (float)(M_PI * 0.5);
                s[e] = IOSSABERSHAPE_BEAM_END * (flex_t)(1.0f - cosf(th));
                k[e] = (flex_t)sinf(th);
            }
            else s[e] = IOSSABERSHAPE_BEAM_END + (1 - IOSSABERSHAPE_BEAM_END) * (flex_t)(j - IOSSABERSHAPE_BEAM_END_SLICES) / IOSSABERSHAPE_SLICES;
        }
        for (int e = 0; e < 2; e++) {
            const rdVector3 p = { pTip->x + (pBase->x - pTip->x) * s[e], pTip->y + (pBase->y - pTip->y) * s[e], pTip->z + (pBase->z - pTip->z) * s[e] };
            const flex_t ax = (tx + (bx - tx) * s[e]) * k[e], az = (tz + (bz - tz) * s[e]) * k[e];
            // (0 and 1 the slice's tip side, + and - across; 2 and 3 its base side, - and +)
            rdVector3* a = &iosSaberShape_aSideVerts[e ? 3 : 0];
            rdVector3* b = &iosSaberShape_aSideVerts[e ? 2 : 1];
            a->x = p.x + ax; a->y = p.y; a->z = p.z + az;
            b->x = p.x - ax; b->y = p.y; b->z = p.z - az;
            iosSaberShape_aSideUVs[e ? 3 : 0].x = w;
            iosSaberShape_aSideUVs[e ? 3 : 0].y = h * s[e];
            iosSaberShape_aSideUVs[e ? 2 : 1].x = 0;
            iosSaberShape_aSideUVs[e ? 2 : 1].y = h * s[e];
        }
        rdPolyline_DrawFace(pLine, &l->face, iosSaberShape_aSideVerts, &info);
    }
    iosSaberShape_DrawGlow(pLine, pTip, pBase, angCos, angSin);
    return 1;
}

// ------------------------------------------------------------ CROSS

// The blade: its half width at the hilt (of the template's base radius); the
// last IOSSABERSHAPE_CROSS_TAPER of it narrows to its point. Drawn in this many
// slices, each edge point moved in or out by up to IOSSABERSHAPE_CROSS_JAG of
// the width there (each edge on its own), and now and then one by up to
// IOSSABERSHAPE_CROSS_FLARE more: new each frame of the game's clock.
#define IOSSABERSHAPE_CROSS_WIDTH 0.85f
#define IOSSABERSHAPE_CROSS_TAPER 0.22f
#define IOSSABERSHAPE_CROSS_SLICES 28
#define IOSSABERSHAPE_CROSS_JAG 0.22f
#define IOSSABERSHAPE_CROSS_FLARE 0.55f
#define IOSSABERSHAPE_CROSS_FLARE_CHANCE 0.15f
// Up to this many sparks off its sides each frame (each there that often):
// thin and pointed, this long (of the blade's half width), slanting out
// toward the tip
#define IOSSABERSHAPE_CROSS_SPARKS 3
#define IOSSABERSHAPE_CROSS_SPARK_CHANCE 0.6f
#define IOSSABERSHAPE_CROSS_SPARK_MIN 1.2f
#define IOSSABERSHAPE_CROSS_SPARK_MAX 3.4f
#define IOSSABERSHAPE_CROSS_SPARK_WIDTH 0.28f
// The side blades: this long (of the blade's length), as wide at the hilt
// (of the blade's), the whole of their outer IOSSABERSHAPE_CROSS_GUARD_TAPER
// narrowing to their points; out from this far up the blade (of its length);
// in this many slices
#define IOSSABERSHAPE_CROSS_GUARD_LEN 0.13f
#define IOSSABERSHAPE_CROSS_GUARD_WIDTH 0.8f
#define IOSSABERSHAPE_CROSS_GUARD_TAPER 0.6f
#define IOSSABERSHAPE_CROSS_GUARD_AT 0.0f
#define IOSSABERSHAPE_CROSS_GUARD_SLICES 5
// The glow: its radius at the hilt (of the blade's half width; its texels glowing
// out to IOSSABERSHAPE_GLOW_REACH of it), and at a
// point (of that), a little wider or narrower each frame
#define IOSSABERSHAPE_CROSS_GLOW_WIDTH 7.5f
#define IOSSABERSHAPE_CROSS_GLOW_POINT 0.45f
#define IOSSABERSHAPE_CROSS_GLOW_FLICKER 0.06f

// The crackle's random numbers: its own (never the game's, which the game's
// own play and the photo modes count on), from the game's clock and whose
// blade it is, so the same frame draws the same blade
static uint32_t iosSaberShape_rand = 1;
static void iosSaberShape_Seed(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    iosSaberShape_rand = h ? h : 1;
}
static flex_t iosSaberShape_Rand(void) // (0..1)
{
    uint32_t x = iosSaberShape_rand;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    iosSaberShape_rand = x;
    return (flex_t)((x >> 8) * (1.0 / 16777216.0));
}

// (cos, sin) across a line from base to tip as it is seen on screen, as
// rdPolyline_Draw lays a blade
static void iosSaberShape_Across(const rdVector3* pTip, const rdVector3* pBase, flex_t* pCos, flex_t* pSin)
{
    flex_t dx = pTip->x - pBase->x, dz = pTip->z - pBase->z;
    if (rdCamera_g_pCurCamera->projectType != rdCameraProjectType_Ortho && pTip->y > (flex_t)0.0001 && pBase->y > (flex_t)0.0001) {
        dx = pTip->x / pTip->y - pBase->x / pBase->y;
        dz = pTip->z / pTip->y - pBase->z / pBase->y;
    }
    const flex_t m = stdMath_Fabs(dx) > stdMath_Fabs(dz) ? stdMath_Fabs(dx) : stdMath_Fabs(dz);
    if (m > 0) {
        dx /= m;
        dz /= m;
    }
    const flex_t mag = stdMath_Sqrt(dx * dx + dz * dz);
    *pCos = 1;
    *pSin = 0;
    if (mag > 0) {
        *pCos = dz / mag;
        *pSin = -dx / mag;
    }
}

// A jagged blade from pBase out to its point at pTip (camera space), drawn
// with pFace (the blade's side: its texture across the width, along the
// length): its half width r at the base, the outer taper of it narrowing to
// the point, in n slices, each edge point moved by up to jag of the width
// there (and now and then by flare more). Its (cos, sin) across, out.
static rdVector3 iosSaberShape_aCrossVerts[4];
static rdVector2 iosSaberShape_aCrossUVs[4];
static void iosSaberShape_Spike(rdThing* pLine, rdFace* pFace, flex_t uvW, flex_t uvH, const rdVector3* pTip, const rdVector3* pBase,
                                flex_t r, flex_t taper, int n, flex_t jag, flex_t flare, flex_t* pCos, flex_t* pSin)
{
    flex_t c, sn;
    iosSaberShape_Across(pTip, pBase, &c, &sn);
    if (pCos) *pCos = c;
    if (pSin) *pSin = sn;
    rdMeshinfo info;
    memset(&info, 0, sizeof(info));
    info.numVertices = 4;
    info.aVertices = iosSaberShape_aCrossVerts;
    info.aTexVerticies = iosSaberShape_aCrossUVs;
    rdVector3 prevA = *pTip, prevB = *pTip; // (the tip: a point)
    flex_t prevS = 0;
    for (int i = 1; i <= n; i++) {
        const flex_t s = (flex_t)i / n; // (0 the tip, 1 the base)
        const rdVector3 p = { pTip->x + (pBase->x - pTip->x) * s, pTip->y + (pBase->y - pTip->y) * s, pTip->z + (pBase->z - pTip->z) * s };
        flex_t k = s >= taper ? (flex_t)1 : (flex_t)powf((float)(s / taper), 0.75f);
        flex_t aw[2];
        for (int e = 0; e < 2; e++) {
            flex_t j = (iosSaberShape_Rand() * 2 - 1) * jag;
            if (flare > 0 && iosSaberShape_Rand() < IOSSABERSHAPE_CROSS_FLARE_CHANCE) j += iosSaberShape_Rand() * flare;
            aw[e] = r * k * (1 + j);
        }
        // (0 and 1 the slice's tip side, + and - across; 2 and 3 its base side, - and +)
        rdVector3 a = { p.x + aw[0] * c, p.y, p.z + aw[0] * sn };
        rdVector3 b = { p.x - aw[1] * c, p.y, p.z - aw[1] * sn };
        iosSaberShape_aCrossVerts[0] = prevA;
        iosSaberShape_aCrossVerts[1] = prevB;
        iosSaberShape_aCrossVerts[2] = b;
        iosSaberShape_aCrossVerts[3] = a;
        iosSaberShape_aCrossUVs[0].x = uvW; iosSaberShape_aCrossUVs[0].y = uvH * prevS;
        iosSaberShape_aCrossUVs[1].x = 0;   iosSaberShape_aCrossUVs[1].y = uvH * prevS;
        iosSaberShape_aCrossUVs[2].x = 0;   iosSaberShape_aCrossUVs[2].y = uvH * s;
        iosSaberShape_aCrossUVs[3].x = uvW; iosSaberShape_aCrossUVs[3].y = uvH * s;
        rdPolyline_DrawFace(pLine, pFace, iosSaberShape_aCrossVerts, &info);
        prevA = a;
        prevB = b;
        prevS = s;
    }
}

// The side blades' way out from the blade (camera space): across the hand
// (its matrix's first axis), square to the blade; 0 if there is none
static int iosSaberShape_GuardAxis(const rdMatrix34* pToCam, const rdVector3* pAlong, rdVector3* pOut)
{
    rdVector3 a = pToCam->rvec;
    const flex_t d = a.x * pAlong->x + a.y * pAlong->y + a.z * pAlong->z;
    a.x -= pAlong->x * d;
    a.y -= pAlong->y * d;
    a.z -= pAlong->z * d;
    const flex_t m = stdMath_Sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (m <= (flex_t)0.000001) return 0;
    pOut->x = a.x / m;
    pOut->y = a.y / m;
    pOut->z = a.z / m;
    return 1;
}

// The whole of CROSS (camera space: the hilt's matrix, if known; the blade's
// tip and base; r, its half width at the hilt): the blade and its sparks, the
// side blades, and the glow round all three (g; none, NULL). pFace and the UVs
// the blade's side.
static void iosSaberShape_Cross(rdThing* pLine, rdFace* pFace, flex_t uvW, flex_t uvH, const iosSaberShapeGlow* g, rdTexMode_t texMode,
                                const rdMatrix34* pToCam, const rdVector3* pTip, const rdVector3* pBase, flex_t r, int bBlade)
{
    const rdVector3 d = { pTip->x - pBase->x, pTip->y - pBase->y, pTip->z - pBase->z };
    const flex_t len = stdMath_Sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len <= (flex_t)0.000001) return;
    const rdVector3 along = { d.x / len, d.y / len, d.z / len };
    flex_t c = 1, sn = 0;

    // the blade, jagged, to its point (not for a thrown saber: its blade is the model's own)
    if (bBlade) {
        iosSaberShape_Spike(pLine, pFace, uvW, uvH, pTip, pBase, r, IOSSABERSHAPE_CROSS_TAPER, IOSSABERSHAPE_CROSS_SLICES,
                            IOSSABERSHAPE_CROSS_JAG, IOSSABERSHAPE_CROSS_FLARE, &c, &sn);
        // its sparks: from a point on a side, out and on toward the tip
        for (int k = 0; k < IOSSABERSHAPE_CROSS_SPARKS; k++) {
            const int bShown = iosSaberShape_Rand() < IOSSABERSHAPE_CROSS_SPARK_CHANCE;
            const flex_t s = (flex_t)0.12 + iosSaberShape_Rand() * (flex_t)0.68; // (from the tip)
            const flex_t side = iosSaberShape_Rand() < 0.5f ? (flex_t)-1 : (flex_t)1;
            const flex_t ang = (flex_t)(0.35 + 0.6 * iosSaberShape_Rand()); // (out of the side, radians toward the tip)
            const flex_t sl = r * (IOSSABERSHAPE_CROSS_SPARK_MIN + (IOSSABERSHAPE_CROSS_SPARK_MAX - IOSSABERSHAPE_CROSS_SPARK_MIN) * iosSaberShape_Rand());
            if (!bShown) continue;
            const flex_t o = r * (flex_t)0.8 * side, ca = cosf((float)ang) * side, sa = sinf((float)ang);
            const rdVector3 q = { pTip->x - d.x * s + c * o, pTip->y - d.y * s, pTip->z - d.z * s + sn * o };
            const rdVector3 t = { q.x + (c * ca + along.x * sa) * sl, q.y + along.y * sa * sl, q.z + (sn * ca + along.z * sa) * sl };
            iosSaberShape_Spike(pLine, pFace, uvW, uvH, &t, &q, r * IOSSABERSHAPE_CROSS_SPARK_WIDTH, 1, 2, 0.2f, 0, NULL, NULL);
        }
    }
    else iosSaberShape_Across(pTip, pBase, &c, &sn);

    // the side blades, from the hilt out across the hand, either way
    rdVector3 axis, aRoot[2], aTip[2];
    flex_t aCos[2] = { 1, 1 }, aSin[2] = { 0, 0 };
    const int bGuards = pToCam && iosSaberShape_GuardAxis(pToCam, &along, &axis);
    const flex_t gl = len * IOSSABERSHAPE_CROSS_GUARD_LEN, gr = r * IOSSABERSHAPE_CROSS_GUARD_WIDTH;
    for (int k = 0; bGuards && k < 2; k++) {
        const flex_t side = k ? (flex_t)-1 : (flex_t)1;
        aRoot[k].x = pBase->x + d.x * IOSSABERSHAPE_CROSS_GUARD_AT;
        aRoot[k].y = pBase->y + d.y * IOSSABERSHAPE_CROSS_GUARD_AT;
        aRoot[k].z = pBase->z + d.z * IOSSABERSHAPE_CROSS_GUARD_AT;
        aTip[k].x = aRoot[k].x + axis.x * gl * side;
        aTip[k].y = aRoot[k].y + axis.y * gl * side;
        aTip[k].z = aRoot[k].z + axis.z * gl * side;
        if (pFace)
            iosSaberShape_Spike(pLine, pFace, uvW, uvH, &aTip[k], &aRoot[k], gr, IOSSABERSHAPE_CROSS_GUARD_TAPER, IOSSABERSHAPE_CROSS_GUARD_SLICES,
                                IOSSABERSHAPE_CROSS_JAG, 0, &aCos[k], &aSin[k]);
        else
            iosSaberShape_Across(&aTip[k], &aRoot[k], &aCos[k], &aSin[k]);
    }

    // the glow round them, a little wider or narrower each frame (the side blades' none at their roots, inside the blade's)
    if (!g) return;
    iosSaberShape_GlowSetBack(1);
    const flex_t f = 1 + (iosSaberShape_Rand() * 2 - 1) * IOSSABERSHAPE_CROSS_GLOW_FLICKER;
    const flex_t rg = r * IOSSABERSHAPE_CROSS_GLOW_WIDTH * f;
    iosSaberShape_GlowQuads(pLine, g, texMode, pTip, pBase, rg * IOSSABERSHAPE_CROSS_GLOW_POINT, rg, c, sn, IOSSABERSHAPE_END_TIP | IOSSABERSHAPE_END_BASE);
    for (int k = 0; bGuards && k < 2; k++)
        iosSaberShape_GlowQuads(pLine, g, texMode, &aTip[k], &aRoot[k], gr * IOSSABERSHAPE_CROSS_GLOW_WIDTH * f * IOSSABERSHAPE_CROSS_GLOW_POINT,
                                gr * IOSSABERSHAPE_CROSS_GLOW_WIDTH * f, aCos[k], aSin[k], IOSSABERSHAPE_END_TIP);
    iosSaberShape_GlowSetBack(0);
}

int iosSaberShape_DrawBlade(rdThing* pLine, const rdMatrix34* pToCam, const rdVector3* pTip, const rdVector3* pBase, float angCos, float angSin)
{
    if (!pLine || pLine != iosSaberShape_pLine || !pLine->polyline) return 0;
    if (!iosSaberShape_bCross) return iosSaberShape_DrawBeam(pLine, pTip, pBase, angCos, angSin);
    rdPolyline* l = pLine->polyline;
    if (!l->extraUVTipMaybe || !l->face.vertexUVIdx) return 0; // (untextured: as the game draws it)
    // (this frame's crackle, this blade's own)
    iosSaberShape_Seed((uint32_t)sithTime_g_msecGameTime, iosSaberShape_crossId);
    iosSaberShape_Cross(pLine, &l->face, l->extraUVTipMaybe[0].x, l->extraUVTipMaybe[2].y, iosSaberShape_pGlow, l->face.textureMode, pToCam, pTip, pBase,
                        iosSaberShape_savedBase * IOSSABERSHAPE_CROSS_WIDTH, 1);
    return 1;
}

// A thrown saber's blade: the mesh whose faces have its material (the first
// geoset's), and its long axis (its end points) in that mesh's own space; 0
// if it has none. (Its hilt: the end nearer the middle of the mesh's other
// faces (none: A) is A. Across: of its other two axes, the one it is wider
// along, the way it spins.)
static int iosSaberShape_ThrownBlade(const rdModel3* m, const rdMaterial* pBlade, int* pMesh, rdVector3* pA, rdVector3* pB, int* pAcross)
{
    if (!m->numGeos) return 0;
    const rdGeoset* gs = &m->aGeos[0];
    for (uint32_t k = 0; k < gs->numMeshes; k++) {
        const rdMesh* mesh = &gs->aMeshes[k];
        rdVector3 lo = { 0, 0, 0 }, hi = { 0, 0, 0 };
        int bAny = 0;
        for (int f = 0; f < mesh->numFaces; f++) {
            const rdFace* face = &mesh->faces[f];
            if (face->material != pBlade || !face->vertexPosIdx) continue;
            for (uint32_t v = 0; v < face->numVertices; v++) {
                const int idx = face->vertexPosIdx[v];
                if (idx < 0 || idx >= mesh->numVertices) continue;
                const rdVector3* p = &mesh->aVertices[idx];
                if (!bAny) { lo = hi = *p; bAny = 1; }
                if (p->x < lo.x) lo.x = p->x;
                if (p->y < lo.y) lo.y = p->y;
                if (p->z < lo.z) lo.z = p->z;
                if (p->x > hi.x) hi.x = p->x;
                if (p->y > hi.y) hi.y = p->y;
                if (p->z > hi.z) hi.z = p->z;
            }
        }
        if (!bAny) continue;
        const flex_t e[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
        const int ax = e[0] >= e[1] && e[0] >= e[2] ? 0 : (e[1] >= e[2] ? 1 : 2);
        rdVector3 c = { (lo.x + hi.x) * (flex_t)0.5, (lo.y + hi.y) * (flex_t)0.5, (lo.z + hi.z) * (flex_t)0.5 };
        *pA = *pB = c;
        if (ax == 0) { pA->x = lo.x; pB->x = hi.x; }
        else if (ax == 1) { pA->y = lo.y; pB->y = hi.y; }
        else { pA->z = lo.z; pB->z = hi.z; }
        *pMesh = (int)k;
        if (pAcross) {
            const int o1 = ax == 0 ? 1 : 0, o2 = ax == 2 ? 1 : 2;
            *pAcross = e[o1] >= e[o2] ? o1 : o2;
            // (the hilt's end: nearer the middle of the other faces)
            flex_t sum = 0;
            int num = 0;
            for (int f = 0; f < mesh->numFaces; f++) {
                const rdFace* face = &mesh->faces[f];
                if (face->material == pBlade || !face->vertexPosIdx) continue;
                for (uint32_t v = 0; v < face->numVertices; v++) {
                    const int idx = face->vertexPosIdx[v];
                    if (idx < 0 || idx >= mesh->numVertices) continue;
                    const rdVector3* p = &mesh->aVertices[idx];
                    sum += ax == 0 ? p->x : ax == 1 ? p->y : p->z;
                    num++;
                }
            }
            if (num) {
                const flex_t mid = sum / num, la = ax == 0 ? lo.x : ax == 1 ? lo.y : lo.z, ha = ax == 0 ? hi.x : ax == 1 ? hi.y : hi.z;
                if (stdMath_Fabs(mid - ha) < stdMath_Fabs(mid - la)) {
                    const rdVector3 t = *pA;
                    *pA = *pB;
                    *pB = t;
                }
            }
        }
        return e[ax] > 0;
    }
    return 0;
}

// Its glow as a held blade's, along the blade as it is drawn now (its mesh's
// node, as the model was just posed); CROSS's, and its side blades
void iosSaberShape_DrawThrownGlow(SithThing* pThing, const rdMaterial* pBlade, const unsigned char rgb[3], int form,
                                  const rdFace* pSideFace, rdMaterial* pSideMat)
{
    const int bCross = form == IOSSABERSHAPE_FORM_CROSS;
    if ((!bCross && iosSaberShape_shape != IOSSABERSHAPE_BEAM) || !pThing || !pBlade || !rgb || !rdCamera_g_pCurCamera) return;
    rdThing* t = &pThing->renderData;
    if (t->type != RD_THING_MODEL3 || !t->model3 || !t->paJointMatrices) return;
    const rdModel3* m = t->model3;
    int mesh = -1, node = -1;
    rdVector3 a, b;
    int across = 0;
    if (!iosSaberShape_ThrownBlade(m, pBlade, &mesh, &a, &b, bCross ? &across : NULL)) return;
    for (uint32_t n = 0; n < m->numHNodes && node < 0; n++)
        if ((int)m->aHierarchyNodes[n].meshIdx == mesh) node = (int)n;
    if (node < 0) return;
    const iosSaberShapeGlow* g = iosSaberShape_Glow(rgb, bCross ? IOSSABERSHAPE_GAIN_CROSS_THROWN : IOSSABERSHAPE_GAIN_THROWN);
    if (!g && !bCross) return;
    // (the mesh's space to the camera's)
    rdMatrix34 toCam;
    rdMatrix_Multiply34(&toCam, &rdCamera_g_pCurCamera->orient, &t->paJointMatrices[node]);
    rdVector3 tip, base;
    rdMatrix_TransformPoint34(&tip, &b, &toCam);
    rdMatrix_TransformPoint34(&base, &a, &toCam);
    if (tip.y <= (flex_t)0.0001 || base.y <= (flex_t)0.0001) return;
    // across the blade on screen (as rdPolyline_Draw lays a blade)
    flex_t dx = tip.x / tip.y - base.x / base.y, dz = tip.z / tip.y - base.z / base.y;
    const flex_t mag = stdMath_Sqrt(dx * dx + dz * dz);
    flex_t angCos = 1, angSin = 0;
    if (mag > 0) {
        angCos = dz / mag;
        angSin = -dx / mag;
    }
    // (lit and textured as the thrown saber's own faces are)
    static rdThing glowThing;
    glowThing = *t;
    glowThing.curGeoMode = RD_GEOMETRY_FULL;
    glowThing.curLightMode = RD_LIGHTMODE_FULLYLIT;
    if (bCross) {
        // CROSS: as a held one's, out from its hilt across the way it spins (the
        // model's axis it is wider along); its side drawn as the thrower's saber's,
        // in its colour; its own crackle
        rdMatrix34 guard = toCam;
        guard.rvec = across == 0 ? toCam.rvec : across == 1 ? toCam.lvec : toCam.uvec;
        rdFace face;
        flex_t uvW = 1, uvH = 1;
        const int bSide = pSideFace && pSideMat && pSideFace->vertexUVIdx;
        if (bSide) {
            face = *pSideFace;
            face.material = pSideMat;
            const rdTexinfo* ti = pSideMat->num_texinfo ? pSideMat->texinfos[0] : NULL;
            const tVBuffer* v = ti && ti->texture_ptr ? ti->texture_ptr->texture_struct[0] : NULL;
            uvW = (flex_t)(v ? v->format.width : 1) - (flex_t)0.01;
            uvH = (flex_t)(v ? v->format.height : 1) - (flex_t)0.01;
        }
        iosSaberShape_Seed((uint32_t)sithTime_g_msecGameTime, 0x8000u + pThing->idx);
        iosSaberShape_Cross(&glowThing, bSide ? &face : NULL, uvW, uvH, g, glowThing.curTexMode, &guard, &tip, &base,
                            stdMath_Dist3D1(b.x - a.x, b.y - a.y, b.z - a.z) * IOSSABERSHAPE_THROWN_BEAM, 0);
        return;
    }
    // (as wide as a held blade's of that length: the model's blade is drawn wider, with its blur)
    const flex_t r = stdMath_Dist3D1(b.x - a.x, b.y - a.y, b.z - a.z) * IOSSABERSHAPE_THROWN_BEAM * IOSSABERSHAPE_GLOW_WIDTH;
    iosSaberShape_GlowQuads(&glowThing, g, glowThing.curTexMode, &tip, &base, r, r, angCos, angSin, IOSSABERSHAPE_END_TIP | IOSSABERSHAPE_END_BASE);
}

#endif // JK_ENHANCED

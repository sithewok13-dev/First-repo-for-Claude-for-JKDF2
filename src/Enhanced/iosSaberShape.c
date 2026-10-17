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
// widest); the glow's, of the beam's (at each end)
#define IOSSABERSHAPE_BEAM_WIDTH 0.9f
#define IOSSABERSHAPE_GLOW_WIDTH 5.5f
// The tip as wide as the hilt in the world; but nearer the far end than the
// hilt is (in first person, about twice as far from the eye), it would look
// half as wide on screen and come to a point again. So it is made wider in
// the world as it is further off, to look this much of the hilt's width on
// screen, up to this many times the hilt's in the world.
#define IOSSABERSHAPE_TIP_ON_SCREEN 0.7f
#define IOSSABERSHAPE_TIP_MAX 2.6f
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

// How strong each glow is: a held blade's, and a thrown saber's (its blade a model, a little brighter of its own)
enum { IOSSABERSHAPE_GAIN_BLADE = 0, IOSSABERSHAPE_GAIN_THROWN, IOSSABERSHAPE_NUM_GAINS };
static const float iosSaberShape_aGain[IOSSABERSHAPE_NUM_GAINS] = { 1.0f, 0.85f };

// ------------------------------------------------------------ the glow textures

// Each glow in use: made from the game's round blade tip (mat\saberblue0.mat,
// a 32x32 disc in both games, its texture's alpha on) with its texels a soft
// round glow in the colour, at its strength; loaded privately, freed with
// the level. The renderer blends them premultiplied (colour + what was there
// times one less their alpha): a low alpha, so the glow mostly adds.
// (room for every colour of both looks, the game's own blades' and the thrown
// ones', at both strengths, on one level)
#define IOSSABERSHAPE_MAX_GLOWS 96
typedef struct {
    unsigned char rgb[3];
    int gain;
    int loaded; // 1: loaded; -1: tried, and couldn't be
    rdMaterial mat;
    flex_t w, h;
} iosSaberShapeGlow;
static iosSaberShapeGlow iosSaberShape_aGlow[IOSSABERSHAPE_MAX_GLOWS];
static int iosSaberShape_numGlows = 0;

// (a glow texel's alpha: at least just over the renderer's discard; and how
// much of what is behind it covers, of how bright it is)
#define IOSSABERSHAPE_ADD_ALPHA 3
#define IOSSABERSHAPE_GLOW_COVER 0.35f
#define IOSSABERSHAPE_GLOW_COVER_MAX 0.25f

static void iosSaberShape_MakeTexels(uint8_t* pOut, int w, int h, const unsigned char rgb[3], float gain)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float dx = ((x + 0.5f) / w) * 2.0f - 1.0f, dy = ((y + 0.5f) / h) * 2.0f - 1.0f;
            const float t = sqrtf(dx * dx + dy * dy);
            uint8_t* o = &pOut[(y * w + x) * 4];
            if (t >= 1.0f) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            const float f = powf(1.0f - t, IOSSABERSHAPE_GLOW_FALLOFF) * IOSSABERSHAPE_GLOW_PEAK * gain;
            float c[3];
            for (int k = 0; k < 3; k++) {
                c[k] = rgb[k] * f + IOSSABERSHAPE_GLOW_WHITE * f * f;
                if (c[k] > 255.0f) c[k] = 255.0f;
            }
            o[0] = (uint8_t)(c[2] + 0.5f); // (B, G, R, A: as TEX_MODE_16BPP reads them)
            o[1] = (uint8_t)(c[1] + 0.5f);
            o[2] = (uint8_t)(c[0] + 0.5f);
            // (and covering a little of what is behind, as much as it glows: over a dark
            // scene it adds, over a bright one it tints, rather than washing it out white: subtle there)
            float a = 255.0f * f * IOSSABERSHAPE_GLOW_COVER;
            if (a > 255.0f * IOSSABERSHAPE_GLOW_COVER_MAX) a = 255.0f * IOSSABERSHAPE_GLOW_COVER_MAX;
            o[3] = a < IOSSABERSHAPE_ADD_ALPHA ? IOSSABERSHAPE_ADD_ALPHA : (uint8_t)(a + 0.5f);
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
            iosSaberShape_MakeTexels(pOut, w, h, g->rgb, iosSaberShape_aGain[g->gain]);
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
#define IOSSABERSHAPE_MAX_QUEUED 384
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
// along the line of sight: where it was on screen, just behind)
static void iosSaberShape_SetBack(rdVector3* p)
{
    if (rdCamera_g_pCurCamera->projectType == rdCameraProjectType_Ortho || p->y <= (flex_t)0.0001) {
        p->y += IOSSABERSHAPE_SET_BACK;
        return;
    }
    const flex_t k = (p->y + IOSSABERSHAPE_SET_BACK) / p->y;
    p->x *= k;
    p->y *= k;
    p->z *= k;
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

// The blade being drawn with BEAM (between BeginBlade and EndBlade), its
// widths as they were, and its glow
static rdThing* iosSaberShape_pLine = NULL;
static flex_t iosSaberShape_savedBase, iosSaberShape_savedTip;
static iosSaberShapeGlow* iosSaberShape_pGlow = NULL;

void iosSaberShape_BeginBlade(SithThing* pThing, rdThing* pLine, const unsigned char rgb[3])
{
    iosSaberShape_EndBlade(); // (begun twice with no end between: put back first)
    (void)pThing;
    if (iosSaberShape_shape != IOSSABERSHAPE_BEAM || !pLine || !pLine->polyline) return;
    rdPolyline* l = pLine->polyline;
    iosSaberShape_pLine = pLine;
    iosSaberShape_savedBase = l->baseRadius;
    iosSaberShape_savedTip = l->tipRadius;
    // (even: as wide at the tip as at the hilt; the tip face, a disc of the tip's
    // radius centred on the end, closes it round)
    l->baseRadius = l->tipRadius = l->baseRadius * IOSSABERSHAPE_BEAM_WIDTH;
    iosSaberShape_pGlow = rgb ? iosSaberShape_Glow(rgb, IOSSABERSHAPE_GAIN_BLADE) : NULL;
}

void iosSaberShape_FitTip(rdThing* pLine, float tipDepth, float baseDepth)
{
    if (!pLine || pLine != iosSaberShape_pLine || !pLine->polyline) return;
    rdPolyline* l = pLine->polyline;
    float k = 1.0f;
    if (rdCamera_g_pCurCamera && rdCamera_g_pCurCamera->projectType != rdCameraProjectType_Ortho && baseDepth > 0.0001f && tipDepth > baseDepth)
        k = IOSSABERSHAPE_TIP_ON_SCREEN * tipDepth / baseDepth;
    if (k < 1.0f) k = 1.0f;
    if (k > IOSSABERSHAPE_TIP_MAX) k = IOSSABERSHAPE_TIP_MAX;
    l->tipRadius = l->baseRadius * k;
}

void iosSaberShape_EndBlade(void)
{
    if (!iosSaberShape_pLine) return;
    if (iosSaberShape_pLine->polyline) {
        iosSaberShape_pLine->polyline->baseRadius = iosSaberShape_savedBase;
        iosSaberShape_pLine->polyline->tipRadius = iosSaberShape_savedTip;
    }
    iosSaberShape_pLine = NULL;
    iosSaberShape_pGlow = NULL;
}

// The beam widens toward its tip (iosSaberShape_FitTip), so its side and the
// glow's length are not rectangles: drawn as one quad (two triangles), their
// texture's middle line bends off the blade's along the diagonal, to one side
// only. Each is drawn in this many slices along the blade instead, each its
// own width, so the middle stays on the middle.
#define IOSSABERSHAPE_SLICES 8

// The glow: square to the blade on screen, the base's half disc, the length
// (in slices) and the tip's half disc (camera space: tip, base, the glow's
// radius at each, (cos, sin) across the blade on screen). The glow texture's
// middle row runs along the length (across it, the glow's profile) and its
// top and bottom halves make the round ends, so they meet with no seam and
// nothing is drawn twice.
static void iosSaberShape_GlowQuads(rdThing* pLine, const iosSaberShapeGlow* g, rdTexMode_t texMode, const rdVector3* pTip,
                                    const rdVector3* pBase, flex_t rTip, flex_t rBase, flex_t angCos, flex_t angSin)
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
    iosSaberShape_Vert(0, &tipOut, tx, tz, w, 0);
    iosSaberShape_Vert(1, &tipOut, -tx, -tz, 0, 0);
    iosSaberShape_Vert(2, pTip, -tx, -tz, 0, mid);
    iosSaberShape_Vert(3, pTip, tx, tz, w, mid);
    iosSaberShape_Quad(pLine, pMat, texMode);
    // the hilt's end
    iosSaberShape_Vert(0, pBase, bx, bz, w, mid);
    iosSaberShape_Vert(1, pBase, -bx, -bz, 0, mid);
    iosSaberShape_Vert(2, &baseOut, -bx, -bz, 0, h);
    iosSaberShape_Vert(3, &baseOut, bx, bz, w, h);
    iosSaberShape_Quad(pLine, pMat, texMode);
}

static void iosSaberShape_DrawGlow(rdThing* pLine, const rdVector3* pTip, const rdVector3* pBase, float angCos, float angSin)
{
    if (!pLine || pLine != iosSaberShape_pLine || !iosSaberShape_pGlow || !pLine->polyline) return;
    iosSaberShape_GlowQuads(pLine, iosSaberShape_pGlow, pLine->polyline->face.textureMode, pTip, pBase,
                            pLine->polyline->tipRadius * IOSSABERSHAPE_GLOW_WIDTH, pLine->polyline->baseRadius * IOSSABERSHAPE_GLOW_WIDTH,
                            angCos, angSin);
}

// The beam's side, as rdPolyline_Draw lays the blade (its corners, its
// texture's corners), but in slices from the tip down, each its own width
static rdVector3 iosSaberShape_aSideVerts[4];
static rdVector2 iosSaberShape_aSideUVs[4];

int iosSaberShape_DrawBeam(rdThing* pLine, const rdVector3* pTip, const rdVector3* pBase, float angCos, float angSin)
{
    if (!pLine || pLine != iosSaberShape_pLine || !pLine->polyline) return 0;
    rdPolyline* l = pLine->polyline;
    if (!l->extraUVTipMaybe || !l->face.vertexUVIdx) return 0; // (untextured: as the game draws it)
    const flex_t w = l->extraUVTipMaybe[0].x, h = l->extraUVTipMaybe[2].y;
    const flex_t tx = l->tipRadius * angCos, tz = l->tipRadius * angSin, bx = l->baseRadius * angCos, bz = l->baseRadius * angSin;
    rdMeshinfo info;
    memset(&info, 0, sizeof(info));
    info.numVertices = 4;
    info.aVertices = iosSaberShape_aSideVerts;
    info.aTexVerticies = iosSaberShape_aSideUVs;
    for (int i = 0; i < IOSSABERSHAPE_SLICES; i++) {
        const flex_t s[2] = { (flex_t)i / IOSSABERSHAPE_SLICES, (flex_t)(i + 1) / IOSSABERSHAPE_SLICES };
        for (int e = 0; e < 2; e++) {
            const rdVector3 p = { pTip->x + (pBase->x - pTip->x) * s[e], pTip->y + (pBase->y - pTip->y) * s[e], pTip->z + (pBase->z - pTip->z) * s[e] };
            const flex_t ax = tx + (bx - tx) * s[e], az = tz + (bz - tz) * s[e];
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

// A thrown saber's blade: the mesh whose faces have its material (the first
// geoset's), and its long axis (its end points) in that mesh's own space; 0
// if it has none
static int iosSaberShape_ThrownBlade(const rdModel3* m, const rdMaterial* pBlade, int* pMesh, rdVector3* pA, rdVector3* pB)
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
        return e[ax] > 0;
    }
    return 0;
}

// Its glow as a held blade's, along the blade as it is drawn now (its mesh's
// node, as the model was just posed)
void iosSaberShape_DrawThrownGlow(SithThing* pThing, const rdMaterial* pBlade, const unsigned char rgb[3])
{
    if (iosSaberShape_shape != IOSSABERSHAPE_BEAM || !pThing || !pBlade || !rgb || !rdCamera_g_pCurCamera) return;
    rdThing* t = &pThing->renderData;
    if (t->type != RD_THING_MODEL3 || !t->model3 || !t->paJointMatrices) return;
    const rdModel3* m = t->model3;
    int mesh = -1, node = -1;
    rdVector3 a, b;
    if (!iosSaberShape_ThrownBlade(m, pBlade, &mesh, &a, &b)) return;
    for (uint32_t n = 0; n < m->numHNodes && node < 0; n++)
        if ((int)m->aHierarchyNodes[n].meshIdx == mesh) node = (int)n;
    if (node < 0) return;
    const iosSaberShapeGlow* g = iosSaberShape_Glow(rgb, IOSSABERSHAPE_GAIN_THROWN);
    if (!g) return;
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
    // (as wide as a held blade's of that length: the model's blade is drawn wider, with its blur)
    const flex_t r = stdMath_Dist3D1(b.x - a.x, b.y - a.y, b.z - a.z) * IOSSABERSHAPE_THROWN_BEAM * IOSSABERSHAPE_GLOW_WIDTH;
    iosSaberShape_GlowQuads(&glowThing, g, glowThing.curTexMode, &tip, &base, r, r, angCos, angSin);
}

#endif // JK_ENHANCED

// Added: entire file. The player's lightsaber as they choose it (see iosSaber.h).

#include "Platform/iOS/iosSaber.h"

#ifdef TARGET_IOS

#include <stdlib.h>
#include <string.h>

#include "Engine/rdMaterial.h"
#include "Platform/std3D.h"
#include "World/sithWorld.h"
#include "World/sithThing.h"
#include "World/jkPlayer.h"
#include "Gameplay/sithTime.h"
#include "Platform/iOS/iosSelfie.h"
#include "Primitives/rdMatrix.h"
#include "Gameplay/sithPlayer.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "SDL2_helper.h"
#include "jk.h"

// ------------------------------------------------------------ the colours

// A colour: the game's own materials for it (saber<stock>0.mat and 1.mat),
// or one made from the game's blue blade -- the white middle (core) going
// out to this edge colour where the blue one is blue
typedef struct {
    const char* key;
    const char* name;
    const char* stock;          // NULL: made
    unsigned char core[3];      // made: the middle
    unsigned char edge[3];      // made: the edges; and the swatch for all
} iosSaberColour;

// Round the wheel from the gap: the game's own, then by hue, red to pink,
// then white and black
static const iosSaberColour iosSaber_aColours[] = {
    { "game",      "ORIGINAL",   NULL,     {   0,   0,   0 }, { 150, 150, 150 } },
    { "red",       "RED",        "red",    {   0,   0,   0 }, { 255,  40,  30 } },
    { "crimson",   "CRIMSON",    "dred",   {   0,   0,   0 }, { 170,   0,  20 } },
    { "orange",    "ORANGE",     "orange", {   0,   0,   0 }, { 255, 120,  20 } },
    { "amber",     "AMBER",      NULL,     { 255, 250, 225 }, { 255, 165,   0 } },
    { "yellow",    "YELLOW",     "yellow", {   0,   0,   0 }, { 255, 230,  40 } },
    { "lime",      "LIME",       NULL,     { 245, 255, 225 }, { 150, 255,  20 } },
    { "green",     "GREEN",      "green",  {   0,   0,   0 }, {  40, 220,  60 } },
    { "teal",      "TEAL",       NULL,     { 230, 255, 250 }, {   0, 190, 150 } },
    { "cyan",      "CYAN",       NULL,     { 235, 255, 255 }, {   0, 235, 255 } },
    { "lightblue", "LIGHT BLUE", NULL,     { 245, 250, 255 }, { 120, 195, 255 } },
    { "blue",      "BLUE",       "blue",   {   0,   0,   0 }, {  50, 110, 255 } },
    { "purple",    "PURPLE",     "purple", {   0,   0,   0 }, { 160,  60, 255 } },
    { "magenta",   "MAGENTA",    NULL,     { 255, 235, 255 }, { 255,  20, 220 } },
    { "pink",      "PINK",       NULL,     { 255, 245, 250 }, { 255, 110, 185 } },
    { "white",     "WHITE",      NULL,     { 255, 255, 255 }, { 215, 225, 255 } },
    { "black",     "BLACK",      NULL,     {  10,  10,  14 }, { 245, 248, 255 } },
};
#define IOSSABER_NUM_COLOURS ((int)(sizeof(iosSaber_aColours) / sizeof(iosSaber_aColours[0])))

int iosSaber_NumColours(void)
{
    return IOSSABER_NUM_COLOURS;
}

const char* iosSaber_ColourName(int id)
{
    return (id >= 0 && id < IOSSABER_NUM_COLOURS) ? iosSaber_aColours[id].name : NULL;
}

const char* iosSaber_ColourKey(int id)
{
    return (id >= 0 && id < IOSSABER_NUM_COLOURS) ? iosSaber_aColours[id].key : NULL;
}

int iosSaber_ColourByKey(const char* key)
{
    for (int i = 0; key && i < IOSSABER_NUM_COLOURS; i++) {
        if (!strcmp(iosSaber_aColours[i].key, key)) return i;
    }
    return -1;
}

// The game's own colour, as near as can be told: the one of the game's
// colours whose side material the local player's blade has now, or -1
static int iosSaber_GameColour(void)
{
    SithThing* p = sithPlayer_g_pLocalPlayerThing;
    if (!p || !p->playerInfo || !p->playerInfo->polyline.face.material) return -1;
    char name[64];
    stdString_SafeStrCopy(name, p->playerInfo->polyline.face.material->mat_fpath, sizeof(name));
    for (char* q = name; *q; q++) {
        if (*q >= 'A' && *q <= 'Z') *q += 'a' - 'A';
    }
    for (int i = 0; i < IOSSABER_NUM_COLOURS; i++) {
        char side[32];
        if (!iosSaber_aColours[i].stock) continue;
        snprintf(side, sizeof(side), "saber%s1", iosSaber_aColours[i].stock);
        if (strstr(name, side)) return i;
    }
    return -1;
}

void iosSaber_ColourRGB(int id, unsigned char* pR, unsigned char* pG, unsigned char* pB)
{
    if (id < 0 || id >= IOSSABER_NUM_COLOURS) id = IOSSABER_COLOUR_GAME;
    if (id == IOSSABER_COLOUR_GAME) {
        const int game = iosSaber_GameColour();
        if (game >= 0) id = game;
    }
    *pR = iosSaber_aColours[id].edge[0];
    *pG = iosSaber_aColours[id].edge[1];
    *pB = iosSaber_aColours[id].edge[2];
}

// ------------------------------------------------------------ the styles

// A style: its blades, each on a hand (in time, more: crossguards' side
// blades and a double-bladed hilt's other end would add where on the hilt
// and which way each points, and a hilt of its own)
enum { IOSSABER_HAND_RIGHT = 0, IOSSABER_HAND_LEFT };
typedef struct {
    const char* name;
    int numBlades;
    int aHand[IOSSABER_MAX_BLADES];
    int bAvailable;
} iosSaberStyle;

static const iosSaberStyle iosSaber_aStyles[IOSSABER_NUM_STYLES] = {
    [IOSSABER_STYLE_SINGLE] = { "SINGLE", 1, { IOSSABER_HAND_RIGHT }, 1 },
    [IOSSABER_STYLE_DUAL]   = { "DUAL",   2, { IOSSABER_HAND_RIGHT, IOSSABER_HAND_LEFT }, 1 },
};

static int iosSaber_style = IOSSABER_STYLE_SINGLE;
static int iosSaber_aColour[IOSSABER_NUM_STYLES][IOSSABER_MAX_BLADES]; // (all the game's own to start)

int iosSaber_StyleAvailable(int style)
{
    return style >= 0 && style < IOSSABER_NUM_STYLES && iosSaber_aStyles[style].bAvailable;
}

const char* iosSaber_StyleName(int style)
{
    return (style >= 0 && style < IOSSABER_NUM_STYLES) ? iosSaber_aStyles[style].name : NULL;
}

int iosSaber_StyleNumBlades(int style)
{
    return (style >= 0 && style < IOSSABER_NUM_STYLES) ? iosSaber_aStyles[style].numBlades : 0;
}

int iosSaber_GetStyle(void)
{
    return iosSaber_style;
}

void iosSaber_SetStyle(int style)
{
    if (iosSaber_StyleAvailable(style)) iosSaber_style = style;
}

int iosSaber_GetColour(int style, int blade)
{
    if (style < 0 || style >= IOSSABER_NUM_STYLES || blade < 0 || blade >= IOSSABER_MAX_BLADES) return IOSSABER_COLOUR_GAME;
    return iosSaber_aColour[style][blade];
}

void iosSaber_SetColour(int style, int blade, int id)
{
    if (style < 0 || style >= IOSSABER_NUM_STYLES || blade < 0 || blade >= IOSSABER_MAX_BLADES) return;
    if (id < 0 || id >= IOSSABER_NUM_COLOURS) return;
    iosSaber_aColour[style][blade] = id;
}

// ------------------------------------------------------------ the materials

// Each colour's side (1) and tip (0) materials, loaded (privately: not the
// world's, so the game's own blades never change) the first time it is drawn
// on a level; a made colour's also carry their true-colour texels
enum { IOSSABER_SIDE = 0, IOSSABER_TIP, IOSSABER_NUM_PARTS };
static rdMaterial iosSaber_aMat[IOSSABER_NUM_COLOURS][IOSSABER_NUM_PARTS];
static int iosSaber_aLoaded[IOSSABER_NUM_COLOURS]; // 1: loaded; -1: tried, and couldn't be
static rdMaterial* iosSaber_pSavedSide = NULL;   // the game's, while a blade is drawn in another colour
static rdMaterial* iosSaber_pSavedTip = NULL;

// A made colour's texels, from the blue blade's: as bright as it is, and
// from core to edge as far as it is from white to blue (its saturation,
// nearly three times over: only the middle stays white, as on the game's own
// blades). Texel 0 (the
// tip's corners) stays clear. B, G, R, A: as TEX_MODE_16BPP reads them.
static void iosSaber_MakeTexels(const iosSaberColour* c, const uint8_t* pIdx, int w, int h, int pitch, const rdColor24* pal, uint8_t* pOut)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int idx = pIdx[y * pitch + x];
            uint8_t* o = &pOut[(y * w + x) * 4];
            if (idx == 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            const int r = pal[idx].r, g = pal[idx].g, b = pal[idx].b;
            const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            const float v = mx / 255.0f;
            float t = mx ? 2.8f * (mx - mn) / (float)mx : 0.0f;
            if (t > 1.0f) t = 1.0f;
            float out[3];
            for (int k = 0; k < 3; k++) out[k] = (c->core[k] * (1.0f - t) + c->edge[k] * t) * v;
            o[0] = (uint8_t)(out[2] + 0.5f);
            o[1] = (uint8_t)(out[1] + 0.5f);
            o[2] = (uint8_t)(out[0] + 0.5f);
            o[3] = 255;
        }
    }
}

// Every texture (and size) of a material: its texels made in this colour
static int iosSaber_Recolour(rdMaterial* pMat, const iosSaberColour* c, const rdColor24* pal)
{
    for (uint32_t i = 0; i < pMat->num_texinfo; i++) {
        rdTexture* pTex = pMat->texinfos[i] ? pMat->texinfos[i]->texture_ptr : NULL;
        for (uint32_t j = 0; pTex && j < pTex->num_mipmaps; j++) {
            tVBuffer* v = pTex->texture_struct[j];
            if (!v || !v->sdlSurface || !v->sdlSurface->pixels || v->format.format.is16bit) return 0;
            const int w = v->format.width, h = v->format.height;
            uint8_t* pOut = (uint8_t*)malloc((size_t)w * h * 4);
            if (!pOut) return 0;
            iosSaber_MakeTexels(c, (const uint8_t*)v->sdlSurface->pixels, w, h, v->sdlSurface->pitch, pal, pOut);
            free(v->pTrueColour);
            v->pTrueColour = pOut;
            // (anything already uploaded is the 8-bit picture)
            std3D_PurgeSurfaceRefs(&pTex->alphaMats[j]);
            std3D_PurgeSurfaceRefs(&pTex->opaqueMats[j]);
            pTex->alphaMats[j].texture_loaded = 0;
            pTex->opaqueMats[j].texture_loaded = 0;
        }
    }
    return 1;
}

static void iosSaber_FreeMaterial(rdMaterial* pMat)
{
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

static int iosSaber_Load(int id)
{
    if (iosSaber_aLoaded[id]) return iosSaber_aLoaded[id] > 0;
    iosSaber_aLoaded[id] = -1;
    const iosSaberColour* c = &iosSaber_aColours[id];
    const rdColor24* pal = (sithWorld_g_pCurrentWorld && sithWorld_g_pCurrentWorld->colormaps)
                           ? sithWorld_g_pCurrentWorld->colormaps[0].colors : NULL;
    if (!c->stock && !pal) return 0;
    char aPath[IOSSABER_NUM_PARTS][64];
    for (int p = 0; p < IOSSABER_NUM_PARTS; p++) {
        snprintf(aPath[p], sizeof(aPath[p]), "mat\\saber%s%d.mat", c->stock ? c->stock : "blue", p == IOSSABER_SIDE ? 1 : 0);
    }
    for (int p = 0; p < IOSSABER_NUM_PARTS; p++) {
        if (!rdMaterial_LoadEntry(aPath[p], &iosSaber_aMat[id][p], 0, 0)
            || (!c->stock && !iosSaber_Recolour(&iosSaber_aMat[id][p], c, pal))) {
            for (int q = 0; q <= p; q++) iosSaber_FreeMaterial(&iosSaber_aMat[id][q]);
            return 0;
        }
    }
    iosSaber_aLoaded[id] = 1;
    return 1;
}

static rdModel3* iosSaber_pArmModel; // (below: the second saber's arm)
static int iosSaber_echoCount;

void iosSaber_FreeWorld(void)
{
    for (int id = 0; id < IOSSABER_NUM_COLOURS; id++) {
        if (iosSaber_aLoaded[id] > 0) {
            for (int p = 0; p < IOSSABER_NUM_PARTS; p++) iosSaber_FreeMaterial(&iosSaber_aMat[id][p]);
        }
        iosSaber_aLoaded[id] = 0;
    }
    // (the arms' joints found again for the next level's model, even one at the same address)
    iosSaber_pArmModel = NULL;
    iosSaber_echoCount = 0;
}

// ------------------------------------------------------------ drawing

// (only the player's own, in single player: no one else's game knows of it)
static int iosSaber_IsPlayers(SithThing* pThing)
{
    return pThing && pThing == sithPlayer_g_pLocalPlayerThing && !sithNet_isMulti;
}

int iosSaber_HasSecondBlade(SithThing* pThing)
{
    // (not in a selfie: it poses the player with one hand on the phone and the other free, or showing off
    // the one weapon)
    return iosSaber_IsPlayers(pThing) && iosSaber_aStyles[iosSaber_style].numBlades >= 2 && !iosSelfie_IsActive();
}

void iosSaber_BeginBlade(SithThing* pThing, rdThing* pLine, int blade)
{
    iosSaber_pSavedSide = iosSaber_pSavedTip = NULL;
    if (!iosSaber_IsPlayers(pThing) || !pLine || !pLine->polyline) return;
    const iosSaberStyle* st = &iosSaber_aStyles[iosSaber_style];
    // (a blade the style doesn't have -- the game's own second one -- as its first)
    if (blade < 0 || blade >= st->numBlades) blade = 0;
    const int id = iosSaber_aColour[iosSaber_style][blade];
    if (id == IOSSABER_COLOUR_GAME || !iosSaber_Load(id)) return;
    rdPolyline* l = pLine->polyline;
    iosSaber_pSavedSide = l->face.material;
    iosSaber_pSavedTip = l->tipFace.material;
    l->face.material = &iosSaber_aMat[id][IOSSABER_SIDE];
    l->tipFace.material = &iosSaber_aMat[id][IOSSABER_TIP];
}

void iosSaber_EndBlade(rdThing* pLine)
{
    if (iosSaber_pSavedSide && pLine && pLine->polyline) {
        pLine->polyline->face.material = iosSaber_pSavedSide;
        pLine->polyline->tipFace.material = iosSaber_pSavedTip;
    }
    iosSaber_pSavedSide = iosSaber_pSavedTip = NULL;
}

// ------------------------------------------------------------ the second saber's arm

// The arms' joints, right and left, of the model it was last for: shoulder, forearm, hand (by their
// names: "...rshouldr"/"...rshoulder", "...rforearm", "...rhand", and the same with "l")
#define IOSSABER_ARM_NODES 3
#define IOSSABER_LEFT_SPREAD_DEG 40.0f
static rdModel3* iosSaber_pArmModel = NULL;
static int iosSaber_aArmR[IOSSABER_ARM_NODES], iosSaber_aArmL[IOSSABER_ARM_NODES];
static int iosSaber_bArms = 0;

// The right arm's poses (each joint relative to the one it hangs from), kept a while
#define IOSSABER_ECHO_FRAMES 48
#define IOSSABER_ECHO_GAP_MS 500 // (not posed for longer than this: start over)
typedef struct {
    uint32_t ms;
    rdMatrix34 aJoint[IOSSABER_ARM_NODES];
} iosSaberEcho;
static iosSaberEcho iosSaber_aEcho[IOSSABER_ECHO_FRAMES];
static int iosSaber_echoHead = 0, iosSaber_echoCount = 0;

static int iosSaber_FindNode(rdModel3* pModel, const char* side, const char* const* aSuffix)
{
    for (int i = 0; i < pModel->numHNodes; i++) {
        const char* name = pModel->aHierarchyNodes[i].name;
        const int l = (int)strlen(name);
        for (int k = 0; aSuffix[k]; k++) {
            char want[32];
            snprintf(want, sizeof(want), "%s%s", side, aSuffix[k]);
            const int w = (int)strlen(want);
            if (l >= w && !__strcmpi(name + l - w, want)) return i;
        }
    }
    return -1;
}

static void iosSaber_FindArms(rdModel3* pModel)
{
    static const char* const aShoulder[] = { "shouldr", "shoulder", NULL };
    static const char* const aForearm[] = { "forearm", NULL };
    static const char* const aHand[] = { "hand", NULL };
    const char* const* aaPart[IOSSABER_ARM_NODES] = { aShoulder, aForearm, aHand };
    iosSaber_pArmModel = pModel;
    iosSaber_bArms = 1;
    iosSaber_echoCount = 0;
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) {
        iosSaber_aArmR[k] = iosSaber_FindNode(pModel, "r", aaPart[k]);
        iosSaber_aArmL[k] = iosSaber_FindNode(pModel, "l", aaPart[k]);
        if (iosSaber_aArmR[k] < 0 || iosSaber_aArmL[k] < 0 || iosSaber_aArmR[k] == iosSaber_aArmL[k]) iosSaber_bArms = 0;
    }
}

void iosSaber_PoseArms(rdThing* pRdThing)
{
    SithThing* pThing = pRdThing ? pRdThing->pThing : NULL;
    if (!iosSaber_HasSecondBlade(pThing) || !(pThing->jkFlags & JKFLAG_SABERON) || pRdThing != &pThing->renderData
        || !pRdThing->model3 || !pRdThing->paJointMatrices)
        return;
    if (pRdThing->model3 != iosSaber_pArmModel) iosSaber_FindArms(pRdThing->model3);
    if (!iosSaber_bArms) return;

    // This frame's right arm kept (once a frame: the latest pose for that time)
    const uint32_t now = sithTime_g_msecGameTime;
    iosSaberEcho* pNow = &iosSaber_aEcho[iosSaber_echoHead];
    if (iosSaber_echoCount && (now < pNow->ms || now - pNow->ms > IOSSABER_ECHO_GAP_MS)) iosSaber_echoCount = 0;
    if (!iosSaber_echoCount || now != pNow->ms) {
        iosSaber_echoHead = (iosSaber_echoHead + 1) % IOSSABER_ECHO_FRAMES;
        if (iosSaber_echoCount < IOSSABER_ECHO_FRAMES) iosSaber_echoCount++;
        pNow = &iosSaber_aEcho[iosSaber_echoHead];
        pNow->ms = now;
    }
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) pNow->aJoint[k] = pRdThing->paJointMatrices[iosSaber_aArmR[k]];

    // ...the one IOSSABER_ECHO_MS before (or the oldest kept), mirrored onto the left arm: each joint as
    // its right one is, seen in a mirror across the body (x the other way, before and after)
    const iosSaberEcho* pPast = pNow;
    for (int k = 0; k < iosSaber_echoCount; k++) {
        const iosSaberEcho* e = &iosSaber_aEcho[(iosSaber_echoHead - k + IOSSABER_ECHO_FRAMES) % IOSSABER_ECHO_FRAMES];
        pPast = e;
        if (now - e->ms >= IOSSABER_ECHO_MS) break;
    }
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) {
        const rdMatrix34* r = &pPast->aJoint[k];
        rdMatrix34* l = &pRdThing->paJointMatrices[iosSaber_aArmL[k]];
        l->rvec.x =  r->rvec.x; l->rvec.y = -r->rvec.y; l->rvec.z = -r->rvec.z;
        l->lvec.x = -r->lvec.x; l->lvec.y =  r->lvec.y; l->lvec.z =  r->lvec.z;
        l->uvec.x = -r->uvec.x; l->uvec.y =  r->uvec.y; l->uvec.z =  r->uvec.z;
        l->scale.x = -r->scale.x; l->scale.y = r->scale.y; l->scale.z = r->scale.z;
    }
    // ...and the shoulder turned out a little, so the second saber is held off to the side, not where the
    // first is (the game's stance holds the hilt in front of the body's middle, which a mirror keeps there)
    rdVector3 spread = { 0.0f, IOSSABER_LEFT_SPREAD_DEG, 0.0f };
    rdMatrix_PreRotate34(&pRdThing->paJointMatrices[iosSaber_aArmL[0]], &spread);
}

#endif // TARGET_IOS

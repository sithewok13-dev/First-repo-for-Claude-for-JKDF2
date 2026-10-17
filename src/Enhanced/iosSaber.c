// Added: entire file. The player's lightsaber as they choose it (see iosSaber.h).

#include "Enhanced/iosSaber.h"
#include "Enhanced/iosStance.h"
#include "Enhanced/iosSaberThrow.h"
#include "Enhanced/iosSaberShape.h"
#include "Primitives/rdModel3.h"

#ifdef JK_ENHANCED

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Engine/rdMaterial.h"
#include "Platform/std3D.h"
#include "World/sithWorld.h"
#include "World/sithThing.h"
#include "World/jkPlayer.h"
#include "Gameplay/sithTime.h"
#include "Enhanced/iosSelfie.h"
#include "Primitives/rdMatrix.h"
#include "Gameplay/sithPlayer.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "SDL2_helper.h"
#include "jk.h"
#include "Main/jkPlayerLook.h" // (other players' sabers, in multiplayer)

// ------------------------------------------------------------ the colours

// A colour: the game's own materials for it (saber<stock>0.mat and 1.mat),
// or one made from another of the game's blades (from) -- its white middle
// (core) going out to this edge colour where that one is coloured. The
// game's blue blade is nearly all white (a thin rim of blue): a strong
// colour is made from the green one instead (a narrow white middle, a wide
// band of colour), a pale one from the blue.
typedef struct {
    const char* key;
    const char* name;
    const char* stock;          // NULL: made
    const char* from;           // made: the game's blade it is made from
    unsigned char core[3];      // made: the middle
    unsigned char edge[3];      // made: the edges; and the swatch for all
    float spread;               // made: how soon the colour comes in out from the middle (0: 2.8)
    float glow;                 // made: how slowly the colour fades to the edges (0: as the game's
                                // blade does), the falloff's power: under 1 holds the colour out
    int hide;                   // not on the wheel in these games (IOSSABER_HIDE_...): kept, saved
                                // and drawn as ever when it is already chosen, or another player's
} iosSaberColour;
enum { IOSSABER_HIDE_JK = 1, IOSSABER_HIDE_MOTS = 2 };

// Each look's colours, by id (each table in the same order, with the same keys
// and names): REFORGED's, then CLASSIC's.
//
// REFORGED. Round the wheel from the gap: the game's own, then by hue, red to pink,
// then white and black. Red, light blue and purple are the classic film
// blades, sampled across: red a narrow white middle into a deep rose red that
// fades fast; light blue a pale cyan-white middle easing into a soft, pale sky
// blue; purple a wide white middle into a strong violet that holds out far.
// The other strong colours glow the same way (crimson darker and fading faster
// than red, pink lightly); yellow stays the game's own yellow blade, blue the
// deep blue, and white and black the plain look of the game's blue blade.
// Green is not on Jedi Knight's wheel: ORIGINAL is its green.
static const iosSaberColour iosSaber_aColours[] = {
    { "game",      "ORIGINAL",   NULL,    NULL,    {   0,   0,   0 }, { 150, 150, 150 } },
    { "red",       "RED",        NULL,    "green", { 255, 248, 248 }, { 230,  45,  75 }, 2.0f, 0.6f },
    { "crimson",   "CRIMSON",    NULL,    "green", { 255, 236, 236 }, { 165,  10,  30 }, 2.0f, 0.8f },
    { "orange",    "ORANGE",     NULL,    "green", { 255, 250, 238 }, { 255,  95,  20 }, 1.6f, 0.6f },
    { "amber",     "AMBER",      NULL,    "green", { 255, 250, 232 }, { 255, 158,  18 }, 1.6f, 0.6f },
    { "yellow",    "YELLOW",     "yellow", NULL,   {   0,   0,   0 }, { 255, 230,  40 } },
    { "lime",      "LIME",       NULL,    "green", { 248, 255, 235 }, { 150, 240,  40 }, 1.4f, 0.6f },
    { "green",     "GREEN",      NULL,    "green", { 242, 255, 242 }, {  50, 225,  75 }, 1.4f, 0.6f, IOSSABER_HIDE_JK },
    { "teal",      "TEAL",       NULL,    "green", { 238, 255, 250 }, {  15, 185, 150 }, 1.4f, 0.6f },
    { "cyan",      "CYAN",       NULL,    "green", { 240, 255, 255 }, {  20, 225, 240 }, 1.4f, 0.6f },
    { "lightblue", "LIGHT BLUE", NULL,    "green", { 240, 252, 255 }, { 115, 160, 240 }, 1.4f, 0.6f },
    { "blue",      "BLUE",       NULL,    "green", { 235, 245, 255 }, {  50, 110, 255 } },
    { "purple",    "PURPLE",     NULL,    "green", { 255, 255, 255 }, { 190,  67, 240 }, 1.2f, 0.55f },
    { "magenta",   "MAGENTA",    NULL,    "green", { 255, 245, 255 }, { 245,  35, 195 }, 1.4f, 0.6f },
    { "pink",      "PINK",       NULL,    "green", { 255, 248, 252 }, { 255, 115, 185 }, 2.0f, 0.8f },
    { "white",     "WHITE",      NULL,    "blue",  { 255, 255, 255 }, { 215, 225, 255 } },
    { "black",     "BLACK",      NULL,    "blue",  {  10,  10,  14 }, { 245, 248, 255 } },
};
#define IOSSABER_NUM_COLOURS ((int)(sizeof(iosSaber_aColours) / sizeof(iosSaber_aColours[0])))

// CLASSIC: the colours as they were first made, row for row: the game's own
// red, crimson, orange, yellow, green and purple blades, the others made with
// the plain look of the game's blades, and green on both games' wheels
static const iosSaberColour iosSaber_aClassicColours[] = {
    { "game",      "ORIGINAL",   NULL,    NULL,    {   0,   0,   0 }, { 150, 150, 150 } },
    { "red",       "RED",        "red",   NULL,    {   0,   0,   0 }, { 255,  40,  30 } },
    { "crimson",   "CRIMSON",    "dred",  NULL,    {   0,   0,   0 }, { 170,   0,  20 } },
    { "orange",    "ORANGE",     "orange", NULL,   {   0,   0,   0 }, { 255, 120,  20 } },
    { "amber",     "AMBER",      NULL,    "green", { 255, 250, 225 }, { 255, 165,   0 } },
    { "yellow",    "YELLOW",     "yellow", NULL,   {   0,   0,   0 }, { 255, 230,  40 } },
    { "lime",      "LIME",       NULL,    "green", { 245, 255, 225 }, { 150, 255,  20 } },
    { "green",     "GREEN",      "green", NULL,    {   0,   0,   0 }, {  40, 220,  60 } },
    { "teal",      "TEAL",       NULL,    "green", { 230, 255, 250 }, {   0, 190, 150 } },
    { "cyan",      "CYAN",       NULL,    "green", { 235, 255, 255 }, {   0, 235, 255 } },
    { "lightblue", "LIGHT BLUE", NULL,    "blue",  { 245, 250, 255 }, { 120, 195, 255 } },
    { "blue",      "BLUE",       NULL,    "green", { 235, 245, 255 }, {  50, 110, 255 } },
    { "purple",    "PURPLE",     "purple", NULL,   {   0,   0,   0 }, { 160,  60, 255 } },
    { "magenta",   "MAGENTA",    NULL,    "green", { 255, 235, 255 }, { 255,  20, 220 } },
    { "pink",      "PINK",       NULL,    "green", { 255, 245, 250 }, { 255, 110, 185 } },
    { "white",     "WHITE",      NULL,    "blue",  { 255, 255, 255 }, { 215, 225, 255 } },
    { "black",     "BLACK",      NULL,    "blue",  {  10,  10,  14 }, { 245, 248, 255 } },
};
typedef char iosSaber_aClassicColours_size_check[(sizeof(iosSaber_aClassicColours) / sizeof(iosSaber_aClassicColours[0])
                                                  == IOSSABER_NUM_COLOURS) ? 1 : -1];

// ------------------------------------------------------------ the look

static int iosSaber_look = IOSSABER_LOOK_REFORGED;
static const char* const iosSaber_aLookName[IOSSABER_NUM_LOOKS] = {
    [IOSSABER_LOOK_CLASSIC] = "CLASSIC", [IOSSABER_LOOK_REFORGED] = "REFORGED"
};

int iosSaber_GetLook(void)
{
    return iosSaber_look;
}

void iosSaber_SetLook(int look)
{
    if (look >= 0 && look < IOSSABER_NUM_LOOKS) iosSaber_look = look;
}

void iosSaber_ToggleLook(void)
{
    iosSaber_look = iosSaber_look == IOSSABER_LOOK_CLASSIC ? IOSSABER_LOOK_REFORGED : IOSSABER_LOOK_CLASSIC;
}

const char* iosSaber_LookName(int look)
{
    return (look >= 0 && look < IOSSABER_NUM_LOOKS) ? iosSaber_aLookName[look] : NULL;
}

int iosSaber_LookByName(const char* name)
{
    for (int i = 0; name && i < IOSSABER_NUM_LOOKS; i++)
        if (!strcmp(iosSaber_aLookName[i], name)) return i;
    return -1;
}

// A colour as the look draws it (id in range)
static const iosSaberColour* iosSaber_Row(int look, int id)
{
    return look == IOSSABER_LOOK_CLASSIC ? &iosSaber_aClassicColours[id] : &iosSaber_aColours[id];
}

int iosSaber_NumColours(void)
{
    return IOSSABER_NUM_COLOURS;
}

int iosSaber_ColourOnWheel(int id)
{
    if (id < 0 || id >= IOSSABER_NUM_COLOURS) return 0;
    return !(iosSaber_Row(iosSaber_look, id)->hide & (Main_bMotsCompat ? IOSSABER_HIDE_MOTS : IOSSABER_HIDE_JK));
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

// The game's own blades (each colour here is made instead), and the swatch each
// had as the game's: ORIGINAL shows the game's own blade exactly as the game
// draws it, and its swatch with it
static const struct { const char* side; unsigned char rgb[3]; } iosSaber_aGameSwatches[] = {
    { "saberdred1",   { 170,   0,  20 } },
    { "saberred1",    { 255,  40,  30 } },
    { "saberorange1", { 255, 120,  20 } },
    { "sabergreen1",  {  40, 220,  60 } },
    { "saberyellow1", { 255, 230,  40 } },
    { "saberblue1",   {  50, 110, 255 } },
    { "saberpurple1", { 160,  60, 255 } },
};

// The swatch of the game's blade a material is (by its name), or NULL
static const unsigned char* iosSaber_SwatchOf(const rdMaterial* pMat)
{
    if (!pMat) return NULL;
    char name[64];
    stdString_SafeStrCopy(name, pMat->mat_fpath, sizeof(name));
    for (char* q = name; *q; q++) {
        if (*q >= 'A' && *q <= 'Z') *q += 'a' - 'A';
    }
    for (int i = 0; i < (int)(sizeof(iosSaber_aGameSwatches) / sizeof(iosSaber_aGameSwatches[0])); i++) {
        if (strstr(name, iosSaber_aGameSwatches[i].side)) return iosSaber_aGameSwatches[i].rgb;
    }
    return NULL;
}

// The game's own colour's swatch, as near as can be told: that of the game's
// blade whose side material the local player's blade has now, or NULL
static const unsigned char* iosSaber_GameSwatch(void)
{
    SithThing* p = sithPlayer_g_pLocalPlayerThing;
    if (!p || !p->playerInfo) return NULL;
    return iosSaber_SwatchOf(p->playerInfo->polyline.face.material);
}

void iosSaber_ColourRGB(int id, unsigned char* pR, unsigned char* pG, unsigned char* pB)
{
    if (id < 0 || id >= IOSSABER_NUM_COLOURS) id = IOSSABER_COLOUR_GAME;
    const unsigned char* c = iosSaber_Row(iosSaber_look, id)->edge;
    if (id == IOSSABER_COLOUR_GAME) {
        const unsigned char* game = iosSaber_GameSwatch();
        if (game) c = game;
    }
    *pR = c[0];
    *pG = c[1];
    *pB = c[2];
}

// ------------------------------------------------------------ the styles

// A style: its blades, each on a hand, and whether the blade is drawn with a
// crossguard (its side blades drawn with it, in its colour, iosSaberShape: no
// blade of their own). (In time, more: a double-bladed hilt's other end would
// add where on the hilt and which way it points, and a hilt of its own.)
enum { IOSSABER_HAND_RIGHT = 0, IOSSABER_HAND_LEFT };
typedef struct {
    const char* name;
    int numBlades;
    int aHand[IOSSABER_MAX_BLADES];
    int bAvailable;
    int bCrossguard;
} iosSaberStyle;

static const iosSaberStyle iosSaber_aStyles[IOSSABER_NUM_STYLES] = {
    [IOSSABER_STYLE_SINGLE] = { "SINGLE", 1, { IOSSABER_HAND_RIGHT }, 1, 0 },
    [IOSSABER_STYLE_DUAL]   = { "DUAL",   2, { IOSSABER_HAND_RIGHT, IOSSABER_HAND_LEFT }, 1, 0 },
    [IOSSABER_STYLE_CROSS]  = { "CROSS",  1, { IOSSABER_HAND_RIGHT }, 1, 1 },
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

int iosSaber_StyleHasCrossguard(int style)
{
    return (style >= 0 && style < IOSSABER_NUM_STYLES) ? iosSaber_aStyles[style].bCrossguard : 0;
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

int iosSaber_GetShownColour(int style, int blade)
{
    const int id = iosSaber_GetColour(style, blade);
    return iosSaber_ColourOnWheel(id) ? id : IOSSABER_COLOUR_GAME;
}

// ------------------------------------------------------------ the materials

// Each colour's side (1) and tip (0) materials, loaded (privately: not the
// world's, so the game's own blades never change) the first time it is drawn
// on a level, in the look it is drawn in (each look's kept apart, so switching
// back and forth makes nothing again); a made colour's also carry their
// true-colour texels
enum { IOSSABER_SIDE = 0, IOSSABER_TIP, IOSSABER_NUM_PARTS };
static rdMaterial iosSaber_aMat[IOSSABER_NUM_LOOKS][IOSSABER_NUM_COLOURS][IOSSABER_NUM_PARTS];
static int iosSaber_aLoaded[IOSSABER_NUM_LOOKS][IOSSABER_NUM_COLOURS]; // 1: loaded; -1: tried, and couldn't be
static rdMaterial* iosSaber_pSavedSide = NULL;   // the game's, while a blade is drawn in another colour
static rdMaterial* iosSaber_pSavedTip = NULL;

// A made colour's texels, from its game blade's: as bright as it is, and
// from core to edge as far as it is from white to its colour (its saturation,
// nearly three times over: only the middle stays white, as on the game's own
// blades). Texel 0 (the
// tip's corners) stays clear. B, G, R, A: as TEX_MODE_16BPP reads them.
#define IOSSABER_PALE_EDGE 0.75f
static void iosSaber_MakeTexels(const iosSaberColour* c, const uint8_t* pIdx, int w, int h, int pitch, const rdColor24* pal, uint8_t* pOut)
{
    // (pale: its edge colour light -- white -- and no glow of its own)
    const int bPale = !c->glow && c->edge[0] >= 120 && c->edge[1] >= 120 && c->edge[2] >= 120;
    const float spread = c->spread ? c->spread : 2.8f;
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
            // (a pale colour's edges kept bright: the blue blade's darkest
            // specks, in white, look like grit against a bright sky)
            float v = mx / 255.0f;
            if (bPale && v < IOSSABER_PALE_EDGE) v = IOSSABER_PALE_EDGE + (v - IOSSABER_PALE_EDGE) * 0.35f;
            if (c->glow) v = powf(v, c->glow);
            float t = mx ? spread * (mx - mn) / (float)mx : 0.0f;
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

// (the look's materials for a colour, loaded if need be; NULL if they can't be)
static rdMaterial* iosSaber_Load(int id)
{
    const int look = iosSaber_look;
    rdMaterial* aMat = iosSaber_aMat[look][id];
    int* pLoaded = &iosSaber_aLoaded[look][id];
    if (*pLoaded) return *pLoaded > 0 ? aMat : NULL;
    *pLoaded = -1;
    const iosSaberColour* c = iosSaber_Row(look, id);
    const rdColor24* pal = (sithWorld_g_pCurrentWorld && sithWorld_g_pCurrentWorld->colormaps)
                           ? sithWorld_g_pCurrentWorld->colormaps[0].colors : NULL;
    if (!c->stock && !pal) return NULL;
    char aPath[IOSSABER_NUM_PARTS][64];
    for (int p = 0; p < IOSSABER_NUM_PARTS; p++) {
        snprintf(aPath[p], sizeof(aPath[p]), "mat\\saber%s%d.mat", c->stock ? c->stock : c->from, p == IOSSABER_SIDE ? 1 : 0);
    }
    for (int p = 0; p < IOSSABER_NUM_PARTS; p++) {
        if (!rdMaterial_LoadEntry(aPath[p], &aMat[p], 0, 0)
            || (!c->stock && !iosSaber_Recolour(&aMat[p], c, pal))) {
            for (int q = 0; q <= p; q++) iosSaber_FreeMaterial(&aMat[q]);
            return NULL;
        }
    }
    *pLoaded = 1;
    return aMat;
}

// ------------------------------------------------------------ thrown sabers

// A thrown saber (Mysteries of the Sith's +fsab_..., a spinning model whose
// blade is its first material, sab0<colour>.mat) drawn in a colour: made from
// its green or blue one as the hand-held blades are (iosSaber_MakeTexels), loaded as needed and
// freed with the level (each look's apart). While one is drawn, its blade's faces have it.
static rdMaterial iosSaber_aThrownMat[IOSSABER_NUM_LOOKS][IOSSABER_NUM_COLOURS];
static int iosSaber_aThrownLoaded[IOSSABER_NUM_LOOKS][IOSSABER_NUM_COLOURS]; // 1: loaded; -1: tried, and couldn't be
static rdModel3* iosSaber_pThrownModel = NULL; // (the one being drawn, its blade as it was and as it is drawn)
static rdMaterial* iosSaber_pThrownBlade = NULL;
static rdMaterial* iosSaber_pThrownDrawn = NULL;

static const char* iosSaber_ThrownName(const char* key)
{
    static const char* aMap[][2] = { { "red", "red" }, { "dred", "dred" }, { "orange", "oran" }, { "yellow", "yell" },
                                     { "green", "gree" }, { "blue", "blue" }, { "purple", "purp" } };
    for (int i = 0; key && i < (int)(sizeof(aMap) / sizeof(aMap[0])); i++)
        if (!strcmp(aMap[i][0], key)) return aMap[i][1];
    return NULL;
}

static rdMaterial* iosSaber_ThrownMat(int id)
{
    if (id <= IOSSABER_COLOUR_GAME || id >= IOSSABER_NUM_COLOURS) return NULL;
    const int look = iosSaber_look;
    rdMaterial* pMat = &iosSaber_aThrownMat[look][id];
    int* pLoaded = &iosSaber_aThrownLoaded[look][id];
    if (*pLoaded) return *pLoaded > 0 ? pMat : NULL;
    *pLoaded = -1;
    const iosSaberColour* c = iosSaber_Row(look, id);
    const char* name = iosSaber_ThrownName(c->stock ? c->stock : c->from);
    const rdColor24* pal = (sithWorld_g_pCurrentWorld && sithWorld_g_pCurrentWorld->colormaps)
                           ? sithWorld_g_pCurrentWorld->colormaps[0].colors : NULL;
    if (!name || (!c->stock && !pal)) return NULL;
    char path[64];
    snprintf(path, sizeof(path), "3do\\mat\\sab0%s.mat", name); // (as the thrown saber models have them)
    if (!rdMaterial_LoadEntry(path, pMat, 0, 0)) return NULL;
    if (!c->stock && !iosSaber_Recolour(pMat, c, pal)) {
        iosSaber_FreeMaterial(pMat);
        return NULL;
    }
    *pLoaded = 1;
    return pMat;
}

static void iosSaber_SwapThrownBlade(rdModel3* m, rdMaterial* from, rdMaterial* to)
{
    for (uint32_t g = 0; g < m->numGeos && g < 4; g++) {
        for (uint32_t k = 0; k < m->aGeos[g].numMeshes; k++) {
            rdMesh* mesh = &m->aGeos[g].aMeshes[k];
            for (int f = 0; f < mesh->numFaces; f++)
                if (mesh->faces[f].material == from) mesh->faces[f].material = to;
        }
    }
}

void iosSaber_BeginThrown(SithThing* pThing)
{
    iosSaber_pThrownModel = NULL;
    const int id = iosSaberThrow_ThrownColour(pThing);
    if (id <= IOSSABER_COLOUR_GAME || pThing->renderData.type != RD_THING_MODEL3) return;
    rdModel3* m = pThing->renderData.model3;
    if (!m || m->sizeMaterials < 1 || !m->aMaterials || !m->aMaterials[0]) return;
    rdMaterial* pMat = iosSaber_ThrownMat(id);
    if (!pMat) return;
    iosSaber_pThrownModel = m;
    iosSaber_pThrownBlade = m->aMaterials[0];
    iosSaber_pThrownDrawn = pMat;
    iosSaber_SwapThrownBlade(m, iosSaber_pThrownBlade, pMat);
}

// The swatch of the game's thrown saber a blade material is (3do\mat\sab0<name>.mat), or NULL
static const unsigned char* iosSaber_ThrownSwatchOf(const rdMaterial* pMat)
{
    static const char* aKey[] = { "dred", "red", "orange", "yellow", "green", "blue", "purple" };
    if (!pMat) return NULL;
    char name[64];
    stdString_SafeStrCopy(name, pMat->mat_fpath, sizeof(name));
    for (char* q = name; *q; q++) {
        if (*q >= 'A' && *q <= 'Z') *q += 'a' - 'A';
    }
    for (int i = 0; i < (int)(sizeof(aKey) / sizeof(aKey[0])); i++) {
        char thrown[32], side[32];
        snprintf(thrown, sizeof(thrown), "sab0%s", iosSaber_ThrownName(aKey[i]));
        if (!strstr(name, thrown)) continue;
        snprintf(side, sizeof(side), "saber%s1", aKey[i]);
        for (int k = 0; k < (int)(sizeof(iosSaber_aGameSwatches) / sizeof(iosSaber_aGameSwatches[0])); k++)
            if (!strcmp(iosSaber_aGameSwatches[k].side, side)) return iosSaber_aGameSwatches[k].rgb;
    }
    return NULL;
}

void iosSaber_EndThrown(SithThing* pThing)
{
    // its glow (iosSaberShape), in the colour it is drawn in: the one put on, or the game's own;
    // thrown with a crossguard (CROSS), its side blades too, as the held blade's side
    const int bCross = pThing && iosSaber_StyleHasCrossguard(iosSaberThrow_ThrownStyle(pThing));
    if (pThing && (bCross || iosSaberShape_Get() == IOSSABERSHAPE_BEAM)) {
        const int id = iosSaberThrow_ThrownColour(pThing);
        if (id >= IOSSABER_COLOUR_GAME && id < IOSSABER_NUM_COLOURS) {
            const unsigned char* rgb = NULL;
            const rdMaterial* pBlade = NULL;
            if (iosSaber_pThrownModel) {
                rgb = iosSaber_Row(iosSaber_look, id)->edge;
                pBlade = iosSaber_pThrownDrawn;
            }
            else if (pThing->renderData.type == RD_THING_MODEL3 && pThing->renderData.model3
                     && pThing->renderData.model3->sizeMaterials >= 1 && pThing->renderData.model3->aMaterials) {
                pBlade = pThing->renderData.model3->aMaterials[0];
                rgb = iosSaber_ThrownSwatchOf(pBlade);
            }
            const rdFace* pSideFace = NULL;
            rdMaterial* pSideMat = NULL;
            if (bCross) {
                SithThing* p = sithPlayer_g_pLocalPlayerThing;
                if (p && p->playerInfo && p->playerInfo->polyline.face.material) pSideFace = &p->playerInfo->polyline.face;
                rdMaterial* aMat = id == IOSSABER_COLOUR_GAME ? NULL : iosSaber_Load(id);
                pSideMat = aMat ? &aMat[IOSSABER_SIDE] : pSideFace ? pSideFace->material : NULL;
            }
            iosSaberShape_DrawThrownGlow(pThing, pBlade, rgb ? rgb : iosSaber_aColours[IOSSABER_COLOUR_GAME].edge,
                                         bCross ? IOSSABERSHAPE_FORM_CROSS : IOSSABERSHAPE_FORM_CHOSEN, pSideFace, pSideMat);
        }
    }
    if (!iosSaber_pThrownModel) return;
    iosSaber_SwapThrownBlade(iosSaber_pThrownModel, iosSaber_pThrownDrawn, iosSaber_pThrownBlade);
    iosSaber_pThrownModel = NULL;
}

static void iosSaber_ForgetArms(void); // (below: the second saber's arm)

void iosSaber_FreeWorld(void)
{
    for (int look = 0; look < IOSSABER_NUM_LOOKS; look++) {
        for (int id = 0; id < IOSSABER_NUM_COLOURS; id++) {
            if (iosSaber_aLoaded[look][id] > 0) {
                for (int p = 0; p < IOSSABER_NUM_PARTS; p++) iosSaber_FreeMaterial(&iosSaber_aMat[look][id][p]);
            }
            iosSaber_aLoaded[look][id] = 0;
            if (iosSaber_aThrownLoaded[look][id] > 0) iosSaber_FreeMaterial(&iosSaber_aThrownMat[look][id]);
            iosSaber_aThrownLoaded[look][id] = 0;
        }
    }
    iosSaberShape_FreeWorld();
    // (the arms' joints found again for the next level's model, even one at the same address)
    iosSaber_ForgetArms();
}

// ------------------------------------------------------------ drawing

// (the player's own)
static int iosSaber_IsPlayers(SithThing* pThing)
{
    return pThing && pThing == sithPlayer_g_pLocalPlayerThing;
}

// A thing's saber as its player chose it: the local player's own, or in multiplayer
// another player's as their game sent it (jkPlayerLook); 0 for anyone else
static int iosSaber_LookOf(SithThing* pThing, int* pStyle, int aId[IOSSABER_MAX_BLADES])
{
    if (iosSaber_IsPlayers(pThing)) {
        *pStyle = iosSaber_style;
        for (int b = 0; b < IOSSABER_MAX_BLADES; b++) aId[b] = iosSaber_GetShownColour(iosSaber_style, b);
        return 1;
    }
#ifdef QOL_IMPROVEMENTS
    const jkPlayerLook* pLook = jkPlayerLook_ForThing(pThing);
    if (pLook) {
        *pStyle = (pLook->style >= 0 && pLook->style < IOSSABER_NUM_STYLES && iosSaber_StyleAvailable(pLook->style)) ? pLook->style : IOSSABER_STYLE_SINGLE;
        for (int b = 0; b < IOSSABER_MAX_BLADES; b++) {
            const int id = (b < JKPLAYERLOOK_MAX_BLADES && pLook->aColourKey[b][0]) ? iosSaber_ColourByKey(pLook->aColourKey[b]) : -1;
            aId[b] = id < 0 ? IOSSABER_COLOUR_GAME : id;
        }
        return 1;
    }
#endif
    return 0;
}

int iosSaber_HasSecondBlade(SithThing* pThing)
{
    int style, aId[IOSSABER_MAX_BLADES];
    if (!iosSaber_LookOf(pThing, &style, aId) || iosSaber_aStyles[style].numBlades < 2) return 0;
    // (the player's own: not in a selfie, which poses the player with one hand on the phone and the other
    // free, or showing off the one weapon; and both in hand: one thrown, the other is held as one saber,
    // iosSaberThrow)
    if (iosSaber_IsPlayers(pThing))
        return !iosSelfie_IsActive() && iosSaberThrow_HandsInHand() == 3;
    return 1;
}

// (the blade's width as it was, while it is drawn shimmering: two sabers)
static rdPolyline* iosSaber_pWidthLine = NULL;
static flex_t iosSaber_savedBase, iosSaber_savedTip;

void iosSaber_BeginBlade(SithThing* pThing, rdThing* pLine, int blade)
{
    iosSaberShape_EndBlade(); // (begun twice with no end between: its shape's widths put back first)
    iosSaber_pSavedSide = iosSaber_pSavedTip = NULL;
    if (iosSaber_pWidthLine) { // (begun twice with no end between: its width put back first)
        iosSaber_pWidthLine->baseRadius = iosSaber_savedBase;
        iosSaber_pWidthLine->tipRadius = iosSaber_savedTip;
        iosSaber_pWidthLine = NULL;
    }
    int style, aId[IOSSABER_MAX_BLADES];
    if (!pLine || !pLine->polyline || !iosSaber_LookOf(pThing, &style, aId)) return;
    // two sabers: each blade's width shimmers a little, out of step with the other (iosStance; the
    // player's own)
    if (iosSaber_IsPlayers(pThing) && iosSaber_HasSecondBlade(pThing)) {
        rdPolyline* w = pLine->polyline;
        const flex_t k = iosStance_BladeWidth(blade);
        iosSaber_pWidthLine = w;
        iosSaber_savedBase = w->baseRadius;
        iosSaber_savedTip = w->tipRadius;
        w->baseRadius *= k;
        w->tipRadius *= k;
    }
    const iosSaberStyle* st = &iosSaber_aStyles[style];
    // (a blade the style doesn't have -- the game's own second one -- as its first)
    if (blade < 0 || blade >= st->numBlades) blade = 0;
    int id = aId[blade];
    // (two sabers with the right one thrown: the left one, in hand, in its own colour)
    if (style == IOSSABER_STYLE_DUAL && blade == 0 && iosSaber_IsPlayers(pThing) && iosSaberThrow_HandsInHand() == 2)
        id = aId[1];
    rdMaterial* aMat = id == IOSSABER_COLOUR_GAME ? NULL : iosSaber_Load(id);
    // its shape (iosSaberShape), whatever the colour: its glow in the blade's own colour (the
    // look's edge colour; the game's own blade's, by its material); the crossguard's, CROSS
    const unsigned char* rgb = aMat ? iosSaber_Row(iosSaber_look, id)->edge : iosSaber_SwatchOf(pLine->polyline->face.material);
    iosSaberShape_BeginBlade(pThing, pLine, rgb ? rgb : iosSaber_aColours[IOSSABER_COLOUR_GAME].edge,
                             st->bCrossguard ? IOSSABERSHAPE_FORM_CROSS : IOSSABERSHAPE_FORM_CHOSEN);
    if (!aMat) return;
    rdPolyline* l = pLine->polyline;
    iosSaber_pSavedSide = l->face.material;
    iosSaber_pSavedTip = l->tipFace.material;
    l->face.material = &aMat[IOSSABER_SIDE];
    l->tipFace.material = &aMat[IOSSABER_TIP];
}

void iosSaber_EndBlade(rdThing* pLine)
{
    iosSaberShape_EndBlade(); // (first: it put its widths on over the shimmer's)
    if (iosSaber_pWidthLine) {
        iosSaber_pWidthLine->baseRadius = iosSaber_savedBase;
        iosSaber_pWidthLine->tipRadius = iosSaber_savedTip;
        iosSaber_pWidthLine = NULL;
    }
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

// The right arm's poses (each joint relative to the one it hangs from), kept a while
#define IOSSABER_ECHO_FRAMES 48
#define IOSSABER_ECHO_GAP_MS 500 // (not posed for longer than this: start over)
typedef struct {
    uint32_t ms;
    rdMatrix34 aJoint[IOSSABER_ARM_NODES];
} iosSaberEcho;

// Each two-saber player's (the player's own, and in multiplayer others'): their arms and poses kept
#define IOSSABER_ARM_SLOTS 8
typedef struct {
    SithThing* pThing;
    uint32_t usedMs;
    rdModel3* pArmModel;
    int aArmR[IOSSABER_ARM_NODES], aArmL[IOSSABER_ARM_NODES];
    int bArms;
    iosSaberEcho aEcho[IOSSABER_ECHO_FRAMES];
    int echoHead, echoCount;
} iosSaberArms;
static iosSaberArms iosSaber_aArms[IOSSABER_ARM_SLOTS];

static void iosSaber_ForgetArms(void)
{
    for (int i = 0; i < IOSSABER_ARM_SLOTS; i++) {
        iosSaber_aArms[i].pThing = NULL;
        iosSaber_aArms[i].pArmModel = NULL;
        iosSaber_aArms[i].echoCount = 0;
    }
}

// A thing's slot: its own, or the one used longest ago (started over)
static iosSaberArms* iosSaber_ArmsOf(SithThing* pThing, uint32_t now)
{
    iosSaberArms* pOldest = &iosSaber_aArms[0];
    for (int i = 0; i < IOSSABER_ARM_SLOTS; i++) {
        iosSaberArms* a = &iosSaber_aArms[i];
        if (a->pThing == pThing) { a->usedMs = now; return a; }
        if (!a->pThing || (pOldest->pThing && (int32_t)(a->usedMs - pOldest->usedMs) < 0)) pOldest = a;
    }
    pOldest->pThing = pThing;
    pOldest->usedMs = now;
    pOldest->pArmModel = NULL;
    pOldest->echoCount = 0;
    return pOldest;
}

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

static void iosSaber_FindArms(iosSaberArms* a, rdModel3* pModel)
{
    static const char* const aShoulder[] = { "shouldr", "shoulder", NULL };
    static const char* const aForearm[] = { "forearm", NULL };
    static const char* const aHand[] = { "hand", NULL };
    const char* const* aaPart[IOSSABER_ARM_NODES] = { aShoulder, aForearm, aHand };
    a->pArmModel = pModel;
    a->bArms = 1;
    a->echoCount = 0;
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) {
        a->aArmR[k] = iosSaber_FindNode(pModel, "r", aaPart[k]);
        a->aArmL[k] = iosSaber_FindNode(pModel, "l", aaPart[k]);
        if (a->aArmR[k] < 0 || a->aArmL[k] < 0 || a->aArmR[k] == a->aArmL[k]) a->bArms = 0;
    }
}

void iosSaber_PoseArms(rdThing* pRdThing)
{
    SithThing* pThing = pRdThing ? pRdThing->pThing : NULL;
    if (!iosSaber_HasSecondBlade(pThing) || !(pThing->jkFlags & JKFLAG_SABERON) || pRdThing != &pThing->renderData
        || !pRdThing->model3 || !pRdThing->paJointMatrices)
        return;
    const uint32_t now = sithTime_g_msecGameTime;
    iosSaberArms* a = iosSaber_ArmsOf(pThing, now);
    if (pRdThing->model3 != a->pArmModel) iosSaber_FindArms(a, pRdThing->model3);
    if (!a->bArms) return;

    // This frame's right arm kept (once a frame: the latest pose for that time)
    iosSaberEcho* pNow = &a->aEcho[a->echoHead];
    if (a->echoCount && (now < pNow->ms || now - pNow->ms > IOSSABER_ECHO_GAP_MS)) a->echoCount = 0;
    if (!a->echoCount || now != pNow->ms) {
        a->echoHead = (a->echoHead + 1) % IOSSABER_ECHO_FRAMES;
        if (a->echoCount < IOSSABER_ECHO_FRAMES) a->echoCount++;
        pNow = &a->aEcho[a->echoHead];
        pNow->ms = now;
    }
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) pNow->aJoint[k] = pRdThing->paJointMatrices[a->aArmR[k]];

    // ...the one IOSSABER_ECHO_MS before (or the oldest kept), mirrored onto the left arm: each joint as
    // its right one is, seen in a mirror across the body (x the other way, before and after)
    const iosSaberEcho* pPast = pNow;
    for (int k = 0; k < a->echoCount; k++) {
        const iosSaberEcho* e = &a->aEcho[(a->echoHead - k + IOSSABER_ECHO_FRAMES) % IOSSABER_ECHO_FRAMES];
        pPast = e;
        if (now - e->ms >= IOSSABER_ECHO_MS) break;
    }
    for (int k = 0; k < IOSSABER_ARM_NODES; k++) {
        const rdMatrix34* r = &pPast->aJoint[k];
        rdMatrix34* l = &pRdThing->paJointMatrices[a->aArmL[k]];
        l->rvec.x =  r->rvec.x; l->rvec.y = -r->rvec.y; l->rvec.z = -r->rvec.z;
        l->lvec.x = -r->lvec.x; l->lvec.y =  r->lvec.y; l->lvec.z =  r->lvec.z;
        l->uvec.x = -r->uvec.x; l->uvec.y =  r->uvec.y; l->uvec.z =  r->uvec.z;
        l->scale.x = -r->scale.x; l->scale.y = r->scale.y; l->scale.z = r->scale.z;
    }
    // ...and the shoulder turned out a little, so the second saber is held off to the side, not where the
    // first is (the game's stance holds the hilt in front of the body's middle, which a mirror keeps there)
    rdVector3 spread = { 0.0f, IOSSABER_LEFT_SPREAD_DEG, 0.0f };
    rdMatrix_PreRotate34(&pRdThing->paJointMatrices[a->aArmL[0]], &spread);
}

#endif // JK_ENHANCED

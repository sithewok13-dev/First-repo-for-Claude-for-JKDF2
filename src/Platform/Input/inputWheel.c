// Added: entire file. See inputWheel.h.
//
// The maps, numbers and looks are the iOS touch overlay's (iosTouch.m); the
// drawing follows the Android touch overlay's GL wheel (touchUi.c,
// exp/android-touch), with the saber wheel and the pad's and keyboard's
// wording added, as on iOS.

#include "Platform/Input/inputWheel.h"

// (built for the enhanced input, ENH_INPUT, and for the touch overlay,
// TOUCHUI_ENABLED, which uses it for its wheels with or without it)
#if defined(ENH_INPUT) || defined(TOUCHUI_ENABLED)

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Enhanced/iosGame.h"
#include "Enhanced/iosSaber.h"
#include "Enhanced/iosSaberShape.h"
#include "Platform/platServices.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The ring: its outer radius at most INPUTWHEEL_R1_MAX points, the hole a
// fraction of that; the slice pointed at pops out this far
#define INPUTWHEEL_R1_MAX 168.0f
#define INPUTWHEEL_R0_FRAC 0.37f
#define INPUTWHEEL_POP 10.0f
// A slice pointed at stays pointed at this many degrees past its edges
#define INPUTWHEEL_HYST 4.0f
// A learned power's level under its name: four star slots, the first that
// many filled (sizes as iosTouch.m's IOSTOUCH_WHEEL_STAR_*)
#define INPUTWHEEL_STARS 4
#define INPUTWHEEL_STAR_CELL 8.0f
#define INPUTWHEEL_STAR_STEP 9.0f
#define INPUTWHEEL_STAR_GAP 2.0f
#define INPUTWHEEL_STAR_R 4.3f
#define INPUTWHEEL_STAR_INNER 0.45f
#define INPUTWHEEL_STAR_LINE 0.9f
#define INPUTWHEEL_STAR_EDGE 0.2f
#define INPUTWHEEL_STAR_MIN 0.6f
// A weapon's ammo count under its name
#define INPUTWHEEL_AMMO_FONT 9.0f
#define INPUTWHEEL_AMMO_MIN 0.7f
#define INPUTWHEEL_AMMO_INK 0.705f
// The name in the middle shrinks to fit, down to this; a weapon's name that
// would need smaller goes on two lines
#define INPUTWHEEL_TITLE_MIN 0.6f
// The labels' font: bold, condensed
#define INPUTWHEEL_XSCALE 0.82f
// The saber wheel's choices: each this big, this far out from the ring and apart
#define INPUTWHEEL_CHIP_W 64.0f
#define INPUTWHEEL_CHIP_H 28.0f
#define INPUTWHEEL_CHIP_OUT 22.0f
#define INPUTWHEEL_CHIP_GAP 8.0f
// (all six down one side, the smallest screens: this much more between the two kinds)
#define INPUTWHEEL_CHIP_KIND_GAP 8.0f
// The look's switch, at the top of the middle (over the colour's name, as
// EQUIP is under the hint): this big at most, smaller to keep inside the
// middle on a small wheel
#define INPUTWHEEL_LOOK_W 76.0f
#define INPUTWHEEL_LOOK_H 24.0f
#define INPUTWHEEL_LOOK_Y 33.0f // (its middle, this far over the wheel's)
// The saber's bin (Jedi Knight, Mysteries of the Sith)
#define INPUTWHEEL_SABER_BIN_JK 10
#define INPUTWHEEL_SABER_BIN_MOTS 131

#define INPUTWHEEL_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define INPUTWHEEL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define INPUTWHEEL_MAX_F(a, b) ((a) > (b) ? (a) : (b))

// ------------------------------------------------------------ the maps (iosTouch.m's)

// Jedi Knight, as its Force screen lays the powers out: the light side down
// the left, the dark side down the right, the neutral powers across the top
static const inputWheelGroup inputWheel_aJkGroups[] = {
    { "LIGHT\nSIDE", 150.0f, 120, 200, 255 },
    { NULL,           90.0f, 255, 214, 120 },
    { "DARK\nSIDE",   30.0f, 255, 105,  90 },
};
static const inputWheelSlice inputWheel_aJkSlices[] = {
    { 29, 0, 239.0f }, { 28, 0, 217.0f }, { 27, 0, 195.0f }, { 26, 0, 173.0f }, { 25, 0, 151.0f }, // Protection, Absorb, Blinding, Persuasion, Healing
    { 21, 1, 123.0f }, { 22, 1, 101.0f }, { 23, 1,  79.0f }, { 24, 1,  57.0f },                    // Jump, Speed, Seeing, Pull
    { 30, 2,  29.0f }, { 31, 2,   7.0f }, { 32, 2, -15.0f }, { 33, 2, -37.0f }, { 34, 2, -59.0f }, // Throw, Grip, Lightning, Destruction, Deadly Sight
};
// Mysteries of the Sith: its four tiers, round from the bottom left
static const inputWheelGroup inputWheel_aMotsGroups[] = {
    { "TIER I",   207.0f, 255, 214, 120 },
    { "TIER II",  120.0f, 110, 220, 170 },
    { "TIER III",  42.0f, 120, 200, 255 },
    { "TIER IV",  -36.0f, 190, 150, 255 },
};
static const inputWheelSlice inputWheel_aMotsSlices[] = {
    { 38, 0, 243.0f }, { 22, 0, 225.0f }, { 23, 0, 207.0f }, { 36, 0, 189.0f }, { 21, 0, 171.0f },
    { 24, 1, 147.0f }, { 31, 1, 129.0f }, { 35, 1, 111.0f }, { 37, 1,  93.0f },
    { 39, 2,  69.0f }, { 25, 2,  51.0f }, { 27, 2,  33.0f }, { 26, 2,  15.0f },
    { 28, 3,  -9.0f }, { 33, 3, -27.0f }, { 29, 3, -45.0f }, { 34, 3, -63.0f },
};
// The weapons, in the number key order, coloured by their ammo
static const inputWheelGroup inputWheel_aWeaponGroups[] = {
    { NULL, 0.0f, 255, 214, 120 }, // fists, lightsaber
    { NULL, 0.0f, 120, 200, 255 }, // energy
    { NULL, 0.0f, 110, 220, 170 }, // power cells
    { NULL, 0.0f, 255, 105,  90 }, // thermals, rail charges, sequencers
    { NULL, 0.0f, 190, 150, 255 }, // E-Web rounds, carbonite
};
static const inputWheelSlice inputWheel_aJkWeaponSlices[] = {
    {  1, 0, 234.0f }, {  2, 1, 202.0f }, {  3, 1, 170.0f }, {  4, 3, 138.0f }, {  5, 2, 106.0f },
    {  6, 2,  74.0f }, {  7, 3,  42.0f }, {  8, 3,  10.0f }, {  9, 2, -22.0f }, { 10, 0, -54.0f },
};
static const inputWheelSlice inputWheel_aMotsWeaponSlices[] = {
    { 121, 0, 242.0f }, { 131, 0, 223.0f }, { 122, 1, 204.0f }, { 132, 1, 185.0f }, { 123, 1, 166.0f }, { 133, 1, 147.0f },
    { 124, 3, 128.0f }, { 134, 3, 109.0f }, { 135, 2,  90.0f }, { 126, 2,  71.0f }, { 127, 3,  52.0f }, { 137, 3,  33.0f },
    { 128, 3,  14.0f }, { 138, 3,  -5.0f }, { 129, 2, -24.0f }, { 130, 4, -43.0f }, { 140, 4, -62.0f },
};
static const inputWheelMap inputWheel_jkForce = { inputWheel_aJkSlices, INPUTWHEEL_COUNT(inputWheel_aJkSlices),
    inputWheel_aJkGroups, INPUTWHEEL_COUNT(inputWheel_aJkGroups), 22.0f, 6.0f };
static const inputWheelMap inputWheel_motsForce = { inputWheel_aMotsSlices, INPUTWHEEL_COUNT(inputWheel_aMotsSlices),
    inputWheel_aMotsGroups, INPUTWHEEL_COUNT(inputWheel_aMotsGroups), 18.0f, 6.0f };
static const inputWheelMap inputWheel_jkWeapons = { inputWheel_aJkWeaponSlices, INPUTWHEEL_COUNT(inputWheel_aJkWeaponSlices),
    inputWheel_aWeaponGroups, INPUTWHEEL_COUNT(inputWheel_aWeaponGroups), 32.0f, 0.0f };
static const inputWheelMap inputWheel_motsWeapons = { inputWheel_aMotsWeaponSlices, INPUTWHEEL_COUNT(inputWheel_aMotsWeaponSlices),
    inputWheel_aWeaponGroups, INPUTWHEEL_COUNT(inputWheel_aWeaponGroups), 19.0f, 0.0f };
typedef char inputWheel_assertSizes[(INPUTWHEEL_COUNT(inputWheel_aJkSlices) <= INPUTWHEEL_MAX
                                     && INPUTWHEEL_COUNT(inputWheel_aMotsSlices) <= INPUTWHEEL_MAX
                                     && INPUTWHEEL_COUNT(inputWheel_aJkWeaponSlices) <= INPUTWHEEL_MAX
                                     && INPUTWHEEL_COUNT(inputWheel_aMotsWeaponSlices) <= INPUTWHEEL_MAX
                                     && INPUTWHEEL_COUNT(inputWheel_aMotsGroups) <= INPUTWHEEL_MAX_GROUPS
                                     && INPUTWHEEL_COUNT(inputWheel_aWeaponGroups) <= INPUTWHEEL_MAX_GROUPS) ? 1 : -1];

// The saber wheel: every blade colour in its own slice and colour, the
// game's own by the gap on the left, round through the hues to the right
#define INPUTWHEEL_SABER_SLICE_DEG 18.0f
static inputWheelSlice inputWheel_aSaberSlices[INPUTWHEEL_MAX];
static inputWheelGroup inputWheel_aSaberGroups[INPUTWHEEL_MAX]; // (one per slice: its colour)
static inputWheelMap inputWheel_saber = { inputWheel_aSaberSlices, 0, inputWheel_aSaberGroups, 0, INPUTWHEEL_SABER_SLICE_DEG, 0.0f };

static void inputWheel_SetupSaber(void)
{
    // (each colour this game's wheel offers, in order: a slice's bin is its colour)
    int aId[INPUTWHEEL_MAX], n = 0;
    for (int id = 0; id < iosSaber_NumColours() && n < INPUTWHEEL_MAX; id++)
        if (iosSaber_ColourOnWheel(id)) aId[n++] = id;
    for (int i = 0; i < n; i++) {
        inputWheelGroup* g = &inputWheel_aSaberGroups[i];
        g->title = NULL;
        g->titleDeg = 0.0f;
        iosSaber_ColourRGB(aId[i], &g->r, &g->g, &g->b);
        inputWheel_aSaberSlices[i].bin = aId[i];
        inputWheel_aSaberSlices[i].group = i;
        inputWheel_aSaberSlices[i].deg = 90.0f + INPUTWHEEL_SABER_SLICE_DEG * ((n - 1) * 0.5f - i);
    }
    inputWheel_saber.numSlices = n;
    inputWheel_saber.numGroups = n;
}

const inputWheelMap* inputWheel_GetMap(int kind, int bMots)
{
    if (kind == INPUTWHEEL_WEAPONS) return bMots ? &inputWheel_motsWeapons : &inputWheel_jkWeapons;
    if (kind == INPUTWHEEL_SABER) {
        inputWheel_SetupSaber();
        return &inputWheel_saber;
    }
    return bMots ? &inputWheel_motsForce : &inputWheel_jkForce;
}

float inputWheel_AngleDiff(float a, float b)
{
    float d = fmodf(a - b, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

int inputWheel_SliceForAngle(const inputWheelMap* m, float deg, int cur)
{
    if (!m) return INPUTWHEEL_CANCEL;
    const float half = m->sliceDeg * 0.5f;
    if (cur >= 0 && cur < m->numSlices && fabsf(inputWheel_AngleDiff(deg, m->aSlices[cur].deg)) <= half + INPUTWHEEL_HYST) return cur;
    int best = INPUTWHEEL_CANCEL;
    float bestDiff = 0.0f;
    for (int i = 0; i < m->numSlices; i++) {
        const float d = fabsf(inputWheel_AngleDiff(deg, m->aSlices[i].deg));
        if (d <= half + m->gapDeg * 0.5f && (best < 0 || d < bestDiff)) {
            best = i;
            bestDiff = d;
        }
    }
    return best;
}

// ------------------------------------------------------------ state

// A wheel's labels fitted to its slices ([0] the name alone, [1] with the
// row under it), kept until the map or the size changes
typedef struct {
    const inputWheelMap* map;
    float R1;
    float rowScale;
    float font[INPUTWHEEL_MAX][2];
    float labelR[INPUTWHEEL_MAX][2];
    float labelW[INPUTWHEEL_MAX][2], labelH[INPUTWHEEL_MAX][2];
} inputWheelFit;

static struct {
    int bOpen, bTapMode, kind;
    const inputWheelMap* pMap;
    int aEarned[INPUTWHEEL_MAX], aLevel[INPUTWHEEL_MAX], aOwned[INPUTWHEEL_MAX], aAmmo[INPUTWHEEL_MAX];
    int hot, lastHot;
    int bNeedle;
    int bCancelShown; // (lifting cancels with nothing pointed at: a finger off the ring, inputWheel_SetCancelShown)
    float needleDeg;
    int saberTarget;
    void (*pfnSlice)(void);
    const char* (*pfnHint)(int bPickable, int bCancel);
    int (*pfnClear)(float x0, float y0, float x1, float y1);
    float topClear;
    // where it was last laid out
    float cx, cy, R0, R1;
    float chipX[INPUTWHEEL_NUM_CHIPS], chipY[INPUTWHEEL_NUM_CHIPS], chipW[INPUTWHEEL_NUM_CHIPS], chipH[INPUTWHEEL_NUM_CHIPS];
    int aChipShown[INPUTWHEEL_NUM_CHIPS];
    inputWheelFit aFit[INPUTWHEEL_NUM_KINDS];
} inputWheel_s = { .hot = -1, .lastHot = -1, .saberTarget = INPUTWHEEL_SABER_BOTH };

static touchUiDrawList* inputWheel_pDraw = NULL; // (the list being drawn into)

static const char* inputWheel_SlotName(int bin)
{
    if (inputWheel_s.kind == INPUTWHEEL_SABER) return iosSaber_ColourName(bin);
    return inputWheel_s.kind == INPUTWHEEL_WEAPONS ? iosGame_GetWeaponName(bin) : iosGame_GetPowerName(bin);
}

static int inputWheel_SaberBin(void)
{
    return iosGame_IsMots() ? INPUTWHEEL_SABER_BIN_MOTS : INPUTWHEEL_SABER_BIN_JK;
}

// The blade of the style a colour goes to (with BOTH, the right one: the
// one shown as picked)
static int inputWheel_SaberBlade(void)
{
    if (iosSaber_StyleNumBlades(iosSaber_GetStyle()) < 2) return 0;
    return inputWheel_s.saberTarget == INPUTWHEEL_SABER_LEFT ? 1 : 0;
}

static void inputWheel_Tick(void)
{
    platServices_Haptic(PLATSERVICES_HAPTIC_TICK);
}

// A tick each time something else is pointed at (a slice or the gap)
static void inputWheel_HotChanged(void)
{
    const int hot = inputWheel_s.hot;
    if (hot == inputWheel_s.lastHot) return;
    inputWheel_s.lastHot = hot;
    if (hot == -1) return;
    inputWheel_Tick();
    if (inputWheel_s.pfnSlice) inputWheel_s.pfnSlice();
}

// ------------------------------------------------------------ the open wheel

static void inputWheel_ReadSlices(void);

int inputWheel_Open(int kind, int bTapMode)
{
    if (inputWheel_s.bOpen || kind < 0 || kind >= INPUTWHEEL_NUM_KINDS) return 0;
    if (kind == INPUTWHEEL_SABER && !iosGame_IsSaberButtonShown()) return 0;
    inputWheel_s.kind = kind;
    inputWheel_ReadSlices();
    inputWheel_s.bOpen = 1;
    inputWheel_s.bTapMode = bTapMode != 0;
    inputWheel_s.hot = inputWheel_s.lastHot = -1;
    inputWheel_s.bNeedle = 0;
    inputWheel_s.bCancelShown = 0;
    iosGame_SetHold(1);
    return 1;
}

// The open wheel's map and each slice's state, as they are now (the saber
// wheel's: made again from the colours, in the look)
static void inputWheel_ReadSlices(void)
{
    const int kind = inputWheel_s.kind;
    const inputWheelMap* m = inputWheel_GetMap(kind, iosGame_IsMots());
    inputWheel_s.pMap = m;
    for (int i = 0; i < m->numSlices; i++) {
        const int bin = m->aSlices[i].bin;
        if (kind == INPUTWHEEL_SABER) {
            // (every colour can be picked, saber or not: it is only how it looks)
            inputWheel_s.aEarned[i] = inputWheel_s.aOwned[i] = 1;
            inputWheel_s.aAmmo[i] = -1;
            inputWheel_s.aLevel[i] = 0;
        }
        else if (kind == INPUTWHEEL_WEAPONS) {
            int ammo = -1, bSelectable = 0;
            inputWheel_s.aOwned[i] = iosGame_GetWeapon(bin, &ammo, &bSelectable) != 0;
            inputWheel_s.aEarned[i] = inputWheel_s.aOwned[i] && bSelectable;
            inputWheel_s.aAmmo[i] = inputWheel_s.aOwned[i] ? ammo : -1;
            inputWheel_s.aLevel[i] = 0;
        }
        else {
            inputWheel_s.aEarned[i] = iosGame_IsPowerAvailable(bin);
            inputWheel_s.aLevel[i] = iosGame_GetPowerLevel(bin);
            inputWheel_s.aOwned[i] = inputWheel_s.aEarned[i];
            inputWheel_s.aAmmo[i] = -1;
        }
    }
    if (kind == INPUTWHEEL_SABER) inputWheel_s.aFit[kind].map = NULL; // (fitted again: the colours' names)
}

int inputWheel_IsOpen(void) { return inputWheel_s.bOpen; }
int inputWheel_GetKind(void) { return inputWheel_s.kind; }
int inputWheel_IsTapMode(void) { return inputWheel_s.bTapMode; }
const inputWheelMap* inputWheel_GetOpenMap(void) { return inputWheel_s.bOpen ? inputWheel_s.pMap : NULL; }

void inputWheel_SetTapMode(int bTapMode)
{
    if (inputWheel_s.bOpen) inputWheel_s.bTapMode = bTapMode != 0;
}

void inputWheel_SetSliceHook(void (*pfn)(void)) { inputWheel_s.pfnSlice = pfn; }
void inputWheel_SetHintFn(const char* (*pfn)(int bPickable, int bCancel)) { inputWheel_s.pfnHint = pfn; }

void inputWheel_SetCancelShown(int bShown)
{
    if (inputWheel_s.bOpen) inputWheel_s.bCancelShown = bShown != 0;
}

void inputWheel_SetKeepClear(int (*pfn)(float x0, float y0, float x1, float y1), float topClear)
{
    inputWheel_s.pfnClear = pfn;
    inputWheel_s.topClear = topClear;
}

void inputWheel_Point(int slice, int bNeedle, float needleDeg)
{
    if (!inputWheel_s.bOpen) return;
    const inputWheelMap* m = inputWheel_s.pMap;
    if (slice >= m->numSlices || slice < INPUTWHEEL_CANCEL) slice = -1;
    inputWheel_s.hot = slice;
    inputWheel_s.bNeedle = bNeedle != 0;
    inputWheel_s.needleDeg = needleDeg;
    inputWheel_s.bCancelShown = 0;
    inputWheel_HotChanged();
}

void inputWheel_Aim(int bAim, float deg)
{
    if (!inputWheel_s.bOpen) return;
    if (!bAim) {
        inputWheel_Point(-1, 0, 0.0f);
        return;
    }
    inputWheel_Point(inputWheel_SliceForAngle(inputWheel_s.pMap, deg, inputWheel_s.hot), 1, deg);
}

void inputWheel_Step(int dir)
{
    const inputWheelMap* m = inputWheel_s.pMap;
    if (!inputWheel_s.bOpen || !m || m->numSlices <= 0) return;
    // (the slices go round clockwise in their order)
    int i = inputWheel_s.hot >= 0 ? inputWheel_s.hot + (dir > 0 ? 1 : -1) : (dir > 0 ? 0 : m->numSlices - 1);
    i = (i + m->numSlices) % m->numSlices;
    inputWheel_Point(i, 1, m->aSlices[i].deg);
}

int inputWheel_GetHot(void)
{
    return inputWheel_s.bOpen ? inputWheel_s.hot : -1;
}

int inputWheel_IsPickable(int slice)
{
    return inputWheel_s.bOpen && slice >= 0 && slice < inputWheel_s.pMap->numSlices && inputWheel_s.aEarned[slice];
}

// The colour picked on the saber wheel: for the style's blade (with two:
// the left, the right or both). (Remembered by the platform: enhSettings,
// or the iOS overlay's own.)
static void inputWheel_SetSaberColour(int id)
{
    const int st = iosSaber_GetStyle();
    const int bTwo = iosSaber_StyleNumBlades(st) >= 2;
    for (int b = 0; b < (bTwo ? 2 : 1); b++) {
        if (bTwo && inputWheel_s.saberTarget != INPUTWHEEL_SABER_BOTH && b != inputWheel_SaberBlade()) continue;
        iosSaber_SetColour(st, b, id);
    }
}

int inputWheel_Close(int slice)
{
    if (!inputWheel_s.bOpen) return 0;
    const inputWheelMap* m = inputWheel_s.pMap;
    if (inputWheel_s.kind == INPUTWHEEL_SABER && slice >= 0 && slice < m->numSlices) {
        // the saber wheel stays open on a colour: set now, and open to pick
        // more until the gap, its button again or EQUIP closes it
        inputWheel_SetSaberColour(m->aSlices[slice].bin);
        inputWheel_s.bTapMode = 1;
        inputWheel_s.hot = inputWheel_s.lastHot = -1;
        inputWheel_s.bNeedle = 0;
        inputWheel_s.bCancelShown = 0;
        inputWheel_Tick();
        return 1;
    }
    if (slice >= 0 && slice < m->numSlices && inputWheel_s.aEarned[slice]) {
        const int bin = m->aSlices[slice].bin;
        if (inputWheel_s.kind == INPUTWHEEL_FORCE) iosGame_SelectPower(bin);
        else if (bin != iosGame_GetCurWeapon()) iosGame_SelectWeapon(bin); // (the game takes it once it runs again)
    }
    inputWheel_s.bOpen = 0;
    inputWheel_s.bTapMode = 0;
    inputWheel_s.hot = inputWheel_s.lastHot = -1;
    inputWheel_s.bNeedle = 0;
    inputWheel_s.bCancelShown = 0;
    inputWheel_s.pfnSlice = NULL;
    inputWheel_s.pfnHint = NULL;
    inputWheel_s.pfnClear = NULL;
    inputWheel_s.topClear = 0.0f;
    iosGame_SetHold(0);
    return 0;
}

int inputWheel_CanEquipSaber(void)
{
    int ammo = 0, bSelectable = 0;
    return iosGame_GetWeapon(inputWheel_SaberBin(), &ammo, &bSelectable) && bSelectable;
}

int inputWheel_GetSaberTarget(void) { return inputWheel_s.saberTarget; }

void inputWheel_SetSaberTarget(int target)
{
    if (target >= INPUTWHEEL_SABER_RIGHT && target <= INPUTWHEEL_SABER_BOTH) inputWheel_s.saberTarget = target;
}

void inputWheel_SaberChip(int chip)
{
    if (!inputWheel_s.bOpen || inputWheel_s.kind != INPUTWHEEL_SABER) return;
    if (chip == INPUTWHEEL_CHIP_EQUIP) {
        // the saber in hand, as set: the wheel closes with nothing else
        // changed, and the game takes it once it runs again; not had, nothing
        if (!inputWheel_CanEquipSaber()) return;
        const int bin = inputWheel_SaberBin();
        inputWheel_Close(-1);
        if (bin != iosGame_GetCurWeapon()) iosGame_SelectWeapon(bin);
        return;
    }
    if (chip == INPUTWHEEL_CHIP_SINGLE || chip == INPUTWHEEL_CHIP_DUAL) {
        const int st = chip == INPUTWHEEL_CHIP_DUAL ? IOSSABER_STYLE_DUAL : IOSSABER_STYLE_SINGLE;
        if (!iosSaber_StyleAvailable(st)) return;
        iosSaber_SetStyle(st);
    }
    else if (chip >= INPUTWHEEL_CHIP_LEFT && chip <= INPUTWHEEL_CHIP_RIGHT) {
        if (iosSaber_StyleNumBlades(iosSaber_GetStyle()) < 2) return;
        inputWheel_s.saberTarget = chip == INPUTWHEEL_CHIP_LEFT ? INPUTWHEEL_SABER_LEFT
                                 : chip == INPUTWHEEL_CHIP_RIGHT ? INPUTWHEEL_SABER_RIGHT : INPUTWHEEL_SABER_BOTH;
    }
    else if (chip == INPUTWHEEL_CHIP_LOOK) {
        // the other look: the colours (which slices, their swatches, the
        // blades) in it at once; the wheel open as it was
        iosSaber_ToggleLook();
        inputWheel_ReadSlices();
    }
    else if (chip == INPUTWHEEL_CHIP_SHAPE) {
        // the blade's shape, BEAM on or off: the blades drawn in it at once
        iosSaberShape_Toggle();
    }
    else return;
    inputWheel_s.bTapMode = 1;
    inputWheel_s.hot = inputWheel_s.lastHot = -1;
    inputWheel_s.bNeedle = 0;
    inputWheel_s.bCancelShown = 0;
    inputWheel_Tick();
}

// ------------------------------------------------------------ layout

static int inputWheel_InSlice(float px, float py, float deg, float pad)
{
    const float dx = px - inputWheel_s.cx, dy = inputWheel_s.cy - py;
    const float r = sqrtf(dx * dx + dy * dy);
    if (r < inputWheel_s.R0 + pad || r > inputWheel_s.R1 - pad) return 0;
    // (the slices are drawn a degree in from their edges)
    const float edge = inputWheel_s.pMap->sliceDeg * 0.5f - 1.0f - pad / r * 180.0f / (float)M_PI;
    return fabsf(inputWheel_AngleDiff(atan2f(dy, dx) * 180.0f / (float)M_PI, deg)) <= edge;
}

int inputWheel_SliceAt(float x, float y, int cur)
{
    if (!inputWheel_s.bOpen) return -1;
    const float dx = x - inputWheel_s.cx, dy = inputWheel_s.cy - y;
    const float r = sqrtf(dx * dx + dy * dy);
    if (r < inputWheel_s.R0 - 4.0f || r > inputWheel_s.R1 + INPUTWHEEL_POP + 14.0f) return -1;
    return inputWheel_SliceForAngle(inputWheel_s.pMap, atan2f(dy, dx) * 180.0f / (float)M_PI, cur);
}

void inputWheel_GetGeometry(float* pX, float* pY, float* pR0, float* pR1)
{
    if (pX) *pX = inputWheel_s.cx;
    if (pY) *pY = inputWheel_s.cy;
    if (pR0) *pR0 = inputWheel_s.R0;
    if (pR1) *pR1 = inputWheel_s.R1;
}

int inputWheel_ChipAt(float x, float y)
{
    if (!inputWheel_s.bOpen || inputWheel_s.kind != INPUTWHEEL_SABER) return -1;
    for (int i = 0; i < INPUTWHEEL_NUM_CHIPS; i++) {
        if (!inputWheel_s.aChipShown[i]) continue;
        if (fabsf(x - inputWheel_s.chipX[i]) > inputWheel_s.chipW[i] * 0.5f + 6.0f || fabsf(y - inputWheel_s.chipY[i]) > inputWheel_s.chipH[i] * 0.5f + 6.0f)
            continue;
        // (the look's switch: only in the middle, stopping short of where a slice takes a touch, R0 - 4)
        if (i == INPUTWHEEL_CHIP_LOOK && hypotf(x - inputWheel_s.cx, y - inputWheel_s.cy) > inputWheel_s.R0 - 5.0f) continue;
        return i;
    }
    return -1;
}

int inputWheel_GetChip(int chip, float* pX, float* pY)
{
    if (chip < 0 || chip >= INPUTWHEEL_NUM_CHIPS) return 0;
    if (pX) *pX = inputWheel_s.chipX[chip];
    if (pY) *pY = inputWheel_s.chipY[chip];
    return inputWheel_s.bOpen && inputWheel_s.aChipShown[chip];
}

// A slice's name, two-word names on two lines
static void inputWheel_LabelText(int i, char* out, int outLen)
{
    const char* name = inputWheel_SlotName(inputWheel_s.pMap->aSlices[i].bin);
    snprintf(out, (size_t)outLen, "%s", name ? name : "?");
    for (char* p = out; *p; p++)
        if (*p == ' ') *p = '\n';
}

// Whether a slice ever has a row under its name: every power (once learned);
// every weapon but the ones with no ammo (group 0); no colour
static int inputWheel_SliceHasRow(int i)
{
    if (inputWheel_s.kind == INPUTWHEEL_SABER) return 0;
    return inputWheel_s.kind != INPUTWHEEL_WEAPONS || inputWheel_s.pMap->aSlices[i].group != 0;
}

static void inputWheel_RowSize(float* pW, float* pH)
{
    const float k = inputWheel_s.aFit[inputWheel_s.kind].rowScale;
    if (inputWheel_s.kind == INPUTWHEEL_WEAPONS) {
        float w;
        touchUiDraw_MeasureText("000", INPUTWHEEL_AMMO_FONT, 1.0f, &w, NULL);
        *pW = w * k;
        *pH = INPUTWHEEL_AMMO_FONT * INPUTWHEEL_AMMO_INK * k;
        return;
    }
    *pW = ((INPUTWHEEL_STARS - 1) * INPUTWHEEL_STAR_STEP + INPUTWHEEL_STAR_CELL) * k;
    *pH = INPUTWHEEL_STAR_CELL * k;
}

// The biggest size (11 pt down to 7) a slice's name (with its row, bRow)
// fits at somewhere along the slice, as near the middle of the band as it can
static int inputWheel_FitLabel(int i, float s11w, float s11h, int bRow)
{
    const inputWheelMap* m = inputWheel_s.pMap;
    inputWheelFit* F = &inputWheel_s.aFit[inputWheel_s.kind];
    const float cx = inputWheel_s.cx, cy = inputWheel_s.cy, R0 = inputWheel_s.R0, R1 = inputWheel_s.R1;
    float rowW, rowH;
    inputWheel_RowSize(&rowW, &rowH);
    const float a = m->aSlices[i].deg * (float)M_PI / 180.0f;
    F->font[i][bRow] = bRow ? 0.0f : 7.0f;
    F->labelR[i][bRow] = R0 + (R1 - R0) * 0.58f;
    F->labelW[i][bRow] = s11w * 7.0f / 11.0f;
    F->labelH[i][bRow] = s11h * 7.0f / 11.0f;
    int bFound = 0;
    for (float size = 11.0f; size >= 7.0f && !bFound; size -= 0.5f) {
        const float w = s11w * size / 11.0f, h = s11h * size / 11.0f;
        const float bw = bRow ? INPUTWHEEL_MAX_F(w, rowW) : w;
        const float bh = bRow ? h + INPUTWHEEL_STAR_GAP + rowH : h;
        float bestOff = 0.0f;
        for (int k = 0; k <= 20; k++) {
            const float f = 0.30f + 0.50f * k / 20.0f;
            const float rr = R0 + (R1 - R0) * f;
            const float px = cx + rr * cosf(a), py = cy - rr * sinf(a);
            int bIn = 1;
            for (int sx = -1; sx <= 1 && bIn; sx++)
                for (int sy = -1; sy <= 1 && bIn; sy++)
                    bIn = inputWheel_InSlice(px + sx * bw * 0.5f, py + sy * bh * 0.5f, m->aSlices[i].deg, 2.0f);
            if (bIn && (!bFound || fabsf(f - 0.58f) < bestOff)) {
                bFound = 1;
                bestOff = fabsf(f - 0.58f);
                F->font[i][bRow] = size;
                F->labelR[i][bRow] = rr;
                F->labelW[i][bRow] = w;
                F->labelH[i][bRow] = h;
            }
        }
    }
    return bFound;
}

static void inputWheel_FitLabels(void)
{
    const inputWheelMap* m = inputWheel_s.pMap;
    inputWheelFit* F = &inputWheel_s.aFit[inputWheel_s.kind];
    const float rowMin = inputWheel_s.kind == INPUTWHEEL_WEAPONS ? INPUTWHEEL_AMMO_MIN : INPUTWHEEL_STAR_MIN;
    float aW[INPUTWHEEL_MAX], aH[INPUTWHEEL_MAX];
    for (int i = 0; i < m->numSlices; i++) {
        char text[64];
        inputWheel_LabelText(i, text, sizeof(text));
        touchUiDraw_MeasureText(text, 11.0f, INPUTWHEEL_XSCALE, &aW[i], &aH[i]);
        F->rowScale = 1.0f;
        inputWheel_FitLabel(i, aW[i], aH[i], 0);
        F->font[i][1] = 0.0f;
    }
    // the rows (stars, ammo) shrink together until every one fits
    for (int n = 0;; n++) {
        int bAll = 1;
        F->rowScale = 1.0f - 0.05f * n;
        const int bLast = F->rowScale <= rowMin + 0.001f;
        for (int i = 0; i < m->numSlices && (bAll || bLast); i++)
            if (inputWheel_SliceHasRow(i) && !inputWheel_FitLabel(i, aW[i], aH[i], 1)) bAll = 0;
        if (bAll || bLast) break;
    }
}

static void inputWheel_LayoutChips(const inputWheelScreen* s);

void inputWheel_Layout(const inputWheelScreen* s)
{
    if (!inputWheel_s.bOpen || !s) return;
    const float left = INPUTWHEEL_MAX_F(s->inL, 8.0f), right = s->w - INPUTWHEEL_MAX_F(s->inR, 8.0f);
    const float top = INPUTWHEEL_MAX_F(s->inT, 8.0f), bottom = s->h - INPUTWHEEL_MAX_F(s->inB, 8.0f);
    inputWheel_s.cx = (left + right) * 0.5f;
    inputWheel_s.cy = (top + bottom) * 0.5f + 4.0f;
    inputWheel_s.R1 = INPUTWHEEL_MIN(INPUTWHEEL_R1_MAX, (bottom - top) * 0.5f - INPUTWHEEL_POP - 6.0f);
    inputWheel_s.R0 = roundf(inputWheel_s.R1 * INPUTWHEEL_R0_FRAC);
    inputWheelFit* F = &inputWheel_s.aFit[inputWheel_s.kind];
    if (inputWheel_s.pMap != F->map || inputWheel_s.R1 != F->R1) {
        F->map = inputWheel_s.pMap;
        F->R1 = inputWheel_s.R1;
        inputWheel_FitLabels();
    }
    inputWheel_LayoutChips(s);
}

// Whether a box is where the opener lets things go (inputWheel_SetKeepClear)
static int inputWheel_IsClear(float x0, float y0, float x1, float y1)
{
    if (!inputWheel_s.pfnClear) return 1;
    return y0 >= inputWheel_s.topClear && inputWheel_s.pfnClear(x0, y0, x1, y1);
}

// The look's switch, at the top of the saber wheel's middle (of radius R0):
// its size, and how far over the middle its own middle is -- a pill kept
// wholly inside the middle, clear of the slices (iosTouch.m's the same)
static void inputWheel_LookChipSize(float R0, float* pW, float* pH, float* pUp)
{
    const float h = INPUTWHEEL_MIN(INPUTWHEEL_LOOK_H, roundf(R0 * 0.42f));
    const float up = INPUTWHEEL_MIN(INPUTWHEEL_LOOK_Y, roundf(R0 * 0.62f));
    // (its round ends' middles, a either side and up over the wheel's
    // middle, no further out than R0 - 1 - h/2: a^2 + up^2 <= that^2)
    const float k = R0 - 1.0f - h * 0.5f, a2 = k * k - up * up;
    const float w = a2 > 0.0f ? 2.0f * (sqrtf(a2) + h * 0.5f) : h;
    *pW = INPUTWHEEL_MIN(INPUTWHEEL_LOOK_W, floorf(w));
    *pH = h;
    *pUp = up;
}

// The saber wheel's choices: SINGLE, DUAL and the shape's switch down the
// left of the ring, with two blades LEFT, BOTH and RIGHT on its right, EQUIP
// in the middle under the hint, the look's switch in the middle over the
// colour's name. Kept clear of the opener's buttons (inputWheel_SetKeepClear):
// each column moved up or down as little as it can be; where the right has no
// such place, all six down the left (a little more apart between the two kinds).
static void inputWheel_LayoutChips(const inputWheelScreen* s)
{
    const int bSaber = inputWheel_s.bOpen && inputWheel_s.kind == INPUTWHEEL_SABER;
    const int bTwo = iosSaber_StyleNumBlades(iosSaber_GetStyle()) >= 2;
    const float cx = inputWheel_s.cx, cy = inputWheel_s.cy;
    const float step = INPUTWHEEL_CHIP_H + INPUTWHEEL_CHIP_GAP;
    float R = inputWheel_s.R1 + INPUTWHEEL_POP + INPUTWHEEL_CHIP_OUT + INPUTWHEEL_CHIP_W * 0.5f;
    // (kept on the screen: in toward the ring if it is narrow)
    const float maxR = INPUTWHEEL_MIN(cx - INPUTWHEEL_MAX_F(s->inL, 8.0f), s->w - INPUTWHEEL_MAX_F(s->inR, 8.0f) - cx) - INPUTWHEEL_CHIP_W * 0.5f;
    if (R > maxR) R = maxR;
    float aShift[2] = { 0.0f, 0.0f };
    int bOneColumn = 0;
    if (bSaber && inputWheel_s.pfnClear) {
        const float top = INPUTWHEEL_MAX_F(s->inT, 8.0f), bottom = s->h - INPUTWHEEL_MAX_F(s->inB, 8.0f);
        for (int col = 0; col < 2; col++) {
            const int n = 3;
            const float x = cx + (col ? R : -R);
            int bFound = 0;
            for (int k = 0; k <= 24 && !bFound; k++) {
                const float dy = ((k & 1) ? 6.0f : -6.0f) * ((k + 1) / 2);
                const float y0 = cy + dy - (n - 1) * 0.5f * step - INPUTWHEEL_CHIP_H * 0.5f;
                const float y1 = y0 + (n - 1) * step + INPUTWHEEL_CHIP_H;
                if (y1 > bottom || !inputWheel_IsClear(x - INPUTWHEEL_CHIP_W * 0.5f, y0, x + INPUTWHEEL_CHIP_W * 0.5f, y1)) continue;
                aShift[col] = dy;
                bFound = 1;
            }
            if (col == 1 && !bFound) bOneColumn = 1;
        }
        if (bOneColumn) {
            const float half = (5.0f * step + INPUTWHEEL_CHIP_KIND_GAP) * 0.5f + INPUTWHEEL_CHIP_H * 0.5f;
            aShift[0] = INPUTWHEEL_MIN(INPUTWHEEL_MAX_F(0.0f, INPUTWHEEL_MAX_F(top, inputWheel_s.topClear) + half - cy), bottom - half - cy);
        }
    }
    for (int i = 0; i < INPUTWHEEL_NUM_CHIPS; i++) {
        inputWheel_s.chipW[i] = INPUTWHEEL_CHIP_W;
        inputWheel_s.chipH[i] = INPUTWHEEL_CHIP_H;
        if (i == INPUTWHEEL_CHIP_EQUIP) {
            inputWheel_s.aChipShown[i] = bSaber;
            inputWheel_s.chipX[i] = cx;
            inputWheel_s.chipY[i] = cy + 34.0f;
            continue;
        }
        if (i == INPUTWHEEL_CHIP_LOOK) {
            float up;
            inputWheel_s.aChipShown[i] = bSaber;
            inputWheel_LookChipSize(inputWheel_s.R0, &inputWheel_s.chipW[i], &inputWheel_s.chipH[i], &up);
            inputWheel_s.chipX[i] = cx;
            inputWheel_s.chipY[i] = cy - up;
            continue;
        }
        // (the left column: SINGLE, DUAL, the shape; the right: LEFT, BOTH, RIGHT)
        const int bStyle = i <= INPUTWHEEL_CHIP_DUAL || i == INPUTWHEEL_CHIP_SHAPE;
        inputWheel_s.aChipShown[i] = bSaber && (bStyle || bTwo) && !(i == INPUTWHEEL_CHIP_DUAL && !iosSaber_StyleAvailable(IOSSABER_STYLE_DUAL));
        if (!inputWheel_s.aChipShown[i]) continue;
        const int k = i == INPUTWHEEL_CHIP_SHAPE ? 2 : bStyle ? i - INPUTWHEEL_CHIP_SINGLE : i - INPUTWHEEL_CHIP_LEFT;
        const int n = 3;
        if (bOneColumn) {
            inputWheel_s.chipX[i] = cx - R;
            const int slot = k + (bStyle ? 0 : 3);
            inputWheel_s.chipY[i] = cy + aShift[0] + slot * step + (slot >= 3 ? INPUTWHEEL_CHIP_KIND_GAP : 0.0f) - (5.0f * step + INPUTWHEEL_CHIP_KIND_GAP) * 0.5f;
        }
        else {
            inputWheel_s.chipX[i] = cx + (bStyle ? -R : R);
            inputWheel_s.chipY[i] = cy + aShift[bStyle ? 0 : 1] + (k - (n - 1) * 0.5f) * step;
        }
    }
}

// ------------------------------------------------------------ drawing

// Text for the label font (printable ASCII): the few others the prompts
// and names use, in words
void inputWheel_Ascii(const char* in, char* out, int outSize)
{
    static const struct { const char* utf8; const char* ascii; } aMap[] = {
        { "\xE2\x9C\x95", "Cross" }, { "\xE2\x97\x8B", "Circle" }, { "\xE2\x96\xA1", "Square" }, { "\xE2\x96\xB3", "Triangle" },
        { "\xE2\x86\x91", "up" }, { "\xE2\x86\x93", "down" }, { "\xE2\x86\x90", "left" }, { "\xE2\x86\x92", "right" },
        { "\xE2\x80\x94", "-" }, { "\xE2\x80\x93", "-" },
    };
    int o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o < outSize - 1;) {
        if (*p < 0x80) {
            out[o++] = (*p >= 32 && *p < 127) ? (char)*p : ' ';
            p++;
            continue;
        }
        const char* rep = "?";
        int len = 1;
        for (unsigned i = 0; i < sizeof(aMap) / sizeof(aMap[0]); i++) {
            const size_t n = strlen(aMap[i].utf8);
            if (!strncmp((const char*)p, aMap[i].utf8, n)) {
                rep = aMap[i].ascii;
                len = (int)n;
                break;
            }
        }
        if (len == 1) {
            // (another character: one '?', its continuation bytes skipped)
            while (p[len] >= 0x80 && p[len] < 0xC0) len++;
        }
        for (const char* r = rep; *r && o < outSize - 1; r++) out[o++] = *r;
        p += len;
    }
    out[o] = 0;
}


// A group's colour, mixed k of the way to it from the wheel's dark grey
static touchUiColor inputWheel_Colour(const inputWheelGroup* g, float k, float a)
{
    return touchUi_Rgba((g->r * k + 32.0f * (1.0f - k)) / 255.0f, (g->g * k + 32.0f * (1.0f - k)) / 255.0f,
                        (g->b * k + 32.0f * (1.0f - k)) / 255.0f, a);
}

static void inputWheel_DrawStar(float cx, float cy, float k, int bFilled, int bPop)
{
    touchUiDrawList* l = inputWheel_pDraw;
    const float R = INPUTWHEEL_STAR_R * k, y = cy + R * (1.0f - cosf((float)M_PI / 5.0f)) * 0.5f;
    float xy[20];
    for (int j = 0; j < 10; j++) {
        const float r = (j & 1) ? R * INPUTWHEEL_STAR_INNER : R;
        const float a = (-90.0f + 36.0f * j) * (float)M_PI / 180.0f;
        xy[2 * j] = cx + r * cosf(a);
        xy[2 * j + 1] = y + r * sinf(a);
    }
    touchUiColor gold = bPop ? touchUi_Rgba(25.0f / 255.0f, 20.0f / 255.0f, 10.0f / 255.0f, 1.0f)
                             : touchUi_Rgba(1.0f, 204.0f / 255.0f, 64.0f / 255.0f, 1.0f);
    if (bFilled) {
        touchUiDraw_Polygon(l, xy, 10, gold);
        if (!bPop)
            for (int j = 0; j < 10; j++)
                touchUiDraw_Line(l, xy[2 * j], xy[2 * j + 1], xy[2 * ((j + 1) % 10)], xy[2 * ((j + 1) % 10) + 1],
                                 INPUTWHEEL_STAR_EDGE * k, touchUi_Black(0.78f));
    }
    else {
        gold.a = bPop ? 0.47f : 0.43f;
        for (int j = 0; j < 10; j++)
            touchUiDraw_Line(l, xy[2 * j], xy[2 * j + 1], xy[2 * ((j + 1) % 10)], xy[2 * ((j + 1) % 10) + 1],
                             INPUTWHEEL_STAR_LINE * k, gold);
    }
}

// The shape's switch: its name, then a small on/off switch (a track, its knob
// to the right and gold when on), so it reads as a switch, not a choice
#define INPUTWHEEL_SWITCH_W 20.0f
#define INPUTWHEEL_SWITCH_H 12.0f
static void inputWheel_DrawShapeSwitch(touchUiDrawList* l, float x, float y, float w, int bOn)
{
    const float tx = x + w * 0.5f - 7.0f - INPUTWHEEL_SWITCH_W; // (the track's left)
    touchUiDraw_Text(l, "BEAM", x - INPUTWHEEL_SWITCH_W * 0.5f - 3.0f, y, 11.0f, 1.0f, touchUi_White(bOn ? 1.0f : 0.75f));
    const touchUiColor gold = touchUi_Rgba(1.0f, 0.85f, 0.3f, 0.95f);
    touchUiDraw_RRect(l, tx, y - INPUTWHEEL_SWITCH_H * 0.5f, INPUTWHEEL_SWITCH_W, INPUTWHEEL_SWITCH_H, INPUTWHEEL_SWITCH_H * 0.5f,
                      bOn ? touchUi_Rgba(1.0f, 0.85f, 0.3f, 0.55f) : touchUi_White(0.18f), 1.0f, bOn ? gold : touchUi_White(0.5f));
    const float kr = INPUTWHEEL_SWITCH_H * 0.5f - 2.0f;
    touchUiDraw_Circle(l, bOn ? tx + INPUTWHEEL_SWITCH_W - 2.0f - kr : tx + 2.0f + kr, y, kr, bOn ? touchUi_White(1.0f) : touchUi_White(0.7f), 0.0f, touchUi_Clear());
}

// The saber wheel's choices where they were laid out, each lit if it is
// the one set (the blades' in their colours), EQUIP dim while there is no
// saber to take
static void inputWheel_DrawChips(void)
{
    static const char* const aName[INPUTWHEEL_NUM_CHIPS] = { "SINGLE", "DUAL", "LEFT", "BOTH", "RIGHT", "EQUIP", "", "BEAM" };
    touchUiDrawList* l = inputWheel_pDraw;
    const int st = iosSaber_GetStyle();
    const int bTwo = iosSaber_StyleNumBlades(st) >= 2;
    for (int i = 0; i < INPUTWHEEL_NUM_CHIPS; i++) {
        if (!inputWheel_s.aChipShown[i]) continue;
        const float x = inputWheel_s.chipX[i], y = inputWheel_s.chipY[i];
        float alpha;
        int bOn = 0;
        if (i == INPUTWHEEL_CHIP_EQUIP) alpha = inputWheel_CanEquipSaber() ? 1.0f : 0.35f;
        else if (i == INPUTWHEEL_CHIP_LOOK) alpha = 1.0f;
        else if (i == INPUTWHEEL_CHIP_SHAPE) {
            bOn = iosSaberShape_Get() == IOSSABERSHAPE_BEAM;
            alpha = bOn ? 1.0f : 0.8f;
        }
        else {
            if (i == INPUTWHEEL_CHIP_SINGLE) bOn = !bTwo;
            else if (i == INPUTWHEEL_CHIP_DUAL) bOn = bTwo;
            else bOn = (i == INPUTWHEEL_CHIP_LEFT && inputWheel_s.saberTarget == INPUTWHEEL_SABER_LEFT)
                       || (i == INPUTWHEEL_CHIP_BOTH && inputWheel_s.saberTarget == INPUTWHEEL_SABER_BOTH)
                       || (i == INPUTWHEEL_CHIP_RIGHT && inputWheel_s.saberTarget == INPUTWHEEL_SABER_RIGHT);
            alpha = bOn ? 1.0f : 0.8f;
        }
        touchUiColor text = touchUi_White(1.0f);
        if (i == INPUTWHEEL_CHIP_LEFT || i == INPUTWHEEL_CHIP_RIGHT) {
            unsigned char r, g, b;
            iosSaber_ColourRGB(iosSaber_GetShownColour(st, i == INPUTWHEEL_CHIP_LEFT ? 1 : 0), &r, &g, &b);
            text = touchUi_Rgba((r * 0.7f + 255.0f * 0.3f) / 255.0f, (g * 0.7f + 255.0f * 0.3f) / 255.0f, (b * 0.7f + 255.0f * 0.3f) / 255.0f, 1.0f);
        }
        // (the shape's switch shows its state on its own switch: its border as an unpicked choice's)
        const int bLit = bOn && i != INPUTWHEEL_CHIP_SHAPE;
        const touchUiColor border = bLit ? touchUi_Rgba(1.0f, 0.85f, 0.3f, 0.95f) : touchUi_White(0.4f);
        const float w = inputWheel_s.chipW[i], h = inputWheel_s.chipH[i];
        const char* name = i == INPUTWHEEL_CHIP_LOOK ? iosSaber_LookName(iosSaber_GetLook()) : aName[i];
        float size = 11.0f, tw;
        // (the look's name kept inside its switch: smaller on a small wheel)
        touchUiDraw_MeasureText(name, size, 1.0f, &tw, NULL);
        if (tw > w - 10.0f) size = INPUTWHEEL_MAX_F(8.0f, floorf(size * (w - 10.0f) / tw * 2.0f) / 2.0f);
        l->alpha = alpha;
        touchUiDraw_RRect(l, x - w * 0.5f, y - h * 0.5f, w, h, h * 0.5f, touchUi_Black(0.55f), bLit ? 2.0f : 1.5f, border);
        if (i == INPUTWHEEL_CHIP_SHAPE) inputWheel_DrawShapeSwitch(l, x, y, w, bOn);
        else touchUiDraw_Text(l, name, x, y, size, 1.0f, text);
        l->alpha = 1.0f;
    }
}

void inputWheel_Draw(touchUiDrawList* pList, const inputWheelScreen* pScreen)
{
    if (!inputWheel_s.bOpen || !pList || !pScreen) return;
    inputWheel_pDraw = pList;
    touchUiDrawList* l = pList;
    inputWheel_Layout(pScreen);
    const inputWheelMap* m = inputWheel_s.pMap;
    const int kind = inputWheel_s.kind;
    const int bWeapons = kind == INPUTWHEEL_WEAPONS, bSaber = kind == INPUTWHEEL_SABER;
    const float cx = inputWheel_s.cx, cy = inputWheel_s.cy, R0 = inputWheel_s.R0, R1 = inputWheel_s.R1;
    const inputWheelFit* F = &inputWheel_s.aFit[kind];

    // the game dimmed; the disc
    touchUiDraw_RRect(l, 0.0f, 0.0f, pScreen->w, pScreen->h, 0.0f, touchUi_Black(0.45f), 0.0f, touchUi_Clear());
    touchUiDraw_Circle(l, cx, cy, R1 + 14.0f, touchUi_Black(0.85f), 0.0f, touchUi_Clear());

    const int hot = inputWheel_s.hot;
    const int bCancel = hot == INPUTWHEEL_CANCEL || (hot == -1 && inputWheel_s.bCancelShown);
    const int cur = bSaber ? iosSaber_GetShownColour(iosSaber_GetStyle(), inputWheel_SaberBlade())
                  : bWeapons ? iosGame_GetCurWeapon() : iosGame_GetCurPower();
    int curSlice = -1, bAnyEarned = 0;
    for (int i = 0; i < m->numSlices; i++) {
        if (inputWheel_s.aEarned[i]) bAnyEarned = 1;
        if (m->aSlices[i].bin == cur) curSlice = i;
    }

    touchUiColor aText[INPUTWHEEL_MAX];
    for (int pass = 0; pass < 2; pass++) { // (the popped-out slice on top)
        for (int i = 0; i < m->numSlices; i++) {
            const inputWheelSlice* sl = &m->aSlices[i];
            const inputWheelGroup* g = &m->aGroups[sl->group];
            const int bEarned = inputWheel_s.aEarned[i];
            const int bHot = i == hot;
            const int bPop = bHot && bEarned;
            if (bPop != pass) continue;
            touchUiColor fill, stroke, text;
            float lw;
            if (bWeapons && !bEarned && inputWheel_s.aOwned[i]) {
                // has it, can't switch to it: no ammo for it
                fill = inputWheel_Colour(g, 0.22f, 1.0f);
                stroke = (i == curSlice) ? touchUi_White(1.0f) : inputWheel_Colour(g, bHot ? 0.8f : 0.55f, 1.0f);
                lw = (bHot || i == curSlice) ? 2.5f : 2.0f;
                text = touchUi_White(0.75f);
            }
            else if (!bEarned) {
                const float v = bHot ? 0.24f : 0.18f;
                fill = touchUi_Rgba(v, v, v, 0.92f);
                stroke = inputWheel_Colour(g, bHot ? 0.7f : 0.45f, 1.0f);
                lw = bHot ? 2.5f : 2.0f;
                const float t = bHot ? 0.75f : 0.59f;
                text = touchUi_Rgba(t, t, t, 1.0f);
            }
            else if (bPop) {
                fill = inputWheel_Colour(g, 1.0f, 1.0f);
                stroke = touchUi_White(1.0f);
                lw = 2.5f;
                text = touchUi_Rgba(0.08f, 0.08f, 0.08f, 1.0f);
            }
            else {
                fill = inputWheel_Colour(g, 0.42f, 1.0f);
                stroke = (i == curSlice) ? touchUi_White(1.0f) : inputWheel_Colour(g, 0.8f, 1.0f);
                lw = (i == curSlice) ? 2.5f : 2.0f;
                text = touchUi_White(1.0f);
            }
            aText[i] = text;
            const float half = m->sliceDeg * 0.5f - 1.0f; // (a degree in from each edge)
            touchUiDraw_Wedge(l, cx, cy, sl->deg - half, sl->deg + half, R0, R1 + (bPop ? INPUTWHEEL_POP : 0.0f), fill, lw, stroke);
        }
    }

    // the needle: which way the stick, the mouse or the sliding thumb points
    if (inputWheel_s.bNeedle && hot != -1) {
        const float a = inputWheel_s.needleDeg * (float)M_PI / 180.0f;
        const touchUiColor nc = (hot >= 0 && inputWheel_s.aEarned[hot]) ? inputWheel_Colour(&m->aGroups[m->aSlices[hot].group], 1.0f, 1.0f)
                                                                        : touchUi_Rgba(0.6f, 0.6f, 0.6f, 1.0f);
        touchUiDraw_Line(l, cx + (R0 - 14.0f) * cosf(a), cy - (R0 - 14.0f) * sinf(a), cx + (R0 - 3.0f) * cosf(a),
                         cy - (R0 - 3.0f) * sinf(a), 3.0f, nc);
    }

    // each name in its slice, with its stars or its ammo count under it
    for (int i = 0; i < m->numSlices; i++) {
        const int bRow = F->font[i][1] > 0.0f && inputWheel_SliceHasRow(i) && (bWeapons ? inputWheel_s.aAmmo[i] >= 0 : inputWheel_s.aEarned[i]);
        const int bPop = i == hot && inputWheel_s.aEarned[i];
        const float a = m->aSlices[i].deg * (float)M_PI / 180.0f;
        const float px = cx + F->labelR[i][bRow] * cosf(a), py = cy - F->labelR[i][bRow] * sinf(a);
        float rowW, rowH;
        inputWheel_RowSize(&rowW, &rowH);
        char text[64];
        inputWheel_LabelText(i, text, sizeof(text));
        const float ly = bRow ? py - (INPUTWHEEL_STAR_GAP + rowH) * 0.5f : py;
        touchUiDraw_Text(l, text, px, ly, F->font[i][bRow], INPUTWHEEL_XSCALE, aText[i]);
        if (!bRow) continue;
        const float rcy = py + (F->labelH[i][bRow] + INPUTWHEEL_STAR_GAP) * 0.5f;
        if (!bWeapons) {
            const float k = F->rowScale;
            for (int j = 0; j < INPUTWHEEL_STARS; j++) {
                const float x = px + (j - (INPUTWHEEL_STARS - 1) * 0.5f) * INPUTWHEEL_STAR_STEP * k;
                inputWheel_DrawStar(x, rcy, k, j < inputWheel_s.aLevel[i], bPop);
            }
        }
        else {
            char num[16];
            snprintf(num, sizeof(num), "%d", INPUTWHEEL_MIN(inputWheel_s.aAmmo[i], 999));
            touchUiDraw_Text(l, num, px, rcy, INPUTWHEEL_AMMO_FONT * F->rowScale, 1.0f, aText[i]);
        }
    }

    // the groups' names beside the ring (LIGHT SIDE, TIER I...)
    const float left = INPUTWHEEL_MAX_F(pScreen->inL, 8.0f), right = pScreen->w - INPUTWHEEL_MAX_F(pScreen->inR, 8.0f);
    const float top = INPUTWHEEL_MAX_F(pScreen->inT, 8.0f), bottom = pScreen->h - INPUTWHEEL_MAX_F(pScreen->inB, 8.0f);
    for (int gi = 0; gi < m->numGroups; gi++) {
        const inputWheelGroup* grp = &m->aGroups[gi];
        if (!grp->title) continue;
        float tw, th;
        touchUiDraw_MeasureText(grp->title, 12.0f, 1.0f, &tw, &th);
        for (int k = 0; k <= 30; k++) {
            const float a = (grp->titleDeg + ((k & 1) ? 2.0f : -2.0f) * ((k + 1) / 2)) * (float)M_PI / 180.0f;
            float posx = cx, posy = cy;
            for (float r = R1 + 20.0f; r < R1 + 140.0f; r += 2.0f) {
                posx = cx + r * cosf(a);
                posy = cy - r * sinf(a);
                const float ex = INPUTWHEEL_MAX_F(fabsf(posx - cx) - tw * 0.5f, 0.0f);
                const float ey = INPUTWHEEL_MAX_F(fabsf(posy - cy) - th * 0.5f, 0.0f);
                if (sqrtf(ex * ex + ey * ey) >= R1 + 20.0f) break;
            }
            if (posx - tw * 0.5f < left || posx + tw * 0.5f > right || posy - th * 0.5f < top || posy + th * 0.5f > bottom) continue;
            if (!inputWheel_IsClear(posx - tw * 0.5f, posy - th * 0.5f, posx + tw * 0.5f, posy + th * 0.5f)) continue;
            touchUiDraw_Text(l, grp->title, posx, posy, 12.0f, 1.0f, inputWheel_Colour(grp, 1.0f, 1.0f));
            break;
        }
    }

    // the middle: the name, what letting go does, "cancel" in the gap
    const char* name;
    touchUiColor tc;
    const char* hint;
    const touchUiColor grey = touchUi_Rgba(0.6f, 0.6f, 0.6f, 1.0f);
    if (hot >= 0) {
        name = inputWheel_SlotName(m->aSlices[hot].bin);
        tc = inputWheel_s.aEarned[hot] ? inputWheel_Colour(&m->aGroups[m->aSlices[hot].group], 1.0f, 1.0f) : grey;
        if (inputWheel_s.aEarned[hot]) hint = "lift to select";
        else if (!bWeapons) hint = "not learned yet";
        else hint = inputWheel_s.aOwned[hot] ? "no ammo" : "not found yet";
    }
    else if (bCancel) {
        name = "CANCEL";
        tc = grey;
        hint = "lift to cancel";
    }
    else if (!bAnyEarned && !bWeapons && !bSaber) {
        name = "NO POWERS";
        tc = grey;
        hint = "none learned yet";
    }
    else {
        name = (curSlice >= 0) ? inputWheel_SlotName(cur) : (bSaber ? "SABER" : bWeapons ? "WEAPONS" : "FORCE");
        tc = (curSlice >= 0) ? inputWheel_Colour(&m->aGroups[m->aSlices[curSlice].group], 1.0f, 1.0f) : touchUi_White(1.0f);
        if (bSaber && iosSaber_StyleNumBlades(iosSaber_GetStyle()) >= 2)
            hint = inputWheel_s.saberTarget == INPUTWHEEL_SABER_LEFT ? "colour for the left saber"
                 : inputWheel_s.saberTarget == INPUTWHEEL_SABER_RIGHT ? "colour for the right saber" : "colour for both sabers";
        else if (bSaber) hint = inputWheel_s.bTapMode ? "tap a colour" : "slide toward a colour";
        else if (bWeapons) hint = inputWheel_s.bTapMode ? "tap a weapon" : "slide toward a weapon";
        else hint = inputWheel_s.bTapMode ? "tap a power" : "slide toward a power";
    }
    if (inputWheel_s.pfnHint) {
        // what letting go of the pad's button (or A, a click) does, in the
        // opener's words (why a slice can't be picked stays as it is)
        const int bPickable = hot >= 0 && inputWheel_s.aEarned[hot];
        const char* h = (hot >= 0 && !bPickable) ? NULL : inputWheel_s.pfnHint(bPickable, bCancel);
        if (h) {
            // (a PlayStation pad's button symbols, in words: the font is ASCII)
            static char hintAscii[96];
            inputWheel_Ascii(h, hintAscii, sizeof(hintAscii));
            hint = hintAscii;
        }
    }
    {
        // the title: "BOW- CASTER" and "LIGHT- SABER" whole; a weapon's name
        // too long for one line goes on two
        char title[64];
        const char* src = name ? name : "";
        int o = 0;
        for (int k = 0; src[k] && o < (int)sizeof(title) - 1; k++) {
            if (src[k] == '-' && src[k + 1] == ' ') {
                k++;
                continue;
            }
            title[o++] = src[k];
        }
        title[o] = 0;
        const float Wt = 2.0f * R0 * 0.8f;
        float w1;
        touchUiDraw_MeasureText(title, 17.0f, 1.0f, &w1, NULL);
        if (bWeapons && strchr(title, ' ') && w1 * INPUTWHEEL_TITLE_MIN > Wt) {
            for (char* p = title; *p; p++)
                if (*p == ' ') *p = '\n';
            float w2, h2;
            touchUiDraw_MeasureText(title, 17.0f, 1.0f, &w2, &h2);
            const float k = INPUTWHEEL_MIN(1.0f, INPUTWHEEL_MIN(Wt / w2, (0.6f * R0 + 5.0f) / h2));
            const float size = floorf(17.0f * k * 2.0f) / 2.0f;
            const float h = ceilf(h2 * size / 17.0f);
            touchUiDraw_Text(l, title, cx, cy + 5.0f - h * 0.5f, size, 1.0f, tc);
        }
        else {
            float size = 17.0f;
            if (w1 > Wt) size *= INPUTWHEEL_MAX_F(INPUTWHEEL_TITLE_MIN, Wt / w1);
            touchUiDraw_Text(l, title, cx, cy - 7.0f, size, 1.0f, tc);
        }
    }
    {
        float hw, size = 10.0f;
        touchUiDraw_MeasureText(hint, size, 1.0f, &hw, NULL);
        if (hw > 2.0f * R0) size *= INPUTWHEEL_MAX_F(0.8f, 2.0f * R0 / hw);
        touchUiDraw_Text(l, hint, cx, cy + 13.0f, size, 1.0f, touchUi_Rgba(0.8f, 0.8f, 0.8f, 1.0f));
    }
    touchUiDraw_Text(l, "cancel", cx, cy + R1 - 22.0f, 9.0f, 1.0f, touchUi_Rgba(0.59f, 0.59f, 0.59f, 1.0f));

    if (bSaber) inputWheel_DrawChips();
    inputWheel_pDraw = NULL;
}

#endif // ENH_INPUT || TOUCHUI_ENABLED

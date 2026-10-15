// Added: entire file. See enhSettings.h.

#include "Enhanced/enhSettings.h"

#if defined(JK_ENHANCED) && !defined(TARGET_IOS)

#include <string.h>

#include "Enhanced/iosSaber.h"
#include "Enhanced/iosGame.h"
#include "Main/sithCvar.h"
#include "General/stdString.h"
#include "engine_config.h"
#include "types_enums.h"

// The cvars' own copies (linked)
static char enhSettings_saberStyle[SITHCVAR_MAX_STRLEN];
static char enhSettings_aColour[3][SITHCVAR_MAX_STRLEN]; // single; dual right, left
static int32_t enhSettings_jumpStyle = IOSGAME_JUMP_CLASSIC; // (the desktops' own jump, as before; iOS: STEPS)
static int32_t enhSettings_bThirdPerson = 0;

static const char* enhSettings_aColourName[3] = { "enh_saberColourSingle", "enh_saberColourDualRight", "enh_saberColourDualLeft" };

// What was last made the same both ways (cvars and gameplay), to tell which changed
typedef struct {
    int style;
    int aColour[3];
    int jumpStyle;
    int bThirdPerson;
} enhSettingsState;
static enhSettingsState enhSettings_synced;
static int enhSettings_bSynced = 0;

static void enhSettings_ColourSlot(int i, int* pStyle, int* pBlade)
{
    *pStyle = i == 0 ? IOSSABER_STYLE_SINGLE : IOSSABER_STYLE_DUAL;
    *pBlade = i == 2 ? 1 : 0;
}

void enhSettings_StartupCvars(void)
{
    sithCvar_RegisterStr("enh_saberStyle", "SINGLE", enhSettings_saberStyle, CVARFLAG_GLOBAL);
    for (int i = 0; i < 3; i++)
        sithCvar_RegisterStr(enhSettings_aColourName[i], "game", enhSettings_aColour[i], CVARFLAG_GLOBAL);
    sithCvar_RegisterInt("enh_forceJumpStyle", IOSGAME_JUMP_CLASSIC, &enhSettings_jumpStyle, CVARFLAG_GLOBAL);
    sithCvar_RegisterBool("enh_thirdPerson", 0, &enhSettings_bThirdPerson, CVARFLAG_GLOBAL);
}

// The cvars, as the gameplay takes them (an unknown name or key: the default)
static void enhSettings_FromCvars(enhSettingsState* p)
{
    p->style = IOSSABER_STYLE_SINGLE;
    for (int st = 0; st < IOSSABER_NUM_STYLES; st++)
        if (!strcmp(enhSettings_saberStyle, iosSaber_StyleName(st))) p->style = st;
    for (int i = 0; i < 3; i++) {
        const int id = iosSaber_ColourByKey(enhSettings_aColour[i]);
        p->aColour[i] = id >= 0 ? id : IOSSABER_COLOUR_GAME;
    }
    p->jumpStyle = (enhSettings_jumpStyle >= 0 && enhSettings_jumpStyle < IOSGAME_JUMP_NUM_STYLES) ? enhSettings_jumpStyle : IOSGAME_JUMP_CLASSIC;
    p->bThirdPerson = enhSettings_bThirdPerson ? 1 : 0;
}

static void enhSettings_FromGameplay(enhSettingsState* p)
{
    p->style = iosSaber_GetStyle();
    for (int i = 0; i < 3; i++) {
        int st, blade;
        enhSettings_ColourSlot(i, &st, &blade);
        p->aColour[i] = iosSaber_GetColour(st, blade);
    }
    p->jumpStyle = iosGame_GetForceJumpStyle();
    p->bThirdPerson = iosGame_IsThirdPerson() ? 1 : 0;
}

void enhSettings_Sync(void)
{
    enhSettingsState c, g;
    enhSettings_FromCvars(&c);
    if (!enhSettings_bSynced) {
        // (the first time: the saved ones into the gameplay)
        enhSettings_FromGameplay(&g);
        enhSettings_synced = g;
        enhSettings_bSynced = 1;
    }
    else {
        enhSettings_FromGameplay(&g);
    }
    enhSettingsState* s = &enhSettings_synced;

    // A cvar changed: into the gameplay
    for (int i = 0; i < 3; i++) {
        if (c.aColour[i] != s->aColour[i]) {
            int st, blade;
            enhSettings_ColourSlot(i, &st, &blade);
            iosSaber_SetColour(st, blade, c.aColour[i]);
            s->aColour[i] = g.aColour[i] = c.aColour[i];
        }
    }
    if (c.style != s->style) {
        iosSaber_SetStyle(c.style);
        s->style = g.style = c.style;
    }
    if (c.jumpStyle != s->jumpStyle) {
        iosGame_SetForceJumpStyle(c.jumpStyle);
        s->jumpStyle = g.jumpStyle = c.jumpStyle;
    }
    if (c.bThirdPerson != s->bThirdPerson) {
        iosGame_SetThirdPerson(c.bThirdPerson);
        s->bThirdPerson = g.bThirdPerson = c.bThirdPerson;
    }

    // The gameplay changed (the camera key, a pick made in the game): into the cvar
    for (int i = 0; i < 3; i++) {
        if (g.aColour[i] != s->aColour[i]) {
            const char* key = iosSaber_ColourKey(g.aColour[i]);
            sithCvar_SetStr(enhSettings_aColourName[i], key ? key : "game");
            s->aColour[i] = g.aColour[i];
        }
    }
    if (g.style != s->style) {
        sithCvar_SetStr("enh_saberStyle", iosSaber_StyleName(g.style));
        s->style = g.style;
    }
    if (g.jumpStyle != s->jumpStyle) {
        sithCvar_SetInt("enh_forceJumpStyle", g.jumpStyle);
        s->jumpStyle = g.jumpStyle;
    }
    if (g.bThirdPerson != s->bThirdPerson) {
        sithCvar_SetBool("enh_thirdPerson", g.bThirdPerson);
        s->bThirdPerson = g.bThirdPerson;
    }
}

#endif // JK_ENHANCED && !TARGET_IOS

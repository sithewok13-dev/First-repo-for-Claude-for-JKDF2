// Added: entire file. A game controller's default bindings, and bringing a
// player's older ones up to them. See iosPad.h.

#include "Platform/iOS/iosPad.h"

#ifdef IOSPAD_ENABLED

#include <math.h>

#include "types.h"
#include "globals.h"
#include "Devices/sithControl.h"
#include "Main/Main.h"
#include "stdPlatform.h"

typedef struct {
    int func;
    int key;   // a pad key, or (axis) a pad axis
    int bAxis;
    int bReversed;
    float val; // an axis's sensitivity
} iosPadBind;

// The sticks, the same in both versions (MOTS slides the other way round)
#define IOSPADBINDS_STICKS(slideReversed) \
    { INPUT_FUNC_FORWARD, AXIS_JOY1_Y, 1, 1, 1.0f },  \
    { INPUT_FUNC_SLIDE,   AXIS_JOY1_X, 1, (slideReversed), 1.0f }, \
    { INPUT_FUNC_PITCH,   AXIS_JOY1_R, 1, 1, 1.25f }, \
    { INPUT_FUNC_TURN,    AXIS_JOY1_Z, 1, 1, 1.5f }

static const iosPadBind iosPadBinds_aV2[] = {
    IOSPADBINDS_STICKS(0),
    { INPUT_FUNC_JUMP,       KEY_JOY1_B1,     0, 0, 0 }, // A
    { INPUT_FUNC_DUCK,       KEY_JOY1_B2,     0, 0, 0 }, // B
    { INPUT_FUNC_ACTIVATE,   KEY_JOY1_B3,     0, 0, 0 }, // X
    { INPUT_FUNC_USESKILL,   KEY_JOY1_B4,     0, 0, 0 }, // Y
    { INPUT_FUNC_MAP,        KEY_JOY1_B5,     0, 0, 0 }, // Back (a tap)
    { INPUT_FUNC_FAST,       KEY_JOY1_B8,     0, 0, 0 }, // L3 (iosPad: a click toggles)
    { INPUT_FUNC_CAMERAMODE, KEY_JOY1_B9,     0, 0, 0 }, // R3
    { INPUT_FUNC_NEXTSKILL,  KEY_JOY1_B10,    0, 0, 0 }, // LB (held: the Force wheel)
    { INPUT_FUNC_NEXTWEAPON, KEY_JOY1_B11,    0, 0, 0 }, // RB (held: the weapon wheel)
    { INPUT_FUNC_FIRE2,      KEY_JOY1_B16,    0, 0, 0 }, // LT
    { INPUT_FUNC_FIRE1,      KEY_JOY1_B17,    0, 0, 0 }, // RT
    { INPUT_FUNC_USEINV,     KEY_JOY1_HUP,    0, 0, 0 },
    { INPUT_FUNC_NEXTINV,    KEY_JOY1_HRIGHT, 0, 0, 0 },
    { INPUT_FUNC_PREVINV,    KEY_JOY1_HDOWN,  0, 0, 0 },
    // (D-pad left: nothing, so it opens the saber wheel)
};
static const iosPadBind iosPadBinds_aV2Mots[] = { IOSPADBINDS_STICKS(1) };

// Version 1: what sithControl_MapDefaultsJoystick bound before
static const iosPadBind iosPadBinds_aV1[] = {
    IOSPADBINDS_STICKS(0),
    { INPUT_FUNC_USELASTSELECTED, KEY_JOY1_B1,     0, 0, 0 },
    { INPUT_FUNC_DUCK,            KEY_JOY1_B2,     0, 0, 0 },
    { INPUT_FUNC_ACTIVATE,        KEY_JOY1_B3,     0, 0, 0 },
    { INPUT_FUNC_JUMP,            KEY_JOY1_B4,     0, 0, 0 },
    { INPUT_FUNC_USEINV,          KEY_JOY1_B8,     0, 0, 0 },
    { INPUT_FUNC_USESKILL,        KEY_JOY1_B9,     0, 0, 0 },
    { INPUT_FUNC_NEXTINV,         KEY_JOY1_HUP,    0, 0, 0 },
    { INPUT_FUNC_PREVINV,         KEY_JOY1_HDOWN,  0, 0, 0 },
    { INPUT_FUNC_PREVSKILL,       KEY_JOY1_HLEFT,  0, 0, 0 },
    { INPUT_FUNC_NEXTSKILL,       KEY_JOY1_HRIGHT, 0, 0, 0 },
    { INPUT_FUNC_PREVWEAPON,      KEY_JOY1_B10,    0, 0, 0 },
    { INPUT_FUNC_NEXTWEAPON,      KEY_JOY1_B11,    0, 0, 0 },
    { INPUT_FUNC_FIRE2,           KEY_JOY1_B16,    0, 0, 0 },
    { INPUT_FUNC_FIRE1,           KEY_JOY1_B17,    0, 0, 0 },
};

#define IOSPADBINDS_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define IOSPADBINDS_NUM_STICKS 4

static int iosPadBinds_IsPadEntry(const stdControlKeyInfoEntry* e)
{
    if (e->flags & INPUT_MAPPING_FLAG_AXIS) return e->dxKeyNum >= AXIS_JOY1_X && e->dxKeyNum <= AXIS_JOY2_V;
    return KEY_IS_JOY_BUTTON(e->dxKeyNum);
}

// The binding b as this game has it (MOTS slides its stick the other way)
static iosPadBind iosPadBinds_ForGame(const iosPadBind* b, int i)
{
    iosPadBind r = *b;
    if (Main_bMotsCompat && i < IOSPADBINDS_NUM_STICKS) r = iosPadBinds_aV2Mots[i];
    return r;
}

static void iosPadBinds_Apply(const iosPadBind* b)
{
    if (b->bAxis) {
        stdControlKeyInfoEntry* e = sithControl_BindAxis(b->func, b->key, b->bReversed ? INPUT_MAPPING_FLAG_AXIS_REVERSED : 0);
        if (e) e->binaryAxisVal = b->val;
    }
    else {
        sithControl_BindControl(b->func, b->key, 0);
    }
}

void iosPadBinds_MapDefaults(void)
{
    for (int i = 0; i < IOSPADBINDS_COUNT(iosPadBinds_aV2); i++) {
        iosPadBind b = iosPadBinds_ForGame(&iosPadBinds_aV2[i], i);
        iosPadBinds_Apply(&b);
    }
}

// Whether e (func's) is one of version 1's defaults, as they were made
static int iosPadBinds_IsV1(int func, const stdControlKeyInfoEntry* e)
{
    for (int i = 0; i < IOSPADBINDS_COUNT(iosPadBinds_aV1); i++) {
        iosPadBind b = iosPadBinds_ForGame(&iosPadBinds_aV1[i], i);
        if (b.func != func || b.key != e->dxKeyNum) continue;
        if (b.bAxis != ((e->flags & INPUT_MAPPING_FLAG_AXIS) != 0)) continue;
        if (!b.bAxis) return 1;
        if (b.bReversed != ((e->flags & INPUT_MAPPING_FLAG_AXIS_REVERSED) != 0)) continue;
        if (e->flags & INPUT_MAPPING_FLAG_RAW_AXIS) continue;
        if (fabsf((float)e->binaryAxisVal - b.val) > 0.01f) continue;
        return 1;
    }
    return 0;
}

// Takes every pad binding away
static void iosPadBinds_UnbindAll(void)
{
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        uint32_t n = 0;
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            if (!iosPadBinds_IsPadEntry(&k->aEntries[j])) k->aEntries[n++] = k->aEntries[j];
        }
        k->numEntries = n;
    }
}

// Whether anything is bound to the pad axis
static int iosPadBinds_AxisBound(int axis)
{
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            if ((k->aEntries[j].flags & INPUT_MAPPING_FLAG_AXIS) && k->aEntries[j].dxKeyNum == axis) return 1;
        }
    }
    return 0;
}

// Whether func has a pad axis bound to it
static int iosPadBinds_FuncHasPadAxis(int func)
{
    const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[func];
    for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
        const stdControlKeyInfoEntry* e = &k->aEntries[j];
        if ((e->flags & INPUT_MAPPING_FLAG_AXIS) && iosPadBinds_IsPadEntry(e)) return 1;
    }
    return 0;
}

int iosPadBinds_Migrate(int savedVersion)
{
    if (savedVersion >= IOSPADBINDS_VERSION) return 0;

    int numPad = 0, numOwn = 0;
    for (int f = 0; f < INPUT_FUNC_MAX; f++) {
        const stdControlKeyInfo* k = &sithControl_aInputFuncToKeyinfo[f];
        for (uint32_t j = 0; j < k->numEntries && j < 8; j++) {
            const stdControlKeyInfoEntry* e = &k->aEntries[j];
            if (!iosPadBinds_IsPadEntry(e)) continue;
            numPad++;
            if (!iosPadBinds_IsV1(f, e)) numOwn++;
        }
    }

    if (numOwn == 0) {
        // version 1's defaults (all, or some): the new ones instead
        iosPadBinds_UnbindAll();
        iosPadBinds_MapDefaults();
        stdPlatform_Printf("iosPadBinds: %d version 1 pad bindings -> version %d defaults\n", numPad, IOSPADBINDS_VERSION);
        return 1;
    }

    // the player's own: kept, with the sticks bound where nothing is
    int numAdded = 0;
    for (int i = 0; i < IOSPADBINDS_NUM_STICKS; i++) {
        iosPadBind b = iosPadBinds_ForGame(&iosPadBinds_aV2[i], i);
        if (iosPadBinds_FuncHasPadAxis(b.func) || iosPadBinds_AxisBound(b.key)) continue;
        iosPadBinds_Apply(&b);
        numAdded++;
    }
    stdPlatform_Printf("iosPadBinds: %d pad bindings, %d the player's own: kept, %d stick bindings added\n", numPad, numOwn, numAdded);
    return 1;
}

#endif // IOSPAD_ENABLED

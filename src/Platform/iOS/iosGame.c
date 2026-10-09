// Added: entire file. See iosGame.h.

#include "Platform/iOS/iosGame.h"

#ifdef TARGET_IOS

#include "Dss/sithGamesave.h"
#include "General/stdString.h"
#include "Main/jkDev.h"
#include "Main/jkHud.h"
#include "Main/sithMain.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithTime.h"
#include "Devices/sithSoundMixer.h"
#include "Main/jkMain.h"
#include "World/jkPlayer.h"
#include "World/sithWorld.h"
#include "stdPlatform.h"
#include "jk.h"

#define IOSGAME_QUICKSAVE_FNAME "quicksave.jks"

// The local player, if a level is running and it has player data
static SithThing* iosGame_GetPlayer(void)
{
    SithThing* pPlayer = sithPlayer_g_pLocalPlayerThing;
    if (!sithWorld_g_pCurrentWorld || !pPlayer || pPlayer->type != SITH_THING_PLAYER
        || !pPlayer->actorParams.pPlayer || pPlayer->actorParams.pPlayer == (SithPlayer*)-136)
        return NULL;
    return pPlayer;
}

const char* iosGame_GetForcePowerName(void)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    return pPlayer ? iosGame_GetPowerName(pPlayer->actorParams.pPlayer->curPower) : NULL;
}

const char* iosGame_GetPowerName(int bin)
{
    switch (bin)
    {
        case SITHBIN_F_JUMP:        return "JUMP";
        case SITHBIN_F_SPEED:       return "SPEED";
        case SITHBIN_F_SEEING:      return "SEEING";
        case SITHBIN_F_PULL:        return "PULL";
        case SITHBIN_F_HEALING:     return "HEALING";
        case SITHBIN_F_PERSUASION:  return "PERSUADE";
        case SITHBIN_F_BLINDING:    return "BLINDING";
        case SITHBIN_F_ABSORB:      return "ABSORB";
        case SITHBIN_F_PROTECTION:  return "PROTECT";
        case SITHBIN_F_THROW:       return "THROW";
        case SITHBIN_F_GRIP:        return "GRIP";
        case SITHBIN_F_LIGHTNING:   return "LIGHTNING";
        case SITHBIN_F_DESTRUCTION: return "DESTRUCT";
        case SITHBIN_F_DEADLYSIGHT: return "DEADLY SIGHT";
        // Mysteries of the Sith
        case SITHBIN_F_DEFENSE:     return "DEFENSE";
        case SITHBIN_F_FARSIGHT:    return "FAR SIGHT";
        case SITHBIN_F_PROJECT:     return "PROJECT";
        case SITHBIN_F_SABERTHROW:  return "SABER THROW";
        case SITHBIN_F_PUSH:        return "PUSH";
        case SITHBIN_F_CHAINLIGHT:  return "CHAIN LIGHT";
        default:                    return NULL;
    }
}

int iosGame_QuickLoad(void)
{
    char path[128];
    sithGamesave_Header header;
    int bHeaderOk = 0;

    // Single player only, and only once a level is running (the overlay is
    // hidden everywhere else anyway)
    if (sithNet_isMulti || !sithWorld_g_pCurrentWorld || !sithPlayer_g_pLocalPlayerThing)
        return 0;

    sithGamesave_GetProfilePath(path, sizeof(path), IOSGAME_QUICKSAVE_FNAME);
    stdFile_t f = pLowLevelHS->fileOpen(path, "rb");
    if (f)
    {
        // Same checks the Load menu makes before it lists a save
        bHeaderOk = pLowLevelHS->fileRead(f, &header, sizeof(header)) == sizeof(header)
                    && (header.version == 6 || header.version == 0x7D6);
        pLowLevelHS->fileClose(f);
    }
    if (!bHeaderOk)
    {
        jkDev_PrintUniString(u"No quicksave yet");
        return 0;
    }
    header.episodeName[sizeof(header.episodeName) - 1] = 0;
    header.jklName[sizeof(header.jklName) - 1] = 0;
    header.saveName[255] = 0;

    // Mirrors jkGuiSaveLoad_Show's load path: a save from this level restores
    // in place; one from another level goes through the level loader.
    if (__strcmpi(header.episodeName, sithWorld_g_pCurrentWorld->episodeName)
        || __strcmpi(header.jklName, sithWorld_g_pCurrentWorld->map_jkl_fname))
    {
        // The menu passes the part of the save name after '~' as the title
        char16_t* pTitle = __wcschr(header.saveName, U'~');
        pTitle = pTitle ? pTitle + 1 : header.saveName;
        jkMain_sub_4034D0(header.episodeName, IOSGAME_QUICKSAVE_FNAME, header.jklName, pTitle);
    }
    else if (!jkPlayer_LoadSave(IOSGAME_QUICKSAVE_FNAME))
    {
        jkDev_PrintUniString(u"Quick load failed");
        return 0;
    }

    jkDev_PrintUniString(u"Quick-loaded");
    return 1;
}

int iosGame_IsMots(void)
{
    return Main_bMotsCompat ? 1 : 0;
}

int iosGame_IsPowerAvailable(int bin)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (!pPlayer || bin < 0 || bin >= SITHBIN_NUMBINS)
        return 0;

    // The same test the next/previous power keys make (sithInventory_FindNextTypeID).
    // Jedi Knight's also wants the force power flag; Mysteries of the Sith's
    // goes by its own list of powers instead (sithInventory_aMotsForcePowerBins).
    int flags = sithInventory_g_aTypes[bin].flags;
    if (!(flags & SITHINVENTORY_TYPE_REGISTERED))
        return 0;
    if (!Main_bMotsCompat && !(flags & SITHINVENTORY_TYPE_AUTOAIM))
        return 0;
    return (pPlayer->actorParams.pPlayer->aItems[bin].state & SITHINVENTORY_ITEM_AVAILABLE) != 0;
}

int iosGame_GetPowerLevel(int bin)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (!pPlayer || bin < 0 || bin >= SITHBIN_NUMBINS)
        return 0;

    // The stars the Force screen shows (jkGuiForce_ForceStarsDraw): 1-4 in
    // Jedi Knight once learned (none at 0), 0-4 in Mysteries of the Sith.
    // The Force screen stops at 4 (curLevel < 4), but it, the rank-8 capstone
    // and save loads write the bin directly (sithPlayer_SetInvItemAmount,
    // DSS_INVENTORY), without items.dat's min/max, so it is clamped here. A
    // float, truncated as the Force screen does.
    int level = (int)sithInventory_GetInventory(pPlayer, bin);
    return level < 0 ? 0 : (level > 4 ? 4 : level);
}

int iosGame_GetCurPower(void)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    return pPlayer ? pPlayer->actorParams.pPlayer->curPower : -1;
}

void iosGame_SelectPower(int bin)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (pPlayer && bin != pPlayer->actorParams.pPlayer->curPower)
        sithInventory_SelectPower(pPlayer, bin);
}

int iosGame_GetItem(int bin, int* pAmount, int* pActive)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    *pAmount = 0;
    *pActive = 0;
    if (!pPlayer || bin < 0 || bin >= SITHBIN_NUMBINS || !sithInventory_IsInventoryAvailable(pPlayer, bin))
        return 0;
    // The HUD's inventory strip lists an item only while it has some
    *pAmount = (int)sithInventory_GetInventory(pPlayer, bin);
    *pActive = sithInventory_IsInventoryActivated(pPlayer, bin);
    return *pAmount > 0;
}

void iosGame_SelectItem(int bin)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (pPlayer && bin != pPlayer->actorParams.pPlayer->curItemID)
        sithInventory_SelectItem(pPlayer, bin);
}

static int iosGame_bHoldWanted = 0;
static int iosGame_bHolding = 0;

void iosGame_SetHold(int bHold)
{
    iosGame_bHoldWanted = bHold;
}

int iosGame_HoldGameplay(void)
{
    int bHold = iosGame_bHoldWanted && !sithNet_isMulti;
    if (bHold && !iosGame_bHolding)
    {
        // Sounds pause with the clock, as when the Esc menu opens
        sithTime_Pause();
        sithSoundMixer_StopAll();
        iosGame_bHolding = 1;
    }
    else if (!bHold && iosGame_bHolding)
    {
        // Picks the clock and the sounds up where they stopped, as leaving the
        // Esc menu does
        sithSoundMixer_ResumeAll();
        sithTime_Resume();
        iosGame_bHolding = 0;
    }
    // The world is still drawn while held, and drawing skips anything already
    // drawn this render tick -- which only the (skipped) update moves on
    if (bHold)
        sithAdvanceRenderTick();
    return bHold;
}

// Full force meter: Jedi rank x 50, the level the game itself fills it to
// (kyle.cog "Set Mana to full", force_well.cog, pow_mana.cog); the HUD's own
// 0-400 scale is the meter at the top rank
static float iosGame_GetForceManaMax(SithThing* pPlayer)
{
    if (Main_bMotsCompat)
    {
        float maxMana = (float)sithInventory_GetInventory(pPlayer, SITHBIN_MAXMANA);
        if (maxMana > 0.0f)
            return maxMana;
    }
    return (float)sithInventory_GetInventory(pPlayer, SITHBIN_JEDI_RANK) * 50.0f;
}

float iosGame_GetForceMana(int* pbFull)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    *pbFull = 0;
    if (!pPlayer)
        return -1.0f;
    float mana = (float)sithInventory_GetInventory(pPlayer, SITHBIN_FORCEMANA);
    float maxMana = iosGame_GetForceManaMax(pPlayer);
    if (maxMana <= 0.0f)
        return 0.0f;
    if (mana >= maxMana)
        *pbFull = 1;
    float frac = mana / maxMana;
    return frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
}

int iosGame_IsHolding(void)
{
    return iosGame_bHolding;
}

unsigned int iosGame_GetFrameCount(void)
{
    // Counted once per jkGame_Update, the same count the "framerate" console
    // command divides by time
    return (unsigned int)Video_dword_5528A0;
}

int iosGame_IsAlwaysRun(void)
{
    // The option's checkbox sets bit 2 (jkGuiControlOptions_Show), saved with
    // the player's controls (sithControl_WriteConf "flags="). Both
    // sithControl_PlayerMovement and its Mysteries of the Sith version run
    // when it is set or INPUT_FUNC_FAST (Shift) is held.
    return (sithWeapon_controlOptions & 2) ? 1 : 0;
}

// What one count on a mouse axis adds to an input function's axis, summed
// over its raw bindings to that axis: sithControl_GetAxis reverses each one
// that is reversed and scales it by its binaryAxisVal (none if 0)
static float iosGame_MouseAxisScale(int func, int axis)
{
    float scale = 0.0f;
    stdControlKeyInfo* pInfo = &sithControl_aInputFuncToKeyinfo[func];
    for (uint32_t i = 0; i < pInfo->numEntries; i++)
    {
        stdControlKeyInfoEntry* pEntry = &pInfo->aEntries[i];
        if (pEntry->dxKeyNum != axis || !(pEntry->flags & INPUT_MAPPING_FLAG_RAW_AXIS))
            continue;
        float v = (pEntry->binaryAxisVal != 0.0f) ? (float)pEntry->binaryAxisVal : 1.0f;
        scale += (pEntry->flags & INPUT_MAPPING_FLAG_AXIS_REVERSED) ? -v : v;
    }
    return scale;
}

void iosGame_GetMouseLookDegrees(float* pTurn, float* pPitch)
{
    // The turn axis is how far the player turns left in a frame (degrees:
    // the turn rate is set to it times the frame rate), the pitch axis how
    // far the head tilts up
    *pTurn = -iosGame_MouseAxisScale(INPUT_FUNC_TURN, AXIS_MOUSE_X);
    *pPitch = -iosGame_MouseAxisScale(INPUT_FUNC_PITCH, AXIS_MOUSE_Y);
}

void iosGame_ToggleChat(void)
{
    if (jkHud_bChatOpen)
    {
        jkHud_idk_time(); // closes it, as Return does after sending
        return;
    }
    if (iosGame_GetPlayer())
        jkHud_Chat();
}

#endif // TARGET_IOS

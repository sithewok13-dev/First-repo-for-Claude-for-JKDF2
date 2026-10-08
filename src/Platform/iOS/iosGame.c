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

// The order the game cycles through them in (sithInventory_aMotsForcePowerBins
// for Mysteries of the Sith)
static const int iosGame_aJkPowerBins[] = {
    SITHBIN_F_JUMP, SITHBIN_F_SPEED, SITHBIN_F_SEEING, SITHBIN_F_PULL,
    SITHBIN_F_HEALING, SITHBIN_F_PERSUASION, SITHBIN_F_BLINDING, SITHBIN_F_ABSORB, SITHBIN_F_PROTECTION,
    SITHBIN_F_THROW, SITHBIN_F_GRIP, SITHBIN_F_LIGHTNING, SITHBIN_F_DESTRUCTION, SITHBIN_F_DEADLYSIGHT,
};
static const int iosGame_aMotsPowerBins[] = {
    SITHBIN_F_JUMP, SITHBIN_F_SPEED, SITHBIN_F_SEEING, SITHBIN_F_PROJECT, SITHBIN_F_PUSH, SITHBIN_F_PULL,
    SITHBIN_F_GRIP, SITHBIN_F_FARSIGHT, SITHBIN_F_SABERTHROW, SITHBIN_F_HEALING, SITHBIN_F_PERSUASION,
    SITHBIN_F_BLINDING, SITHBIN_F_CHAINLIGHT, SITHBIN_F_ABSORB, SITHBIN_F_PROTECTION,
    SITHBIN_F_DESTRUCTION, SITHBIN_F_DEADLYSIGHT,
};

int iosGame_GetForcePowers(int* aBins, int maxBins)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (!pPlayer) return 0;

    const int* aOrder = Main_bMotsCompat ? iosGame_aMotsPowerBins : iosGame_aJkPowerBins;
    int numOrder = Main_bMotsCompat ? (int)(sizeof(iosGame_aMotsPowerBins) / sizeof(int))
                                    : (int)(sizeof(iosGame_aJkPowerBins) / sizeof(int));
    int num = 0;
    for (int i = 0; i < numOrder && num < maxBins; i++)
    {
        // The same test the next/previous power keys make (sithInventory_FindNextTypeID)
        int bin = aOrder[i];
        int flags = sithInventory_g_aTypes[bin].flags;
        if ((flags & SITHINVENTORY_TYPE_AUTOAIM) && (flags & SITHINVENTORY_TYPE_REGISTERED)
            && (pPlayer->actorParams.pPlayer->aItems[bin].state & SITHINVENTORY_ITEM_AVAILABLE))
            aBins[num++] = bin;
    }
    return num;
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
        sithTime_Pause();
        iosGame_bHolding = 1;
    }
    else if (!bHold && iosGame_bHolding)
    {
        // Picks the clock up where it stopped, as leaving the Esc menu does
        sithTime_Resume();
        iosGame_bHolding = 0;
    }
    // The world is still drawn while held, and drawing skips anything already
    // drawn this render tick -- which only the (skipped) update moves on
    if (bHold)
        sithAdvanceRenderTick();
    return bHold;
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

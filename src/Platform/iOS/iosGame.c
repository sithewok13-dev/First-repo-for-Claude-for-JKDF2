// Added: entire file. See iosGame.h.

#include "Platform/iOS/iosGame.h"

#ifdef TARGET_IOS

#include "Dss/sithGamesave.h"
#include "General/stdString.h"
#include "Main/jkDev.h"
#include "Main/jkMain.h"
#include "World/jkPlayer.h"
#include "World/sithWorld.h"
#include "stdPlatform.h"
#include "jk.h"

#define IOSGAME_QUICKSAVE_FNAME "quicksave.jks"

const char* iosGame_GetForcePowerName(void)
{
    SithThing* pPlayer = sithPlayer_g_pLocalPlayerThing;
    if (!sithWorld_g_pCurrentWorld || !pPlayer || !pPlayer->actorParams.pPlayer)
        return NULL;

    switch (pPlayer->actorParams.pPlayer->curPower)
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

#endif // TARGET_IOS

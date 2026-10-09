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
#include "Cog/sithCog.h"
#include "World/sithThing.h"
#include "World/sithWeapon.h"
#include "Devices/sithSoundMixer.h"
#include "Main/jkMain.h"
#include "World/jkPlayer.h"
#include "World/sithWorld.h"
#include "General/stdFileUtil.h"
#include "General/stdFnames.h"
#include "General/util.h"
#include "Main/jkRes.h"
#include "stdPlatform.h"
#include "jk.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "SDL2_helper.h"

#define IOSGAME_QUICKSAVE_FNAME "quicksave.jks"
// How long (game time) a weapon picked on the touch overlay's wheel waits for
// the game to take it: a switch already under way and the new weapon's mount
// take about 1.5 s (iosGame_SelectWeapon)
#define IOSGAME_WEAPON_PICK_WAIT 3.0

// Mysteries of the Sith: the weapon each number key last selected, by weapon
// index (sithWeapon.c; not in its header)
extern int sithWeapon_motsAConv[10];

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

    // A weapon picked on the touch overlay's wheel and still waiting belongs
    // to the game being thrown away, not the one loaded
    iosGame_CancelWeaponPick();

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

// ------------------------------------------------------------ weapons

const char* iosGame_GetWeaponName(int bin)
{
    switch (bin)
    {
        // Jedi Knight: the game's own names (jkstrings.uni SELECT1..SELECT0)
        case SITHBIN_FISTS:              return "FISTS";
        case SITHBIN_BRYARPISTOL:        return "BRYAR PISTOL";
        case SITHBIN_STORMTROOPER_RIFLE: return "STORMTROOPER RIFLE";
        case SITHBIN_THERMAL_DETONATOR:  return "THERMAL DETONATOR";
        case SITHBIN_TUSKEN_PROD:        return "BOWCASTER"; // items.dat's tusken_prod
        case SITHBIN_REPEATER:           return "REPEATER";
        case SITHBIN_RAIL_DETONATOR:     return "RAIL DETONATOR";
        case SITHBIN_SEQUENCER_CHARGE:   return "SEQUENCER CHARGE";
        case SITHBIN_CONCUSSION_RIFLE:   return "CONCUSSION RIFLE";
        case SITHBIN_LIGHTSABER:         return "LIGHTSABER";
        // Mysteries of the Sith: from the bins' names, short enough for its
        // 17 narrower slices with the ammo count under them
        case SITHBIN_MOTS_FISTS:              return "FISTS";
        case SITHBIN_MOTS_BRYARPISTOL:        return "BRYAR";
        case SITHBIN_MOTS_STORMTROOPER_RIFLE: return "RIFLE";
        case SITHBIN_MOTS_THERMAL_DETONATOR:  return "THERMAL";
        case SITHBIN_MOTS_REPEATER:           return "REPEATER";
        case SITHBIN_MOTS_RAIL_DETONATOR:     return "RAIL DET";
        case SITHBIN_MOTS_SEQUENCER_CHARGE:   return "SEQUENCER";
        case SITHBIN_MOTS_CONCUSSION_RIFLE:   return "CONC RIFLE";
        case SITHBIN_MOTS_EWEB:               return "E-WEB";
        case SITHBIN_MOTS_LIGHTSABER:         return "LIGHT- SABER"; // (one word too wide for its slice)
        case SITHBIN_MOTS_BLASTECH:           return "BLASTECH";
        case SITHBIN_MOTS_STORMTROOPER_SCOPE: return "SCOPE RIFLE";
        case SITHBIN_MOTS_FLASH_BOMB:         return "FLASH BOMB";
        case SITHBIN_MOTS_TUSKEN_PROD:        return "BOW- CASTER";
        case SITHBIN_MOTS_RAIL_SEEKER:        return "RAIL SEEKER";
        case SITHBIN_MOTS_MANUAL_SEQUENCER:   return "MANUAL SEQ";
        case SITHBIN_MOTS_CARBO_GUN:          return "CARBO GUN";
        default:                              return NULL;
    }
}

// The bin a weapon's ammo counter shows, as the HUD's (jkHud_GetWeaponAmmo),
// or -1 for none (fists, lightsaber)
static int iosGame_WeaponAmmoBin(int bin)
{
    if (Main_bMotsCompat)
    {
        // By weapon index (jkHud.c, Mysteries of the Sith branch), with the
        // same quirk: a number below 11 there is a weapon index, not a bin
        static const int aAmmo[21] = {
            -1, -1, SITHBIN_ENERGY, SITHBIN_ENERGY, SITHBIN_MOTS_THERMAL_DETONATOR, SITHBIN_CARBPELLETS,
            SITHBIN_POWER, SITHBIN_RAILCHARGES, SITHBIN_MOTS_SEQUENCER_CHARGE, SITHBIN_POWER, SITHBIN_EWEB_ROUNDS,
            -1, SITHBIN_ENERGY, SITHBIN_ENERGY, SITHBIN_MOTS_FLASH_BOMB, SITHBIN_POWER,
            -1, SITHBIN_SEEKRAILS, SITHBIN_MOTS_SEQUENCER_CHARGE, -1, SITHBIN_CARBPELLETS
        };
        int idx = sithInventory_SelectWeaponPrior(bin);
        if (idx < 0 || idx > 20)
            return -1;
        int ammo = aAmmo[idx];
        return (ammo < 11) ? sithInventory_SelectWeaponFollowing(ammo) : ammo;
    }
    // By bin (jkHud.c, Jedi Knight branch): thermal detonators and sequencer
    // charges are their own ammo
    static const int aAmmo[11] = {
        -1, -1, SITHBIN_ENERGY, SITHBIN_ENERGY, SITHBIN_THERMAL_DETONATOR, SITHBIN_POWER,
        SITHBIN_POWER, SITHBIN_RAILCHARGES, SITHBIN_SEQUENCER_CHARGE, SITHBIN_POWER, -1
    };
    return (bin >= 0 && bin <= 10) ? aAmmo[bin] : -1;
}

int iosGame_GetWeapon(int bin, int* pAmmo, int* pbSelectable)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    *pAmmo = -1;
    *pbSelectable = 0;
    if (!pPlayer || bin < 0 || bin >= SITHBIN_NUMBINS || !(sithInventory_g_aTypes[bin].flags & ITEMINFO_WEAPON))
        return 0;
    // Every weapon is available from the start (items.dat's DEFAULT flag):
    // the player has one once its bin isn't empty
    if (sithInventory_GetInventory(pPlayer, bin) == 0.0 || !sithInventory_IsInventoryAvailable(pPlayer, bin))
        return 0;
    int ammoBin = iosGame_WeaponAmmoBin(bin);
    if (ammoBin >= 0 && ammoBin < SITHBIN_NUMBINS)
    {
        int ammo = (int)sithInventory_GetInventory(pPlayer, ammoBin); // truncated, as the HUD does
        *pAmmo = ammo < 0 ? 0 : ammo;
    }
    // What sithWeapon_SelectWeapon asks before it switches: the weapon cog's
    // answer to the ammo query (sender -1), which only reads the inventory.
    // (It turns auto-switching's bit 2 off around the question; so does this.)
    sithCog* pCog = sithInventory_g_aTypes[bin].cog;
    int bOk = 1;
    if (pCog)
    {
        int autoSwitch = sithWeapon_bAutoSwitch & 2, multiAutoSwitch = sithWeapon_bMultiplayerAutoSwitch & 2;
        sithWeapon_bAutoSwitch &= ~2;
        sithWeapon_bMultiplayerAutoSwitch &= ~2u;
        bOk = sithCog_SendMessageEx(pCog, SITH_MESSAGE_AUTOSELECT, SENDERTYPE_SYSTEM, -1, SENDERTYPE_THING, pPlayer->idx, 0, 0.0, 0.0, 0.0, 0.0) >= 0.0;
        sithWeapon_bAutoSwitch |= autoSwitch;
        sithWeapon_bMultiplayerAutoSwitch |= multiAutoSwitch;
    }
    *pbSelectable = bOk;
    return 1;
}

// A weapon picked on the wheel, waiting for the game to take it (-1: none),
// when it was picked and until when it waits (game time)
static int iosGame_pendingWeapon = -1;
static flex32_t iosGame_pendingFrom = 0.0f, iosGame_pendingUntil = 0.0f;

int iosGame_GetCurWeapon(void)
{
    SithThing* pPlayer = iosGame_GetPlayer();
    if (!pPlayer)
        return -1;
    if (iosGame_pendingWeapon >= 0)
        return iosGame_pendingWeapon;
    if (sithWeapon_8BD024 != -1) // a switch under way: the weapon it goes to
        return sithWeapon_8BD024;
    return pPlayer->actorParams.pPlayer->curWeaponID;
}

void iosGame_CancelWeaponPick(void)
{
    iosGame_pendingWeapon = -1;
}

// Hands the waiting pick to the game once it would take the weapon's number
// key, as sithWeapon_ProcessWeaponControls does; until then it waits. Called
// on picking and before every gameplay tick that isn't held.
static void iosGame_TryPendingWeapon(void)
{
    int bin = iosGame_pendingWeapon;
    if (bin < 0)
        return;
    SithThing* pPlayer = iosGame_GetPlayer();
    int ammo = 0, bSelectable = 0;
    // (The weapon in hand while a switch is under way is the one being put
    // away: picking it waits for the switch, then switches back.)
    if (!pPlayer || (pPlayer->flags & SITH_TF_DEAD) || !iosGame_GetWeapon(bin, &ammo, &bSelectable) || !bSelectable
        || (bin == pPlayer->actorParams.pPlayer->curWeaponID && sithWeapon_8BD024 == -1)
        || sithTime_g_secGameTime > iosGame_pendingUntil || sithTime_g_secGameTime < iosGame_pendingFrom)
    {
        // taken away, already in hand, waited long enough, or the game clock
        // went back (a game loaded some other way than QUICK LOAD)
        iosGame_pendingWeapon = -1;
        return;
    }
    // The number keys' gates: not while the weapon fires its own targeting,
    // a weapon mounts or holsters, or another switch is under way
    if ((pPlayer->weaponParams.flags & SITH_WF_EMITAITARGETEDEVENT) || sithTime_g_secGameTime < sithWeapon_secMountWait
        || sithWeapon_8BD024 != -1)
        return;
    iosGame_pendingWeapon = -1;
    if (Main_bMotsCompat)
    {
        // As its number key: asks the level's PLAYERACTION cog (it may say no,
        // once, as to a key press), and is remembered as that key's weapon
        int idx = sithInventory_SelectWeaponPrior(bin);
        if (idx <= 0)
            return;
        int inputFunc = INPUT_FUNC_ACTIVATE + ((idx % 10) ? (idx % 10) : 10);
        if (!sithThing_MotsTick(7, 0, (flex_t)inputFunc))
            return;
        sithWeapon_motsAConv[idx % 10] = idx;
    }
    sithWeapon_SelectWeapon(pPlayer, bin, 0);
}

int iosGame_SelectWeapon(int bin)
{
    int ammo = 0, bSelectable = 0;
    if (!iosGame_GetPlayer() || !iosGame_GetWeapon(bin, &ammo, &bSelectable) || !bSelectable)
        return 0;
    iosGame_pendingWeapon = bin;
    iosGame_pendingFrom = sithTime_g_secGameTime;
    iosGame_pendingUntil = sithTime_g_secGameTime + (flex32_t)IOSGAME_WEAPON_PICK_WAIT;
    iosGame_TryPendingWeapon();
    return 1;
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
    else
        iosGame_TryPendingWeapon(); // a weapon picked on the wheel, once the game would take it
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

// ------------------------------------------------------------ startup game

// The last game chosen at launch ("jk1" or "mots"), kept in Library/, which
// the Files app doesn't show
#define IOSGAME_STARTUP_CHOICE_FNAME "Library/openjkdf2_startup_game.txt"

void iosGame_ChooseStartupGame(void)
{
    const char* home = getenv("HOME");
    if (!home) return;

    char docs[256], jk1[256], mots[256], choiceFile[256];
    stdFnames_MakePath(docs, sizeof(docs), home, "Documents");
    stdFnames_MakePath(jk1, sizeof(jk1), docs, "jk1");
    stdFnames_MakePath(mots, sizeof(mots), docs, "mots");
    stdFnames_MakePath(choiceFile, sizeof(choiceFile), home, IOSGAME_STARTUP_CHOICE_FNAME);
    stdFileUtil_MkDir(jk1);
    stdFileUtil_MkDir(mots);

    // Each game is there once its folder has its jk_.cd; Mysteries of the
    // Sith's has to be its own (as the Expansions & Mods screen checks).
    // Looked up from inside Documents, by relative path: the file system is
    // case-sensitive, and when the exact case misses ("Resource", "JK_.CD")
    // the lookup that ignores case (fcaseopen) lists every folder on the way
    // -- from / for an absolute path, and the sandbox won't list the ones
    // above the app's own, so it would never find it. (The data folder is
    // chosen, and gone into, right after this.)
    char cwd[256];
    int bHaveCwd = getcwd(cwd, sizeof(cwd)) != NULL;
    int bHaveJk = 0, bHaveMots = 0;
    if (chdir(docs) == 0) {
        bHaveJk = util_FileExists("jk1/resource/jk_.cd");
        if (util_FileExists("mots/resource/jk_.cd")) {
            int keyval = jkRes_ReadKeyFromFile("mots/resource/jk_.cd");
            bHaveMots = JKRES_IS_MOTS_MAGIC(keyval);
        }
        if (bHaveCwd) chdir(cwd);
    }
    stdPlatform_Printf("iOS: Jedi Knight %s, Mysteries of the Sith %s\n",
                       bHaveJk ? "found" : "not found", bHaveMots ? "found" : "not found");

    int bLastMots = 0;
    FILE* f = fopen(choiceFile, "r");
    if (f) {
        char buf[16] = {0};
        if (fgets(buf, sizeof(buf), f)) bLastMots = !strncmp(buf, "mots", 4);
        fclose(f);
    }

    int bMots = 0;
    if (bHaveJk && bHaveMots) {
        // The last game played is the default button
        const SDL_MessageBoxButtonData buttons[] = {
            { bLastMots ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Jedi Knight" },
            { bLastMots ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0, 1, "Mysteries of the Sith" },
        };
        const SDL_MessageBoxData box = {
            SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
            NULL,
            "OpenJKDF2",
            "Which game do you want to play?",
            SDL_arraysize(buttons),
            buttons,
            NULL
        };
        int button = -1;
        if (SDL_ShowMessageBox(&box, &button) && (button == 0 || button == 1)) {
            bMots = button == 1;
        }
        else {
            // no box (or no answer): the last game again
            stdPlatform_Printf("iOS: game chooser didn't show: %s\n", SDL_GetError());
            bMots = bLastMots;
        }
    }
    else {
        bMots = bHaveMots && !bHaveJk;
    }

    Main_bMotsCompat = bMots;
    openjkdf2_bOrigWasDF2 = !bMots;
    stdPlatform_Printf("iOS: starting %s\n", bMots ? "Mysteries of the Sith" : "Jedi Knight");

    if (bHaveJk && bHaveMots) {
        f = fopen(choiceFile, "w");
        if (f) {
            fputs(bMots ? "mots\n" : "jk1\n", f);
            fclose(f);
        }
    }
}

#endif // TARGET_IOS

// Added: entire file. See jkCoop.h.

#include "Main/jkCoop.h"

#ifdef QOL_IMPROVEMENTS

#include "jk.h"
#include "stdPlatform.h"
#include "Dss/sithMulti.h"
#include "Gameplay/sithPlayer.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithTime.h"
#include "Engine/sithCollision.h"
#include "Engine/sithPhysics.h"
#include "World/sithThing.h"
#include "World/sithSector.h"
#include "World/sithWorld.h"
#include "Primitives/rdVector.h"
#include "Main/jkEpisode.h"
#include "Main/jkMain.h"
#include "General/stdString.h"
#include "Win95/stdComm.h"

int jkCoop_IsActive(void)
{
    return sithNet_isMulti && (sithNet_MultiModeFlags & MULTIMODEFLAG_COOP);
}

// ------------------------------------------------------------------ gear between levels

static char jkCoop_aHostLevel[32]; // (the host's level, below)

// (a kit older than this is from another game: a level, its cutscene and its
// screens take less)
#define JKCOOP_KIT_MAX_AGE_MS (15 * 60 * 1000)
// (in a new level, given once the player is in: a guest joins after loading)
#define JKCOOP_KIT_DELAY_MS (2000)

typedef struct jkCoopKitBin
{
    flex_t amount;
    int32_t state;
} jkCoopKitBin;

static jkCoopKitBin jkCoop_aKit[SITHBIN_NUMBINS];
static int jkCoop_bKitValid = 0;      // jkCoop_aKit holds a kit
static uint32_t jkCoop_kitMs = 0;     // when it was taken
static int jkCoop_bKitToGive = 0;     // this level: still to be given
static int jkCoop_bKitTaken = 0;      // this level: its starting kit taken
static uint32_t jkCoop_levelOpenMs = 0;
static int jkCoop_bWasDead = 0;

static SithThing* jkCoop_LocalPlayer(void)
{
    SithThing* pMe = sithPlayer_g_pLocalPlayerThing;
    if (!pMe || pMe->type != SITH_THING_PLAYER || !pMe->actorParams.pPlayer)
        return NULL;
    return pMe;
}

// The bins a player carries from one level to the next
static int jkCoop_BinCarried(int bin)
{
    const SithInventoryType* pType = &sithInventory_g_aTypes[bin];
    if (!(pType->flags & SITHINVENTORY_TYPE_REGISTERED))
        return 0;
    if (pType->flags & SITHINVENTORY_TYPE_NOT_CARRIED_BETWEEN_LEVELS)
        return 0;
    // (what each level counts for itself)
    if (bin == SITHBIN_SECRETS || bin == SITHBIN_MAXSECRETS || bin == SITHBIN_PEDS_KILLED || bin == SITHBIN_PEDS_TOTAL)
        return 0;
    return 1;
}

static void jkCoop_TakeKit(SithPlayer* pPlayer)
{
    SithInventoryItem* aItems = pPlayer->aItems;
    for (int i = 0; i < SITHBIN_NUMBINS; i++) {
        jkCoop_aKit[i].amount = 0.0;
        jkCoop_aKit[i].state = 0;
        if (!jkCoop_BinCarried(i) || (aItems[i].state & SITHINVENTORY_ITEM_CHEAT_GIVEN))
            continue;
        jkCoop_aKit[i].amount = aItems[i].amount;
        jkCoop_aKit[i].state = aItems[i].state & (SITHINVENTORY_ITEM_AVAILABLE | SITHINVENTORY_ITEM_FOUND);
    }
    jkCoop_bKitValid = 1;
    jkCoop_kitMs = stdPlatform_GetTimeMsec();
}

// The kit given: nothing less than the level gave (its own starting weapons and
// ammo); what can go below zero (the Jedi's alignment) as it was, but only
// between levels (bLevel), not undoing this level's after dying
static void jkCoop_GiveKit(SithThing* pMe, int bLevel)
{
    SithInventoryItem* aItems = pMe->actorParams.pPlayer->aItems;
    for (int i = 0; i < SITHBIN_NUMBINS; i++) {
        if (!jkCoop_BinCarried(i))
            continue;
        const jkCoopKitBin* pBin = &jkCoop_aKit[i];
        if (sithInventory_g_aTypes[i].min < 0.0) {
            if (bLevel)
                sithInventory_SetInventory(pMe, i, pBin->amount);
        }
        else if (pBin->amount > aItems[i].amount)
            sithInventory_SetInventory(pMe, i, pBin->amount);
        aItems[i].state |= pBin->state;
    }
}

void jkCoop_LevelEnded(void)
{
    // (the player's record, not their thing: also after the level has closed, the
    // Force powers screen's choices made)
    if (!jkCoop_IsActive() || !sithPlayer_g_pLocalPlayer)
        return;
    jkCoop_TakeKit(sithPlayer_g_pLocalPlayer);
}

void jkCoop_SessionEnded(void)
{
    jkCoop_bKitValid = 0;
    jkCoop_bKitToGive = 0;
    jkCoop_aHostLevel[0] = 0;
}

void jkCoop_LevelOpened(void)
{
    jkCoop_levelOpenMs = stdPlatform_GetTimeMsec();
    jkCoop_bKitTaken = 0;
    jkCoop_bWasDead = 0;
    if (!jkCoop_IsActive() || (jkCoop_bKitValid && jkCoop_levelOpenMs - jkCoop_kitMs > JKCOOP_KIT_MAX_AGE_MS))
        jkCoop_bKitValid = 0;
    jkCoop_bKitToGive = jkCoop_bKitValid;
}

void jkCoop_Tick(void)
{
    SithThing* pMe = jkCoop_LocalPlayer();
    if (!jkCoop_IsActive() || !pMe || stdPlatform_GetTimeMsec() - jkCoop_levelOpenMs < JKCOOP_KIT_DELAY_MS)
        return;

    const int bDead = (pMe->flags & SITH_TF_DEAD) || pMe->actorParams.health <= 0.0;
    if (bDead) {
        jkCoop_bWasDead = 1;
        return;
    }

    if (jkCoop_bKitToGive) {
        // (the last level's kit, on top of what this one starts with)
        jkCoop_GiveKit(pMe, 1);
        jkCoop_bKitToGive = 0;
    }
    if (!jkCoop_bKitTaken) {
        // (this level's starting kit, for coming back after dying)
        jkCoop_TakeKit(pMe->actorParams.pPlayer);
        jkCoop_bKitTaken = 1;
    }
    if (jkCoop_bWasDead) {
        jkCoop_GiveKit(pMe, 0);
        jkCoop_bWasDead = 0;
    }
}

// ------------------------------------------------------------------ starting spots

// (room between two players, beyond their sizes)
#define JKCOOP_SPAWN_GAP ((flex_t)0.01)
// (a spot with no floor this far below the player's middle is over a drop)
#define JKCOOP_SPAWN_FLOOR_DROP ((flex_t)0.2)

// Another player (alive, in the level) within reach of pPos for pPlayer
static int jkCoop_SpotTaken(SithThing* pPlayer, const rdVector3* pPos)
{
    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    for (int i = 0; i <= pWorld->numThings; i++) {
        SithThing* pOther = &pWorld->aThings[i];
        if (pOther == pPlayer || pOther->type != SITH_THING_PLAYER || !pOther->sector || (pOther->flags & (SITH_TF_DEAD | SITH_TF_DISABLED)))
            continue;
        const flex_t reach = pPlayer->collideSize + pOther->collideSize + JKCOOP_SPAWN_GAP;
        if (rdVector_Dist3(&pOther->position, pPos) < reach)
            return 1;
    }
    return 0;
}

void jkCoop_PlacePlayer(SithThing* pPlayer)
{
    if (!jkCoop_IsActive() || !pPlayer || !pPlayer->sector || !sithWorld_g_pCurrentWorld)
        return;
    if (!jkCoop_SpotTaken(pPlayer, &pPlayer->position))
        return;

    // beside, behind, ahead, then aslant, a step further each round
    static const flex_t aSide[] = { 1, -1, 0, 0, 1, -1, 1, -1 };
    static const flex_t aBack[] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    const flex_t step = (pPlayer->collideSize * 2.5) + JKCOOP_SPAWN_GAP;
    rdVector3 start = pPlayer->position;
    // (the way there tried a little above the floor, by a narrower ball, so the
    // floor itself isn't in the way)
    const flex_t wayRadius = pPlayer->collideSize * 0.5;
    rdVector3 startUp = start;
    startUp.z += wayRadius;
    for (int round = 1; round <= 3; round++) {
        for (int k = 0; k < (int)(sizeof(aSide) / sizeof(aSide[0])); k++) {
            rdVector3 spot = start;
            rdVector_ScaleAdd3Acc(&spot, &pPlayer->orient.rvec, aSide[k] * step * round);
            rdVector_ScaleAdd3Acc(&spot, &pPlayer->orient.lvec, aBack[k] * step * round);
            if (jkCoop_SpotTaken(pPlayer, &spot))
                continue;
            rdVector3 spotUp = spot;
            spotUp.z += wayRadius;
            if (!sithCollision_CheckPathClearOfWalls(pPlayer->sector, &startUp, &spotUp, wayRadius))
                continue;
            SithSector* pSector = sithCollision_FindSectorInRadius(pPlayer->sector, &start, &spot, 0.0);
            if (!pSector)
                continue;
            rdVector3 below = spot;
            below.z -= JKCOOP_SPAWN_FLOOR_DROP;
            if (sithCollision_CheckPathClearOfWalls(pSector, &spot, &below, 0.0))
                continue; // (no floor near)

            sithThing_ExitSector(pPlayer);
            pPlayer->position = spot;
            sithThing_EnterSector(pPlayer, pSector, 1, 0);
            sithPhysics_FindFloor(pPlayer, 1);
            return;
        }
    }
    // (nowhere free: where the game put them)
}

// ------------------------------------------------------------------ the host's level

int jkCoop_HostLevelFromJoining(SithMessage* pMsg)
{
    jkCoop_aHostLevel[0] = 0;
    // (code, 0, then the level's file name: 32 bytes)
    if (!pMsg || !jkCoop_IsActive() || stdComm_bIsServer || pMsg->netMsg.msg_size < 8 + 32)
        return 0;
    NETMSG_IN_START(pMsg);
    NETMSG_POPS32();
    NETMSG_POPS32();
    NETMSG_POPSTR(jkCoop_aHostLevel, 32);
    int bKnown = 0;
    for (int i = 0; i < jkEpisode_mLoad.numSeq && jkEpisode_mLoad.paEntries && !bKnown; i++)
        bKnown = !__strcmpi(jkEpisode_mLoad.paEntries[i].fileName, jkCoop_aHostLevel);
    // (not a level of this episode: the game's own "wrong level"; nor the one just
    // loaded, a late answer to an earlier try)
    if (!jkCoop_aHostLevel[0] || !bKnown
        || (sithWorld_g_pCurrentWorld && !__strcmpi(sithWorld_g_pCurrentWorld->map_jkl_fname, jkCoop_aHostLevel))) {
        jkCoop_aHostLevel[0] = 0;
        return 0;
    }
    stdPlatform_Printf("OpenJKDF2: co-op, the host is on %s\n", jkCoop_aHostLevel);
    return 1;
}

int jkCoop_FollowHostLevel(void)
{
    if (!jkCoop_aHostLevel[0] || !jkCoop_IsActive())
        return 0;
    // (only a level of this episode)
    if (!jkEpisode_idk4(&jkEpisode_mLoad, jkCoop_aHostLevel)) {
        jkCoop_aHostLevel[0] = 0;
        return 0;
    }
    stdString_SafeStrCopy(jkMain_aLevelJklFname, jkCoop_aHostLevel, 128);
    jkPlayer_bLoadingSomething = 1; // (the next level worked out from this one)
    jkSmack_gameMode = 2;
    jkCoop_aHostLevel[0] = 0;
    return 1;
}

// ------------------------------------------------------------------ menus that go on by themselves

static jkGuiMenu* jkCoop_pAutoMenu = NULL;
static void (*jkCoop_pfAutoMenuOwn)(jkGuiMenu*) = NULL;
static uint32_t jkCoop_autoMenuStartMs = 0;
static uint32_t jkCoop_autoMenuWaitMs = 0;

static void jkCoop_AutoMenuIdle(jkGuiMenu* pMenu)
{
    if (jkCoop_pfAutoMenuOwn)
        jkCoop_pfAutoMenuOwn(pMenu);
    if (pMenu == jkCoop_pAutoMenu && stdPlatform_GetTimeMsec() - jkCoop_autoMenuStartMs > jkCoop_autoMenuWaitMs)
        pMenu->lastClicked = 1; // (OK)
}

void jkCoop_MenuAutoContinue(jkGuiMenu* pMenu, uint32_t msWait)
{
    if (!pMenu || !jkCoop_IsActive() || jkCoop_pAutoMenu)
        return;
    jkCoop_pAutoMenu = pMenu;
    jkCoop_pfAutoMenuOwn = pMenu->idkFunc;
    jkCoop_autoMenuStartMs = stdPlatform_GetTimeMsec();
    jkCoop_autoMenuWaitMs = msWait;
    pMenu->idkFunc = jkCoop_AutoMenuIdle;
}

void jkCoop_MenuDone(jkGuiMenu* pMenu)
{
    if (!pMenu || pMenu != jkCoop_pAutoMenu)
        return;
    pMenu->idkFunc = jkCoop_pfAutoMenuOwn;
    jkCoop_pAutoMenu = NULL;
    jkCoop_pfAutoMenuOwn = NULL;
}

#endif // QOL_IMPROVEMENTS

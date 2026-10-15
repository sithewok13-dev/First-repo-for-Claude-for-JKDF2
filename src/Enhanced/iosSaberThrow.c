// Added: entire file (iosSaberThrow.h)

#ifdef JK_ENHANCED

#include "Enhanced/iosSaberThrow.h"
#include "Enhanced/iosSaber.h"

#include "Cog/sithCog.h"
#include "Cog/sithCogParse.h"
#include "Devices/sithSound.h"
#include "Dss/sithMulti.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithPlayer.h"
#include "Gameplay/sithTime.h"
#include "General/stdMath.h"
#include "Main/Main.h"
#include "World/sithTemplate.h"
#include "World/sithThing.h"
#include "World/sithWeapon.h"
#include "World/sithWorld.h"
#include "jk.h"

#include <string.h>

// Mysteries of the Sith's bins, as force_saberthrow.cog has them
#define IOSSABERTHROW_BIN_POWER   37  // f_saberthrow (its stars: the cost)
#define IOSSABERTHROW_BIN_MANA    14  // forcemana
#define IOSSABERTHROW_BIN_DARK    64  // s_dark: no cost
#define IOSSABERTHROW_BIN_MAXMANA 69  // maxmana: a full meter throws, whatever the cost
#define IOSSABERTHROW_BIN_DRAWING 89  // drawingsaber: the saber being drawn
#define IOSSABERTHROW_BIN_SABER   SITHBIN_MOTS_LIGHTSABER
#define IOSSABERTHROW_BIN_FISTS   SITHBIN_MOTS_FISTS

// The game's throw gives the saber back after 5 s however it went (its timer):
// so does this, a moment later (the returning saber caught before, as a rule)
#define IOSSABERTHROW_BACK_SECS 5.5
// A returning saber is made where the thrown one struck: one this near the
// place a saber of ours was last seen is that saber's
#define IOSSABERTHROW_FIND_DIST 1.5f
// The left saber's path, turned this far from the right's (degrees)
#define IOSSABERTHROW_TURN_DEG 6.0f

// Where each saber is
enum {
    IOSSABERTHROW_IN_HAND = 0,
    IOSSABERTHROW_FLYING,     // the thrown saber (thing) on its way
    IOSSABERTHROW_STRUCK,     // it struck: its returning saber not found yet
    IOSSABERTHROW_RETURNING,  // the returning saber (thing) on its way back
};

typedef struct {
    int state;
    int thingIdx;          // FLYING: the thrown saber; RETURNING: the returning one
    uint32_t signature;    // (that thing's, so another in its place isn't taken for it)
    rdVector3 lastPos;     // where it was last seen
    double secThrown;
    int colour;            // the SABER wheel's id it was thrown in
} iosSaberThrowHand;

static iosSaberThrowHand iosSaberThrow_aHand[2]; // 0: the right (first blade), 1: the left
static double iosSaberThrow_secLast = -100.0;    // the last throw
static int iosSaberThrow_pendingHand = -1;       // a throw due (tapped again within the stagger)
static double iosSaberThrow_secPending = 0.0;
static SithThing* iosSaberThrow_pPlayer = NULL;  // the player it is all for (another: everything back)
static int iosSaberThrow_bGiving = 0;            // (giving the saber back itself: not a catch)
// The colour each saber thrown last flies in, kept through a reset (the cheat's saber taken back in
// mid-flight: it flies on in its colour till it strikes)
static struct { int thingIdx; uint32_t signature; int colour; } iosSaberThrow_aFlyColour[2] = { { -1, 0, 0 }, { -1, 0, 0 } };

static SithThing* iosSaberThrow_Player(void)
{
    SithThing* p = sithPlayer_g_pLocalPlayerThing;
    if (!Main_bMotsCompat || sithNet_isMulti || !sithWorld_g_pCurrentWorld || !p || p->type != SITH_THING_PLAYER
        || !p->actorParams.pPlayer || p->actorParams.pPlayer == (SithPlayer*)-136)
        return NULL;
    return p;
}

static int iosSaberThrow_IsDual(void)
{
    return iosSaber_GetStyle() == IOSSABER_STYLE_DUAL && iosSaber_StyleAvailable(IOSSABER_STYLE_DUAL);
}

void iosSaberThrow_Reset(void)
{
    memset(iosSaberThrow_aHand, 0, sizeof(iosSaberThrow_aHand));
    iosSaberThrow_aHand[0].thingIdx = iosSaberThrow_aHand[1].thingIdx = -1;
    iosSaberThrow_pendingHand = -1;
    iosSaberThrow_secLast = -100.0;
    iosSaberThrow_pPlayer = NULL;
}

int iosSaberThrow_HandsInHand(void)
{
    if (!iosSaberThrow_Player()) return 3;
    return (iosSaberThrow_aHand[0].state == IOSSABERTHROW_IN_HAND ? 1 : 0)
         | (iosSaberThrow_aHand[1].state == IOSSABERTHROW_IN_HAND ? 2 : 0);
}

int iosSaberThrow_AnyOut(void)
{
    return iosSaberThrow_HandsInHand() != 3;
}

// A thing of ours still there (the same one, not another made in its place)
static SithThing* iosSaberThrow_Thing(const iosSaberThrowHand* h)
{
    SithWorld* w = sithWorld_g_pCurrentWorld;
    if (!w || h->thingIdx < 0 || h->thingIdx > w->numThings) return NULL;
    SithThing* t = &w->aThings[h->thingIdx];
    if (!t->type || t->signature != h->signature || (t->flags & (SITH_TF_DEAD | SITH_TF_DESTROYED))) return NULL;
    return t;
}

// force_saberthrow.cog's "inbubble" (it can't be thrown in one)
static int iosSaberThrow_InBubble(void)
{
    sithCog* pCog = sithInventory_GetType(IOSSABERTHROW_BIN_POWER) ? sithInventory_GetType(IOSSABERTHROW_BIN_POWER)->cog : NULL;
    if (!pCog || !pCog->pSymbolTable) return 0;
    SithCogSymbol* s = sithCogParse_GetSymbol(pCog->pSymbolTable, "inbubble");
    return s && s->val.data[0] != 0;
}

// The Force a throw costs (by the power's stars, as the cog has it); 0: not learned
static flex_t iosSaberThrow_Cost(SithThing* p)
{
    switch ((int)sithInventory_GetInventory(p, IOSSABERTHROW_BIN_POWER)) {
        case 1: return 100.0;
        case 2: return 80.0;
        case 3: return 60.0;
        case 4: return 40.0;
    }
    return 0.0;
}

// The game's rules for a throw, now (force_saberthrow.cog's "activated")
static int iosSaberThrow_CanThrow(SithThing* p)
{
    SithPlayer* pl = p->actorParams.pPlayer;
    if ((p->flags & SITH_TF_DEAD) || p->actorParams.health <= 0.0) return 0;
    if (iosSaberThrow_InBubble()) return 0;
    // the saber in hand (and not being drawn, nor switched from)
    if (pl->curWeaponID != IOSSABERTHROW_BIN_SABER || sithWeapon_8BD024 != -1) return 0;
    if (sithInventory_GetInventory(p, IOSSABERTHROW_BIN_DRAWING) == 1.0) return 0;
    if (sithInventory_GetInventory(p, IOSSABERTHROW_BIN_SABER) <= 0.0) return 0;
    // the game's own throw not under way, nor its wait after one
    if (sithInventory_IsInventoryActivated(p, IOSSABERTHROW_BIN_POWER)) return 0;
    if (sithTime_g_secGameTime < pl->aItems[IOSSABERTHROW_BIN_POWER].binWait) return 0;
    const flex_t cost = iosSaberThrow_Cost(p);
    if (cost <= 0.0) return 0;
    const flex_t mana = sithInventory_GetInventory(p, IOSSABERTHROW_BIN_MANA);
    if (mana < cost && mana < sithInventory_GetInventory(p, IOSSABERTHROW_BIN_MAXMANA)) return 0;
    return 1;
}

// The thrown saber the game would throw for this blade colour (by the game's
// own blade, as the cog picks it: yellow if it is none of its seven)
static SithThing* iosSaberThrow_Template(SithThing* p)
{
    static const char* aMat[7] = { "saberblue1", "sabergreen1", "saberyellow1", "saberorange1", "saberred1", "saberdred1", "saberpurple1" };
    static const char* aTpl[7] = { "+fsab_blue", "+fsab_green", "+fsab_yellow", "+fsab_orange", "+fsab_red", "+fsab_dred", "+fsab_purple" };
    int k = 2;
    const rdMaterial* m = p->playerInfo ? p->playerInfo->polyline.face.material : NULL;
    if (m) {
        char name[64];
        int n = 0;
        for (const char* q = m->mat_fpath; *q && n < (int)sizeof(name) - 1; q++)
            name[n++] = (*q >= 'A' && *q <= 'Z') ? (char)(*q + 'a' - 'A') : *q;
        name[n] = 0;
        for (int i = 0; i < 7; i++) if (strstr(name, aMat[i])) { k = i; break; }
    }
    return sithTemplate_GetTemplate(aTpl[k]);
}

// The game's own throw's disturbance: every actor this near hears it
static void iosSaberThrow_Disturb(SithThing* p, flex_t radius)
{
    SithWorld* w = sithWorld_g_pCurrentWorld;
    for (int i = 0; i <= w->numThings; i++) {
        SithThing* t = &w->aThings[i];
        if (t->type != SITH_THING_ACTOR || (t->flags & (SITH_TF_DISABLED | SITH_TF_DEAD | SITH_TF_DESTROYED))) continue;
        if (rdVector_Dist3(&t->position, &p->position) <= radius)
            sithCog_ThingSendMessageEx(t, NULL, SITH_MESSAGE_SPLASH, IOSSABERTHROW_BIN_POWER, 0, 0, 0);
    }
}

// Throws this hand's saber, by the game's rules (checked first)
static int iosSaberThrow_Throw(SithThing* p, int hand)
{
    if (!iosSaberThrow_CanThrow(p)) return 0;
    SithThing* pTpl = iosSaberThrow_Template(p);
    if (!pTpl) return 0;
    const flex_t cost = iosSaberThrow_Cost(p);
    if (sithInventory_GetInventory(p, IOSSABERTHROW_BIN_DARK) != 1.0)
        sithInventory_ChangeInventory(p, IOSSABERTHROW_BIN_MANA, -cost);
    iosSaberThrow_Disturb(p, cost);

    static sithSound* pSnd = NULL;
    static SithWorld* pSndWorld = NULL;
    if (pSndWorld != sithWorld_g_pCurrentWorld) { pSnd = sithSound_Load("SaberThrow.wav", 0); pSndWorld = sithWorld_g_pCurrentWorld; }

    // the left saber from a little to the left, its path turned a little
    rdVector3 offset = { hand ? -0.03f : 0.0f, 0.0f, 0.0f }, zero = { 0.0f, 0.0f, 0.0f };
    SithThing* pSaber = sithWeapon_FireProjectile(p, pTpl, pSnd, 24, &offset, &zero, 1.0, 0, 10.0, 10.0, 0);
    if (!pSaber) return 0;
    if (hand) {
        flex_t s, c;
        stdMath_SinCos(IOSSABERTHROW_TURN_DEG, &s, &c);
        rdVector3* v = &pSaber->physicsParams.vel;
        const flex_t x = v->x * c - v->y * s, y = v->x * s + v->y * c;
        v->x = x; v->y = y;
    }

    iosSaberThrowHand* h = &iosSaberThrow_aHand[hand];
    h->state = IOSSABERTHROW_FLYING;
    h->thingIdx = pSaber->idx;
    h->signature = pSaber->signature;
    h->lastPos = pSaber->position;
    h->secThrown = sithTime_g_secGameTime;
    h->colour = iosSaber_GetColour(IOSSABER_STYLE_DUAL, hand);
    iosSaberThrow_aFlyColour[hand].thingIdx = pSaber->idx;
    iosSaberThrow_aFlyColour[hand].signature = pSaber->signature;
    iosSaberThrow_aFlyColour[hand].colour = h->colour;
    iosSaberThrow_secLast = sithTime_g_secGameTime;

    // both thrown: as the game's own throw leaves her, the saber out of the
    // inventory and her fists in hand -- without the saber's put-away (the
    // cog's way: no weapon, then the fists, and the saber cog told to tidy up)
    if (iosSaberThrow_HandsInHand() == 0) {
        sithInventory_SetInventory(p, IOSSABERTHROW_BIN_SABER, 0.0);
        sithWeapon_SetFireWait(p, -1.0);
        sithWeapon_SetMountWait(p, 0.0);
        sithInventory_SetCurrentWeapon(p, 0);
        sithWeapon_SelectWeapon(p, IOSSABERTHROW_BIN_FISTS, 0);
        sithCog* pSaberCog = sithInventory_GetType(IOSSABERTHROW_BIN_SABER) ? sithInventory_GetType(IOSSABERTHROW_BIN_SABER)->cog : NULL;
        if (pSaberCog) sithCog_SendMessage(pSaberCog, SITH_MESSAGE_USER0, SENDERTYPE_SYSTEM, 0, SENDERTYPE_THING, p->idx, 0);
    }
    return 1;
}

int iosSaberThrow_Activate(SithThing* pPlayer)
{
    SithThing* p = iosSaberThrow_Player();
    if (!p || pPlayer != p || !iosSaberThrow_IsDual()) return 0;
    iosSaberThrow_pPlayer = p;
    // the next saber in hand: the right, then the left; none, nothing (and
    // never more than one due)
    const int mask = iosSaberThrow_HandsInHand();
    const int hand = (mask & 1) ? 0 : ((mask & 2) ? 1 : -1);
    if (hand < 0 || iosSaberThrow_pendingHand >= 0) return 1;
    if (sithTime_g_secGameTime - iosSaberThrow_secLast < IOSSABERTHROW_STAGGER) {
        iosSaberThrow_pendingHand = hand;
        iosSaberThrow_secPending = iosSaberThrow_secLast + IOSSABERTHROW_STAGGER;
        return 1;
    }
    iosSaberThrow_Throw(p, hand);
    return 1;
}

// A saber back in its hand; the lightsaber given back too if it isn't had
// (the game's timer's way), and in hand again from the fists
static void iosSaberThrow_Back(SithThing* p, int hand, int bGive)
{
    iosSaberThrowHand* h = &iosSaberThrow_aHand[hand];
    h->state = IOSSABERTHROW_IN_HAND;
    h->thingIdx = -1;
    if (bGive && sithInventory_GetInventory(p, IOSSABERTHROW_BIN_SABER) <= 0.0) {
        iosSaberThrow_bGiving = 1;
        sithInventory_SetInventory(p, IOSSABERTHROW_BIN_SABER, 1.0);
        iosSaberThrow_bGiving = 0;
        if (sithInventory_GetInventory(p, IOSSABERTHROW_BIN_SABER) > 0.0 && p->actorParams.pPlayer->curWeaponID == IOSSABERTHROW_BIN_FISTS)
            sithWeapon_SelectWeapon(p, IOSSABERTHROW_BIN_SABER, 0);
    }
}

void iosSaberThrow_SaberGiven(void)
{
    SithThing* p = iosSaberThrow_Player();
    if (!p || iosSaberThrow_bGiving || !iosSaberThrow_AnyOut()) return;
    // the one caught: the returning saber nearest the player (still there as
    // the game gives it), else whichever has been out longest
    int best = -1;
    flex_t bestDist = 1e9;
    for (int k = 0; k < 2; k++) {
        iosSaberThrowHand* h = &iosSaberThrow_aHand[k];
        if (h->state != IOSSABERTHROW_RETURNING) continue;
        SithThing* t = iosSaberThrow_Thing(h);
        const flex_t d = rdVector_Dist3(t ? &t->position : &h->lastPos, &p->position);
        if (d < bestDist) { bestDist = d; best = k; }
    }
    for (int k = 0; best < 0 && k < 2; k++) {
        if (iosSaberThrow_aHand[k].state == IOSSABERTHROW_IN_HAND) continue;
        if (best < 0 || iosSaberThrow_aHand[k].secThrown < iosSaberThrow_aHand[best].secThrown) best = k;
    }
    if (best >= 0) iosSaberThrow_Back(p, best, 0);
}

void iosSaberThrow_Tick(void)
{
    SithThing* p = iosSaberThrow_Player();
    if (!p || (iosSaberThrow_pPlayer && p != iosSaberThrow_pPlayer)) {
        // no level, another game, multiplayer: nothing out
        if (iosSaberThrow_AnyOut() || iosSaberThrow_pendingHand >= 0 || iosSaberThrow_pPlayer) iosSaberThrow_Reset();
        return;
    }
    const double now = sithTime_g_secGameTime;
    if (now < iosSaberThrow_secLast) { iosSaberThrow_Reset(); return; } // (the clock went back: a load)

    // the second saber due: thrown if it still can be (else not at all)
    if (iosSaberThrow_pendingHand >= 0 && now >= iosSaberThrow_secPending) {
        const int hand = iosSaberThrow_pendingHand;
        iosSaberThrow_pendingHand = -1;
        if (iosSaberThrow_IsDual() && iosSaberThrow_aHand[hand].state == IOSSABERTHROW_IN_HAND) iosSaberThrow_Throw(p, hand);
    }
    if ((p->flags & SITH_TF_DEAD) || p->actorParams.health <= 0.0) iosSaberThrow_pendingHand = -1;

    SithWorld* w = sithWorld_g_pCurrentWorld;
    SithThing* pReturnTpl = sithTemplate_GetTemplate("+fsaberreturns");
    for (int k = 0; k < 2; k++) {
        iosSaberThrowHand* h = &iosSaberThrow_aHand[k];
        if (h->state == IOSSABERTHROW_IN_HAND) continue;
        SithThing* t = iosSaberThrow_Thing(h);
        if (h->state == IOSSABERTHROW_FLYING) {
            if (t) h->lastPos = t->position;
            else h->state = IOSSABERTHROW_STRUCK;
        }
        if (h->state == IOSSABERTHROW_STRUCK && pReturnTpl) {
            // its returning saber: the nearest one where it struck, not the other's
            int best = -1;
            flex_t bestDist = IOSSABERTHROW_FIND_DIST;
            for (int i = 0; i <= w->numThings; i++) {
                SithThing* r = &w->aThings[i];
                if (!r->type || r->pTemplate != pReturnTpl || (r->flags & (SITH_TF_DEAD | SITH_TF_DESTROYED))) continue;
                const iosSaberThrowHand* o = &iosSaberThrow_aHand[1 - k];
                if (o->state == IOSSABERTHROW_RETURNING && o->thingIdx == i && o->signature == r->signature) continue;
                const flex_t d = rdVector_Dist3(&r->position, &h->lastPos);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            if (best >= 0) {
                h->state = IOSSABERTHROW_RETURNING;
                h->thingIdx = best;
                h->signature = w->aThings[best].signature;
                h->lastPos = w->aThings[best].position;
            }
        }
        else if (h->state == IOSSABERTHROW_RETURNING && t) {
            h->lastPos = t->position;
        }
        // back after a while however it went (lost, or not caught), as the
        // game's timer gives it back
        if (h->state != IOSSABERTHROW_IN_HAND && now - h->secThrown >= IOSSABERTHROW_BACK_SECS) {
            if (h->state == IOSSABERTHROW_RETURNING && t) sithThing_DestroyThing(t); // (no second saber to catch later)
            iosSaberThrow_Back(p, k, 1);
        }
    }
}

int iosSaberThrow_ThrownColour(SithThing* pThing)
{
    if (!pThing || pThing->type != SITH_THING_WEAPON) return -1;
    SithThing* p = iosSaberThrow_Player();
    if (!p) return -1;
    for (int k = 0; k < 2; k++) {
        if (iosSaberThrow_aFlyColour[k].thingIdx == pThing->idx && iosSaberThrow_aFlyColour[k].signature == pThing->signature)
            return iosSaberThrow_aFlyColour[k].colour;
    }
    // the game's own throw (one saber): its blade's colour
    if (pThing->pParent != p || !pThing->pTemplate) return -1;
    static const char* aTpl[7] = { "+fsab_blue", "+fsab_green", "+fsab_yellow", "+fsab_orange", "+fsab_red", "+fsab_dred", "+fsab_purple" };
    for (int i = 0; i < 7; i++) {
        if (pThing->pTemplate == sithTemplate_GetTemplate(aTpl[i]))
            return iosSaber_GetColour(IOSSABER_STYLE_SINGLE, 0);
    }
    return -1;
}

#endif // JK_ENHANCED

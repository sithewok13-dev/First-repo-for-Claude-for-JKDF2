// Added: entire file. See iosStance.h.

#ifdef TARGET_IOS

#include "Platform/iOS/iosStance.h"

#include <math.h>
#include <string.h>

#include "Gameplay/sithTime.h"
#include "Primitives/rdMatrix.h"
#include "jk.h"

// A hand's pose, on top of the arm's own animation: its blade tipped out
// (roll, degrees; + away from the middle of the view), its tip toward the
// middle of the view (pitch, degrees; + forward), and the hand lowered and
// brought forward (model units)
typedef struct {
    float roll, pitch, lower, fwd;
} iosStancePose;

typedef struct {
    iosStancePose aHand[2]; // right, left
} iosStanceDef;

// Five layers, from the slowest:
//  - the posture: the relaxed V most of the time, now and then another,
//    moved between on springs, the left hand a moment after the right
//  - the body, shared by both hands (breathing, a slow weight shift): the
//    larger part of the movement, so the two sabers move with one person
//  - each hand's own float: slow uneven waves, never in step with the other's
//  - small adjustments, one hand at a time (a dip, a rise, a wrist rolled in
//    or out, easing forward, a re-centre), at uneven times with long quiet
//    stretches now and then; the other hand answers faintly
//  - rarely, an accent: a loose X, a high-low guard, an inward parry, a
//    block-like move, brief, then back to the posture
// All of it fades out at once for a swing, and back after.

enum {
    IOSSTANCE_V = 0,     // both out in a loose V, the left a little lower
    IOSSTANCE_HIGHLOW,   // one nearly upright and higher, the other out and low (the hands swap at times)
    IOSSTANCE_DUELIST,   // the right tipped toward the middle, the left low and out
    IOSSTANCE_WIDE,      // both further out, the middle of the view open
    IOSSTANCE_LOW,       // both hands low, the blades rising from the bottom corners
    IOSSTANCE_READY,     // both a little forward, the right leading (after a fight)
    // the accents (brief, then back to the posture)
    IOSSTANCE_CROSS,     // the blades brought into a loose X low down
    IOSSTANCE_GUARD,     // a quick high-low guard
    IOSSTANCE_PARRY,     // the right turned in across the middle, as in a small parry
    IOSSTANCE_BLOCK,     // both brought up and forward, as if to block
    IOSSTANCE_NUM
};
#define IOSSTANCE_FIRST_ACCENT IOSSTANCE_CROSS

#define IOSSTANCE_U 0.001f // (a model unit's thousandth: a hand moves a few of these)
static const iosStanceDef iosStance_aDef[IOSSTANCE_NUM] = {
    [IOSSTANCE_V]       = { { {  9.0f,  0.0f,  0.0f * IOSSTANCE_U, 0.0f }, {  9.0f,  0.0f,  4.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_HIGHLOW] = { { { -3.0f,  2.0f, -4.0f * IOSSTANCE_U, 0.0f }, { 22.0f,  0.0f,  9.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_DUELIST] = { { { -12.0f, 12.0f, -2.0f * IOSSTANCE_U, 4.0f * IOSSTANCE_U }, { 20.0f, -4.0f, 9.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_WIDE]    = { { { 19.0f,  0.0f,  2.0f * IOSSTANCE_U, 0.0f }, { 19.0f,  0.0f,  3.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_LOW]     = { { { 18.0f, -6.0f, 26.0f * IOSSTANCE_U, 0.0f }, { 18.0f, -6.0f, 28.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_READY]   = { { { -5.0f, 14.0f, -2.0f * IOSSTANCE_U, 6.0f * IOSSTANCE_U }, { -2.0f, 10.0f, 0.0f, 4.0f * IOSSTANCE_U } } },
    [IOSSTANCE_CROSS]   = { { { -26.0f, 4.0f, 10.0f * IOSSTANCE_U, 2.0f * IOSSTANCE_U }, { -26.0f, 4.0f, 10.0f * IOSSTANCE_U, 2.0f * IOSSTANCE_U } } },
    [IOSSTANCE_GUARD]   = { { { -6.0f,  5.0f, -8.0f * IOSSTANCE_U, 2.0f * IOSSTANCE_U }, { 20.0f, -2.0f, 11.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_PARRY]   = { { { -17.0f, 9.0f, -2.0f * IOSSTANCE_U, 4.0f * IOSSTANCE_U }, {  8.0f,  2.0f,  5.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_BLOCK]   = { { { -5.0f, 11.0f, -6.0f * IOSSTANCE_U, 5.0f * IOSSTANCE_U }, { -3.0f,  9.0f, -4.0f * IOSSTANCE_U, 4.0f * IOSSTANCE_U } } },
};
// How often each posture is picked after the V, and each accent, out of their sums
static const int iosStance_aWeight[IOSSTANCE_NUM] = {
    [IOSSTANCE_HIGHLOW] = 30, [IOSSTANCE_DUELIST] = 24, [IOSSTANCE_WIDE] = 20, [IOSSTANCE_LOW] = 16, [IOSSTANCE_READY] = 6,
    [IOSSTANCE_CROSS] = 30, [IOSSTANCE_GUARD] = 30, [IOSSTANCE_PARRY] = 22, [IOSSTANCE_BLOCK] = 18,
};
// The resting hands a little further apart than the postures above have them (degrees out)
#define IOSSTANCE_SPREAD 4.5f

// Posture timing (seconds): the V held this long (now and then much longer), another this long;
// after a fight, ready this long
#define IOSSTANCE_HOME_MIN 12.0f
#define IOSSTANCE_HOME_MAX 30.0f
#define IOSSTANCE_HOME_LONG_CHANCE 0.15f
#define IOSSTANCE_HOME_LONG_MAX 50.0f
#define IOSSTANCE_AWAY_MIN 5.0f
#define IOSSTANCE_AWAY_MAX 11.0f
#define IOSSTANCE_READY_MIN 2.5f
#define IOSSTANCE_READY_MAX 4.5f
// Accents: one every so often, held this long; none this soon after a fight
#define IOSSTANCE_ACCENT_GAP_MIN 25.0f
#define IOSSTANCE_ACCENT_GAP_MAX 45.0f
#define IOSSTANCE_ACCENT_MIN 0.8f
#define IOSSTANCE_ACCENT_MAX 1.2f
#define IOSSTANCE_ACCENT_AFTER_FIGHT 8.0f
// Small adjustments: the gaps between them mostly 3-8 s (most often near the middle); one in six
// a quiet stretch of 8-12 s; one in eight a second straight after (1-2 s: a re-grip); each lasts
// 0.7-1.2 s, eased in and out; the other hand answers a tenth as much a moment later
#define IOSSTANCE_MICRO_GAP_MIN 3.0f
#define IOSSTANCE_MICRO_GAP_MAX 8.0f
#define IOSSTANCE_MICRO_QUIET_CHANCE (1.0f / 6.0f)
#define IOSSTANCE_MICRO_QUIET_MIN 8.0f
#define IOSSTANCE_MICRO_QUIET_MAX 12.0f
#define IOSSTANCE_MICRO_PAIR_CHANCE (1.0f / 8.0f)
#define IOSSTANCE_MICRO_PAIR_MIN 1.0f
#define IOSSTANCE_MICRO_PAIR_MAX 2.0f
#define IOSSTANCE_MICRO_DUR_MIN 0.7f
#define IOSSTANCE_MICRO_DUR_MAX 1.2f
#define IOSSTANCE_MICRO_OTHER_HAND 0.65f // (the next one in the other hand, mostly)
#define IOSSTANCE_ECHO 0.1f
#define IOSSTANCE_ECHO_DELAY 0.15f
// The springs (per second): the right hand leads, the left follows a moment later; quicker for an accent
#define IOSSTANCE_SPRING_R 5.0f
#define IOSSTANCE_SPRING_L 4.2f
#define IOSSTANCE_SPRING_ACCENT 8.0f
#define IOSSTANCE_LEFT_LAG 0.16f
// The pose fades out this fast when a swing begins, and back in this fast after
#define IOSSTANCE_FADE_OUT 14.0f
#define IOSSTANCE_FADE_IN 2.2f
// The body (shared): breathing (rise and fall, a hint of lean) and a slow weight shift;
// each hand's own float, smaller, on top
#define IOSSTANCE_BREATH_HZ 0.23f
#define IOSSTANCE_BREATH_LOWER (2.4f * IOSSTANCE_U)
#define IOSSTANCE_BREATH_PITCH 0.7f
#define IOSSTANCE_SHIFT_HZ 0.07f
#define IOSSTANCE_SHIFT_ROLL 1.6f
#define IOSSTANCE_FLOAT 1.6f // (each hand's own waves, against what they were)

// The small adjustments: what moves, at its fullest
enum { IOSSTANCE_M_DIP = 0, IOSSTANCE_M_RISE, IOSSTANCE_M_ROLL_OUT, IOSSTANCE_M_ROLL_IN, IOSSTANCE_M_FORWARD, IOSSTANCE_M_CENTRE, IOSSTANCE_M_NUM };
static const iosStancePose iosStance_aMicro[IOSSTANCE_M_NUM] = {
    [IOSSTANCE_M_DIP]      = { -1.0f, -2.5f,  5.0f * IOSSTANCE_U, 0.0f },
    [IOSSTANCE_M_RISE]     = {  0.0f,  2.0f, -4.5f * IOSSTANCE_U, 0.0f },
    [IOSSTANCE_M_ROLL_OUT] = {  6.0f,  0.0f,  0.0f, 0.0f },
    [IOSSTANCE_M_ROLL_IN]  = { -5.5f,  1.0f,  0.0f, 0.0f },
    [IOSSTANCE_M_FORWARD]  = {  0.0f,  3.0f, -1.0f * IOSSTANCE_U, 5.0f * IOSSTANCE_U },
    [IOSSTANCE_M_CENTRE]   = { -2.5f,  1.0f, -1.5f * IOSSTANCE_U, 1.0f * IOSSTANCE_U },
};
static const int iosStance_aMicroWeight[IOSSTANCE_M_NUM] = { 22, 18, 18, 16, 14, 12 };

static struct {
    int bStarted;
    flex_t lastTime;
    float t;             // time at rest (seconds, the game's clock)
    uint32_t rng;
    int stance, lastAway;
    int bSwapped;        // HIGHLOW: the left high this time
    float stanceEnd;     // when the current posture (or accent) ends
    float leftSwitchAt;  // when the left hand takes up the new target
    iosStancePose aTarget[2], aPose[2], aVel[2];
    int leftStance;      // the stance the left hand is on its way to
    float weight;        // 0: the game's own pose (a swing), 1: the stance in full
    int bWasBusy;
    // the posture an accent interrupted, and how long it had left
    int baseStance;
    float baseLeft;
    float nextAccent, lastFight;
    int lastAccent;
    // the small adjustment under way (none: microDur 0) and the next
    float microStart, microDur, nextMicro;
    int microHand, microKind, lastMicro;
    float run;           // how fast the player is moving (0-1), last update
} iosStance_s;

static float iosStance_Rand01(void)
{
    uint32_t x = iosStance_s.rng ? iosStance_s.rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    iosStance_s.rng = x;
    return (x >> 8) * (1.0f / 16777216.0f);
}

static float iosStance_RandRange(float a, float b) { return a + (b - a) * iosStance_Rand01(); }

static iosStancePose iosStance_PoseOf(int stance, int hand)
{
    iosStancePose p = iosStance_aDef[stance].aHand[hand];
    if ((stance == IOSSTANCE_HIGHLOW || stance == IOSSTANCE_GUARD) && iosStance_s.bSwapped)
        p = iosStance_aDef[stance].aHand[!hand];
    return p;
}

static int iosStance_IsAccent(int stance) { return stance >= IOSSTANCE_FIRST_ACCENT; }

// Starts a stance: a posture for as long as it says (or dwell, if given), an accent briefly
static void iosStance_BeginFor(int stance, float now, float dwell)
{
    iosStance_s.stance = stance;
    if (stance == IOSSTANCE_HIGHLOW || stance == IOSSTANCE_GUARD) iosStance_s.bSwapped = iosStance_Rand01() < 0.4f;
    if (dwell > 0.0f) iosStance_s.stanceEnd = now + dwell;
    else if (stance == IOSSTANCE_V) {
        float d = iosStance_RandRange(IOSSTANCE_HOME_MIN, IOSSTANCE_HOME_MAX);
        if (iosStance_Rand01() < IOSSTANCE_HOME_LONG_CHANCE) d = iosStance_RandRange(IOSSTANCE_HOME_MAX, IOSSTANCE_HOME_LONG_MAX);
        iosStance_s.stanceEnd = now + d;
    }
    else if (iosStance_IsAccent(stance)) iosStance_s.stanceEnd = now + iosStance_RandRange(IOSSTANCE_ACCENT_MIN, IOSSTANCE_ACCENT_MAX);
    else if (stance == IOSSTANCE_READY) iosStance_s.stanceEnd = now + iosStance_RandRange(IOSSTANCE_READY_MIN, IOSSTANCE_READY_MAX);
    else iosStance_s.stanceEnd = now + iosStance_RandRange(IOSSTANCE_AWAY_MIN, IOSSTANCE_AWAY_MAX);
    // (the right hand goes now; the left a moment later -- not for the X, done together)
    iosStance_s.aTarget[0] = iosStance_PoseOf(stance, 0);
    iosStance_s.leftStance = stance;
    iosStance_s.leftSwitchAt = (stance == IOSSTANCE_CROSS) ? now : now + IOSSTANCE_LEFT_LAG * iosStance_RandRange(0.6f, 1.4f);
}

static void iosStance_Begin(int stance, float now) { iosStance_BeginFor(stance, now, 0.0f); }

// Weighted among [first, last], never the one just had
static int iosStance_PickWeighted(int first, int last, int skip)
{
    int sum = 0;
    for (int i = first; i <= last; i++) if (i != skip) sum += iosStance_aWeight[i];
    float r = iosStance_Rand01() * sum;
    for (int i = first; i <= last; i++) {
        if (i == skip) continue;
        r -= iosStance_aWeight[i];
        if (r < 0.0f) return i;
    }
    return first == IOSSTANCE_HIGHLOW ? IOSSTANCE_HIGHLOW : first;
}

// The next small adjustment's gap: mostly 3-8 s (nearer the middle), a quiet stretch now and then,
// or a quick second one
static float iosStance_MicroGap(void)
{
    const float r = iosStance_Rand01();
    if (r < IOSSTANCE_MICRO_QUIET_CHANCE) return iosStance_RandRange(IOSSTANCE_MICRO_QUIET_MIN, IOSSTANCE_MICRO_QUIET_MAX);
    if (r < IOSSTANCE_MICRO_QUIET_CHANCE + IOSSTANCE_MICRO_PAIR_CHANCE) return iosStance_RandRange(IOSSTANCE_MICRO_PAIR_MIN, IOSSTANCE_MICRO_PAIR_MAX);
    return 0.5f * (iosStance_RandRange(IOSSTANCE_MICRO_GAP_MIN, IOSSTANCE_MICRO_GAP_MAX) + iosStance_RandRange(IOSSTANCE_MICRO_GAP_MIN, IOSSTANCE_MICRO_GAP_MAX));
}

static void iosStance_BeginMicro(float now)
{
    int sum = 0;
    for (int i = 0; i < IOSSTANCE_M_NUM; i++) if (i != iosStance_s.lastMicro) sum += iosStance_aMicroWeight[i];
    float r = iosStance_Rand01() * sum;
    int kind = 0;
    for (int i = 0; i < IOSSTANCE_M_NUM; i++) {
        if (i == iosStance_s.lastMicro) continue;
        r -= iosStance_aMicroWeight[i];
        if (r < 0.0f) { kind = i; break; }
    }
    iosStance_s.microKind = iosStance_s.lastMicro = kind;
    if (iosStance_Rand01() < IOSSTANCE_MICRO_OTHER_HAND) iosStance_s.microHand = !iosStance_s.microHand;
    iosStance_s.microStart = now;
    iosStance_s.microDur = iosStance_RandRange(IOSSTANCE_MICRO_DUR_MIN, IOSSTANCE_MICRO_DUR_MAX);
}

void iosStance_Reset(void)
{
    memset(&iosStance_s, 0, sizeof(iosStance_s));
}

int iosStance_IsPovBusy(rdPuppet* pPuppet)
{
    if (!pPuppet) return 0;
    for (int i = 0; i < 4; i++) {
        const rdPuppetTrack* tr = &pPuppet->aTracks[i];
        // (the resting hold, and mounting, are played at priority 0; swings and the like above)
        if (tr->status && tr->keyframe && tr->lowPri >= 1) return 1;
    }
    return 0;
}

static void iosStance_Spring(iosStancePose* x, iosStancePose* v, const iosStancePose* target, float w, float dt)
{
    // critically damped: no overshoot, eased in and out; carries on smoothly from wherever it is
    float* px = &x->roll; float* pv = &v->roll; const float* pt = &target->roll;
    for (int k = 0; k < 4; k++) {
        float a = w * w * (pt[k] - px[k]) - 2.0f * w * pv[k];
        pv[k] += a * dt;
        px[k] += pv[k] * dt;
    }
}

void iosStance_Update(SithThing* pActor, int bBusy)
{
    const flex_t gameTime = sithTime_g_secGameTime;
    if (!iosStance_s.bStarted || gameTime < iosStance_s.lastTime || gameTime - iosStance_s.lastTime > 1.0) {
        // (first, after a load, or a long gap: from the V, at rest, at once)
        iosStance_Reset();
        iosStance_s.bStarted = 1;
        iosStance_s.rng = (uint32_t)(gameTime * 1000.0) * 2654435761u + 12345u;
        iosStance_s.lastAway = iosStance_s.lastAccent = iosStance_s.lastMicro = -1;
        iosStance_Begin(IOSSTANCE_V, 0.0f);
        iosStance_s.aPose[0] = iosStance_s.aTarget[0];
        iosStance_s.aPose[1] = iosStance_s.aTarget[1] = iosStance_PoseOf(IOSSTANCE_V, 1);
        iosStance_s.lastTime = gameTime;
        iosStance_s.weight = 0.0f; // (faded in from the game's own pose, both arms together)
        iosStance_s.bWasBusy = bBusy;
        iosStance_s.nextAccent = iosStance_RandRange(IOSSTANCE_ACCENT_GAP_MIN, IOSSTANCE_ACCENT_GAP_MAX);
        iosStance_s.nextMicro = iosStance_MicroGap();
        iosStance_s.lastFight = -100.0f;
        return;
    }
    float dt = (float)(gameTime - iosStance_s.lastTime);
    iosStance_s.lastTime = gameTime;
    if (dt <= 0.0f) return; // (the game holding still: as it was)
    if (dt > 0.1f) dt = 0.1f;
    iosStance_s.t += dt;
    const float now = iosStance_s.t;

    // Moving: how fast (running brings the hands down a little, and puts off the adjustments)
    float run = 0.0f;
    if (pActor) {
        const flex_t maxV = pActor->physicsParams.maxVelocity > 0.0 ? pActor->physicsParams.maxVelocity : 1.0;
        run = (float)(rdVector_Len3(&pActor->physicsParams.vel) / maxV);
        if (run > 1.0f) run = 1.0f;
    }
    iosStance_s.run = run;

    // A swing: the pose gives way at once; after it, ready for a moment, then back to rest
    if (bBusy) {
        iosStance_s.weight -= IOSSTANCE_FADE_OUT * dt;
        if (iosStance_s.weight < 0.0f) iosStance_s.weight = 0.0f;
        iosStance_s.lastFight = now;
        iosStance_s.microDur = 0.0f; // (no adjustment carried through a swing)
    }
    else {
        if (iosStance_s.bWasBusy) {
            iosStance_s.lastAway = IOSSTANCE_READY;
            iosStance_Begin(IOSSTANCE_READY, now);
        }
        iosStance_s.weight += IOSSTANCE_FADE_IN * dt;
        if (iosStance_s.weight > 1.0f) iosStance_s.weight = 1.0f;
    }
    iosStance_s.bWasBusy = bBusy;
    const int bCalm = !bBusy && iosStance_s.weight >= 0.95f && run < 0.5f;

    // What comes next: an accent ends back in the posture it interrupted; from the V, another
    // (weighted, not the last); from another, the V
    if (!bBusy && now >= iosStance_s.stanceEnd) {
        if (iosStance_IsAccent(iosStance_s.stance))
            iosStance_BeginFor(iosStance_s.baseStance, now, iosStance_s.baseLeft > 0.5f ? iosStance_s.baseLeft : 0.0f);
        else if (iosStance_s.stance == IOSSTANCE_V) {
            const int next = iosStance_PickWeighted(IOSSTANCE_HIGHLOW, IOSSTANCE_READY, iosStance_s.lastAway);
            iosStance_s.lastAway = next;
            iosStance_Begin(next, now);
        }
        else
            iosStance_Begin(IOSSTANCE_V, now);
    }
    // An accent, now and then: only calm, a while after a fight, and not over an adjustment
    if (now >= iosStance_s.nextAccent) {
        if (bCalm && !iosStance_IsAccent(iosStance_s.stance) && iosStance_s.stance != IOSSTANCE_READY
            && now - iosStance_s.lastFight >= IOSSTANCE_ACCENT_AFTER_FIGHT && iosStance_s.microDur <= 0.0f) {
            const int accent = iosStance_PickWeighted(IOSSTANCE_FIRST_ACCENT, IOSSTANCE_NUM - 1, iosStance_s.lastAccent);
            iosStance_s.lastAccent = accent;
            iosStance_s.baseStance = iosStance_s.stance;
            iosStance_s.baseLeft = iosStance_s.stanceEnd - now;
            iosStance_Begin(accent, now);
            iosStance_s.nextAccent = now + iosStance_RandRange(IOSSTANCE_ACCENT_GAP_MIN, IOSSTANCE_ACCENT_GAP_MAX);
        }
        else
            iosStance_s.nextAccent = now + iosStance_RandRange(2.0f, 5.0f); // (later)
    }
    if (now >= iosStance_s.leftSwitchAt)
        iosStance_s.aTarget[1] = iosStance_PoseOf(iosStance_s.leftStance, 1);

    // The small adjustments: ended, or due (only calm, not during an accent)
    if (iosStance_s.microDur > 0.0f && now - iosStance_s.microStart >= iosStance_s.microDur + IOSSTANCE_ECHO_DELAY)
        iosStance_s.microDur = 0.0f;
    if (now >= iosStance_s.nextMicro) {
        if (bCalm && iosStance_s.microDur <= 0.0f && !iosStance_IsAccent(iosStance_s.stance)) {
            iosStance_BeginMicro(now);
            iosStance_s.nextMicro = now + iosStance_s.microDur + iosStance_MicroGap();
        }
        else
            iosStance_s.nextMicro = now + iosStance_RandRange(1.0f, 3.0f); // (later)
    }

    // The goal: the posture, a little further apart; running brings the hands down a little (the low
    // stance's way); crouching lowers them, in the air they rise a little
    iosStancePose aGoal[2] = { iosStance_s.aTarget[0], iosStance_s.aTarget[1] };
    for (int h = 0; h < 2; h++) {
        if (!iosStance_IsAccent(iosStance_s.stance)) aGoal[h].roll += IOSSTANCE_SPREAD;
        if (pActor) {
            const iosStancePose low = iosStance_aDef[IOSSTANCE_LOW].aHand[h];
            const float k = 0.5f * run;
            aGoal[h].roll += (low.roll - aGoal[h].roll) * k;
            aGoal[h].lower += (low.lower - aGoal[h].lower) * k;
            if (pActor->physicsParams.flags & SITH_PF_CROUCHING) aGoal[h].lower += 5.0f * IOSSTANCE_U;
            if (pActor->attach_flags == 0 && !(pActor->physicsParams.flags & SITH_PF_FLY)) aGoal[h].lower -= 4.0f * IOSSTANCE_U;
        }
    }
    const float spring = iosStance_IsAccent(iosStance_s.stance) ? IOSSTANCE_SPRING_ACCENT : 0.0f;
    iosStance_Spring(&iosStance_s.aPose[0], &iosStance_s.aVel[0], &aGoal[0], spring > 0.0f ? spring : IOSSTANCE_SPRING_R, dt);
    iosStance_Spring(&iosStance_s.aPose[1], &iosStance_s.aVel[1], &aGoal[1], spring > 0.0f ? spring : IOSSTANCE_SPRING_L, dt);
}

// A smooth bump: 0 at the ends, 1 in the middle, eased in and out
static float iosStance_Bump(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    const float s = sinf((float)M_PI * u);
    return s * s;
}

// The body's and this hand's movement now (t: the hand's own time; the left's is a moment behind)
static void iosStance_AddLife(int hand, float t, iosStancePose* p)
{
    const float twoPi = 2.0f * (float)M_PI;
    // the body, the same for both hands (the left a moment later): breathing, a hint of lean with it,
    // and the weight going slowly from foot to foot (the blades tipping the same way on screen)
    const float tb = hand ? t - IOSSTANCE_LEFT_LAG : t;
    const float breath = sinf(twoPi * IOSSTANCE_BREATH_HZ * tb);
    p->lower += IOSSTANCE_BREATH_LOWER * breath;
    p->pitch += IOSSTANCE_BREATH_PITCH * breath;
    const float shift = IOSSTANCE_SHIFT_ROLL * sinf(twoPi * IOSSTANCE_SHIFT_HZ * tb + 0.4f);
    p->roll += hand ? -shift : shift;
    // each hand's own float: slow waves at uneven rates, different for each hand
    const float ph = hand ? 1.7f : 0.0f;
    const float fr = hand ? 1.13f : 1.0f;
    const float k = IOSSTANCE_FLOAT;
    p->roll += k * (1.1f * sinf(twoPi * 0.21f * fr * t + ph) + 0.5f * sinf(twoPi * 0.37f * fr * t + 2.0f * ph) + 0.25f * sinf(twoPi * 0.61f * fr * t + 0.3f));
    p->pitch += k * (0.8f * sinf(twoPi * 0.17f * fr * t + 0.5f + ph) + 0.3f * sinf(twoPi * 0.41f * fr * t) + 0.2f * sinf(twoPi * 0.53f * fr * t + 1.1f * ph));
    p->lower += k * 0.6f * IOSSTANCE_U * sinf(twoPi * 0.29f * fr * t + 0.6f + ph);
    // the small adjustment: the hand making it in full, the other answering faintly a moment later
    if (iosStance_s.microDur > 0.0f) {
        const int bMine = hand == iosStance_s.microHand;
        const float u = (iosStance_s.t - iosStance_s.microStart - (bMine ? 0.0f : IOSSTANCE_ECHO_DELAY)) / iosStance_s.microDur;
        const float b = iosStance_Bump(u) * (bMine ? 1.0f : IOSSTANCE_ECHO);
        const iosStancePose* m = &iosStance_aMicro[iosStance_s.microKind];
        p->roll += b * m->roll;
        p->pitch += b * m->pitch;
        p->lower += b * m->lower;
        p->fwd += b * m->fwd;
    }
}

int iosStance_GetHand(int hand, const rdVector3* pPivot, rdMatrix34* pOut)
{
    if (!iosStance_s.bStarted || hand < 0 || hand > 1) return 0;
    const float w = iosStance_s.weight;
    const iosStancePose base = iosStance_s.aPose[hand];
    iosStancePose p = base;
    iosStancePose life = { 0.0f, 0.0f, 0.0f, 0.0f };
    iosStance_AddLife(hand, iosStance_s.t, &life);
    // (less of the life while a swing has the arm, and while running)
    const float lifeK = (0.35f + 0.65f * w) * (1.0f - 0.5f * iosStance_s.run);
    p.roll += lifeK * life.roll;
    p.pitch += lifeK * life.pitch;
    p.lower += lifeK * life.lower;
    p.fwd += lifeK * life.fwd;
    // The stance itself, as much as the weight says (a swing: none; the life stays, less of it)
    rdVector3 pyr;
    pyr.x = (w * p.pitch) + (1.0f - w) * (p.pitch - base.pitch);
    pyr.y = 0.0f;
    pyr.z = (w * p.roll) + (1.0f - w) * (p.roll - base.roll); // (+: the tip away from the middle)
    rdMatrix34 r;
    rdMatrix_BuildRotate34(&r, &pyr);
    // turned round the hand, then moved
    rdVector3 rp;
    rdMatrix_TransformVector34(&rp, pPivot, &r);
    r.scale.x = pPivot->x - rp.x;
    r.scale.y = pPivot->y - rp.y + (w * p.fwd + (1.0f - w) * (p.fwd - base.fwd));
    r.scale.z = pPivot->z - rp.z - (w * p.lower + (1.0f - w) * (p.lower - base.lower));
    *pOut = r;
    return 1;
}

flex_t iosStance_BladeWidth(int blade)
{
    // two slow waves, out of step between the blades, and a faint quick shimmer: within 4% either way
    const float t = (float)sithTime_g_secGameTime;
    const float b = blade ? 1.0f : 0.0f;
    float k = 0.025f * sinf(2.0f * (float)M_PI * (1.7f + 0.45f * b) * t + 2.1f * b)
            + 0.010f * sinf(2.0f * (float)M_PI * (2.9f - 0.3f * b) * t + 0.7f)
            + 0.006f * sinf(2.0f * (float)M_PI * (9.0f + 1.3f * b) * t);
    return 1.0f + k;
}

#endif // TARGET_IOS

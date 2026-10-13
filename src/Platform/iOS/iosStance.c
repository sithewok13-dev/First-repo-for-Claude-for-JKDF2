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

// The stances. The relaxed V is home; the rest are left after a while.
enum {
    IOSSTANCE_V = 0,     // both out in a loose V, the left a little lower
    IOSSTANCE_HIGHLOW,   // one nearly upright and higher, the other out and low (the hands swap at times)
    IOSSTANCE_DUELIST,   // the right tipped toward the middle, the left low and out
    IOSSTANCE_WIDE,      // both further out, the middle of the view open
    IOSSTANCE_LOW,       // both hands low, the blades rising from the bottom corners
    IOSSTANCE_READY,     // both a little forward, the right leading (after a fight)
    IOSSTANCE_CROSS,     // (gesture) the blades brought into a loose X low down, then back
    IOSSTANCE_TWIRL,     // (gesture) one wrist turned over and back, the other still
    IOSSTANCE_NUM
};

#define IOSSTANCE_U 0.001f // (a model unit's thousandth: a hand moves a few of these)
static const iosStanceDef iosStance_aDef[IOSSTANCE_NUM] = {
    [IOSSTANCE_V]       = { { {  9.0f,  0.0f,  0.0f * IOSSTANCE_U, 0.0f }, {  9.0f,  0.0f,  4.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_HIGHLOW] = { { { -3.0f,  2.0f, -4.0f * IOSSTANCE_U, 0.0f }, { 22.0f,  0.0f,  9.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_DUELIST] = { { { -12.0f, 12.0f, -2.0f * IOSSTANCE_U, 4.0f * IOSSTANCE_U }, { 20.0f, -4.0f, 9.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_WIDE]    = { { { 19.0f,  0.0f,  2.0f * IOSSTANCE_U, 0.0f }, { 19.0f,  0.0f,  3.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_LOW]     = { { { 18.0f, -6.0f, 26.0f * IOSSTANCE_U, 0.0f }, { 18.0f, -6.0f, 28.0f * IOSSTANCE_U, 0.0f } } },
    [IOSSTANCE_READY]   = { { { -5.0f, 14.0f, -2.0f * IOSSTANCE_U, 6.0f * IOSSTANCE_U }, { -2.0f, 10.0f, 0.0f, 4.0f * IOSSTANCE_U } } },
    [IOSSTANCE_CROSS]   = { { { -26.0f, 4.0f, 10.0f * IOSSTANCE_U, 2.0f * IOSSTANCE_U }, { -26.0f, 4.0f, 10.0f * IOSSTANCE_U, 2.0f * IOSSTANCE_U } } },
    [IOSSTANCE_TWIRL]   = { { {  9.0f,  0.0f,  0.0f, 0.0f }, {  9.0f,  0.0f,  4.0f * IOSSTANCE_U, 0.0f } } }, // (the turn: below)
};
// How often each is picked after the V (gestures among them), out of the sum
static const int iosStance_aWeight[IOSSTANCE_NUM] = {
    [IOSSTANCE_HIGHLOW] = 26, [IOSSTANCE_DUELIST] = 20, [IOSSTANCE_WIDE] = 16, [IOSSTANCE_LOW] = 14,
    [IOSSTANCE_READY] = 6, [IOSSTANCE_CROSS] = 6, [IOSSTANCE_TWIRL] = 12,
};

// Timing (seconds): the V held this long (now and then much longer), another
// stance this long, the gestures this long; after a fight, ready this long
#define IOSSTANCE_HOME_MIN 8.0f
#define IOSSTANCE_HOME_MAX 26.0f
#define IOSSTANCE_HOME_LONG_CHANCE 0.15f
#define IOSSTANCE_HOME_LONG_MAX 45.0f
#define IOSSTANCE_AWAY_MIN 4.0f
#define IOSSTANCE_AWAY_MAX 9.0f
#define IOSSTANCE_CROSS_TIME 1.1f
#define IOSSTANCE_TWIRL_TIME 0.9f
#define IOSSTANCE_READY_MIN 2.5f
#define IOSSTANCE_READY_MAX 4.5f
// The springs (per second): the right hand leads, the left follows a moment later
#define IOSSTANCE_SPRING_R 5.0f
#define IOSSTANCE_SPRING_L 4.2f
#define IOSSTANCE_LEFT_LAG 0.16f
// The pose fades out this fast when a swing begins, and back in this fast after
#define IOSSTANCE_FADE_OUT 14.0f
#define IOSSTANCE_FADE_IN 2.2f

static struct {
    int bStarted;
    flex_t lastTime;
    float t;             // time at rest (seconds, the game's clock)
    uint32_t rng;
    int stance, lastAway;
    int bSwapped;        // HIGHLOW: the left high this time
    float stanceEnd;     // when the current stance (or gesture) ends
    float leftSwitchAt;  // when the left hand takes up the new target
    iosStancePose aTarget[2], aPose[2], aVel[2];
    int leftStance;      // the stance the left hand is on its way to
    float weight;        // 0: the game's own pose (a swing), 1: the stance in full
    int bWasBusy;
    float twirlStart;
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
    if (stance == IOSSTANCE_HIGHLOW && iosStance_s.bSwapped)
        p = iosStance_aDef[stance].aHand[!hand];
    return p;
}

static void iosStance_Begin(int stance, float now)
{
    iosStance_s.stance = stance;
    if (stance == IOSSTANCE_HIGHLOW) iosStance_s.bSwapped = iosStance_Rand01() < 0.4f;
    if (stance == IOSSTANCE_V) {
        float dwell = iosStance_RandRange(IOSSTANCE_HOME_MIN, IOSSTANCE_HOME_MAX);
        if (iosStance_Rand01() < IOSSTANCE_HOME_LONG_CHANCE) dwell = iosStance_RandRange(IOSSTANCE_HOME_MAX, IOSSTANCE_HOME_LONG_MAX);
        iosStance_s.stanceEnd = now + dwell;
    }
    else if (stance == IOSSTANCE_CROSS) iosStance_s.stanceEnd = now + IOSSTANCE_CROSS_TIME;
    else if (stance == IOSSTANCE_TWIRL) { iosStance_s.stanceEnd = now + IOSSTANCE_TWIRL_TIME; iosStance_s.twirlStart = now; }
    else if (stance == IOSSTANCE_READY) iosStance_s.stanceEnd = now + iosStance_RandRange(IOSSTANCE_READY_MIN, IOSSTANCE_READY_MAX);
    else iosStance_s.stanceEnd = now + iosStance_RandRange(IOSSTANCE_AWAY_MIN, IOSSTANCE_AWAY_MAX);
    // (the right hand goes now; the left a moment later -- not for the gestures, done together)
    iosStance_s.aTarget[0] = iosStance_PoseOf(stance, 0);
    iosStance_s.leftStance = stance;
    iosStance_s.leftSwitchAt = (stance == IOSSTANCE_CROSS) ? now : now + IOSSTANCE_LEFT_LAG * iosStance_RandRange(0.6f, 1.4f);
}

// The next stance after the V: weighted, never the one just had
static int iosStance_PickAway(void)
{
    int sum = 0;
    for (int i = 1; i < IOSSTANCE_NUM; i++) if (i != iosStance_s.lastAway) sum += iosStance_aWeight[i];
    float r = iosStance_Rand01() * sum;
    for (int i = 1; i < IOSSTANCE_NUM; i++) {
        if (i == iosStance_s.lastAway) continue;
        r -= iosStance_aWeight[i];
        if (r < 0.0f) return i;
    }
    return IOSSTANCE_HIGHLOW;
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
        iosStance_s.lastAway = -1;
        iosStance_Begin(IOSSTANCE_V, 0.0f);
        iosStance_s.aPose[0] = iosStance_s.aTarget[0];
        iosStance_s.aPose[1] = iosStance_s.aTarget[1] = iosStance_PoseOf(IOSSTANCE_V, 1);
        iosStance_s.lastTime = gameTime;
        iosStance_s.weight = 0.0f; // (faded in from the game's own pose, both arms together)
        iosStance_s.bWasBusy = bBusy;
        return;
    }
    float dt = (float)(gameTime - iosStance_s.lastTime);
    iosStance_s.lastTime = gameTime;
    if (dt <= 0.0f) return; // (the game holding still: as it was)
    if (dt > 0.1f) dt = 0.1f;
    iosStance_s.t += dt;
    const float now = iosStance_s.t;

    // A swing: the pose gives way at once; after it, ready for a moment, then back to rest
    if (bBusy) {
        iosStance_s.weight -= IOSSTANCE_FADE_OUT * dt;
        if (iosStance_s.weight < 0.0f) iosStance_s.weight = 0.0f;
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

    // What comes next: from the V, another (weighted, not the last); from another, the V
    if (!bBusy && now >= iosStance_s.stanceEnd) {
        if (iosStance_s.stance == IOSSTANCE_V) {
            const int next = iosStance_PickAway();
            iosStance_s.lastAway = next;
            iosStance_Begin(next, now);
        }
        else {
            iosStance_Begin(IOSSTANCE_V, now);
        }
    }
    if (now >= iosStance_s.leftSwitchAt)
        iosStance_s.aTarget[1] = iosStance_PoseOf(iosStance_s.leftStance, 1);

    // Moving: running brings the hands down a little (the low stance's way); crouching lowers
    // them, in the air they rise a little
    iosStancePose aGoal[2] = { iosStance_s.aTarget[0], iosStance_s.aTarget[1] };
    if (pActor) {
        const flex_t maxV = pActor->physicsParams.maxVelocity > 0.0 ? pActor->physicsParams.maxVelocity : 1.0;
        float run = (float)(rdVector_Len3(&pActor->physicsParams.vel) / maxV);
        if (run > 1.0f) run = 1.0f;
        const int bCrouch = (pActor->physicsParams.flags & SITH_PF_CROUCHING) != 0;
        const int bAir = pActor->attach_flags == 0 && !(pActor->physicsParams.flags & SITH_PF_FLY);
        for (int h = 0; h < 2; h++) {
            const iosStancePose low = iosStance_aDef[IOSSTANCE_LOW].aHand[h];
            const float k = 0.5f * run;
            aGoal[h].roll += (low.roll - aGoal[h].roll) * k;
            aGoal[h].lower += (low.lower - aGoal[h].lower) * k;
            if (bCrouch) aGoal[h].lower += 5.0f * IOSSTANCE_U;
            if (bAir) aGoal[h].lower -= 4.0f * IOSSTANCE_U;
        }
    }
    // The twirl: one wrist (the right, mostly) turned over and back
    if (iosStance_s.stance == IOSSTANCE_TWIRL) {
        const float u = (now - iosStance_s.twirlStart) / IOSSTANCE_TWIRL_TIME;
        if (u >= 0.0f && u <= 1.0f)
            aGoal[0].roll += 34.0f * sinf((float)M_PI * u) * sinf((float)M_PI * u);
    }

    iosStance_Spring(&iosStance_s.aPose[0], &iosStance_s.aVel[0], &aGoal[0], IOSSTANCE_SPRING_R, dt);
    iosStance_Spring(&iosStance_s.aPose[1], &iosStance_s.aVel[1], &aGoal[1], IOSSTANCE_SPRING_L, dt);
}

int iosStance_GetHand(int hand, const rdVector3* pPivot, rdMatrix34* pOut)
{
    if (!iosStance_s.bStarted || hand < 0 || hand > 1) return 0;
    const float t = iosStance_s.t;
    const float w = iosStance_s.weight;
    iosStancePose p = iosStance_s.aPose[hand];
    // Each hand's own slow sway (a few slow waves at uneven rates, different for each hand), and
    // breathing; less of it while a swing has the arm
    const float ph = hand ? 1.7f : 0.0f;
    const float fr = hand ? 1.13f : 1.0f;
    const float sway = 0.35f + 0.65f * w;
    p.roll += sway * (1.1f * sinf(2.0f * (float)M_PI * 0.21f * fr * t + ph) + 0.5f * sinf(2.0f * (float)M_PI * 0.37f * fr * t + 2.0f * ph));
    p.pitch += sway * (0.8f * sinf(2.0f * (float)M_PI * 0.17f * fr * t + 0.5f + ph) + 0.3f * sinf(2.0f * (float)M_PI * 0.41f * fr * t));
    p.lower += sway * 1.2f * IOSSTANCE_U * sinf(2.0f * (float)M_PI * 0.24f * t + 0.6f * hand);
    // The stance itself, as much as the weight says (a swing: none)
    rdVector3 pyr;
    pyr.x = (w * p.pitch) + (1.0f - w) * (p.pitch - iosStance_s.aPose[hand].pitch); // (the sway stays)
    pyr.y = 0.0f;
    pyr.z = (w * p.roll) + (1.0f - w) * (p.roll - iosStance_s.aPose[hand].roll); // (+: the tip away from the middle)
    rdMatrix34 r;
    rdMatrix_BuildRotate34(&r, &pyr);
    // turned round the hand, then moved
    rdVector3 rp;
    rdMatrix_TransformVector34(&rp, pPivot, &r);
    r.scale.x = pPivot->x - rp.x;
    r.scale.y = pPivot->y - rp.y + w * p.fwd;
    r.scale.z = pPivot->z - rp.z - (w * p.lower + (1.0f - w) * (p.lower - iosStance_s.aPose[hand].lower));
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

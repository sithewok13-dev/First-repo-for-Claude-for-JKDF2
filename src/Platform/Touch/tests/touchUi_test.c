// Added: entire file. Tests for the touch overlay's core (touchUi.c), with
// scripted, timed touches and a fake game: one or more cases per rule of the
// touch controls standard. Plain C, no GL, no SDL.
//
//   cc -std=c11 -DTOUCHUI_ENABLED -DJK_ENHANCED -I src src/Platform/Touch/tests/touchUi_test.c
//      src/Platform/Touch/touchUi.c src/Platform/Touch/touchUiDraw.c
//      src/Platform/Input/inputWheel.c -lm && ./a.out
//
// The wheels are inputWheel.c's, which asks the game itself (iosGame.h,
// iosSaber.h) and ticks in the hand itself (platServices_Haptic): those are
// the fake game here too, below.

#include "Platform/Touch/touchUi.h"
#include "Platform/Input/inputWheel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------ the fake game

static struct {
    int bMots;
    int aPowerLevel[64]; // >0: learned, at that level (1..4); -1: learned at 0
    int curPower;
    int aWeaponOwned[160], aWeaponAmmo[160], aWeaponSel[160];
    int curWeapon;
    int aItem[64];
    int bChat, chatToggles;
    int bHold, holdSets;
    int quickLoads;
    int selectedPower, selectedWeapon, selectedItem, weaponPicksCancelled;
    int bAlwaysRun;
    int bCheat, cheatToggles, bCanFly, bFlying, flightToggles, bCanNoclip, noclipToggles;
    int numLevels, levelLoaded;
    int bThird;
    int jumpStyle;
    char lastMsg[128];
    int bGauge;
    // the saber
    int bSaberBtn, saberStyle, aSaberColour[2][2], bDualOk;
    // the camera
    int bCaptureFull, captures, lastKinds, lastMode, captureCancels, batch, batchSets;
    int selfieResult, selfieShots, bSelfie, selfieStops;
    int bFreezeOk, bFreeze, freezeEnds, bSmooth;
    float orbitYaw, orbitPitch, panX, panY, zoom;
    int cinemaResult, bCinema, cinemaStops;
} fg;

static int F_IsMots(void) { return fg.bMots; }
static const char* F_GetForcePowerName(void) { return fg.curPower >= 0 ? "SPEED" : NULL; }
static int F_IsPowerAvailable(int bin) { return bin >= 0 && bin < 64 && fg.aPowerLevel[bin] != 0; }
static int F_GetPowerLevel(int bin) { return (bin >= 0 && bin < 64 && fg.aPowerLevel[bin] > 0) ? fg.aPowerLevel[bin] : 0; }
static const char* F_GetPowerName(int bin)
{
    static char b[64][16];
    if (bin < 0 || bin >= 64) return NULL;
    snprintf(b[bin], 16, "POWER %d", bin);
    return b[bin];
}
static int F_GetCurPower(void) { return fg.curPower; }
static void F_SelectPower(int bin) { fg.selectedPower = bin; fg.curPower = bin; }
static const char* F_GetWeaponName(int bin)
{
    static char b[160][24];
    if (bin < 0 || bin >= 160) return NULL;
    snprintf(b[bin], 24, bin == 3 ? "STORMTROOPER RIFLE" : "WPN %d", bin);
    return b[bin];
}
static int F_GetWeapon(int bin, int* pAmmo, int* pSel)
{
    *pAmmo = fg.aWeaponAmmo[bin];
    *pSel = fg.aWeaponSel[bin];
    return fg.aWeaponOwned[bin];
}
static int F_GetCurWeapon(void) { return fg.curWeapon; }
static int F_SelectWeapon(int bin) { fg.selectedWeapon = bin; return 1; }
static void F_CancelWeaponPick(void) { fg.weaponPicksCancelled++; }
static int F_GetItem(int bin, int* pAmount, int* pActive) { *pAmount = fg.aItem[bin]; *pActive = 0; return fg.aItem[bin] > 0; }
static void F_SelectItem(int bin) { fg.selectedItem = bin; }
static int F_QuickLoad(void) { fg.quickLoads++; return 1; }
static void F_SetHold(int b) { fg.bHold = b; fg.holdSets++; }
static int F_IsHolding(void) { return fg.bHold; }
static int F_IsChatOpen(void) { return fg.bChat; }
static void F_ToggleChat(void) { fg.bChat = !fg.bChat; fg.chatToggles++; }
static float F_GetForceMana(int* pbFull) { *pbFull = 0; return 0.5f; }
static unsigned int F_GetFrameCount(void) { return 0; }
static int F_IsAlwaysRun(void) { return fg.bAlwaysRun; }
static void F_GetMouseLookDegrees(float* pTurn, float* pPitch) { *pTurn = 0.1f; *pPitch = 0.1f; }
static int F_CanFly(void) { return fg.bCanFly; }
static int F_ToggleFlight(void) { fg.bFlying = !fg.bFlying; fg.flightToggles++; return fg.bFlying; }
static int F_IsFlying(void) { return fg.bFlying; }
static int F_CanNoclip(void) { return fg.bCanNoclip; }
static int F_ToggleNoclip(void) { fg.noclipToggles++; return 1; }
static int F_NumPickableLevels(void) { return fg.numLevels; }
static int F_GoToLevel(int l) { fg.levelLoaded = l; return 1; }
static int F_IsSithEwokOn(void) { return fg.bCheat; }
static int F_ToggleSithEwok(void) { fg.bCheat = !fg.bCheat; fg.cheatToggles++; return fg.bCheat; }
static int F_TakeSithEwokTypedOff(void) { return 0; }
static void F_SetCheatShown(int b) { (void)b; }
static void F_SetThirdPerson(int b) { fg.bThird = b; }
static int F_IsThirdPerson(void) { return fg.bThird; }
static void F_SetForceJumpStyle(int s) { fg.jumpStyle = s; }
static int F_GetForceJumpRank(void) { return 0; }
static void F_ShowMessage(const char* m) { snprintf(fg.lastMsg, sizeof(fg.lastMsg), "%s", m); }
static int F_GetGaugeRect(int bLeft, float* x0, float* y0, float* x1, float* y1)
{
    if (!fg.bGauge) return 0;
    // an iPhone-sized HUD gauge in the corner
    if (bLeft) { *x0 = 0; *y0 = 393 - 21 - 60; *x1 = 62; *y1 = 393 - 21; }
    else { *x0 = 852 - 62; *y0 = 393 - 21 - 60; *x1 = 852; *y1 = 393 - 21; }
    return 1;
}

static int F_IsSaberButtonShown(void) { return fg.bSaberBtn; }
static int F_SaberNumColours(void) { return 17; }
static const char* F_SaberColourName(int id)
{
    static char b[17][16];
    if (id < 0 || id >= 17) return NULL;
    snprintf(b[id], 16, id ? "COLOUR %d" : "GAME", id);
    return b[id];
}
static const char* F_SaberColourKey(int id)
{
    static char b[17][8];
    if (id < 0 || id >= 17) return NULL;
    snprintf(b[id], 8, "c%d", id);
    return b[id];
}
static int F_SaberColourByKey(const char* k) { int n = -1; return (k[0] == 'c' && sscanf(k + 1, "%d", &n) == 1 && n >= 0 && n < 17) ? n : -1; }
static void F_SaberColourRGB(int id, unsigned char* r, unsigned char* g, unsigned char* b) { *r = (unsigned char)(id * 15); *g = 100; *b = 200; }
static int F_SaberStyleAvailable(int st) { return st == 0 || fg.bDualOk; }
static const char* F_SaberStyleName(int st) { return st ? "DUAL" : "SINGLE"; }
static int F_SaberStyleNumBlades(int st) { return st ? 2 : 1; }
static int F_SaberGetStyle(void) { return fg.saberStyle; }
static void F_SaberSetStyle(int st) { fg.saberStyle = st; }
static int F_SaberGetColour(int st, int b) { return fg.aSaberColour[st & 1][b & 1]; }
static void F_SaberSetColour(int st, int b, int id) { fg.aSaberColour[st & 1][b & 1] = id; }
static int F_CaptureRequest(int kinds, int mode)
{
    if (fg.bCaptureFull) return 0;
    fg.captures++;
    fg.lastKinds = kinds;
    fg.lastMode = mode;
    return 1;
}
static void F_CaptureCancel(void) { fg.captureCancels++; }
static void F_SetPhotoBatch(int b) { fg.batch = b; fg.batchSets++; }
static int F_SelfieStart(int n) { fg.selfieShots = n; if (fg.selfieResult == 1) fg.bSelfie = 1; return fg.selfieResult; }
static void F_SelfieStop(void) { fg.bSelfie = 0; fg.selfieStops++; }
static int F_SelfieIsActive(void) { return fg.bSelfie; }
static int F_FreezeBegin(void) { if (fg.bFreezeOk) fg.bFreeze = 1; return fg.bFreezeOk; }
static void F_FreezeEnd(void) { fg.bFreeze = 0; fg.freezeEnds++; }
static int F_FreezeIsActive(void) { return fg.bFreeze; }
static void F_FreezeSetSmooth(int b) { fg.bSmooth = b; }
static int F_FreezeOrbit(float y, float p) { fg.orbitYaw += y; fg.orbitPitch += p; return 1; }
static void F_FreezePan(float x, float y) { fg.panX += x; fg.panY += y; }
static void F_FreezeZoom(float f) { fg.zoom *= f; }
static int F_CinemaStart(void) { if (fg.cinemaResult == 1) fg.bCinema = 1; return fg.cinemaResult; }
static void F_CinemaStop(void) { fg.bCinema = 0; fg.cinemaStops++; }
static int F_CinemaIsActive(void) { return fg.bCinema; }

static const touchUiGame fakeGame = {
    F_IsMots, F_GetForcePowerName, F_IsPowerAvailable, F_GetPowerLevel, F_GetPowerName, F_GetCurPower, F_SelectPower,
    F_GetWeaponName, F_GetWeapon, F_GetCurWeapon, F_SelectWeapon, F_CancelWeaponPick, F_GetItem, F_SelectItem,
    F_QuickLoad, F_SetHold, F_IsHolding, F_IsChatOpen, F_ToggleChat, F_GetForceMana, F_GetFrameCount, F_IsAlwaysRun,
    F_GetMouseLookDegrees, F_CanFly, F_ToggleFlight, F_IsFlying, F_CanNoclip, F_ToggleNoclip, F_NumPickableLevels,
    F_GoToLevel, F_IsSithEwokOn, F_ToggleSithEwok, F_TakeSithEwokTypedOff, F_SetCheatShown, F_SetThirdPerson,
    F_IsThirdPerson, F_SetForceJumpStyle, F_GetForceJumpRank, F_ShowMessage, F_ShowMessage, F_GetGaugeRect,
    F_IsSaberButtonShown, F_SaberNumColours, F_SaberColourName, F_SaberColourKey, F_SaberColourByKey, F_SaberColourRGB,
    F_SaberStyleAvailable, F_SaberStyleName, F_SaberStyleNumBlades, F_SaberGetStyle, F_SaberSetStyle, F_SaberGetColour,
    F_SaberSetColour, F_CaptureRequest, F_CaptureCancel, F_SetPhotoBatch, F_SelfieStart, F_SelfieStop, F_SelfieIsActive,
    F_FreezeBegin, F_FreezeEnd, F_FreezeIsActive, F_FreezeSetSmooth, F_FreezeOrbit, F_FreezePan, F_FreezeZoom,
    F_CinemaStart, F_CinemaStop, F_CinemaIsActive,
};

// The same fake game as the engine's calls, for the wheels (inputWheel.c)
int iosGame_IsMots(void) { return F_IsMots(); }
int iosGame_IsPowerAvailable(int bin) { return F_IsPowerAvailable(bin); }
int iosGame_GetPowerLevel(int bin) { return F_GetPowerLevel(bin); }
const char* iosGame_GetPowerName(int bin) { return F_GetPowerName(bin); }
int iosGame_GetCurPower(void) { return F_GetCurPower(); }
void iosGame_SelectPower(int bin) { F_SelectPower(bin); }
const char* iosGame_GetWeaponName(int bin) { return F_GetWeaponName(bin); }
int iosGame_GetWeapon(int bin, int* pAmmo, int* pSel) { return F_GetWeapon(bin, pAmmo, pSel); }
int iosGame_GetCurWeapon(void) { return F_GetCurWeapon(); }
int iosGame_SelectWeapon(int bin) { return F_SelectWeapon(bin); }
void iosGame_SetHold(int b) { F_SetHold(b); }
int iosGame_IsSaberButtonShown(void) { return F_IsSaberButtonShown(); }
int iosSaber_NumColours(void) { return F_SaberNumColours(); }
const char* iosSaber_ColourName(int id) { return F_SaberColourName(id); }
void iosSaber_ColourRGB(int id, unsigned char* r, unsigned char* g, unsigned char* b) { F_SaberColourRGB(id, r, g, b); }
int iosSaber_StyleAvailable(int st) { return F_SaberStyleAvailable(st); }
int iosSaber_StyleNumBlades(int st) { return F_SaberStyleNumBlades(st); }
int iosSaber_GetStyle(void) { return F_SaberGetStyle(); }
void iosSaber_SetStyle(int st) { F_SaberSetStyle(st); }
int iosSaber_GetColour(int st, int b) { return F_SaberGetColour(st, b); }
void iosSaber_SetColour(int st, int b, int id) { F_SaberSetColour(st, b, id); }
static int g_platHaptics = 0;
void platServices_Haptic(int kind) { g_platHaptics |= kind == 0 ? TOUCHUI_HAPTIC_TICK : TOUCHUI_HAPTIC_TAP; }

// What the hand felt since last asked: the overlay's own, and the wheel's
static int takeHaptics(void)
{
    const int h = touchUi_TakeHaptics() | g_platHaptics;
    g_platHaptics = 0;
    return h;
}

// ------------------------------------------------------------ the harness

static int g_fails = 0, g_checks = 0;
static const char* g_case = "";
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fails++; printf("FAIL [%s] line %d: %s\n", g_case, __LINE__, #cond); } } while (0)
#define CHECKF(a, b, eps) do { g_checks++; if (fabs((double)(a) - (double)(b)) > (eps)) { g_fails++; printf("FAIL [%s] line %d: %s = %g, want %g\n", g_case, __LINE__, #a, (double)(a), (double)(b)); } } while (0)

enum { SC_A = 4, SC_C = 6, SC_D = 7, SC_E = 8, SC_G = 10, SC_S = 22, SC_W = 26, SC_X = 27, SC_RETURN = 40, SC_F9 = 66, SC_LCTRL = 224, SC_LSHIFT = 225 };

static const char* g_settings = "/tmp/touchUi_test_settings.cfg";
static double T = 0.0;

static void setup_screen(float w, float h, float l, float t, float r, float b)
{
    touchUi_SetScreen(w, h, l, t, r, b);
}

static void setup(void)
{
    memset(&fg, 0, sizeof(fg));
    fg.curPower = -1;
    fg.curWeapon = 2;
    // Jedi Knight: every power learned, at levels 1..4 round
    for (int bin = 21; bin <= 34; bin++) fg.aPowerLevel[bin] = 1 + (bin % 4);
    fg.aWeaponOwned[1] = fg.aWeaponSel[1] = 1; fg.aWeaponAmmo[1] = -1;
    fg.aWeaponOwned[2] = fg.aWeaponSel[2] = 1; fg.aWeaponAmmo[2] = 120;
    fg.aWeaponOwned[3] = 1; fg.aWeaponSel[3] = 0; fg.aWeaponAmmo[3] = 0; // no ammo
    fg.bDualOk = 1;
    fg.selfieResult = fg.cinemaResult = 1;
    fg.bFreezeOk = 1;
    fg.zoom = 1.0f;
    remove(g_settings);
    if (inputWheel_IsOpen()) inputWheel_Close(-1); // (a case that ended with one open)
    touchUi_Init(&fakeGame, g_settings);
    touchUi_SetGyroAvailable(1);
    setup_screen(852, 393, 59, 0, 59, 21); // an iPhone-sized screen, as the standard was made on
    touchUi_SetShown(1);
    T = 100.0;
    touchUi_Tick(T, T);
    takeHaptics();
}

static void bxy(int btn, float* x, float* y)
{
    float r;
    touchUi_GetButton(btn, x, y, &r);
}

static void down(int id, float x, float y) { touchUi_Finger(id, TOUCHUI_PHASE_DOWN, x, y, T); }
static void move(int id, float x, float y) { touchUi_Finger(id, TOUCHUI_PHASE_MOVE, x, y, T); }
static void up(int id, float x, float y) { touchUi_Finger(id, TOUCHUI_PHASE_UP, x, y, T); }
static void cancel(int id) { touchUi_Finger(id, TOUCHUI_PHASE_CANCEL, 0, 0, T); }
static void tick(void) { touchUi_Tick(T, T); }
// time goes on, the frames ticking every 1/60 s
static void wait(double secs)
{
    double end = T + secs;
    while (T + 1.0 / 60.0 < end - 1e-9) {
        T += 1.0 / 60.0;
        tick();
    }
    T = end;
    tick();
}

// The game reading the keyboard n times: how many separate presses of sc it sees
static int presses(int sc, int n)
{
    int count = 0, was = 0;
    for (int i = 0; i < n; i++) {
        int d = touchUi_IsScancodeDown(sc);
        if (d && !was) count++;
        was = d;
    }
    return count;
}

static void downOn(int id, int btn) { float x, y; bxy(btn, &x, &y); down(id, x, y); }
static void upOn(int id, int btn) { float x, y; bxy(btn, &x, &y); up(id, x, y); }

// a point at deg (counter-clockwise from right) and dist from (x, y)
static void polar(float x, float y, float deg, float dist, float* px, float* py)
{
    *px = x + dist * cosf(deg * 3.14159265f / 180.0f);
    *py = y - dist * sinf(deg * 3.14159265f / 180.0f);
}

// ------------------------------------------------------------ the cases

static void test_stick(void)
{
    g_case = "stick: walks where the left thumb lands";
    setup();
    down(1, 150, 300);
    CHECK(touchUi_IsStickActive());
    move(1, 150, 290); // inside the dead zone (30% of 60)
    CHECK(!touchUi_IsKeyHeld(SC_W));
    move(1, 150, 230); // up 70 points: pushed to the edge
    CHECK(touchUi_IsKeyHeld(SC_W) && !touchUi_IsKeyHeld(SC_S));
    move(1, 220, 300);
    CHECK(touchUi_IsKeyHeld(SC_D) && !touchUi_IsKeyHeld(SC_W));
    move(1, 80, 340);
    CHECK(touchUi_IsKeyHeld(SC_A) && touchUi_IsKeyHeld(SC_S));
    up(1, 80, 340);
    CHECK(!touchUi_IsStickActive() && !touchUi_IsKeyHeld(SC_A) && !touchUi_IsKeyHeld(SC_S));

    g_case = "stick: only in the left 42% of the screen";
    down(2, 852 * 0.43f, 300);
    CHECK(!touchUi_IsStickActive());
    up(2, 852 * 0.43f, 300);

    g_case = "stick: run marker (Always Run off): push up past the ring onto it";
    down(1, 200, 330);
    move(1, 200, 254); // 76 up, straight: on the marker (100 above, run from 70)
    CHECK(touchUi_IsStickRunning());
    CHECK(touchUi_IsKeyHeld(SC_W) && touchUi_IsKeyHeld(SC_LSHIFT));
    CHECK(takeHaptics() & TOUCHUI_HAPTIC_TICK);
    move(1, 200, 268); // 62: still running (stops under 60)
    CHECK(touchUi_IsStickRunning());
    move(1, 200, 272); // 58: walking again
    CHECK(!touchUi_IsStickRunning() && touchUi_IsKeyHeld(SC_W) && !touchUi_IsKeyHeld(SC_LSHIFT));
    float px, py;
    polar(200, 330, 90 + 35, 80, &px, &py); // 35 degrees off: no
    move(1, px, py);
    CHECK(!touchUi_IsStickRunning());
    up(1, px, py);

    g_case = "stick: Always Run on: no marker";
    fg.bAlwaysRun = 1;
    tick();
    down(1, 200, 330);
    move(1, 200, 240);
    CHECK(!touchUi_IsStickRunning() && !touchUi_IsKeyHeld(SC_LSHIFT));
    up(1, 200, 240);
}

static void test_look(void)
{
    g_case = "look: a drag elsewhere turns the view (1.6 / 1.3 per point)";
    setup();
    int dx, dy;
    touchUi_TakeLook(&dx, &dy);
    down(1, 500, 200);
    move(1, 510, 210);
    touchUi_TakeLook(&dx, &dy);
    CHECK(dx == 16 && dy == 13);
    move(1, 500, 200);
    touchUi_TakeLook(&dx, &dy);
    CHECK(dx == -16 && dy == -13);
    up(1, 500, 200);

    g_case = "look: a drag on FIRE fires and looks";
    float fx, fy;
    bxy(TOUCHUI_BTN_FIRE, &fx, &fy);
    down(2, fx, fy);
    CHECK(touchUi_IsKeyHeld(SC_LCTRL));
    move(2, fx - 20, fy);
    touchUi_TakeLook(&dx, &dy);
    CHECK(dx == -32);
    CHECK(touchUi_IsKeyHeld(SC_LCTRL));
    up(2, fx - 20, fy);
    CHECK(!touchUi_IsKeyHeld(SC_LCTRL));
}

static void test_buttons(void)
{
    static const struct { int btn; int sc; } aCase[] = {
        { TOUCHUI_BTN_FIRE, SC_LCTRL }, { TOUCHUI_BTN_ALT, 29 }, { TOUCHUI_BTN_DUCK, SC_C },
        { TOUCHUI_BTN_ACT, 44 }, { TOUCHUI_BTN_JUMP, SC_X }, { TOUCHUI_BTN_FORCE, 9 },
    };
    setup();
    for (int i = 0; i < (int)(sizeof(aCase) / sizeof(aCase[0])); i++) {
        g_case = "cluster buttons hold their keys";
        downOn(1, aCase[i].btn);
        CHECK(touchUi_IsKeyHeld(aCase[i].sc));
        CHECK(touchUi_IsScancodeDown(aCase[i].sc));
        upOn(1, aCase[i].btn);
        CHECK(!touchUi_IsKeyHeld(aCase[i].sc));
    }
    g_case = "a tap the game never read still counts once";
    downOn(1, TOUCHUI_BTN_JUMP);
    upOn(1, TOUCHUI_BTN_JUMP);
    CHECK(presses(SC_X, 6) == 1);
}

static void test_quicksave(void)
{
    float qx, qy, lx, ly;
    setup();
    bxy(TOUCHUI_BTN_QUICKSAVE, &qx, &qy);
    bxy(TOUCHUI_BTN_QUICKLOAD, &lx, &ly);

    g_case = "QUICK SAVE: lifted at 0.29 s does nothing";
    down(1, qx, qy);
    wait(0.29);
    CHECKF(touchUi_GetRingProgress(TOUCHUI_BTN_QUICKSAVE), 0.29 / 0.3, 0.02);
    up(1, qx, qy);
    tick();
    CHECK(presses(SC_F9, 8) == 0);
    CHECK(touchUi_GetRingProgress(TOUCHUI_BTN_QUICKSAVE) == 0.0f);

    g_case = "QUICK SAVE: a 0.3 s hold saves once, the finger still down";
    down(1, qx, qy);
    wait(0.15);
    CHECKF(touchUi_GetRingProgress(TOUCHUI_BTN_QUICKSAVE), 0.5, 0.02);
    wait(0.16);
    CHECK(presses(SC_F9, 8) == 1);
    wait(1.0);
    CHECK(presses(SC_F9, 8) == 0);
    up(1, qx, qy);

    g_case = "QUICK SAVE: slid off and back on: nothing";
    down(1, qx, qy);
    wait(0.1);
    move(1, qx + 40, qy);
    move(1, qx, qy);
    wait(0.5);
    CHECK(presses(SC_F9, 8) == 0);
    up(1, qx, qy);

    g_case = "QUICK SAVE: two fingers save once";
    down(1, qx - 3, qy);
    wait(0.1);
    down(2, qx + 3, qy);
    wait(0.5);
    CHECK(presses(SC_F9, 12) == 1);
    up(1, qx, qy);
    up(2, qx, qy);

    g_case = "QUICK LOAD: a 0.3 s hold loads once";
    down(1, lx, ly);
    wait(0.29);
    CHECK(fg.quickLoads == 0);
    wait(0.02);
    CHECK(fg.quickLoads == 1);
    wait(0.5);
    CHECK(fg.quickLoads == 1);
    up(1, lx, ly);

    g_case = "QUICK SAVE and QUICK LOAD together: QUICK SAVE wins a tie";
    fg.quickLoads = 0;
    down(1, lx, ly);
    down(2, qx, qy);
    T += 0.4; // one long frame covers both
    tick();
    CHECK(presses(SC_F9, 8) == 1);
    CHECK(fg.quickLoads == 0);
    up(1, lx, ly);
    up(2, qx, qy);

    g_case = "QUICK SAVE taken by the system: nothing";
    down(1, qx, qy);
    wait(0.1);
    cancel(1);
    wait(0.5);
    CHECK(presses(SC_F9, 8) == 0);

    g_case = "QUICK SAVE while typing: nothing";
    fg.bChat = 1;
    tick();
    down(1, qx, qy);
    wait(0.5);
    CHECK(presses(SC_F9, 8) == 0);
    up(1, qx, qy);
    fg.bChat = 0;
}

static void test_menu(void)
{
    float mx, my;
    setup();
    bxy(TOUCHUI_BTN_MENU, &mx, &my);

    g_case = "MENU: a tap is the game menu (Escape, two frames)";
    down(1, mx, my);
    wait(0.1);
    up(1, mx, my);
    CHECK(touchUi_TakeEscape() == 1);
    CHECK(touchUi_TakeEscape() == 1);
    CHECK(touchUi_TakeEscape() == 0);
    CHECK(!touchUi_IsTrayOpen());

    g_case = "MENU: held 0.44 s: no tray yet";
    down(1, mx, my);
    wait(0.44);
    CHECK(!touchUi_IsTrayOpen());
    g_case = "MENU: held 0.45 s: the tray, and no menu";
    wait(0.02);
    CHECK(touchUi_IsTrayOpen());
    up(1, mx, my);
    CHECK(touchUi_TakeEscape() == 0);

    g_case = "tray: FPS shows the readout and closes the tray";
    CHECK(!touchUi_IsFpsShown());
    downOn(2, TOUCHUI_BTN_TRAYFPS);
    upOn(2, TOUCHUI_BTN_TRAYFPS);
    CHECK(touchUi_IsFpsShown());
    CHECK(!touchUi_IsTrayOpen());

    g_case = "tray: a touch off it closes it";
    down(1, mx, my); wait(0.5); up(1, mx, my);
    CHECK(touchUi_IsTrayOpen());
    down(2, 400, 250);
    CHECK(!touchUi_IsTrayOpen());
    up(2, 400, 250);

    g_case = "tray: the keyboard opens the typing line";
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYKEYS);
    upOn(2, TOUCHUI_BTN_TRAYKEYS);
    CHECK(fg.chatToggles == 1 && fg.bChat);
    fg.bChat = 0;

    g_case = "tray: slide from MENU's hold onto a tray button and lift";
    down(1, mx, my);
    wait(0.5);
    float fx, fy;
    bxy(TOUCHUI_BTN_TRAYFPS, &fx, &fy);
    move(1, fx, fy);
    up(1, fx, fy);
    CHECK(!touchUi_IsFpsShown()); // (switched back off)
    CHECK(touchUi_TakeEscape() == 0);
}

static void test_gyro_settings(void)
{
    float mx, my;
    setup();
    bxy(TOUCHUI_BTN_MENU, &mx, &my);

    g_case = "GYRO: OFF to start, SENS 1.5";
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_OFF);
    CHECKF(touchUi_GetGyroSens(), 1.5, 1e-6);

    g_case = "GYRO: a tap switches it on (TOUCH), the tray stays";
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYGYRO);
    wait(0.1);
    upOn(2, TOUCHUI_BTN_TRAYGYRO);
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_TOUCH);
    CHECK(touchUi_IsTrayOpen());

    g_case = "GYRO held 0.45 s: its options (MODE, SENS)";
    downOn(2, TOUCHUI_BTN_TRAYGYRO);
    wait(0.5);
    CHECK(touchUi_IsGyroRowOpen());
    upOn(2, TOUCHUI_BTN_TRAYGYRO);
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_TOUCH); // (the hold's lift doesn't switch it)

    g_case = "SENS: 1.5 -> 2.0 -> 3.0 -> 1.0 -> 1.5";
    static const float aWant[] = { 2.0f, 3.0f, 1.0f, 1.5f };
    for (int i = 0; i < 4; i++) {
        downOn(3, TOUCHUI_BTN_GYROOPT_SENS);
        upOn(3, TOUCHUI_BTN_GYROOPT_SENS);
        CHECKF(touchUi_GetGyroSens(), aWant[i], 1e-6);
        CHECK(touchUi_IsGyroRowOpen());
    }
    g_case = "MODE: TOUCH -> ALWAYS";
    downOn(3, TOUCHUI_BTN_GYROOPT_MODE);
    upOn(3, TOUCHUI_BTN_GYROOPT_MODE);
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_ALWAYS);
    downOn(3, TOUCHUI_BTN_GYROOPT_SENS);
    upOn(3, TOUCHUI_BTN_GYROOPT_SENS); // 2.0

    g_case = "settings remembered (mode, SENS, FPS)";
    touchUi_Init(&fakeGame, g_settings);
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_ALWAYS);
    CHECKF(touchUi_GetGyroSens(), 2.0, 1e-6);
}

static void gyro_frames(int n, double rate, int* pSum)
{
    *pSum = 0;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 4; k++) {
            T += 0.004;
            // turning left about up, held upright landscape
            touchUi_GyroSample(0.0, rate, 0.0, 0.0, 1.0, 0.0, T);
        }
        tick();
        int dx, dy;
        touchUi_GyroFrame(T, &dx, &dy);
        *pSum += dx;
    }
}

static void test_gyro_aim(void)
{
    int sum;
    setup();
    touchUi_GyroSetSide(1, T - 10.0);

    g_case = "GYRO OFF: never aims";
    gyro_frames(30, 1.0, &sum);
    CHECK(sum == 0);

    g_case = "GYRO TOUCH: aims while a thumb is down";
    float mx, my;
    bxy(TOUCHUI_BTN_MENU, &mx, &my);
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYGYRO); upOn(2, TOUCHUI_BTN_TRAYGYRO);
    downOn(2, TOUCHUI_BTN_FIRE); upOn(2, TOUCHUI_BTN_FIRE); // (closes the tray, touching off it)
    CHECK(touchUi_GetGyroMode() == TOUCHUI_GYRO_TOUCH && !touchUi_IsTrayOpen());
    gyro_frames(30, 1.0, &sum);
    CHECK(sum == 0); // no thumb down
    down(3, 500, 200); // on the look area
    gyro_frames(30, 1.0, &sum);
    // 1 rad/s for 0.48 s at 1.5x = 41 degrees, at 0.1 degrees a count: about -410 (turning left)
    CHECK(sum < -300);
    g_case = "GYRO TOUCH: lifting both thumbs freezes the view";
    up(3, 500, 200);
    gyro_frames(30, 1.0, &sum);
    CHECK(sum == 0);
    g_case = "GYRO TOUCH: the stick counts as a thumb";
    down(4, 150, 300);
    gyro_frames(30, 1.0, &sum);
    CHECK(sum < -300);
    g_case = "GYRO: not while a wheel is open";
    downOn(5, TOUCHUI_BTN_WHEEL);
    wait(0.35);
    CHECK(touchUi_IsWheelOpen());
    gyro_frames(30, 1.0, &sum);
    CHECK(sum == 0);
    up(5, 0, 0);
    up(4, 150, 300);
}

static void test_wheels(void)
{
    float wx, wy, cx, cy, r0, r1, deg;
    setup();
    bxy(TOUCHUI_BTN_WHEEL, &wx, &wy);

    g_case = "FORCE WHEEL: a tap is the next power (E)";
    down(1, wx, wy);
    wait(0.1);
    up(1, wx, wy);
    CHECK(!touchUi_IsWheelOpen());
    CHECK(presses(SC_E, 6) == 1);

    g_case = "FORCE WHEEL: still 0.29 s, lifted: a tap";
    down(1, wx, wy);
    T += 0.29; // (lifted between two frames)
    up(1, wx, wy);
    CHECK(!touchUi_IsWheelOpen());
    CHECK(presses(SC_E, 6) == 1);

    g_case = "FORCE WHEEL: a 9.5 pt slide doesn't open it";
    down(1, wx, wy);
    move(1, wx + 9.5f, wy);
    CHECK(!touchUi_IsWheelOpen());
    up(1, wx + 9.5f, wy);
    presses(SC_E, 6);

    g_case = "FORCE WHEEL: a 10 pt slide opens it at once; the game holds still";
    down(1, wx, wy);
    move(1, wx + 10.0f, wy);
    CHECK(touchUi_IsWheelOpen() && !touchUi_IsWheelTapMode());
    CHECK(fg.bHold == 1);
    CHECK(touchUi_GetWheelKind() == TOUCHUI_WHEEL_FORCE);
    g_case = "FORCE WHEEL: slide toward a power and lift: selected";
    int slice = touchUi_GetWheelSlice(32, &deg); // (Lightning)
    CHECK(slice >= 0);
    float px, py;
    polar(wx, wy, deg, 40, &px, &py);
    move(1, px, py);
    CHECK(touchUi_GetWheelHot() == slice);
    up(1, px, py);
    CHECK(!touchUi_IsWheelOpen());
    CHECK(fg.selectedPower == 32);
    CHECK(fg.bHold == 0);
    CHECK(presses(SC_E, 6) == 0);

    g_case = "FORCE WHEEL: a haptic tick per slice crossed";
    down(1, wx, wy);
    move(1, wx + 10.0f, wy);
    takeHaptics();
    int ticks = 0;
    touchUi_GetWheelGeometry(&cx, &cy, &r0, &r1);
    for (float a = 240.0f; a >= -60.0f; a -= 2.0f) { // round the ring on the wheel itself
        polar(cx, cy, a, (r0 + r1) * 0.5f, &px, &py);
        move(1, px, py);
        if (takeHaptics() & TOUCHUI_HAPTIC_TICK) ticks++;
    }
    CHECK(ticks == 14); // Jedi Knight's 14 powers, one tick each
    g_case = "FORCE WHEEL: the gap at the bottom cancels";
    polar(cx, cy, 270.0f, (r0 + r1) * 0.5f, &px, &py);
    move(1, px, py);
    CHECK(touchUi_GetWheelHot() == -2);
    fg.selectedPower = 0;
    up(1, px, py);
    CHECK(!touchUi_IsWheelOpen() && fg.selectedPower == 0 && fg.bHold == 0);

    g_case = "FORCE WHEEL: lifted back where it started: no change";
    down(1, wx, wy);
    move(1, wx + 30.0f, wy);
    move(1, wx + 2.0f, wy);
    up(1, wx + 2.0f, wy);
    CHECK(!touchUi_IsWheelOpen() && fg.selectedPower == 0);

    g_case = "FORCE WHEEL: held 0.3 s opens it to tap";
    down(1, wx, wy);
    wait(0.28);
    CHECK(!touchUi_IsWheelOpen());
    CHECKF(touchUi_GetRingProgress(TOUCHUI_BTN_WHEEL), 0.28 / 0.3, 0.03);
    wait(0.03);
    CHECK(touchUi_IsWheelOpen() && touchUi_IsWheelTapMode());
    up(1, wx, wy);
    CHECK(touchUi_IsWheelOpen()); // stays open to tap
    slice = touchUi_GetWheelSlice(25, &deg); // (Healing)
    touchUi_GetWheelGeometry(&cx, &cy, &r0, &r1);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    down(2, px, py);
    up(2, px, py);
    CHECK(!touchUi_IsWheelOpen() && fg.selectedPower == 25);

    g_case = "FORCE WHEEL: a tap in the middle closes it, no change";
    down(1, wx, wy); wait(0.35); up(1, wx, wy);
    fg.selectedPower = 0;
    down(2, cx, cy); up(2, cx, cy);
    CHECK(!touchUi_IsWheelOpen() && fg.selectedPower == 0);

    g_case = "FORCE WHEEL: four gold stars a power, filled to its level";
    down(1, wx, wy); wait(0.35); up(1, wx, wy);
    const touchUiDrawList* l = touchUi_Draw(T);
    int filledEdges = 0, emptyEdges = 0;
    for (int i = 0; i < l->numItems; i++) {
        const touchUiDrawItem* it = &l->aItems[i];
        if (it->type != TOUCHUI_DRAW_LINE) continue;
        if (it->stroke.r > 0.99f && it->stroke.g > 0.79f && it->stroke.g < 0.81f && it->stroke.a < 0.5f) emptyEdges++; // an empty star's outline (gold)
        if (it->stroke.r == 0.0f && it->stroke.a > 0.77f && it->stroke.a < 0.79f) filledEdges++;                    // a filled one's dark edge
    }
    int want = 0;
    for (int bin = 21; bin <= 34; bin++) want += fg.aPowerLevel[bin];
    CHECK(filledEdges == want * 10);
    CHECK(emptyEdges == (14 * 4 - want) * 10);
    down(2, cx, cy); up(2, cx, cy);

    g_case = "NEXT WPN: a tap is the next weapon (G)";
    float nx, ny;
    bxy(TOUCHUI_BTN_NEXTWPN, &nx, &ny);
    down(1, nx, ny); wait(0.05); up(1, nx, ny);
    CHECK(presses(SC_G, 6) == 1);
    g_case = "NEXT WPN: slide to a weapon and lift: selected";
    down(1, nx, ny);
    move(1, nx + 12, ny);
    CHECK(touchUi_IsWheelOpen() && touchUi_GetWheelKind() == TOUCHUI_WHEEL_WEAPONS);
    touchUi_GetWheelSlice(1, &deg); // fists
    touchUi_GetWheelGeometry(&cx, &cy, &r0, &r1);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    move(1, px, py);
    up(1, px, py);
    CHECK(fg.selectedWeapon == 1);
    g_case = "NEXT WPN: a weapon with no ammo can't be picked (the wheel stays)";
    down(1, nx, ny); wait(0.35); up(1, nx, ny);
    touchUi_GetWheelSlice(3, &deg);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    fg.selectedWeapon = 0;
    down(2, px, py); up(2, px, py);
    CHECK(touchUi_IsWheelOpen() && fg.selectedWeapon == 0);
    down(2, cx, cy); up(2, cx, cy);
    CHECK(!touchUi_IsWheelOpen());

    g_case = "wheel button taken by the system: nothing";
    down(1, wx, wy); wait(0.1); cancel(1);
    CHECK(presses(SC_E, 6) == 0 && !touchUi_IsWheelOpen());
}

static void test_items_cheats(void)
{
    setup();
    g_case = "BACTA: shows with a tank; a 0.3 s hold uses one";
    float bx, by;
    CHECK(!touchUi_GetButton(TOUCHUI_BTN_BACTA, &bx, &by, NULL));
    fg.aItem[40] = 2;
    tick();
    CHECK(touchUi_GetButton(TOUCHUI_BTN_BACTA, &bx, &by, NULL));
    down(1, bx, by);
    wait(0.2);
    up(1, bx, by);
    CHECK(presses(SC_RETURN, 6) == 0);
    down(1, bx, by);
    wait(0.31);
    CHECK(presses(SC_RETURN, 6) == 1 && fg.selectedItem == 40);
    up(1, bx, by);

    g_case = "CHEAT: the tray's switch shows it and puts sithewok on";
    float mx, my;
    bxy(TOUCHUI_BTN_MENU, &mx, &my);
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYCHEAT); upOn(2, TOUCHUI_BTN_TRAYCHEAT);
    tick();
    CHECK(touchUi_IsCheatShown() && fg.bCheat);
    g_case = "CHEAT: a tap does nothing; held 0.5 s it switches the cheat";
    float cx, cy;
    CHECK(touchUi_GetButton(TOUCHUI_BTN_CHEAT, &cx, &cy, NULL));
    down(1, cx, cy); wait(0.2); up(1, cx, cy);
    CHECK(fg.bCheat);
    down(1, cx, cy); wait(0.49);
    CHECK(fg.bCheat);
    wait(0.02);
    CHECK(!fg.bCheat);
    up(1, cx, cy);

    g_case = "JUMP held 0.5 s (sithewok): flight on; a double tap lands";
    fg.bCheat = 1;
    fg.bCanFly = 1;
    tick();
    downOn(1, TOUCHUI_BTN_JUMP); wait(0.51); upOn(1, TOUCHUI_BTN_JUMP);
    CHECK(fg.bFlying && fg.flightToggles == 1);
    downOn(1, TOUCHUI_BTN_JUMP); wait(0.1); upOn(1, TOUCHUI_BTN_JUMP);
    wait(0.1);
    downOn(1, TOUCHUI_BTN_JUMP); wait(0.1); upOn(1, TOUCHUI_BTN_JUMP);
    CHECK(!fg.bFlying && fg.flightToggles == 2);

    g_case = "VIEW: the tray's switch shows it; a tap is third person";
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYVIEW); upOn(2, TOUCHUI_BTN_TRAYVIEW);
    CHECK(touchUi_IsViewShown());
    downOn(2, TOUCHUI_BTN_VIEW); upOn(2, TOUCHUI_BTN_VIEW);
    CHECK(fg.bThird == 1);
}

static void test_other_input(void)
{
    setup();
    g_case = "a controller in use hides the controls; a touch brings them back and does nothing else";
    touchUi_SetOtherInput(1);
    CHECK(touchUi_IsHiddenForOtherInput());
    CHECK(touchUi_Draw(T)->numItems == 0);
    downOn(1, TOUCHUI_BTN_FIRE);
    CHECK(!touchUi_IsHiddenForOtherInput() && !touchUi_IsKeyHeld(SC_LCTRL));
    upOn(1, TOUCHUI_BTN_FIRE);
    CHECK(touchUi_Draw(T)->numItems > 0);

    g_case = "hidden (a menu): every touch let go";
    downOn(1, TOUCHUI_BTN_FIRE);
    CHECK(touchUi_IsKeyHeld(SC_LCTRL));
    touchUi_SetShown(0);
    CHECK(!touchUi_IsKeyHeld(SC_LCTRL));
    touchUi_SetShown(1);
}

static void test_swapped(void)
{
    setup();
    g_case = "CONTROLS SWAPPED: FIRE on the left, the stick on the right";
    float mx, my, fx, fy;
    bxy(TOUCHUI_BTN_MENU, &mx, &my);
    down(1, mx, my); wait(0.5); up(1, mx, my);
    downOn(2, TOUCHUI_BTN_TRAYCTRL); upOn(2, TOUCHUI_BTN_TRAYCTRL);
    CHECK(touchUi_IsSwapped());
    bxy(TOUCHUI_BTN_FIRE, &fx, &fy);
    CHECK(fx < 852 / 2);
    down(3, 700, 300);
    CHECK(touchUi_IsStickActive());
    up(3, 700, 300);
}

// Every button showing inside the safe area, none on another
// ------------------------------------------------------------ CAMERA and SABER

static void openTray(void)
{
    float mx, my;
    bxy(TOUCHUI_BTN_MENU, &mx, &my);
    down(9, mx, my); wait(0.5); up(9, mx, my);
}

static void test_camera(void)
{
    float cx, cy;
    setup();
    bxy(TOUCHUI_BTN_CAMERA, &cx, &cy);

    g_case = "CAMERA: shown by default, in the top row";
    CHECK(touchUi_IsCameraShown());
    CHECK(touchUi_GetButton(TOUCHUI_BTN_CAMERA, NULL, NULL, NULL));

    g_case = "CAMERA: a tap takes the ticked photos (CLEAN), then the flash and a tap felt";
    down(1, cx, cy); wait(0.2); up(1, cx, cy);
    CHECK(fg.captures == 1 && fg.lastKinds == TOUCHUI_CAPTURE_CLEAN && fg.lastMode == TOUCHUI_CAPTURE_PHOTO);
    CHECK(!touchUi_IsSelfieUp());
    takeHaptics();
    touchUi_CameraFeedback(TOUCHUI_CAPTURE_PHOTO);
    CHECK(takeHaptics() & TOUCHUI_HAPTIC_TAP);
    {
        const touchUiDrawList* l = touchUi_Draw(T);
        const touchUiDrawItem* it = &l->aItems[l->numItems - 1];
        CHECK(it->type == TOUCHUI_DRAW_RRECT && it->x0 == 0.0f && it->fill.r == 1.0f && it->fill.a > 0.5f); // (the flash, over everything)
        l = touchUi_Draw(T + 0.31);
        it = &l->aItems[l->numItems - 1];
        CHECK(!(it->type == TOUCHUI_DRAW_RRECT && it->x0 == 0.0f && it->fill.r == 1.0f && it->fill.g == 1.0f)); // (gone after 0.3 s)
    }

    g_case = "CAMERA: a photo no frame takes in 0.5 s is dropped";
    down(1, cx, cy); wait(0.1); up(1, cx, cy);
    CHECK(fg.captures == 2);
    wait(0.45);
    CHECK(fg.captureCancels == 0);
    wait(0.1);
    CHECK(fg.captureCancels == 1);

    g_case = "CAMERA: the last photos still being saved: a tap takes none";
    fg.bCaptureFull = 1;
    down(1, cx, cy); wait(0.1); up(1, cx, cy);
    CHECK(fg.captures == 2);
    fg.bCaptureFull = 0;

    g_case = "CAMERA: held 0.49 s, lifted: a photo, no selfie";
    down(1, cx, cy); T += 0.49; up(1, cx, cy); tick();
    CHECK(fg.captures == 3 && !touchUi_IsSelfieUp());

    g_case = "CAMERA: held still 0.5 s: the selfie (SELFIES: 3); the lift takes no photo";
    down(1, cx, cy);
    wait(0.25);
    CHECK(touchUi_GetRingProgress(TOUCHUI_BTN_CAMERA) > 0.4f && !touchUi_IsSelfieUp());
    wait(0.26);
    CHECK(touchUi_IsSelfieUp() && fg.bSelfie && fg.selfieShots == 3);
    up(1, cx, cy);
    CHECK(fg.captures == 3);

    g_case = "selfie: every touch ignored, nothing drawn but the flash";
    {
        float fx, fy;
        bxy(TOUCHUI_BTN_FIRE, &fx, &fy);
        down(2, fx, fy); tick();
        CHECK(!touchUi_IsKeyHeld(SC_LCTRL));
        up(2, fx, fy);
        const touchUiDrawList* l = touchUi_Draw(T);
        CHECK(l->numItems == 0);
    }
    g_case = "selfie: over (the engine says so): all back";
    touchUi_PhotoModeEnded(TOUCHUI_MODE_SELFIE);
    CHECK(!touchUi_IsSelfieUp());
    CHECK(touchUi_Draw(T)->numItems > 10);
    g_case = "selfie: ended by the engine without saying: put back at the next frame";
    down(1, cx, cy); wait(0.55); up(1, cx, cy);
    CHECK(touchUi_IsSelfieUp());
    fg.bSelfie = 0;
    tick();
    CHECK(!touchUi_IsSelfieUp());

    g_case = "selfie: can't (multiplayer, a cutscene): a message, nothing hidden";
    fg.selfieResult = TOUCHUI_PHOTOMODE_CANT;
    down(1, cx, cy); wait(0.55); up(1, cx, cy);
    CHECK(!touchUi_IsSelfieUp() && !strcmp(fg.lastMsg, "Can't take a selfie here"));
    fg.selfieResult = TOUCHUI_PHOTOMODE_BUSY;
    down(1, cx, cy); wait(0.55); up(1, cx, cy);
    CHECK(!touchUi_IsSelfieUp() && !strcmp(fg.lastMsg, "Still saving the last photo"));
    fg.selfieResult = 1;

    g_case = "CAMERA half held, a wheel opened by another finger: no half-filled ring left, no selfie";
    {
        float wx, wy;
        bxy(TOUCHUI_BTN_WHEEL, &wx, &wy);
        down(5, cx, cy); wait(0.25);
        CHECK(touchUi_GetRingProgress(TOUCHUI_BTN_CAMERA) > 0.4f);
        down(6, wx, wy); move(6, wx + 12.0f, wy);
        CHECK(touchUi_IsWheelOpen() && touchUi_GetRingProgress(TOUCHUI_BTN_CAMERA) == 0.0f);
        wait(0.4);
        CHECK(!touchUi_IsSelfieUp() && touchUi_GetRingProgress(TOUCHUI_BTN_CAMERA) == 0.0f);
        up(5, cx, cy); up(6, wx + 12.0f, wy);
        float pcx, pcy, r0, r1;
        touchUi_GetWheelGeometry(&pcx, &pcy, &r0, &r1);
        if (touchUi_IsWheelOpen()) { down(7, pcx, pcy + (r0 + r1) * 0.5f); up(7, pcx, pcy + (r0 + r1) * 0.5f); }
        CHECK(!touchUi_IsWheelOpen());
    }

    g_case = "CAMERA: slid 10 pt sideways: a burst, a clean shot every 0.2 s while the finger stays";
    int c0 = fg.captures;
    down(1, cx, cy);
    move(1, cx + 9.5f, cy);
    CHECK(touchUi_GetBurstShots() < 0);
    move(1, cx + 10.5f, cy);
    CHECK(touchUi_GetBurstShots() == 0 && fg.batch == 1);
    wait(1.0 / 60.0);
    CHECK(fg.captures == c0 + 1 && fg.lastMode == TOUCHUI_CAPTURE_BURST && fg.lastKinds == TOUCHUI_CAPTURE_CLEAN);
    wait(0.5);
    CHECK(fg.captures == c0 + 3);
    wait(1.0);
    CHECK(!touchUi_IsSelfieUp()); // (a slide is never a hold)
    CHECK(touchUi_GetBurstShots() == 8);
    wait(2.0);
    CHECK(fg.captures == c0 + 10); // (at most 10)
    up(1, cx + 10.5f, cy);
    CHECK(touchUi_GetBurstShots() < 0 && fg.batch == 0);
    CHECK(fg.captures == c0 + 10); // (the lift takes none)

    g_case = "CAMERA: slid down 20 pt (FREEZE on): on its way, no burst, no photo";
    c0 = fg.captures;
    down(1, cx, cy); move(1, cx, cy + 20.0f); wait(0.1);
    CHECK(touchUi_GetBurstShots() < 0 && !touchUi_IsFreezing());
    up(1, cx, cy + 20.0f);
    CHECK(fg.captures == c0);

    g_case = "CAMERA: slid down 36 pt: Action Freeze; only CAMERA and the X";
    down(1, cx, cy); move(1, cx + 5.0f, cy + 20.0f); move(1, cx + 5.0f, cy + 36.0f);
    CHECK(touchUi_IsFreezing() && fg.bFreeze && fg.bSmooth);
    up(1, cx + 5.0f, cy + 36.0f);
    CHECK(touchUi_IsFreezing());
    g_case = "Action Freeze: one finger turns the camera round, 0.3 degrees a point, smoothly";
    down(2, 300, 200);
    move(2, 400, 200);
    wait(1.0 / 60.0);
    CHECK(fg.orbitYaw < 0.0f && fg.orbitYaw > -30.0f);
    wait(1.0);
    CHECKF(fg.orbitYaw, -30.0, 0.05);
    CHECKF(fg.orbitPitch, 0.0, 0.001);
    g_case = "Action Freeze: a second finger: the two pinch to zoom and move it";
    down(3, 500, 200);
    move(3, 600, 200); // (spread from 100 to 200: twice as close)
    wait(1.0);
    CHECKF(fg.zoom, 2.0, 0.02);
    CHECK(fg.panX > 0.0f);
    up(2, 400, 200); up(3, 600, 200);
    g_case = "Action Freeze: CAMERA tapped: one clean photo";
    c0 = fg.captures;
    down(4, cx, cy); wait(0.1); up(4, cx, cy);
    CHECK(fg.captures == c0 + 1 && fg.lastKinds == TOUCHUI_CAPTURE_CLEAN && fg.lastMode == TOUCHUI_CAPTURE_PHOTO);
    CHECK(touchUi_IsFreezing());
    g_case = "Action Freeze: the X: back to the game, as it was";
    {
        float xx, xy;
        touchUi_GetFreezeClose(&xx, &xy);
        down(5, xx, xy); wait(0.05);
        CHECK(touchUi_IsFreezing());
        up(5, xx, xy);
        CHECK(!touchUi_IsFreezing() && !fg.bFreeze && fg.freezeEnds == 1);
    }

    g_case = "CAMERA: slid up (to near the top edge): the cinematic burst; over: all back";
    down(1, cx, cy); move(1, cx, 4.0f);
    CHECK(touchUi_IsCinemaUp() && fg.bCinema);
    up(1, cx, 4.0f);
    CHECK(touchUi_Draw(T)->numItems == 0);
    touchUi_PhotoModeEnded(TOUCHUI_MODE_CINEMA);
    CHECK(!touchUi_IsCinemaUp());

    g_case = "Action Freeze can't (multiplayer): a message";
    fg.bFreezeOk = 0;
    down(1, cx, cy); move(1, cx, cy + 40.0f);
    CHECK(!touchUi_IsFreezing() && !strcmp(fg.lastMsg, "Can't freeze here"));
    up(1, cx, cy + 40.0f);
    fg.bFreezeOk = 1;

    g_case = "the overlay going (a menu) ends Action Freeze";
    down(1, cx, cy); move(1, cx, cy + 40.0f); up(1, cx, cy + 40.0f);
    CHECK(touchUi_IsFreezing());
    touchUi_SetShown(0);
    CHECK(!touchUi_IsFreezing() && !fg.bFreeze);
    touchUi_SetShown(1);
    tick();

    g_case = "tray: the camera switch held 0.45 s: the camera options";
    openTray();
    CHECK(touchUi_IsTrayOpen());
    downOn(2, TOUCHUI_BTN_TRAYCAM); wait(0.44);
    CHECK(touchUi_GetOptRow() == 0);
    wait(0.02);
    CHECK(touchUi_GetOptRow() == 2);
    upOn(2, TOUCHUI_BTN_TRAYCAM);
    CHECK(touchUi_IsCameraShown()); // (the lift doesn't switch it)
    g_case = "camera options: SELFIES 3, 5, 1; FREEZE off; PHOTO opens the kinds";
    downOn(2, TOUCHUI_BTN_CAMROW_SELFIES); upOn(2, TOUCHUI_BTN_CAMROW_SELFIES);
    CHECK(touchUi_GetSelfieShots() == 5);
    downOn(2, TOUCHUI_BTN_CAMROW_SELFIES); upOn(2, TOUCHUI_BTN_CAMROW_SELFIES);
    CHECK(touchUi_GetSelfieShots() == 1);
    downOn(2, TOUCHUI_BTN_CAMROW_FREEZE); upOn(2, TOUCHUI_BTN_CAMROW_FREEZE);
    CHECK(!touchUi_IsFreezeOn());
    downOn(2, TOUCHUI_BTN_CAMROW_PHOTO); upOn(2, TOUCHUI_BTN_CAMROW_PHOTO);
    CHECK(touchUi_IsKindsRowOpen() && touchUi_GetOptRow() == 2);
    g_case = "photo kinds: HUD and HUD+BTNS ticked, CLEAN off; the last one stays";
    downOn(2, TOUCHUI_BTN_CAMOPT_HUD); upOn(2, TOUCHUI_BTN_CAMOPT_HUD);
    downOn(2, TOUCHUI_BTN_CAMOPT_BTNS); upOn(2, TOUCHUI_BTN_CAMOPT_BTNS);
    downOn(2, TOUCHUI_BTN_CAMOPT_CLEAN); upOn(2, TOUCHUI_BTN_CAMOPT_CLEAN);
    CHECK(touchUi_GetCameraKinds() == (TOUCHUI_CAPTURE_HUD | TOUCHUI_CAPTURE_HUDBTNS));
    downOn(2, TOUCHUI_BTN_CAMOPT_HUD); upOn(2, TOUCHUI_BTN_CAMOPT_HUD);
    downOn(2, TOUCHUI_BTN_CAMOPT_BTNS); upOn(2, TOUCHUI_BTN_CAMOPT_BTNS);
    CHECK(touchUi_GetCameraKinds() == TOUCHUI_CAPTURE_HUDBTNS);
    downOn(2, TOUCHUI_BTN_CAMOPT_HUD); upOn(2, TOUCHUI_BTN_CAMOPT_HUD);
    g_case = "camera options: a touch elsewhere closes them";
    down(3, 400, 300); up(3, 400, 300);
    CHECK(touchUi_GetOptRow() == 0 && !touchUi_IsKindsRowOpen());
    g_case = "CAMERA: a tap takes the kinds ticked";
    down(1, cx, cy); wait(0.1); up(1, cx, cy);
    CHECK(fg.lastKinds == (TOUCHUI_CAPTURE_HUD | TOUCHUI_CAPTURE_HUDBTNS) && fg.lastMode == TOUCHUI_CAPTURE_PHOTO);
    g_case = "FREEZE off: slid down is a burst";
    down(1, cx, cy); move(1, cx, cy + 40.0f);
    CHECK(!touchUi_IsFreezing() && touchUi_GetBurstShots() == 0);
    up(1, cx, cy + 40.0f);
    g_case = "tray: the camera switch tapped hides CAMERA";
    openTray();
    downOn(2, TOUCHUI_BTN_TRAYCAM); wait(0.1); upOn(2, TOUCHUI_BTN_TRAYCAM);
    CHECK(!touchUi_IsCameraShown() && !touchUi_GetButton(TOUCHUI_BTN_CAMERA, NULL, NULL, NULL) && !touchUi_IsTrayOpen());
    g_case = "camera settings remembered";
    touchUi_Init(&fakeGame, g_settings);
    CHECK(!touchUi_IsCameraShown() && touchUi_GetSelfieShots() == 1 && !touchUi_IsFreezeOn()
          && touchUi_GetCameraKinds() == (TOUCHUI_CAPTURE_HUD | TOUCHUI_CAPTURE_HUDBTNS));
}

static void test_saber(void)
{
    float sx, sy, cx, cy, r0, r1, deg;
    setup();
    g_case = "SABER: hidden until the saber is had (or sithewok is on)";
    CHECK(!touchUi_IsSaberShown());
    fg.bSaberBtn = 1;
    tick();
    CHECK(touchUi_IsSaberShown() && touchUi_GetButton(TOUCHUI_BTN_SABER, &sx, &sy, NULL));

    g_case = "SABER: a tap opens its wheel to tap a colour; the game holds still";
    down(1, sx, sy); wait(0.1); up(1, sx, sy);
    CHECK(touchUi_IsWheelOpen() && touchUi_GetWheelKind() == TOUCHUI_WHEEL_SABER && touchUi_IsWheelTapMode() && fg.bHold);
    CHECK(touchUi_GetWheelSliceBin(16) == 16 && touchUi_GetWheelSliceBin(17) == -1);
    CHECK(touchUi_GetSaberChip(TOUCHUI_CHIP_SINGLE, NULL, NULL) && touchUi_GetSaberChip(TOUCHUI_CHIP_DUAL, NULL, NULL)
          && touchUi_GetSaberChip(TOUCHUI_CHIP_EQUIP, NULL, NULL) && !touchUi_GetSaberChip(TOUCHUI_CHIP_LEFT, NULL, NULL));
    g_case = "saber wheel: a colour tapped is set, the wheel stays open, a tick felt";
    touchUi_GetWheelGeometry(&cx, &cy, &r0, &r1);
    touchUi_GetWheelSlice(5, &deg);
    float px, py;
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    takeHaptics();
    down(2, px, py); up(2, px, py);
    CHECK(fg.aSaberColour[0][0] == 5 && touchUi_IsWheelOpen());
    CHECK(takeHaptics() & TOUCHUI_HAPTIC_TICK);
    g_case = "saber wheel: DUAL; LEFT; a colour goes to the left blade only";
    float qx, qy;
    touchUi_GetSaberChip(TOUCHUI_CHIP_DUAL, &qx, &qy);
    down(2, qx, qy); up(2, qx, qy);
    CHECK(fg.saberStyle == TOUCHUI_SABER_DUAL && touchUi_IsWheelOpen());
    CHECK(touchUi_GetSaberChip(TOUCHUI_CHIP_LEFT, &qx, &qy) && touchUi_GetSaberChip(TOUCHUI_CHIP_RIGHT, NULL, NULL));
    down(2, qx, qy); up(2, qx, qy);
    CHECK(touchUi_GetSaberTarget() == TOUCHUI_CHIP_LEFT);
    touchUi_GetWheelSlice(7, &deg);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    down(2, px, py); up(2, px, py);
    CHECK(fg.aSaberColour[1][1] == 7 && fg.aSaberColour[1][0] == 0);
    g_case = "saber wheel: BOTH: a colour goes to both blades";
    touchUi_GetSaberChip(TOUCHUI_CHIP_BOTH, &qx, &qy);
    down(2, qx, qy); up(2, qx, qy);
    touchUi_GetWheelSlice(12, &deg);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    down(2, px, py); up(2, px, py);
    CHECK(fg.aSaberColour[1][1] == 12 && fg.aSaberColour[1][0] == 12);
    CHECK(fg.aSaberColour[0][0] == 5); // (one blade's own colour kept)
    g_case = "saber wheel: a tap in the gap closes it; the game goes on";
    polar(cx, cy, 270.0f, (r0 + r1) * 0.5f, &px, &py);
    down(2, px, py); up(2, px, py);
    CHECK(!touchUi_IsWheelOpen() && !fg.bHold);

    g_case = "SABER: a 10 pt slide opens it to slide; lifted on a colour: set, and open to tap";
    down(1, sx, sy);
    move(1, sx, sy + 10.5f);
    CHECK(touchUi_IsWheelOpen() && !touchUi_IsWheelTapMode());
    touchUi_GetWheelSlice(3, &deg);
    polar(cx, cy, deg, (r0 + r1) * 0.5f, &px, &py);
    move(1, px, py);
    CHECK(touchUi_GetWheelHot() == 3);
    up(1, px, py);
    CHECK(fg.aSaberColour[1][0] == 3 && fg.aSaberColour[1][1] == 3 && touchUi_IsWheelOpen() && touchUi_IsWheelTapMode());
    g_case = "saber wheel: EQUIP without a saber does nothing";
    touchUi_GetSaberChip(TOUCHUI_CHIP_EQUIP, &qx, &qy);
    down(2, qx, qy); up(2, qx, qy);
    CHECK(touchUi_IsWheelOpen());
    g_case = "saber wheel: EQUIP with the saber: in hand, the wheel closed";
    fg.aWeaponOwned[10] = fg.aWeaponSel[10] = 1;
    down(2, qx, qy); up(2, qx, qy);
    CHECK(!touchUi_IsWheelOpen() && fg.selectedWeapon == 10);

    g_case = "SABER: held still 0.3 s: open to tap";
    down(1, sx, sy); wait(0.29);
    CHECK(!touchUi_IsWheelOpen() && touchUi_GetRingProgress(TOUCHUI_BTN_SABER) > 0.8f);
    wait(0.02);
    CHECK(touchUi_IsWheelOpen() && touchUi_IsWheelTapMode());
    up(1, sx, sy);
    CHECK(touchUi_IsWheelOpen());
    touchUi_GetSaberChip(TOUCHUI_CHIP_RIGHT, &qx, &qy);
    down(2, qx, qy); up(2, qx, qy);
    polar(cx, cy, 270.0f, (r0 + r1) * 0.5f, &px, &py);
    down(2, px, py); up(2, px, py);
    CHECK(!touchUi_IsWheelOpen());

    g_case = "saber choices remembered (by key and name)";
    memset(fg.aSaberColour, 0, sizeof(fg.aSaberColour));
    fg.saberStyle = 0;
    touchUi_Init(&fakeGame, g_settings);
    CHECK(fg.saberStyle == TOUCHUI_SABER_DUAL && fg.aSaberColour[0][0] == 5 && fg.aSaberColour[1][0] == 3 && fg.aSaberColour[1][1] == 3);
    CHECK(touchUi_GetSaberTarget() == TOUCHUI_CHIP_RIGHT);

    g_case = "DUAL not offered where the style can't be had";
    setup();
    fg.bSaberBtn = 1; fg.bDualOk = 0;
    tick();
    bxy(TOUCHUI_BTN_SABER, &sx, &sy);
    down(1, sx, sy); wait(0.1); up(1, sx, sy);
    CHECK(touchUi_IsWheelOpen() && !touchUi_GetSaberChip(TOUCHUI_CHIP_DUAL, NULL, NULL));
}

static int g_layoutSaber = 1;
static void check_layout(const char* name, float W, float H, float l, float t, float r, float b)
{
    g_case = name;
    setup();
    fg.aItem[40] = fg.aItem[41] = fg.aItem[42] = 1;
    fg.bCheat = 1;
    fg.bSaberBtn = g_layoutSaber; // (CAMERA, SABER, CHEAT and VIEW all on the top row)
    tick();
    touchUi_SetScreen(W, H, l, t, r, b);
    float mx, my;
    bxy(TOUCHUI_BTN_MENU, &mx, &my);
    down(1, mx, my); wait(0.5); up(1, mx, my); // the tray: CHEAT and VIEW on
    downOn(2, TOUCHUI_BTN_TRAYVIEW); upOn(2, TOUCHUI_BTN_TRAYVIEW);
    tick();
    float aX[TOUCHUI_BTN_COUNT], aY[TOUCHUI_BTN_COUNT], aR[TOUCHUI_BTN_COUNT];
    int aShown[TOUCHUI_BTN_COUNT];
    for (int i = 0; i < TOUCHUI_BTN_TRAYGYRO; i++) aShown[i] = touchUi_GetButton(i, &aX[i], &aY[i], &aR[i]);
    for (int i = 0; i < TOUCHUI_BTN_TRAYGYRO; i++) {
        if (!aShown[i]) continue;
        // inside the safe area (8 pt in from an edge with no inset); the
        // cluster may reach into a side inset with nothing in it, as on iOS
        CHECK(aY[i] - aR[i] >= (t > 8 ? t : 8) - 0.5f);
        CHECK(aY[i] + aR[i] <= H - (b > 8 ? b : 8) + 0.5f);
        CHECK(aX[i] - aR[i] >= 0.0f && aX[i] + aR[i] <= W);
        if (i > TOUCHUI_BTN_FORCE) {
            CHECK(aX[i] - aR[i] >= (l > 8 ? l : 8) - 0.5f);
            CHECK(aX[i] + aR[i] <= W - (r > 8 ? r : 8) + 0.5f);
        }
        for (int j = 0; j < i; j++) {
            if (!aShown[j]) continue;
            float d = hypotf(aX[i] - aX[j], aY[i] - aY[j]);
            if (d < aR[i] + aR[j]) { printf("  overlap: %d and %d\n", i, j); }
            CHECK(d >= aR[i] + aR[j]);
        }
    }
}

// Wedges in what the overlay draws (a wheel's slices)
static int drawnWedges(void)
{
    const touchUiDrawList* l = touchUi_Draw(T);
    int n = 0;
    for (int i = 0; i < l->numItems; i++) n += l->aItems[i].type == TOUCHUI_DRAW_WEDGE;
    return n;
}

// The wheels are the shared ones (inputWheel.c): a pad or the keys open
// them too, one at a time
static void test_shared_wheel(void)
{
    float wx, wy, deg, px, py;
    setup();
    bxy(TOUCHUI_BTN_WHEEL, &wx, &wy);

    g_case = "shared wheel: a finger's wheel is the shared one, drawn here once";
    down(1, wx, wy);
    move(1, wx + 10.0f, wy);
    CHECK(touchUi_IsWheelOpen() && inputWheel_IsOpen() && inputWheel_GetKind() == INPUTWHEEL_FORCE);
    CHECK(fg.bHold == 1);
    CHECK(drawnWedges() == 14);
    g_case = "shared wheel: no other wheel opens over it (a pad, the keys)";
    CHECK(!inputWheel_Open(INPUTWHEEL_WEAPONS, 0));
    g_case = "shared wheel: the finger's slice is the wheel's; a tick on it";
    const int slice = touchUi_GetWheelSlice(24, &deg); // (Pull)
    polar(wx, wy, deg, 40, &px, &py);
    takeHaptics();
    move(1, px, py);
    CHECK(inputWheel_GetHot() == slice && touchUi_GetWheelHot() == slice);
    CHECK(takeHaptics() & TOUCHUI_HAPTIC_TICK);
    up(1, px, py);
    CHECK(!inputWheel_IsOpen() && !touchUi_IsWheelOpen());
    CHECK(fg.selectedPower == 24 && fg.bHold == 0);

    g_case = "shared wheel: a pad's wheel open: fingers leave it alone, nothing drawn here";
    CHECK(inputWheel_Open(INPUTWHEEL_WEAPONS, 1));
    CHECK(fg.bHold == 1);
    down(2, wx, wy);
    wait(0.4);
    move(2, wx + 20.0f, wy);
    up(2, wx + 20.0f, wy);
    CHECK(!touchUi_IsWheelOpen());
    CHECK(inputWheel_IsOpen() && inputWheel_GetKind() == INPUTWHEEL_WEAPONS);
    CHECK(presses(SC_E, 6) == 0);
    CHECK(drawnWedges() == 0);
    inputWheel_Close(-1);
    CHECK(fg.bHold == 0);
    g_case = "shared wheel: closed, the button works again";
    down(3, wx, wy);
    wait(0.1);
    up(3, wx, wy);
    CHECK(presses(SC_E, 6) == 1);

    g_case = "shared wheel: the blade choice is the wheel's (the pad's too), remembered";
    fg.bSaberBtn = 1;
    fg.saberStyle = 1;
    tick();
    inputWheel_SetSaberTarget(INPUTWHEEL_SABER_LEFT); // (as the pad set it)
    CHECK(touchUi_GetSaberTarget() == TOUCHUI_CHIP_LEFT);
    float sx, sy, cx, cy;
    bxy(TOUCHUI_BTN_SABER, &sx, &sy);
    down(4, sx, sy);
    wait(0.1);
    up(4, sx, sy);
    CHECK(touchUi_IsWheelOpen() && touchUi_GetWheelKind() == TOUCHUI_WHEEL_SABER);
    CHECK(touchUi_GetSaberChip(TOUCHUI_CHIP_RIGHT, &cx, &cy));
    down(5, cx, cy);
    up(5, cx, cy);
    CHECK(inputWheel_GetSaberTarget() == INPUTWHEEL_SABER_RIGHT && touchUi_GetSaberTarget() == TOUCHUI_CHIP_RIGHT);
    CHECK(touchUi_IsWheelOpen());
    touchUi_SetShown(0);
    CHECK(!inputWheel_IsOpen() && fg.bHold == 0);
    touchUi_Init(&fakeGame, g_settings);
    CHECK(inputWheel_GetSaberTarget() == INPUTWHEEL_SABER_RIGHT);
}

int main(void)
{
    test_stick();
    test_look();
    test_buttons();
    test_quicksave();
    test_menu();
    test_gyro_settings();
    test_gyro_aim();
    test_wheels();
    test_items_cheats();
    test_other_input();
    test_swapped();
    test_camera();
    test_saber();
    test_shared_wheel();
    check_layout("layout: iPhone-sized (852x393, notch insets)", 852, 393, 59, 0, 59, 21);
    check_layout("layout: Android phone (873x393, cutout on the left)", 873, 393, 32, 0, 0, 0);
    check_layout("layout: Android phone (873x393, cutout on the right)", 873, 393, 0, 0, 32, 0);
    check_layout("layout: tablet (1067x667 at x1.2, nav bar)", 1067, 667, 0, 0, 0, 40);
    check_layout("layout: Android phone (732x360), everything on", 732, 360, 0, 0, 0, 0);
    // (narrower than any iPhone: all three items, CAMERA, SABER, CHEAT and VIEW
    // don't fit in one row there, on iOS either; without SABER they do)
    g_layoutSaber = 0;
    check_layout("layout: small phone (640x360), no saber yet", 640, 360, 0, 0, 0, 0);
    remove(g_settings);
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

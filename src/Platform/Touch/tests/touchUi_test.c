// Added: entire file. Tests for the touch overlay's core (touchUi.c), with
// scripted, timed touches and a fake game: one or more cases per rule of the
// touch controls standard. Plain C, no GL, no SDL.
//
//   cc -std=c11 -I src src/Platform/Touch/tests/touchUi_test.c
//      src/Platform/Touch/touchUi.c src/Platform/Touch/touchUiDraw.c -lm && ./a.out

#include "Platform/Touch/touchUi.h"

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

static const touchUiGame fakeGame = {
    F_IsMots, F_GetForcePowerName, F_IsPowerAvailable, F_GetPowerLevel, F_GetPowerName, F_GetCurPower, F_SelectPower,
    F_GetWeaponName, F_GetWeapon, F_GetCurWeapon, F_SelectWeapon, F_CancelWeaponPick, F_GetItem, F_SelectItem,
    F_QuickLoad, F_SetHold, F_IsHolding, F_IsChatOpen, F_ToggleChat, F_GetForceMana, F_GetFrameCount, F_IsAlwaysRun,
    F_GetMouseLookDegrees, F_CanFly, F_ToggleFlight, F_IsFlying, F_CanNoclip, F_ToggleNoclip, F_NumPickableLevels,
    F_GoToLevel, F_IsSithEwokOn, F_ToggleSithEwok, F_TakeSithEwokTypedOff, F_SetCheatShown, F_SetThirdPerson,
    F_IsThirdPerson, F_SetForceJumpStyle, F_GetForceJumpRank, F_ShowMessage, F_ShowMessage, F_GetGaugeRect,
};

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
    remove(g_settings);
    touchUi_Init(&fakeGame, g_settings);
    touchUi_SetGyroAvailable(1);
    setup_screen(852, 393, 59, 0, 59, 21); // an iPhone-sized screen, as the standard was made on
    touchUi_SetShown(1);
    T = 100.0;
    touchUi_Tick(T, T);
    touchUi_TakeHaptics();
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
    CHECK(touchUi_TakeHaptics() & TOUCHUI_HAPTIC_TICK);
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
    touchUi_TakeHaptics();
    int ticks = 0;
    touchUi_GetWheelGeometry(&cx, &cy, &r0, &r1);
    for (float a = 240.0f; a >= -60.0f; a -= 2.0f) { // round the ring on the wheel itself
        polar(cx, cy, a, (r0 + r1) * 0.5f, &px, &py);
        move(1, px, py);
        if (touchUi_TakeHaptics() & TOUCHUI_HAPTIC_TICK) ticks++;
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
static void check_layout(const char* name, float W, float H, float l, float t, float r, float b)
{
    g_case = name;
    setup();
    fg.aItem[40] = fg.aItem[41] = fg.aItem[42] = 1;
    fg.bCheat = 1;
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
    check_layout("layout: iPhone-sized (852x393, notch insets)", 852, 393, 59, 0, 59, 21);
    check_layout("layout: Android phone (873x393, cutout on the left)", 873, 393, 32, 0, 0, 0);
    check_layout("layout: Android phone (873x393, cutout on the right)", 873, 393, 0, 0, 32, 0);
    check_layout("layout: tablet (1067x667 at x1.2, nav bar)", 1067, 667, 0, 0, 0, 40);
    check_layout("layout: small phone (640x360)", 640, 360, 0, 0, 0, 0);
    remove(g_settings);
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

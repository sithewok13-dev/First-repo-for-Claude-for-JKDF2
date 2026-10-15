// Added: entire file. The platform services (Platform/platServices.h) away
// from iOS: the photos (the CAMERA button, selfies, the cinematic burst) as
// PNG files in the user's Pictures folder, in OpenJKDF2/, named as the iOS
// apps name them; no haptics; no HUD margins; the game's own message text.

#if defined(JK_ENHANCED) && !defined(TARGET_IOS)

#include "Platform/platServices.h"
#include "Enhanced/iosCapture.h"
#include "Enhanced/iosGame.h"
#include "SDL2_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef TARGET_CAN_JKGM
void jkgm_write_png(const char *pFname, int width, int height, uint8_t* paFramebuffer);
#endif

static int platServices_bBatch = 0;
static int platServices_batchShot = 0;
static int platServices_numSaved = 0, platServices_numFailed = 0;
static char platServices_dir[1024];
static const char* platServices_dirShown = ""; // (said in the message)

// The folder the photos go in (made the first time): Pictures/OpenJKDF2/, or
// photos/ next to the game if there is no Pictures folder
static const char* platServices_PhotoDir(void)
{
    if (platServices_dir[0]) return platServices_dir;
    const char* pics = SDL_GetUserFolder(SDL_FOLDER_PICTURES);
    if (pics && pics[0]) {
        snprintf(platServices_dir, sizeof(platServices_dir), "%sOpenJKDF2/", pics);
        platServices_dirShown = "Pictures/OpenJKDF2";
    }
    else {
        snprintf(platServices_dir, sizeof(platServices_dir), "photos/");
        platServices_dirShown = "photos";
    }
    SDL_CreateDirectory(platServices_dir);
    return platServices_dir;
}

// "OpenJKDF2 2026-10-15 18.41.07" (a second capture in the same second: "-2"...)
static void platServices_BaseName(char* pOut, size_t outLen)
{
    static char last[64];
    static int sameSecond = 0;
    char stamp[64];
    time_t now = time(NULL);
    struct tm tmNow;
#ifdef _WIN32
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    strftime(stamp, sizeof(stamp), "OpenJKDF2 %Y-%m-%d %H.%M.%S", &tmNow);
    if (!strcmp(stamp, last)) {
        snprintf(pOut, outLen, "%s-%d", stamp, ++sameSecond + 1);
    }
    else {
        snprintf(last, sizeof(last), "%s", stamp);
        sameSecond = 0;
        snprintf(pOut, outLen, "%s", stamp);
    }
}

// One image (RGBA, top row first) as <dir><name>.png; 1 if written
static int platServices_WritePng(const char* name, const unsigned char* p, int w, int h)
{
#ifdef TARGET_CAN_JKGM
    // (jkgm_write_png takes RGB, bottom row first)
    uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
    if (!rgb) return 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* s = p + ((size_t)(h - 1 - y) * w + x) * 4;
            uint8_t* d = rgb + ((size_t)y * w + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    char path[1400], part[1408];
    snprintf(path, sizeof(path), "%s%s.png", platServices_PhotoDir(), name);
    // (written whole before it appears under its name)
    snprintf(part, sizeof(part), "%s.part", path);
    jkgm_write_png(part, w, h, rgb);
    free(rgb);
    return SDL_RenamePath(part, path) ? 1 : 0;
#else
    (void)name; (void)p; (void)w; (void)h;
    return 0;
#endif
}

// What was saved, once a capture (or a whole batch) is
static void platServices_Report(void)
{
    char msg[128];
    if (platServices_bBatch || (!platServices_numSaved && !platServices_numFailed)) return;
    int n = 0;
    msg[0] = 0;
    if (platServices_numSaved)
        n += snprintf(msg + n, sizeof(msg) - n, "%d photo%s saved to %s", platServices_numSaved, platServices_numSaved == 1 ? "" : "s",
                      platServices_dirShown);
    if (platServices_numFailed && n < (int)sizeof(msg))
        snprintf(msg + n, sizeof(msg) - n, "%s%d couldn't be saved", n ? ", " : "", platServices_numFailed);
    iosGame_ShowMessage(msg);
    platServices_numSaved = platServices_numFailed = 0;
}

void platServices_SetPhotoBatch(int bOn)
{
    platServices_bBatch = bOn != 0;
    if (bOn) platServices_batchShot = 0;
    platServices_Report();
}

void platServices_SavePhotos(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode)
{
    const long bytes = (long)w * h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0));
    char base[96], name[128];
    platServices_BaseName(base, sizeof(base));
    // (numbered: each shot of a burst, each of several selfies)
    const int bNumbered = mode == IOSCAPTURE_MODE_BURST || (mode == IOSCAPTURE_MODE_SELFIE && platServices_bBatch);
    if (mode == IOSCAPTURE_MODE_BURST)
        snprintf(name, sizeof(name), "%s burst %02d", base, ++platServices_batchShot);
    else if (bNumbered)
        snprintf(name, sizeof(name), "%s selfie %d", base, ++platServices_batchShot);
    if (kinds & IOSCAPTURE_CLEAN) {
        if (!bNumbered)
            snprintf(name, sizeof(name), "%s %s", base, mode == IOSCAPTURE_MODE_SELFIE ? "selfie" : "clean");
        if (pClean && platServices_WritePng(name, pClean, w, h)) platServices_numSaved++;
        else platServices_numFailed++;
    }
    // (no on-screen buttons here: the HUD photo stands for both)
    if (kinds & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) {
        snprintf(name, sizeof(name), "%s hud", base);
        if (pHud && platServices_WritePng(name, pHud, w, h)) platServices_numSaved++;
        else platServices_numFailed++;
    }
    free(pClean);
    free(pHud);
    iosCapture_Done(bytes, 0.0);
    platServices_Report();
}

void platServices_Haptic(int kind)
{
    (void)kind;
}

void platServices_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom)
{
    (void)videoW; (void)videoH;
    if (pLeft) *pLeft = 0;
    if (pRight) *pRight = 0;
    if (pBottom) *pBottom = 0;
}

int platServices_GetTopTextRect(int videoW, int videoH, int* pX, int* pY, int* pW)
{
    (void)videoW; (void)videoH; (void)pX; (void)pY; (void)pW;
    return 0;
}

int platServices_IsTouchDevice(void)
{
    return 0;
}

#endif // JK_ENHANCED && !TARGET_IOS

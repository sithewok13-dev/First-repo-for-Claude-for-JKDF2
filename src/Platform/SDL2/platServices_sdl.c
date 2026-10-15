// Added: entire file. The platform services (Platform/platServices.h) away
// from iOS: the photos (the CAMERA button, selfies, the cinematic burst) as
// PNG files in the user's Pictures folder, in OpenJKDF2/, named as the iOS
// apps name them, written on a thread of their own (one at a time, in order)
// and handed back to the capture once a frame (platServices_Tick); no
// haptics; no HUD margins; the game's own message text.

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

// One capture to write: its images (malloc'd, freed once written) and names
typedef struct platServicesJob
{
    unsigned char* aImg[2];
    char aName[2][128];
    int w, h;
    int numFailed;   // (photos asked for and not taken: counted with the rest)
    int numSaved;
    long bytes;      // (held against the capture budget until handed back)
    uint64_t t0;
    struct platServicesJob* pNext;
} platServicesJob;

static SDL_Mutex* platServices_pLock = NULL;
static SDL_Condition* platServices_pWake = NULL;
static SDL_Thread* platServices_pThread = NULL;
static platServicesJob* platServices_pTodo = NULL;  // (oldest first)
static platServicesJob* platServices_pDone = NULL;  // (oldest first)
static int platServices_numPending = 0;             // handed over, not handed back yet
static int platServices_bWriting = 0;               // (the thread is writing one)

static void platServices_Append(platServicesJob** ppList, platServicesJob* pJob)
{
    pJob->pNext = NULL;
    while (*ppList) ppList = &(*ppList)->pNext;
    *ppList = pJob;
}

static void platServices_Write(platServicesJob* pJob)
{
    for (int i = 0; i < 2; i++) {
        if (!pJob->aName[i][0]) continue;
        if (pJob->aImg[i] && platServices_WritePng(pJob->aName[i], pJob->aImg[i], pJob->w, pJob->h)) pJob->numSaved++;
        else pJob->numFailed++;
        free(pJob->aImg[i]);
        pJob->aImg[i] = NULL;
    }
}

static int SDLCALL platServices_Worker(void* unused)
{
    (void)unused;
    for (;;) {
        SDL_LockMutex(platServices_pLock);
        while (!platServices_pTodo) SDL_WaitCondition(platServices_pWake, platServices_pLock);
        platServicesJob* pJob = platServices_pTodo;
        platServices_pTodo = pJob->pNext;
        platServices_bWriting = 1;
        SDL_UnlockMutex(platServices_pLock);
        platServices_Write(pJob);
        SDL_LockMutex(platServices_pLock);
        platServices_Append(&platServices_pDone, pJob);
        platServices_bWriting = 0;
        SDL_UnlockMutex(platServices_pLock);
    }
    return 0;
}

// At exit: the photos already taken are written before the game goes (at
// most some seconds)
static void platServices_FinishWriting(void)
{
    for (int i = 0; i < 200; i++) {
        SDL_LockMutex(platServices_pLock);
        const int bBusy = platServices_pTodo || platServices_bWriting;
        SDL_UnlockMutex(platServices_pLock);
        if (!bBusy) return;
        SDL_Delay(50);
    }
}

// What was saved, once the last capture handed over is (with a batch: at its end)
static void platServices_Report(void)
{
    char msg[128];
    if (platServices_bBatch || platServices_numPending > 0 || (!platServices_numSaved && !platServices_numFailed)) return;
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

// A capture written: counted, and its images' memory given back to the capture
static void platServices_Finish(platServicesJob* pJob)
{
    platServices_numSaved += pJob->numSaved;
    platServices_numFailed += pJob->numFailed;
    if (platServices_numPending > 0) platServices_numPending--;
    iosCapture_Done(pJob->bytes, (double)(SDL_GetTicksNS() - pJob->t0) * 1e-9);
    free(pJob);
    platServices_Report();
}

void platServices_Tick(void)
{
    if (!platServices_pLock) return;
    for (;;) {
        SDL_LockMutex(platServices_pLock);
        platServicesJob* pJob = platServices_pDone;
        if (pJob) platServices_pDone = pJob->pNext;
        SDL_UnlockMutex(platServices_pLock);
        if (!pJob) break;
        platServices_Finish(pJob);
    }
}

void platServices_SetPhotoBatch(int bOn)
{
    platServices_bBatch = bOn != 0;
    if (bOn) platServices_batchShot = 0;
    platServices_Report();
}

void platServices_SavePhotos(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode)
{
    platServicesJob* pJob = (platServicesJob*)calloc(1, sizeof(platServicesJob));
    if (!pJob) {
        free(pClean);
        free(pHud);
        iosCapture_Done((long)w * h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0)), 0.0);
        return;
    }
    pJob->bytes = (long)w * h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0));
    pJob->w = w;
    pJob->h = h;
    pJob->t0 = SDL_GetTicksNS();
    platServices_PhotoDir(); // (found here, on the game's thread)
    char base[96];
    platServices_BaseName(base, sizeof(base));
    // (numbered: each shot of a burst, each of several selfies)
    if (kinds & IOSCAPTURE_CLEAN) {
        if (mode == IOSCAPTURE_MODE_BURST)
            snprintf(pJob->aName[0], sizeof(pJob->aName[0]), "%s burst %02d", base, ++platServices_batchShot);
        else if (mode == IOSCAPTURE_MODE_SELFIE && platServices_bBatch)
            snprintf(pJob->aName[0], sizeof(pJob->aName[0]), "%s selfie %d", base, ++platServices_batchShot);
        else
            snprintf(pJob->aName[0], sizeof(pJob->aName[0]), "%s %s", base, mode == IOSCAPTURE_MODE_SELFIE ? "selfie" : "clean");
        pJob->aImg[0] = pClean;
        pClean = NULL;
    }
    // (no on-screen buttons here: the HUD photo stands for both)
    if (kinds & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) {
        snprintf(pJob->aName[1], sizeof(pJob->aName[1]), "%s hud", base);
        pJob->aImg[1] = pHud;
        pHud = NULL;
    }
    free(pClean); // (taken but not asked for)
    free(pHud);
    platServices_numPending++;
    if (!platServices_pLock) {
        platServices_pLock = SDL_CreateMutex();
        platServices_pWake = SDL_CreateCondition();
        if (platServices_pLock && platServices_pWake)
            platServices_pThread = SDL_CreateThread(platServices_Worker, "photos", NULL);
        if (platServices_pThread)
            atexit(platServices_FinishWriting);
    }
    if (!platServices_pThread) {
        // (no thread: written here, now)
        platServices_Write(pJob);
        platServices_Finish(pJob);
        return;
    }
    SDL_LockMutex(platServices_pLock);
    platServices_Append(&platServices_pTodo, pJob);
    SDL_SignalCondition(platServices_pWake);
    SDL_UnlockMutex(platServices_pLock);
}

void platServices_PhotoModeEnded(int mode)
{
    (void)mode;
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

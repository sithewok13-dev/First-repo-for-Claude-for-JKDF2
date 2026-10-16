// Added: entire file. The platform services (Platform/platServices.h) away
// from iOS: the photos (the CAMERA button, selfies, the cinematic burst) as
// PNG files in the user's Pictures folder, in OpenJKDF2/, named as the iOS
// apps name them, written on a thread of their own (one at a time, in order)
// and handed back to the capture once a frame (platServices_Tick); no
// haptics; no HUD margins; the game's own message text. With the touch
// overlay (TOUCHUI_ENABLED: Android, and the Linux test rig) its haptics, the
// iOS safe-area margins and the message text under its top row.

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
#include <png.h>
#endif

#ifdef TOUCHUI_ENABLED
#include "Platform/Touch/touchUi.h"
#include "Platform/Touch/touchUiSdl.h"
#include "Platform/Touch/touchUiRender_gl.h"
#include "Platform/iOS/iosSafeArea.h"
extern int std3D_windowFbo;
#endif

#ifdef TARGET_ANDROID
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif

static int platServices_bBatch = 0;
static int platServices_batchShot = 0;
static int platServices_numSaved = 0, platServices_numFailed = 0;
static char platServices_dir[1024];
static const char* platServices_dirShown = ""; // (said in the message)
#ifdef TARGET_ANDROID
// Android: the photos go to the device's gallery (MediaStore, Pictures/
// OpenJKDF2) through org.openjkdf2.app.PhotoSaver; where that can't be done
// (Android 9 and older, which would need a permission; the class missing; any
// error), to the app's own Pictures folder instead (its external files,
// Android/data/<app>/files/Pictures/OpenJKDF2, which needs none)
static jclass platServicesAndroid_cls = NULL;
static jmethodID platServicesAndroid_mid = NULL;
static int platServicesAndroid_bLooked = 0;
static volatile int platServicesAndroid_bToFiles = 0; // (a photo went to the app's folder: said in the message)
#endif

// The folder the photos go in (made the first time): Pictures/OpenJKDF2/, or
// photos/ next to the game if there is no Pictures folder
static const char* platServices_PhotoDir(void)
{
    if (platServices_dir[0]) return platServices_dir;
#ifdef TARGET_ANDROID
    {
        const char* ext = SDL_GetAndroidExternalStoragePath();
        if (ext && ext[0]) {
            snprintf(platServices_dir, sizeof(platServices_dir), "%s/Pictures/", ext);
            SDL_CreateDirectory(platServices_dir);
            snprintf(platServices_dir, sizeof(platServices_dir), "%s/Pictures/OpenJKDF2/", ext);
            platServices_dirShown = "Pictures/OpenJKDF2";
            SDL_CreateDirectory(platServices_dir);
            return platServices_dir;
        }
    }
#endif
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

#ifdef TARGET_CAN_JKGM
// libpng's writes go to an SDL stream (UTF-8 paths everywhere, Windows too);
// a short write is an error (a full disk...)
static void platServices_PngWrite(png_structp png, png_bytep data, size_t len)
{
    SDL_IOStream* io = (SDL_IOStream*)png_get_io_ptr(png);
    if (SDL_WriteIO(io, data, len) != len) png_error(png, "write failed");
}

static void platServices_PngFlush(png_structp png)
{
    (void)png;
}

// The image (RGBA, top row first) as a PNG at path; 1 if all of it was
// written. Never aborts: on any error the file is closed and removed.
static int platServices_PngToFile(const char* path, const unsigned char* p, int w, int h)
{
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (!io) return 0;
    png_bytep row = (png_bytep)malloc((size_t)w * 3);
    png_structp png = row ? png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL) : NULL;
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!info) {
        if (png) png_destroy_write_struct(&png, NULL);
        free(row);
        SDL_CloseIO(io);
        SDL_RemovePath(path);
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        free(row);
        SDL_CloseIO(io);
        SDL_RemovePath(path);
        return 0;
    }
    png_set_write_fn(png, io, platServices_PngWrite, platServices_PngFlush);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; y++) {
        const unsigned char* s = p + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = s[x * 4 + 0];
            row[x * 3 + 1] = s[x * 4 + 1];
            row[x * 3 + 2] = s[x * 4 + 2];
        }
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row);
    // (the close flushes: a failure there is a photo not saved)
    if (!SDL_CloseIO(io)) {
        SDL_RemovePath(path);
        return 0;
    }
    return 1;
}
#endif

#if defined(TARGET_ANDROID) && defined(TARGET_CAN_JKGM)
typedef struct { unsigned char* p; size_t len, cap; int bFailed; } platServicesMem;

static void platServices_PngMemWrite(png_structp png, png_bytep data, size_t len)
{
    platServicesMem* m = (platServicesMem*)png_get_io_ptr(png);
    if (m->len + len > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 1 << 20;
        while (cap < m->len + len) cap *= 2;
        unsigned char* q = (unsigned char*)realloc(m->p, cap);
        if (!q) png_error(png, "out of memory");
        m->p = q;
        m->cap = cap;
    }
    memcpy(m->p + m->len, data, len);
    m->len += len;
}

// The image as a PNG in memory (malloc'd, *pLen bytes), NULL on any error
static unsigned char* platServices_PngToMem(const unsigned char* p, int w, int h, size_t* pLen)
{
    platServicesMem m = { NULL, 0, 0, 0 };
    png_bytep row = (png_bytep)malloc((size_t)w * 3);
    png_structp png = row ? png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL) : NULL;
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!info) {
        if (png) png_destroy_write_struct(&png, NULL);
        free(row);
        return NULL;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        free(row);
        free(m.p);
        return NULL;
    }
    png_set_write_fn(png, &m, platServices_PngMemWrite, platServices_PngFlush);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; y++) {
        const unsigned char* src = p + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row);
    *pLen = m.len;
    return m.p;
}

// On the game's thread (which can see the app's classes): PhotoSaver found, once
static void platServicesAndroid_Look(void)
{
    if (platServicesAndroid_bLooked) return;
    platServicesAndroid_bLooked = 1;
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass cls = (*env)->FindClass(env, "org/openjkdf2/app/PhotoSaver");
    if (cls && !(*env)->ExceptionCheck(env)) {
        platServicesAndroid_cls = (jclass)(*env)->NewGlobalRef(env, cls);
        platServicesAndroid_mid = (*env)->GetStaticMethodID(env, platServicesAndroid_cls, "savePng", "(Landroid/content/Context;[BLjava/lang/String;)Z");
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        platServicesAndroid_mid = NULL;
    }
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

// The image into the gallery (MediaStore: Pictures/OpenJKDF2/<name>.png); 1
// if it is there. On the writing thread (SDL attaches it to the VM).
static int platServicesAndroid_ToGallery(const char* name, const unsigned char* p, int w, int h)
{
    if (!platServicesAndroid_mid) return 0;
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return 0;
    size_t len = 0;
    unsigned char* png = platServices_PngToMem(p, w, h, &len);
    if (!png) return 0;
    int bOk = 0;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jbyteArray arr = activity ? (*env)->NewByteArray(env, (jsize)len) : NULL;
    jstring jname = arr ? (*env)->NewStringUTF(env, name) : NULL;
    if (jname) {
        (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte*)png);
        bOk = (*env)->CallStaticBooleanMethod(env, platServicesAndroid_cls, platServicesAndroid_mid, activity, arr, jname) ? 1 : 0;
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        bOk = 0;
    }
    if (jname) (*env)->DeleteLocalRef(env, jname);
    if (arr) (*env)->DeleteLocalRef(env, arr);
    if (activity) (*env)->DeleteLocalRef(env, activity);
    free(png);
    return bOk;
}
#endif

// One image (RGBA, top row first) as <dir><name>.png (or, if that is taken,
// <name>-2.png, -3...: no photo is ever written over); 1 if written
static int platServices_WritePng(const char* name, const unsigned char* p, int w, int h)
{
#if defined(TARGET_ANDROID) && defined(TARGET_CAN_JKGM)
    if (platServicesAndroid_ToGallery(name, p, w, h)) return 1;
    platServicesAndroid_bToFiles = 1;
#endif
#ifdef TARGET_CAN_JKGM
    const char* dir = platServices_PhotoDir();
    SDL_CreateDirectory(dir); // (again each time: it may have been removed meanwhile)
    char path[1400], part[1408];
    snprintf(path, sizeof(path), "%s%s.png", dir, name);
    // (written whole before it appears under its name)
    snprintf(part, sizeof(part), "%s.part", path);
    if (!platServices_PngToFile(part, p, w, h)) return 0;
    for (int k = 2; SDL_GetPathInfo(path, NULL) && k < 1000; k++)
        snprintf(path, sizeof(path), "%s%s-%d.png", dir, name, k);
    if (SDL_GetPathInfo(path, NULL) || !SDL_RenamePath(part, path)) {
        SDL_RemovePath(part);
        return 0;
    }
    return 1;
#else
    (void)name; (void)p; (void)w; (void)h;
    return 0;
#endif
}

// One capture to write: its images (malloc'd, freed once written) and names
typedef struct platServicesJob
{
    unsigned char* aImg[3];  // (clean, HUD, and with the touch overlay HUD+BTNS)
    char aName[3][128];
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
#ifdef TOUCHUI_ENABLED
static platServicesJob* platServicesTouch_pWaiting = NULL; // (its HUD+BTNS photo waits for the overlay to be drawn)
#endif
static void platServices_Hand(platServicesJob* pJob);

static void platServices_Append(platServicesJob** ppList, platServicesJob* pJob)
{
    pJob->pNext = NULL;
    while (*ppList) ppList = &(*ppList)->pNext;
    *ppList = pJob;
}

static void platServices_Write(platServicesJob* pJob)
{
    for (int i = 0; i < 3; i++) {
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
#ifdef TARGET_ANDROID
    if (platServices_numSaved)
        n += snprintf(msg + n, sizeof(msg) - n, "%d photo%s saved to %s", platServices_numSaved, platServices_numSaved == 1 ? "" : "s",
                      platServicesAndroid_bToFiles ? "the app's Pictures/OpenJKDF2" : "Pictures/OpenJKDF2");
    platServicesAndroid_bToFiles = 0;
#else
    if (platServices_numSaved)
        n += snprintf(msg + n, sizeof(msg) - n, "%d photo%s saved to %s", platServices_numSaved, platServices_numSaved == 1 ? "" : "s",
                      platServices_dirShown);
#endif
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
    // (as iosCapture counts it: HUD+BTNS is a third image)
    pJob->bytes = (long)w * h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0) + ((pHud && (kinds & IOSCAPTURE_HUDBTNS)) ? 1 : 0));
    pJob->w = w;
    pJob->h = h;
    pJob->t0 = SDL_GetTicksNS();
    platServices_PhotoDir(); // (found here, on the game's thread)
#if defined(TARGET_ANDROID) && defined(TARGET_CAN_JKGM)
    platServicesAndroid_Look();
#endif
#ifdef TOUCHUI_ENABLED
    // (the flash and the tap: this frame is the photo -- once the buttons are
    // read back, if they are wanted, so it isn't in it)
    if (!((kinds & IOSCAPTURE_HUDBTNS) && pHud)) touchUi_CameraFeedback(mode);
#endif
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
#ifdef TOUCHUI_ENABLED
    // With the touch overlay: the HUD photo if asked for, and HUD+BTNS read
    // back once the overlay is drawn over this frame (platServicesTouch_OverlayDrawn)
    // (the HUD frame was read: the overlay comes next over the same frame)
    const int bWaitBtns = (kinds & IOSCAPTURE_HUDBTNS) && pHud;
    if (kinds & IOSCAPTURE_HUDBTNS) snprintf(pJob->aName[2], sizeof(pJob->aName[2]), "%s hud+buttons", base);
    if (kinds & IOSCAPTURE_HUD) {
        snprintf(pJob->aName[1], sizeof(pJob->aName[1]), "%s hud", base);
        pJob->aImg[1] = pHud;
        pHud = NULL;
    }
#else
    // (no on-screen buttons here: the HUD photo stands for both)
    if (kinds & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) {
        snprintf(pJob->aName[1], sizeof(pJob->aName[1]), "%s hud", base);
        pJob->aImg[1] = pHud;
        pHud = NULL;
    }
#endif
    free(pClean); // (taken but not asked for)
    free(pHud);
    platServices_numPending++;
#ifdef TOUCHUI_ENABLED
    if (bWaitBtns) {
        // (one at a time: a capture waits for the last to be handed over)
        if (platServicesTouch_pWaiting) platServices_Hand(platServicesTouch_pWaiting);
        platServicesTouch_pWaiting = pJob;
        return;
    }
#endif
    platServices_Hand(pJob);
}

// A capture's photos to be written (on the thread, or here if it can't run)
static void platServices_Hand(platServicesJob* pJob)
{
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
#ifdef TOUCHUI_ENABLED
    touchUi_PhotoModeEnded(mode); // (its buttons back)
#endif
    (void)mode;
}

#ifdef TOUCHUI_ENABLED
void platServicesTouch_OverlayDrawn(int bDrawn)
{
    platServicesJob* pJob = platServicesTouch_pWaiting;
    if (!pJob) return;
    platServicesTouch_pWaiting = NULL;
    // (the frame the HUD photo is of, the buttons over it; not drawn: none taken)
    if (bDrawn) pJob->aImg[2] = touchUiRender_ReadRGBA((unsigned int)std3D_windowFbo, pJob->w, pJob->h);
    touchUi_CameraFeedback(IOSCAPTURE_MODE_PHOTO);
    platServices_Hand(pJob);
}
#endif

void platServices_Haptic(int kind)
{
#ifdef TOUCHUI_ENABLED
    touchUiPlat_Haptic(kind == PLATSERVICES_HAPTIC_TICK ? TOUCHUI_HAPTIC_TICK : TOUCHUI_HAPTIC_TAP);
#endif
    (void)kind;
}

void platServices_GetHudMargins(int videoW, int videoH, int* pLeft, int* pRight, int* pBottom)
{
#ifdef TOUCHUI_ENABLED
    iosSafeArea_GetHudMargins(videoW, videoH, pLeft, pRight, pBottom);
    return;
#endif
    (void)videoW; (void)videoH;
    if (pLeft) *pLeft = 0;
    if (pRight) *pRight = 0;
    if (pBottom) *pBottom = 0;
}

int platServices_GetTopTextRect(int videoW, int videoH, int* pX, int* pY, int* pW)
{
#ifdef TOUCHUI_ENABLED
    // (under the touch overlay's top row, as on iOS)
    iosSafeArea_GetTopTextRect(videoW, videoH, pX, pY, pW);
    return 1;
#endif
    (void)videoW; (void)videoH; (void)pX; (void)pY; (void)pW;
    return 0;
}

int platServices_IsTouchDevice(void)
{
#ifdef TOUCHUI_ENABLED
    return 1;
#endif
    return 0;
}

#endif // JK_ENHANCED && !TARGET_IOS

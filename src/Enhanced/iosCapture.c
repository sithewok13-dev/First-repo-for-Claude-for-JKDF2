#include "Enhanced/iosCapture.h"

#ifdef JK_ENHANCED

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "SDL2_helper.h"
#include "Win95/Window.h"
#include "World/sithWorld.h"
#include "Enhanced/iosGame.h"
#include "Platform/platServices.h"

extern GLint std3D_windowFbo;

static int iosCapture_wanted = 0; // the kinds a frame should take (0: none waiting)
static int iosCapture_mode = IOSCAPTURE_MODE_PHOTO;
static long iosCapture_bytesSaving = 0; // handed to iosPhotos, not saved yet
static int iosCapture_numSaving = 0;
static unsigned char* iosCapture_pClean = NULL;
static int iosCapture_w = 0, iosCapture_h = 0;
static iosCaptureStats iosCapture_stats;

static double iosCapture_Now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// One full-screen image now (the window's size)
static long iosCapture_ImageBytes(void)
{
    return (Window_xSize > 0 && Window_ySize > 0) ? (long)Window_xSize * Window_ySize * 4 : 0;
}

// The full-screen images a capture of these kinds holds until it is saved: the
// clean one and the one with the HUD (read back here), and the HUD with the
// buttons drawn on (a third, made by iosPhotos from the HUD one)
static int iosCapture_NumImages(int kinds)
{
    return ((kinds & IOSCAPTURE_CLEAN) ? 1 : 0) + ((kinds & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) ? 1 : 0)
         + ((kinds & IOSCAPTURE_HUDBTNS) ? 1 : 0);
}

int iosCapture_HasRoom(int kinds, int n)
{
    long image = iosCapture_ImageBytes();
    if (image <= 0) return 0;
    return iosCapture_bytesSaving + image * iosCapture_NumImages(kinds & IOSCAPTURE_ALL) * n <= image * IOSCAPTURE_BUDGET_IMAGES;
}

int iosCapture_Request(int kinds, int mode)
{
    kinds &= IOSCAPTURE_ALL;
    if (mode != IOSCAPTURE_MODE_PHOTO) kinds = IOSCAPTURE_CLEAN;
    if (!kinds || iosCapture_wanted || !iosCapture_HasRoom(kinds, 1)) return 0;
    iosCapture_wanted = kinds;
    iosCapture_mode = mode;
    return 1;
}

int iosCapture_IsPending(void)
{
    return iosCapture_wanted != 0;
}

void iosCapture_Cancel(void)
{
    free(iosCapture_pClean);
    iosCapture_pClean = NULL;
    iosCapture_wanted = 0;
}

int iosCapture_IsBusy(void)
{
    return iosCapture_wanted || iosCapture_numSaving;
}

void iosCapture_Done(long bytes, double saveSeconds)
{
    if (iosCapture_numSaving > 0) iosCapture_numSaving--;
    iosCapture_bytesSaving -= bytes;
    if (iosCapture_bytesSaving < 0 || !iosCapture_numSaving) iosCapture_bytesSaving = 0;
    iosCapture_stats.saveSeconds = saveSeconds;
}

void iosCapture_TakeStats(iosCaptureStats* pStats)
{
    if (pStats) *pStats = iosCapture_stats;
    iosCapture_stats.saveSeconds = 0;
    iosCapture_stats.peakBytes = iosCapture_bytesSaving;
    iosCapture_stats.freezeSeconds = 0;
}

void iosCapture_NoteFreeze(double seconds)
{
    iosCapture_stats.freezeSeconds = seconds;
}

// Hands what was taken to iosPhotos
static void iosCapture_Hand(unsigned char* pHud)
{
    unsigned char* pClean = iosCapture_pClean;
    int kinds = iosCapture_wanted;
    iosCapture_pClean = NULL;
    iosCapture_wanted = 0;
    iosCapture_numSaving++;
    // (the same count iosPhotos hands back to iosCapture_Done)
    iosCapture_bytesSaving += (long)iosCapture_w * iosCapture_h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0) + ((pHud && (kinds & IOSCAPTURE_HUDBTNS)) ? 1 : 0));
    if (iosCapture_bytesSaving > iosCapture_stats.peakBytes) iosCapture_stats.peakBytes = iosCapture_bytesSaving;
    platServices_SavePhotos(pClean, pHud, iosCapture_w, iosCapture_h, kinds, iosCapture_mode);
}

// The window as drawn so far, RGBA with the top row first and fully opaque
// (GL_RGBA/GL_UNSIGNED_BYTE: the one read format ANGLE always takes)
static unsigned char* iosCapture_Read(int w, int h)
{
    const double t0 = iosCapture_Now();
    size_t row = (size_t)w * 4;
    unsigned char* p = (unsigned char*)malloc(row * h);
    unsigned char* tmp = (unsigned char*)malloc(row);
    if (!p || !tmp) {
        free(p);
        free(tmp);
        return NULL;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
    if (glGetError() != GL_NO_ERROR) {
        free(p);
        free(tmp);
        return NULL;
    }
    // GL's rows come bottom up
    for (int y = 0; y < h / 2; y++) {
        unsigned char* a = p + row * y;
        unsigned char* b = p + row * (h - 1 - y);
        memcpy(tmp, a, row);
        memcpy(a, b, row);
        memcpy(b, tmp, row);
    }
    free(tmp);
    for (size_t i = 3; i < row * h; i += 4) p[i] = 255;
    iosCapture_stats.readSeconds = iosCapture_Now() - t0;
    return p;
}

void iosCapture_AfterScene(void)
{
    if (!iosCapture_wanted) return;
    // Only a game frame (the overlay asks only during play; this is in case
    // the level ended on the way)
    if (!sithWorld_g_pCurrentWorld || Window_xSize <= 0 || Window_ySize <= 0) {
        iosCapture_Cancel();
        return;
    }
    while (glGetError() != GL_NO_ERROR) {} // not ours
    free(iosCapture_pClean);
    iosCapture_pClean = NULL;
    iosCapture_w = Window_xSize;
    iosCapture_h = Window_ySize;
    if (!(iosCapture_wanted & IOSCAPTURE_CLEAN)) return;
    iosCapture_pClean = iosCapture_Read(iosCapture_w, iosCapture_h);
    if (!iosCapture_pClean) {
        iosCapture_Cancel();
        iosGame_ShowMessage("Couldn't take the photos");
        return;
    }
    // The clean one alone needs nothing more of this frame
    if (iosCapture_wanted == IOSCAPTURE_CLEAN) iosCapture_Hand(NULL);
}

void iosCapture_AfterHud(void)
{
    // (AfterScene ran this frame: the size is set, and the clean one taken
    // if it was wanted)
    if (!(iosCapture_wanted & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) || iosCapture_w <= 0) return;
    if ((iosCapture_wanted & IOSCAPTURE_CLEAN) && !iosCapture_pClean) return;
    unsigned char* pHud = iosCapture_Read(iosCapture_w, iosCapture_h);
    if (!pHud) {
        iosCapture_Cancel();
        iosGame_ShowMessage("Couldn't take the photos");
        return;
    }
    iosCapture_Hand(pHud);
}

#endif // JK_ENHANCED

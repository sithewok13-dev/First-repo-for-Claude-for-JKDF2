#include "Platform/iOS/iosCapture.h"

#ifdef TARGET_IOS

#include <stdlib.h>
#include <string.h>

#include "SDL2_helper.h"
#include "Win95/Window.h"
#include "World/sithWorld.h"
#include "Platform/iOS/iosGame.h"

extern GLint std3D_windowFbo;

static int iosCapture_wanted = 0; // the kinds a frame should take (0: none waiting)
static int iosCapture_mode = IOSCAPTURE_MODE_PHOTO;
static int iosCapture_numSaving = 0; // handed to iosPhotos, not saved yet
static unsigned char* iosCapture_pClean = NULL;
static int iosCapture_w = 0, iosCapture_h = 0;

int iosCapture_Request(int kinds, int mode)
{
    kinds &= IOSCAPTURE_ALL;
    if (mode != IOSCAPTURE_MODE_PHOTO) kinds = IOSCAPTURE_CLEAN;
    if (!kinds || iosCapture_wanted || iosCapture_numSaving >= IOSCAPTURE_MAX_SAVING) return 0;
    iosCapture_wanted = kinds;
    iosCapture_mode = mode;
    return 1;
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

void iosCapture_Done(void)
{
    if (iosCapture_numSaving > 0) iosCapture_numSaving--;
}

// Hands what was taken to iosPhotos
static void iosCapture_Hand(unsigned char* pHud)
{
    unsigned char* pClean = iosCapture_pClean;
    int kinds = iosCapture_wanted;
    iosCapture_pClean = NULL;
    iosCapture_wanted = 0;
    iosCapture_numSaving++;
    iosPhotos_SaveCapture(pClean, pHud, iosCapture_w, iosCapture_h, kinds, iosCapture_mode);
}

// The window as drawn so far, RGBA with the top row first and fully opaque
// (GL_RGBA/GL_UNSIGNED_BYTE: the one read format ANGLE always takes)
static unsigned char* iosCapture_Read(int w, int h)
{
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

#endif // TARGET_IOS

// Added: entire file. The enhanced gameplay's photos (the CAMERA button, selfies,
// the cinematic burst: Platform/iOS/iosCapture) away from iOS, which saves them
// to the Photos library (iosPhotos.m): PNG files in the user's Pictures folder.

#if defined(JK_ENHANCED) && !defined(TARGET_IOS)

#include "Enhanced/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "SDL2_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef TARGET_CAN_JKGM
void jkgm_write_png(const char *pFname, int width, int height, uint8_t* paFramebuffer);
#endif

static int enhPhotos_bBatch = 0;

void iosPhotos_SetBatch(int bOn)
{
    enhPhotos_bBatch = bOn != 0;
}

// (top row first RGBA, as captured -> bottom row first RGB, as jkgm_write_png takes it)
static void enhPhotos_Write(const char* kind, unsigned char* p, int w, int h)
{
#ifdef TARGET_CAN_JKGM
    static int n = 0;
    const char* dir = SDL_GetUserFolder(SDL_FOLDER_PICTURES);
    uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
    if (!rgb) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* s = p + ((size_t)(h - 1 - y) * w + x) * 4;
            uint8_t* d = rgb + ((size_t)y * w + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    char path[1024];
    snprintf(path, sizeof(path), "%sOpenJKDF2_%llu_%d_%s.png", dir ? dir : "", (unsigned long long)SDL_GetTicks(), ++n, kind);
    jkgm_write_png(path, w, h, rgb);
    free(rgb);
#else
    (void)kind; (void)p; (void)w; (void)h;
#endif
}

void iosPhotos_SaveCapture(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int mode)
{
    (void)kinds; (void)mode;
    const long bytes = (long)w * h * 4 * ((pClean ? 1 : 0) + (pHud ? 1 : 0));
    if (pClean) enhPhotos_Write("clean", pClean, w, h);
    if (pHud) enhPhotos_Write("hud", pHud, w, h);
    free(pClean);
    free(pHud);
    iosCapture_Done(bytes, 0.0);
}

#endif // JK_ENHANCED && !TARGET_IOS

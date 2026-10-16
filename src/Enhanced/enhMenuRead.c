// Added: entire file. See enhMenuRead.h.

#include "Enhanced/enhMenuRead.h"

#ifdef JK_ENHANCED

#include <string.h>

#include "General/stdBitmap.h"
#include "Gui/jkGUIRend.h"
#include "Win95/stdDisplay.h"
#include "jk.h"
#include "SDL2_helper.h"

// What is done (flags)
#define ENHMENUREAD_PANELS 1  // a dark panel behind text, boxes and lists
#define ENHMENUREAD_TEXT   2  // text brighter, with a dark outline
#define ENHMENUREAD_ROWBAR 4  // a bar behind a list's chosen row
#ifndef ENHMENUREAD_STYLE
#define ENHMENUREAD_STYLE (ENHMENUREAD_PANELS | ENHMENUREAD_TEXT | ENHMENUREAD_ROWBAR)
#endif

#define ENHMENUREAD_W 640  // the menus' own size (the buffer's top left)
#define ENHMENUREAD_H 480
#define ENHMENUREAD_MAX_MENUS 32
// A text's panel reaches this far past it (normal, strong)
static const int enhMenuRead_aPadX[2] = { 6, 8 };
static const int enhMenuRead_aPadY[2] = { 2, 3 };
#define ENHMENUREAD_TITLE_FONT 5  // this font and up: titles (large*.sft), left as they are

// Two strengths: normal, and strong for the screens where there is the most
// small text (hosting a game: its settings, Advanced). Strong: darker panels,
// all the text near white (a list's chosen row told apart by its bar) and a
// brighter bar.
// The panel: the art under it this bright (out of 256)
static const int enhMenuRead_aDarkLevel[2] = { 64, 22 };
// Strong: the text this near-white
static const int enhMenuRead_aCream[3] = { 255, 244, 222 };
// Text: coloured text goes to this pale gold, when the palette has it (Jedi
// Knight's has); else (Mysteries of the Sith's, all purples) to a paler shade
// of its own colour, this much of the way to white (out of 256). Either way
// with the font's own shading kept. Text already bright (this bright and up: a list's
// chosen row, yellow in Jedi Knight, white in Mysteries of the Sith) goes to
// white, so it still stands apart.
static const int enhMenuRead_aGold[3] = { 255, 214, 120 };
#define ENHMENUREAD_BODY 200  // (a font colour this bright, any channel: its letters' middle, at full)
#define ENHMENUREAD_PALER 150
#define ENHMENUREAD_WHITE_LUM 170
#define ENHMENUREAD_GOLD_NEAR 6000  // (how near the palette's nearest must be, as enhMenuRead_Dist)
// The chosen row's bar: dark gold with the gold text, else purple
static const int enhMenuRead_aBarGold[2][3] = { { 128, 92, 24 }, { 168, 116, 20 } };
static const int enhMenuRead_aBarPurple[2][3] = { { 100, 36, 124 }, { 132, 52, 168 } };

static jkGuiMenu* enhMenuRead_apMenus[ENHMENUREAD_MAX_MENUS];
static uint8_t enhMenuRead_abStrong[ENHMENUREAD_MAX_MENUS];
static int enhMenuRead_numMenus = 0;
static int enhMenuRead_style = ENHMENUREAD_STYLE;

// Palette tables, made again when the palette changes
static rdColor24 enhMenuRead_palette[256];
static int enhMenuRead_bTables = 0;
static uint8_t enhMenuRead_aDark[2][256];
static uint8_t enhMenuRead_aBright[2][256];
static uint8_t enhMenuRead_outline;
static uint8_t enhMenuRead_aBar[2];

// Work buffers
static uint8_t enhMenuRead_aOut[ENHMENUREAD_W * ENHMENUREAD_H];   // what is shown
static uint8_t enhMenuRead_aLast[ENHMENUREAD_W * ENHMENUREAD_H];  // the menu it was made from
static uint8_t enhMenuRead_aIn[ENHMENUREAD_W * ENHMENUREAD_H];    // the menu now
static jkGuiMenu* enhMenuRead_pLastMenu = NULL;
static const uint8_t* enhMenuRead_pLastArt = NULL;
static jkGuiElement* enhMenuRead_pLastFocus = NULL;
static int enhMenuRead_bShown = 0;  // (the last call's result is what is on the screen)
static jkGuiMenu* enhMenuRead_pLastUp = NULL;  // the last menu seen up, marked or not
// Parts drawn afresh every frame (ELEMENT_CUSTOM: the character screen's
// turning model and its saber): shown as they are, and their changing does
// not make the rest be done again
#define ENHMENUREAD_MAX_LIVE 8
typedef struct { int x0, y0, x1, y1; } enhMenuReadBox; // x1, y1 past the end
static enhMenuReadBox enhMenuRead_aLive[ENHMENUREAD_MAX_LIVE];
static int enhMenuRead_numLive = 0;
static uint8_t enhMenuRead_aInk[ENHMENUREAD_W * ENHMENUREAD_H];   // differs from the art
static uint8_t enhMenuRead_aArea[ENHMENUREAD_W * ENHMENUREAD_H];  // which effects apply
static uint8_t enhMenuRead_aOutside[ENHMENUREAD_W * ENHMENUREAD_H]; // not inside a letter (an e's or an o's hole)
static int32_t enhMenuRead_aQueue[ENHMENUREAD_W * ENHMENUREAD_H];
#define AREA_PANEL 1
#define AREA_TEXT  2
#define AREA_BAR   4
#define AREA_BOX   8  // an input box or a list, and its focus frame

static void enhMenuRead_Add(jkGuiMenu* pMenu, int bStrong)
{
    if (!pMenu) return;
    for (int i = 0; i < enhMenuRead_numMenus; i++)
        if (enhMenuRead_apMenus[i] == pMenu) { enhMenuRead_abStrong[i] = (uint8_t)bStrong; return; }
    if (enhMenuRead_numMenus < ENHMENUREAD_MAX_MENUS) {
        enhMenuRead_abStrong[enhMenuRead_numMenus] = (uint8_t)bStrong;
        enhMenuRead_apMenus[enhMenuRead_numMenus++] = pMenu;
    }
}

void enhMenuRead_Mark(jkGuiMenu* pMenu)
{
    enhMenuRead_Add(pMenu, 0);
}

void enhMenuRead_MarkStrong(jkGuiMenu* pMenu)
{
    enhMenuRead_Add(pMenu, 1);
}

// -1 if not marked, else 0 (normal) or 1 (strong)
static int enhMenuRead_Strength(jkGuiMenu* pMenu)
{
    for (int i = 0; i < enhMenuRead_numMenus; i++)
        if (enhMenuRead_apMenus[i] == pMenu) return enhMenuRead_abStrong[i];
    return -1;
}

// How far a palette entry is from a colour (weighted: green counts most, as
// the eye sees it)
static int enhMenuRead_Dist(int i, int r, int g, int b)
{
    const int dr = enhMenuRead_palette[i].r - r, dg = enhMenuRead_palette[i].g - g, db = enhMenuRead_palette[i].b - b;
    return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

// The palette entry nearest a colour (never 0: the see-through one)
static uint8_t enhMenuRead_Nearest(int r, int g, int b)
{
    int best = 1, bestD = 0x7FFFFFFF;
    for (int i = 1; i < 256; i++) {
        const int d = enhMenuRead_Dist(i, r, g, b);
        if (d < bestD) { bestD = d; best = i; }
    }
    return (uint8_t)best;
}

static void enhMenuRead_MakeTables(void)
{
    if (enhMenuRead_bTables && !memcmp(enhMenuRead_palette, stdDisplay_masterPalette, sizeof(enhMenuRead_palette)))
        return;
    memcpy(enhMenuRead_palette, stdDisplay_masterPalette, sizeof(enhMenuRead_palette));
    enhMenuRead_bTables = 1;
    const int bGold = enhMenuRead_Dist(enhMenuRead_Nearest(enhMenuRead_aGold[0], enhMenuRead_aGold[1], enhMenuRead_aGold[2]),
                                       enhMenuRead_aGold[0], enhMenuRead_aGold[1], enhMenuRead_aGold[2]) <= ENHMENUREAD_GOLD_NEAR;
    for (int i = 0; i < 256; i++) {
        const int r = enhMenuRead_palette[i].r, g = enhMenuRead_palette[i].g, b = enhMenuRead_palette[i].b;
        for (int k = 0; k < 2; k++) {
            const int d = enhMenuRead_aDarkLevel[k];
            enhMenuRead_aDark[k][i] = enhMenuRead_Nearest(r * d / 256, g * d / 256, b * d / 256);
        }
        const int lum = (r * 77 + g * 151 + b * 28) >> 8;
        int hi = r > g ? r : g; hi = hi > b ? hi : b;
        // (the fonts' letters are a bright middle shading to a dark edge, and
        // the edge is what keeps the small holes in e, a, o, P... open: the
        // new colour keeps that shading, a font's brightest at full)
        const int t = hi >= ENHMENUREAD_BODY ? 256 : hi * 256 / ENHMENUREAD_BODY;
        if (lum >= ENHMENUREAD_WHITE_LUM) {
            enhMenuRead_aBright[1][i] = enhMenuRead_Nearest(255, 255, 255);
        }
        else {
            enhMenuRead_aBright[1][i] = enhMenuRead_Nearest(enhMenuRead_aCream[0] * t / 256, enhMenuRead_aCream[1] * t / 256, enhMenuRead_aCream[2] * t / 256);
        }
        if (lum >= ENHMENUREAD_WHITE_LUM) {
            enhMenuRead_aBright[0][i] = enhMenuRead_Nearest(255, 250, 235);
        }
        else if (!bGold) {
            // (its own colour at full brightness, paler)
            const int n = hi ? hi : 1;
            const int rn = r * 255 / n, gn = g * 255 / n, bn = b * 255 / n;
            enhMenuRead_aBright[0][i] = enhMenuRead_Nearest((rn + (255 - rn) * ENHMENUREAD_PALER / 256) * t / 256,
                                                            (gn + (255 - gn) * ENHMENUREAD_PALER / 256) * t / 256,
                                                            (bn + (255 - bn) * ENHMENUREAD_PALER / 256) * t / 256);
        }
        else {
            enhMenuRead_aBright[0][i] = enhMenuRead_Nearest(enhMenuRead_aGold[0] * t / 256, enhMenuRead_aGold[1] * t / 256, enhMenuRead_aGold[2] * t / 256);
        }
    }
    enhMenuRead_outline = enhMenuRead_Nearest(0, 0, 0);
    for (int k = 0; k < 2; k++) {
        const int* bar = bGold ? enhMenuRead_aBarGold[k] : enhMenuRead_aBarPurple[k];
        enhMenuRead_aBar[k] = enhMenuRead_Nearest(bar[0], bar[1], bar[2]);
    }
}

static enhMenuReadBox enhMenuRead_Clip(int x, int y, int w, int h)
{
    enhMenuReadBox b = { x, y, x + w, y + h };
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > ENHMENUREAD_W) b.x1 = ENHMENUREAD_W;
    if (b.y1 > ENHMENUREAD_H) b.y1 = ENHMENUREAD_H;
    return b;
}

static void enhMenuRead_SetArea(enhMenuReadBox b, uint8_t flag)
{
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x++)
            enhMenuRead_aArea[y * ENHMENUREAD_W + x] |= flag;
}

// Where in a box something is drawn over the art (not an input box's or a
// list's frame next to it); 0 if nowhere
static int enhMenuRead_InkBounds(enhMenuReadBox b, enhMenuReadBox* pOut)
{
    enhMenuReadBox r = { ENHMENUREAD_W, ENHMENUREAD_H, -1, -1 };
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x++)
            if (enhMenuRead_aInk[y * ENHMENUREAD_W + x] && !(enhMenuRead_aArea[y * ENHMENUREAD_W + x] & AREA_BOX)) {
                if (x < r.x0) r.x0 = x;
                if (y < r.y0) r.y0 = y;
                if (x >= r.x1) r.x1 = x + 1;
                if (y >= r.y1) r.y1 = y + 1;
            }
    if (r.x1 < 0) return 0;
    *pOut = r;
    return 1;
}

// Which effects go where, from the menu's elements
static void enhMenuRead_Areas(jkGuiMenu* pMenu, int k)
{
    memset(enhMenuRead_aArea, 0, sizeof(enhMenuRead_aArea));
    // (boxes and lists first: text next to them is found without their frames)
    for (int pass = 0; pass < 2; pass++)
    for (jkGuiElement* pEl = pMenu->paElements; pEl && pEl->type != ELEMENT_END; pEl++) {
        if (!pEl->bIsVisible) continue;
        const rdRect* r = &pEl->rect;
        if (r->width <= 0 || r->height <= 0) continue;
        const int bBox = pEl->type == ELEMENT_TEXTBOX || pEl->type == ELEMENT_LISTBOX;
        if (bBox != (pass == 0)) continue;
        const enhMenuReadBox box = enhMenuRead_Clip(r->x, r->y, r->width, r->height);
        switch (pEl->type) {
        case ELEMENT_TEXTBOX:
        case ELEMENT_LISTBOX:
            enhMenuRead_SetArea(box, AREA_PANEL | AREA_TEXT);
            enhMenuRead_SetArea(enhMenuRead_Clip(r->x - 3, r->y - 3, r->width + 6, r->height + 6), AREA_BOX);
            if (pEl->type == ELEMENT_LISTBOX && pEl->texInfo.textHeight > 0) {
                // The chosen row, where jkGuiRend_ListBoxDraw puts it
                const jkGuiTexInfo* t = &pEl->texInfo;
                int last = t->textScrollY + t->maxTextEntries - 1;
                int y = r->y + 3;
                if (t->numTextEntries > t->maxTextEntries) {
                    last = t->textScrollY + t->maxTextEntries - 3;
                    y += t->textHeight;
                }
                if (last > t->numTextEntries - 1) last = t->numTextEntries - 1;
                const int sel = pEl->selectedTextEntry;
                if (sel >= t->textScrollY && sel <= last) {
                    y += (sel - t->textScrollY) * t->textHeight;
                    enhMenuRead_SetArea(enhMenuRead_Clip(r->x + 2, y - 1, r->width - 4, t->textHeight + 1), AREA_BAR);
                }
            }
            break;
        case ELEMENT_TEXT:
        case ELEMENT_TEXTBUTTON:
        case ELEMENT_CHECKBOX: {
            if (pEl->type != ELEMENT_CHECKBOX && pEl->textType >= ENHMENUREAD_TITLE_FONT) break;
            enhMenuReadBox ink;
            if (!enhMenuRead_InkBounds(box, &ink)) break;
            enhMenuReadBox textBox = box;
            if (pEl->type == ELEMENT_CHECKBOX) {
                // (the box itself, left of its words, as it is: ticked or not)
                stdBitmap* pCheck = pMenu->ui_structs ? pMenu->ui_structs[pMenu->checkboxBitmapIdx] : NULL;
                if (pCheck && pCheck->mipSurfaces && pCheck->mipSurfaces[0])
                    textBox.x0 = enhMenuRead_Clip(r->x + pCheck->mipSurfaces[0]->format.width + 2, r->y, 0, 0).x0;
            }
            enhMenuRead_SetArea(textBox, AREA_TEXT);
            const int px = enhMenuRead_aPadX[k], py = enhMenuRead_aPadY[k];
            // (strong: the button or checkbox in focus -- the mouse's, a pad's
            // or the keyboard's, which the game shows in another font colour
            // that the near-white text would hide -- on the bar's colour)
            const int bFocus = k && pEl == pMenu->lastMouseOverClickable && pEl->type != ELEMENT_TEXT;
            enhMenuRead_SetArea(enhMenuRead_Clip(ink.x0 - px, ink.y0 - py, ink.x1 - ink.x0 + 2 * px, ink.y1 - ink.y0 + 2 * py),
                                bFocus ? (AREA_PANEL | AREA_BAR) : AREA_PANEL);
            break;
        }
        default:
            break;
        }
    }
}

void enhMenuRead_Invalidate(void)
{
    enhMenuRead_bShown = 0;
}

static const uint8_t* enhMenuRead_Make(jkGuiMenu* pMenu, const uint8_t* pSrc, int w, int h, int pitch, int* pbChanged)
{
    *pbChanged = 1;
    if (!pSrc || !enhMenuRead_style || w < ENHMENUREAD_W || h < ENHMENUREAD_H || pitch < w) return NULL;
    if (jkGame_isDDraw || jkCutscene_isRendering) return NULL;
    if (!pMenu) return NULL;
    const int k = enhMenuRead_Strength(pMenu);
    if (k < 0) return NULL;

    // The art the menu is drawn on
#ifdef STDBITMAP_PARTIAL_LOAD
    tVBuffer* pBg = (pMenu->pBgBitmap && pMenu->pBgBitmap->mipSurfaces) ? pMenu->pBgBitmap->mipSurfaces[0] : pMenu->pTextureOverride;
#else
    tVBuffer* pBg = pMenu->texture;
#endif
    if (!pBg || !pBg->sdlSurface || !pBg->sdlSurface->pixels) return NULL;
    if (pBg->format.width < ENHMENUREAD_W || pBg->format.height < ENHMENUREAD_H) return NULL;
    const uint8_t* pArt = (const uint8_t*)pBg->sdlSurface->pixels;
    const int artPitch = pBg->format.rowSize;

    for (int y = 0; y < ENHMENUREAD_H; y++)
        memcpy(enhMenuRead_aIn + y * ENHMENUREAD_W, pSrc + (size_t)y * pitch, ENHMENUREAD_W);

    // The parts drawn afresh every frame
    enhMenuRead_numLive = 0;
    for (jkGuiElement* pEl = pMenu->paElements; pEl && pEl->type != ELEMENT_END; pEl++) {
        if (pEl->type != ELEMENT_CUSTOM || !pEl->bIsVisible || enhMenuRead_numLive >= ENHMENUREAD_MAX_LIVE) continue;
        const enhMenuReadBox b = enhMenuRead_Clip(pEl->rect.x, pEl->rect.y, pEl->rect.width, pEl->rect.height);
        if (b.x1 > b.x0 && b.y1 > b.y0) enhMenuRead_aLive[enhMenuRead_numLive++] = b;
    }

    // (the same picture as last time, but for those parts: the same result,
    // with them as they are now)
    int bSame = pMenu == enhMenuRead_pLastMenu && pArt == enhMenuRead_pLastArt && pMenu->lastMouseOverClickable == enhMenuRead_pLastFocus
             && !memcmp(enhMenuRead_palette, stdDisplay_masterPalette, sizeof(enhMenuRead_palette));
    if (bSame) {
        int bLiveChanged = 0;
        for (int i = 0; i < enhMenuRead_numLive; i++) {
            const enhMenuReadBox b = enhMenuRead_aLive[i];
            for (int y = b.y0; y < b.y1; y++) {
                uint8_t* in = enhMenuRead_aIn + y * ENHMENUREAD_W + b.x0;
                uint8_t* last = enhMenuRead_aLast + y * ENHMENUREAD_W + b.x0;
                if (memcmp(in, last, b.x1 - b.x0)) { bLiveChanged = 1; memcpy(last, in, b.x1 - b.x0); }
            }
        }
        if (!memcmp(enhMenuRead_aIn, enhMenuRead_aLast, sizeof(enhMenuRead_aIn))) {
            for (int i = 0; i < enhMenuRead_numLive && bLiveChanged; i++) {
                const enhMenuReadBox b = enhMenuRead_aLive[i];
                for (int y = b.y0; y < b.y1; y++)
                    memcpy(enhMenuRead_aOut + y * ENHMENUREAD_W + b.x0, enhMenuRead_aIn + y * ENHMENUREAD_W + b.x0, b.x1 - b.x0);
            }
            *pbChanged = bLiveChanged;
            return enhMenuRead_aOut;
        }
    }
    memcpy(enhMenuRead_aLast, enhMenuRead_aIn, sizeof(enhMenuRead_aLast));
    enhMenuRead_pLastMenu = pMenu;
    enhMenuRead_pLastArt = pArt;
    enhMenuRead_pLastFocus = pMenu->lastMouseOverClickable;
    enhMenuRead_MakeTables();

    const uint8_t* pIn = enhMenuRead_aLast;
    for (int y = 0; y < ENHMENUREAD_H; y++) {
        const uint8_t* s = pIn + y * ENHMENUREAD_W;
        const uint8_t* a = pArt + (size_t)y * artPitch;
        uint8_t* ink = enhMenuRead_aInk + y * ENHMENUREAD_W;
        for (int x = 0; x < ENHMENUREAD_W; x++)
            ink[x] = s[x] != a[x];
    }
    enhMenuRead_Areas(pMenu, k);

    // What is outside the letters: reached, without crossing a letter (side
    // to side or up and down), from where nothing is drawn within 2 pixels
    // (more than any hole in the small fonts). The outline goes only there,
    // so the holes inside letters are never filled.
    {
        // (nothing drawn within 2 side to side, then within 2 up and down)
        for (int y = 0; y < ENHMENUREAD_H; y++) {
            const uint8_t* ink = enhMenuRead_aInk + y * ENHMENUREAD_W;
            uint8_t* o = enhMenuRead_aOutside + y * ENHMENUREAD_W;
            for (int x = 0; x < ENHMENUREAD_W; x++) {
                int bNear = 0;
                for (int dx = -2; dx <= 2 && !bNear; dx++)
                    if (x + dx >= 0 && x + dx < ENHMENUREAD_W && ink[x + dx]) bNear = 1;
                o[x] = !bNear;
            }
        }
        int n = 0;
        for (int y = 0; y < ENHMENUREAD_H; y++)
            for (int x = 0; x < ENHMENUREAD_W; x++) {
                int bFar = 1;
                for (int dy = -2; dy <= 2 && bFar; dy++)
                    if (y + dy >= 0 && y + dy < ENHMENUREAD_H && !enhMenuRead_aOutside[(y + dy) * ENHMENUREAD_W + x]) bFar = 0;
                if (bFar) enhMenuRead_aQueue[n++] = y * ENHMENUREAD_W + x;
            }
        memset(enhMenuRead_aOutside, 0, sizeof(enhMenuRead_aOutside));
        for (int q = 0; q < n; q++) enhMenuRead_aOutside[enhMenuRead_aQueue[q]] = 1;
        for (int q = 0; q < n; q++) {
            const int i = enhMenuRead_aQueue[q], x = i % ENHMENUREAD_W, y = i / ENHMENUREAD_W;
            const int an[4] = { x > 0 ? i - 1 : -1, x < ENHMENUREAD_W - 1 ? i + 1 : -1,
                                y > 0 ? i - ENHMENUREAD_W : -1, y < ENHMENUREAD_H - 1 ? i + ENHMENUREAD_W : -1 };
            for (int j = 0; j < 4; j++) {
                const int m = an[j];
                if (m < 0 || enhMenuRead_aOutside[m] || enhMenuRead_aInk[m]) continue;
                enhMenuRead_aOutside[m] = 1;
                enhMenuRead_aQueue[n++] = m;
            }
        }
    }
    memcpy(enhMenuRead_aOut, pIn, sizeof(enhMenuRead_aOut));

    const int style = enhMenuRead_style;
    for (int y = 0; y < ENHMENUREAD_H; y++) {
        const uint8_t* s = pIn + y * ENHMENUREAD_W;
        const uint8_t* a = pArt + (size_t)y * artPitch;
        uint8_t* o = enhMenuRead_aOut + y * ENHMENUREAD_W;
        const uint8_t* ink = enhMenuRead_aInk + y * ENHMENUREAD_W;
        const uint8_t* area = enhMenuRead_aArea + y * ENHMENUREAD_W;
        for (int x = 0; x < ENHMENUREAD_W; x++) {
            const uint8_t f = area[x];
            if (!f) continue;
            if (ink[x]) {
                if ((style & ENHMENUREAD_TEXT) && (f & AREA_TEXT))
                    o[x] = enhMenuRead_aBright[k][s[x]];
                continue;
            }
            // The art: an outline round text, the chosen row's bar, a panel
            if ((style & ENHMENUREAD_TEXT) && (f & AREA_TEXT) && enhMenuRead_aOutside[y * ENHMENUREAD_W + x]) {
                int bNear = 0;
                for (int dy = -1; dy <= 1 && !bNear; dy++) {
                    const int yy = y + dy;
                    if (yy < 0 || yy >= ENHMENUREAD_H) continue;
                    for (int dx = -1; dx <= 1; dx++) {
                        const int xx = x + dx;
                        if (xx < 0 || xx >= ENHMENUREAD_W) continue;
                        const int i = yy * ENHMENUREAD_W + xx;
                        if (enhMenuRead_aInk[i] && (enhMenuRead_aArea[i] & AREA_TEXT)) { bNear = 1; break; }
                    }
                }
                if (bNear) { o[x] = enhMenuRead_outline; continue; }
            }
            if ((style & ENHMENUREAD_ROWBAR) && (f & AREA_BAR))
                o[x] = enhMenuRead_aBar[k];
            else if ((style & ENHMENUREAD_PANELS) && (f & AREA_PANEL))
                o[x] = enhMenuRead_aDark[k][a[x]];
        }
    }
    return enhMenuRead_aOut;
}

const uint8_t* enhMenuRead_Filter(const uint8_t* pSrc, int w, int h, int pitch, int* pbChanged)
{
    int bChanged;
    jkGuiMenu* pMenu = jkGuiRend_GetActiveMenu();
    if (pMenu) enhMenuRead_pLastUp = pMenu;
    const uint8_t* pOut = enhMenuRead_Make(pMenu, pSrc, w, h, pitch, &bChanged);
    if (pbChanged) *pbChanged = bChanged || !enhMenuRead_bShown;
    enhMenuRead_bShown = pOut != NULL;
    return pOut;
}

void enhMenuRead_CopyShown(tVBuffer* pDst, tVBuffer* pMenuBuf)
{
    if (!pDst || !pDst->sdlSurface || !pDst->sdlSurface->pixels) return;
    if (!pMenuBuf || !pMenuBuf->sdlSurface || !pMenuBuf->sdlSurface->pixels) return;
    if (pDst->format.width < ENHMENUREAD_W || pDst->format.height < ENHMENUREAD_H) return;
    // (the menu up, or -- a dialog can come between menus, with the last
    // one still on the screen -- that one, if it was a marked one)
    jkGuiMenu* pMenu = jkGuiRend_GetActiveMenu();
    if (!pMenu)
        pMenu = enhMenuRead_pLastUp;
    int bChanged;
    const uint8_t* p = enhMenuRead_Make(pMenu, (const uint8_t*)pMenuBuf->sdlSurface->pixels, pMenuBuf->format.width, pMenuBuf->format.height,
                                        pMenuBuf->sdlSurface->pitch, &bChanged);
    if (!p) return;
    uint8_t* d = (uint8_t*)pDst->sdlSurface->pixels;
    for (int y = 0; y < ENHMENUREAD_H; y++)
        memcpy(d + (size_t)y * pDst->format.rowSize, p + y * ENHMENUREAD_W, ENHMENUREAD_W);
}

#endif // JK_ENHANCED

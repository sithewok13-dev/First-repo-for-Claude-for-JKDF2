// Added: entire file. See enhMenuRead.h.

#include "Enhanced/enhMenuRead.h"

#ifdef JK_ENHANCED

#include <string.h>

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
#define ENHMENUREAD_PAD_X 6  // a text's panel reaches this far past it
#define ENHMENUREAD_PAD_Y 2
#define ENHMENUREAD_TITLE_FONT 5  // this font and up: titles (large*.sft), left as they are

// The panel: the art under it this bright (out of 256)
#define ENHMENUREAD_DARK 64
// Text: coloured text goes to this pale gold, at least this bright (out of
// 256), when the palette has it (Jedi Knight's has); else (Mysteries of the
// Sith's, all purples) to a paler shade of its own colour, this much of the way
// to white (out of 256). Text already bright (this bright and up: a list's
// chosen row, yellow in Jedi Knight, white in Mysteries of the Sith) goes to
// white, so it still stands apart.
static const int enhMenuRead_aGold[3] = { 255, 214, 120 };
#define ENHMENUREAD_TEXT_MIN 176
#define ENHMENUREAD_PALER 150
#define ENHMENUREAD_WHITE_LUM 170
#define ENHMENUREAD_GOLD_NEAR 6000  // (how near the palette's nearest must be, as enhMenuRead_Dist)
// The chosen row's bar: dark gold with the gold text, else purple
static const int enhMenuRead_aBarGold[3] = { 128, 92, 24 };
static const int enhMenuRead_aBarPurple[3] = { 100, 36, 124 };

static jkGuiMenu* enhMenuRead_apMenus[ENHMENUREAD_MAX_MENUS];
static int enhMenuRead_numMenus = 0;
static int enhMenuRead_style = ENHMENUREAD_STYLE;

// Palette tables, made again when the palette changes
static rdColor24 enhMenuRead_palette[256];
static int enhMenuRead_bTables = 0;
static uint8_t enhMenuRead_aDark[256];
static uint8_t enhMenuRead_aBright[256];
static uint8_t enhMenuRead_outline;
static uint8_t enhMenuRead_bar;

// Work buffers
static uint8_t enhMenuRead_aOut[ENHMENUREAD_W * ENHMENUREAD_H];   // what is shown
static uint8_t enhMenuRead_aLast[ENHMENUREAD_W * ENHMENUREAD_H];  // the menu it was made from
static jkGuiMenu* enhMenuRead_pLastMenu = NULL;
static const uint8_t* enhMenuRead_pLastArt = NULL;
static uint8_t enhMenuRead_aInk[ENHMENUREAD_W * ENHMENUREAD_H];   // differs from the art
static uint8_t enhMenuRead_aArea[ENHMENUREAD_W * ENHMENUREAD_H];  // which effects apply
#define AREA_PANEL 1
#define AREA_TEXT  2
#define AREA_BAR   4
#define AREA_BOX   8  // an input box or a list, and its focus frame

void enhMenuRead_Mark(jkGuiMenu* pMenu)
{
    if (!pMenu) return;
    for (int i = 0; i < enhMenuRead_numMenus; i++)
        if (enhMenuRead_apMenus[i] == pMenu) return;
    if (enhMenuRead_numMenus < ENHMENUREAD_MAX_MENUS)
        enhMenuRead_apMenus[enhMenuRead_numMenus++] = pMenu;
}

static int enhMenuRead_IsMarked(jkGuiMenu* pMenu)
{
    for (int i = 0; i < enhMenuRead_numMenus; i++)
        if (enhMenuRead_apMenus[i] == pMenu) return 1;
    return 0;
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
        enhMenuRead_aDark[i] = enhMenuRead_Nearest(r * ENHMENUREAD_DARK / 256, g * ENHMENUREAD_DARK / 256, b * ENHMENUREAD_DARK / 256);
        const int lum = (r * 77 + g * 151 + b * 28) >> 8;
        if (lum >= ENHMENUREAD_WHITE_LUM) {
            enhMenuRead_aBright[i] = enhMenuRead_Nearest(255, 250, 235);
        }
        else if (!bGold) {
            enhMenuRead_aBright[i] = enhMenuRead_Nearest(r + (255 - r) * ENHMENUREAD_PALER / 256, g + (255 - g) * ENHMENUREAD_PALER / 256,
                                                         b + (255 - b) * ENHMENUREAD_PALER / 256);
        }
        else {
            int hi = r > g ? r : g; hi = hi > b ? hi : b;
            int v = ENHMENUREAD_TEXT_MIN + hi * (256 - ENHMENUREAD_TEXT_MIN) / 256;
            enhMenuRead_aBright[i] = enhMenuRead_Nearest(enhMenuRead_aGold[0] * v / 256, enhMenuRead_aGold[1] * v / 256, enhMenuRead_aGold[2] * v / 256);
        }
    }
    enhMenuRead_outline = enhMenuRead_Nearest(0, 0, 0);
    const int* bar = bGold ? enhMenuRead_aBarGold : enhMenuRead_aBarPurple;
    enhMenuRead_bar = enhMenuRead_Nearest(bar[0], bar[1], bar[2]);
}

typedef struct { int x0, y0, x1, y1; } enhMenuReadBox; // x1, y1 past the end

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
static void enhMenuRead_Areas(jkGuiMenu* pMenu)
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
            enhMenuRead_SetArea(box, AREA_TEXT);
            enhMenuRead_SetArea(enhMenuRead_Clip(ink.x0 - ENHMENUREAD_PAD_X, ink.y0 - ENHMENUREAD_PAD_Y,
                                                 ink.x1 - ink.x0 + 2 * ENHMENUREAD_PAD_X, ink.y1 - ink.y0 + 2 * ENHMENUREAD_PAD_Y), AREA_PANEL);
            break;
        }
        default:
            break;
        }
    }
}

const uint8_t* enhMenuRead_Filter(const uint8_t* pSrc, int w, int h, int pitch)
{
    if (!pSrc || !enhMenuRead_style || w < ENHMENUREAD_W || h < ENHMENUREAD_H || pitch < w) return NULL;
    if (jkGame_isDDraw || jkCutscene_isRendering) return NULL;
    jkGuiMenu* pMenu = jkGuiRend_GetActiveMenu();
    if (!pMenu || !enhMenuRead_IsMarked(pMenu)) return NULL;

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

    // (the same picture as last time: the same result)
    int bSame = pMenu == enhMenuRead_pLastMenu && pArt == enhMenuRead_pLastArt
             && !memcmp(enhMenuRead_palette, stdDisplay_masterPalette, sizeof(enhMenuRead_palette));
    for (int y = 0; y < ENHMENUREAD_H; y++) {
        const uint8_t* s = pSrc + (size_t)y * pitch;
        uint8_t* last = enhMenuRead_aLast + y * ENHMENUREAD_W;
        if (bSame && memcmp(last, s, ENHMENUREAD_W)) bSame = 0;
        if (!bSame) memcpy(last, s, ENHMENUREAD_W);
    }
    if (bSame) return enhMenuRead_aOut;
    enhMenuRead_pLastMenu = pMenu;
    enhMenuRead_pLastArt = pArt;
    enhMenuRead_MakeTables();

    const uint8_t* pIn = enhMenuRead_aLast;
    for (int y = 0; y < ENHMENUREAD_H; y++) {
        const uint8_t* s = pIn + y * ENHMENUREAD_W;
        const uint8_t* a = pArt + (size_t)y * artPitch;
        uint8_t* ink = enhMenuRead_aInk + y * ENHMENUREAD_W;
        for (int x = 0; x < ENHMENUREAD_W; x++)
            ink[x] = s[x] != a[x];
    }
    enhMenuRead_Areas(pMenu);
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
                    o[x] = enhMenuRead_aBright[s[x]];
                continue;
            }
            // The art: an outline round text, the chosen row's bar, a panel
            if ((style & ENHMENUREAD_TEXT) && (f & AREA_TEXT)) {
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
                o[x] = enhMenuRead_bar;
            else if ((style & ENHMENUREAD_PANELS) && (f & AREA_PANEL))
                o[x] = enhMenuRead_aDark[a[x]];
        }
    }
    return enhMenuRead_aOut;
}

#endif // JK_ENHANCED

// Added: entire file. The touch overlay's draw list: what the core
// (touchUi.c) wants drawn this frame, as a list of simple shapes in points,
// y down, in drawing order; the renderer (touchUiRender_gl.c) turns them into
// pixels. Plain data, so the tests can check it without GL.

#ifndef _PLATFORM_TOUCH_TOUCHUIDRAW_H
#define _PLATFORM_TOUCH_TOUCHUIDRAW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float r, g, b, a; } touchUiColor; // straight (not premultiplied) alpha

enum {
    TOUCHUI_DRAW_CIRCLE = 1, // centre (x0, y0), radius r; fill, then a border lw wide inside the edge
    TOUCHUI_DRAW_ARC,        // a stroke lw wide on the circle (x0, y0, r), from a0 (radians clockwise from
                             // straight up) round clockwise for sweep (2 pi: all round); round caps if bRound;
                             // blur > 0: soft, fading out over that many points (a glow)
    TOUCHUI_DRAW_WEDGE,      // a ring's slice: centre (x0, y0), radii r0..r, angles a0..a1 (degrees
                             // counter-clockwise from pointing right); fill, then a stroke lw wide centred on its edge
    TOUCHUI_DRAW_LINE,       // (x0, y0) to (x1, y1), lw wide, round ends
    TOUCHUI_DRAW_RRECT,      // (x0, y0) top left, w x h, corner radius r; fill, then a border lw wide inside
    TOUCHUI_DRAW_TRI,        // a filled triangle (x0, y0) (x1, y1) (x2, y2); edges not flagged in
                             // edgeMask (bit i: the edge opposite corner i) are inside a bigger shape: no smoothing there
    TOUCHUI_DRAW_TEXT,       // text (in the list's string pool) centred on (x0, y0), size points, each
                             // glyph's width times xscale; lines split at '\n'
};

typedef struct {
    int type;
    float x0, y0, x1, y1, x2, y2;
    float r, r0, lw, blur;
    float a0, a1, sweep;
    int bRound;
    int edgeMask;
    touchUiColor fill, stroke;
    int text;     // offset in the pool
    float size, xscale;
} touchUiDrawItem;

#define TOUCHUI_DRAW_MAX_ITEMS 3072
#define TOUCHUI_DRAW_POOL 16384

typedef struct {
    touchUiDrawItem aItems[TOUCHUI_DRAW_MAX_ITEMS];
    int numItems;
    char aPool[TOUCHUI_DRAW_POOL];
    int poolLen;
    float alpha; // multiplies every colour added (a button's fade)
} touchUiDrawList;

static inline touchUiColor touchUi_Rgba(float r, float g, float b, float a) { touchUiColor c = { r, g, b, a }; return c; }
static inline touchUiColor touchUi_White(float a) { return touchUi_Rgba(1.0f, 1.0f, 1.0f, a); }
static inline touchUiColor touchUi_Black(float a) { return touchUi_Rgba(0.0f, 0.0f, 0.0f, a); }
static inline touchUiColor touchUi_Clear(void) { return touchUi_Rgba(0.0f, 0.0f, 0.0f, 0.0f); }

void touchUiDraw_Begin(touchUiDrawList* l);
void touchUiDraw_Circle(touchUiDrawList* l, float x, float y, float r, touchUiColor fill, float lw, touchUiColor border);
void touchUiDraw_Arc(touchUiDrawList* l, float x, float y, float r, float lw, float a0, float sweep, int bRound, touchUiColor c);
void touchUiDraw_Glow(touchUiDrawList* l, float x, float y, float r, float lw, float a0, float sweep, float blur, touchUiColor c);
void touchUiDraw_Wedge(touchUiDrawList* l, float x, float y, float deg0, float deg1, float r0, float r1,
                       touchUiColor fill, float lw, touchUiColor stroke);
void touchUiDraw_Line(touchUiDrawList* l, float x0, float y0, float x1, float y1, float lw, touchUiColor c);
void touchUiDraw_RRect(touchUiDrawList* l, float x, float y, float w, float h, float r, touchUiColor fill, float lw, touchUiColor border);
void touchUiDraw_Tri(touchUiDrawList* l, float x0, float y0, float x1, float y1, float x2, float y2, int edgeMask, touchUiColor c);
// A simple polygon (n points, either way round), filled
void touchUiDraw_Polygon(touchUiDrawList* l, const float* aXY, int n, touchUiColor c);
void touchUiDraw_Text(touchUiDrawList* l, const char* text, float x, float y, float size, float xscale, touchUiColor c);

// Text measuring (the label font's metrics: touchUiFont.h). Width of the
// widest line and height of all the lines, at size points.
void touchUiDraw_MeasureText(const char* text, float size, float xscale, float* pW, float* pH);
// The height of one line of text, at size points (as UIKit's labels have it)
float touchUiDraw_LineHeight(float size);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_TOUCH_TOUCHUIDRAW_H

// Added: entire file. See touchUiRender_gl.h.

#include "Platform/Touch/touchUiRender_gl.h"

#ifdef TOUCHUI_ENABLED

#include "SDL2_helper.h"
#include "Platform/GL/shader_utils.h"
#include "stdPlatform.h"

#define TOUCHUI_FONT_ATLAS
#include "Platform/Touch/touchUiFont.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The shapes, by their distance to their edge (in pixels): a pixel half in
// is half covered
enum { TR_CIRCLE = 1, TR_ARC, TR_WEDGE, TR_LINE, TR_RRECT, TR_TRI, TR_GLYPH };

static const char* tr_vs =
    "in vec2 a_pos;\n"
    "in vec2 a_local;\n"
    "in float a_kind;\n"
    "in vec4 a_a;\n"
    "in vec4 a_b;\n"
    "in vec4 a_fill;\n"
    "in vec4 a_stroke;\n"
    "uniform vec2 u_size;\n"
    "out vec2 v_local;\n"
    "out float v_kind;\n"
    "out vec4 v_a;\n"
    "out vec4 v_b;\n"
    "out vec4 v_fill;\n"
    "out vec4 v_stroke;\n"
    "void main() {\n"
    "    v_local = a_local; v_kind = a_kind; v_a = a_a; v_b = a_b; v_fill = a_fill; v_stroke = a_stroke;\n"
    "    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char* tr_fs =
    "uniform sampler2D u_tex;\n"
    "in vec2 v_local;\n"
    "in float v_kind;\n"
    "in vec4 v_a;\n"
    "in vec4 v_b;\n"
    "in vec4 v_fill;\n"
    "in vec4 v_stroke;\n"
    "out vec4 fragColor;\n"
    "float cov(float d) { return clamp(0.5 - d, 0.0, 1.0); }\n"
    // fill, then a border lw wide inside the edge, over it
    "vec4 filled(float d, float lw, vec4 fill, vec4 stroke) {\n"
    "    float inside = cov(d);\n"
    "    float b = max(inside - cov(d + lw), 0.0);\n"
    "    vec4 c = fill * inside;\n"
    "    return c * (1.0 - b * stroke.a) + stroke * b;\n"
    "}\n"
    "void main() {\n"
    "    int k = int(v_kind + 0.5);\n"
    "    vec4 fill = vec4(v_fill.rgb * v_fill.a, v_fill.a);\n"
    "    vec4 stroke = vec4(v_stroke.rgb * v_stroke.a, v_stroke.a);\n"
    "    vec4 col = vec4(0.0);\n"
    "    vec2 p = v_local;\n"
    "    if (k == 1) {\n" // circle: a = (radius, border)
    "        col = filled(length(p) - v_a.x, v_a.y, fill, stroke);\n"
    "    } else if (k == 2) {\n" // arc: a = (radius, width, sweep, round), b = (blur, start)
    "        float R = v_a.x, hw = v_a.y * 0.5, sweep = v_a.z, blur = v_b.x, a0 = v_b.y;\n"
    "        float dr = abs(length(p) - R) - hw;\n"
    "        float d = dr;\n"
    "        if (sweep < 6.2831) {\n"
    "            float ang = mod(atan(p.x, -p.y) - a0 + 12.566370, 6.2831853);\n"
    "            if (ang > sweep + (6.2831853 - sweep) * 0.5) ang -= 6.2831853;\n"
    "            if (v_a.w > 0.5) {\n"
    "                vec2 e0 = vec2(sin(a0), -cos(a0)) * R;\n"
    "                vec2 e1 = vec2(sin(a0 + sweep), -cos(a0 + sweep)) * R;\n"
    "                float dc = min(length(p - e0), length(p - e1)) - hw;\n"
    "                d = (ang >= 0.0 && ang <= sweep) ? dr : dc;\n"
    "            } else {\n"
    "                d = max(dr, max(-ang, ang - sweep) * R);\n"
    "            }\n"
    "        }\n"
    "        float c = cov(d);\n"
    "        if (blur > 0.0) { float o = max(d, 0.0) / blur; c = 0.6 * exp(-3.0 * o * o); }\n"
    "        col = stroke * c;\n"
    "    } else if (k == 3) {\n" // wedge: a = (r0, r1, half angle, lw); its middle along +x
    "        float r = length(p);\n"
    "        float t = abs(atan(p.y, p.x)) - v_a.z;\n"
    "        float da = t < 1.5707963 ? r * sin(t) : r;\n"
    "        float d = max(max(r - v_a.y, v_a.x - r), da);\n"
    "        col = fill * cov(d);\n"
    "        float s = cov(abs(d) - v_a.w * 0.5);\n"
    "        col = col * (1.0 - s * stroke.a) + stroke * s;\n"
    "    } else if (k == 4) {\n" // line: a = (length, half width), from the origin along +x
    "        float t = clamp(p.x, 0.0, v_a.x);\n"
    "        col = stroke * cov(length(vec2(p.x - t, p.y)) - v_a.y);\n"
    "    } else if (k == 5) {\n" // rounded rectangle: a = (half w, half h, corner, border)
    "        vec2 q = abs(p) - v_a.xy + v_a.z;\n"
    "        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_a.z;\n"
    "        col = filled(d, v_a.w, fill, stroke);\n"
    "    } else if (k == 6) {\n" // triangle: a.xyz = how far in from each edge (1: an edge inside a bigger shape)
    "        vec3 w = max(fwidth(v_a.xyz), vec3(1e-6));\n"
    "        vec3 e = v_a.xyz / w;\n"
    "        col = fill * clamp(min(min(e.x, e.y), e.z) + 0.5, 0.0, 1.0);\n"
    "    } else if (k == 7) {\n" // glyph: a.xy = atlas uv, a.z = screen pixels an atlas pixel
    "        float s = texture(u_tex, v_a.xy).r * 255.0;\n"
    "        float d = (s - 128.0) * (SPREAD / 127.0) * v_a.z;\n"
    "        col = fill * clamp(d + 0.5, 0.0, 1.0);\n"
    "    }\n"
    "    fragColor = col;\n"
    "}\n";

typedef struct {
    float px, py, lx, ly, kind;
    float a[4], b[4];
    float fill[4], stroke[4];
} trVtx;

static struct {
    void* ctx;
    GLuint prog, vao, vbo, ibo, tex;
    GLint uSize, uTex;
    trVtx* aVtx;
    int numVtx, capVtx;
    GLuint* aIdx;
    int numIdx, capIdx;
    int bFailed;
} tr;

static int tr_Reserve(int nv, int ni)
{
    if (tr.numVtx + nv > tr.capVtx) {
        int cap = tr.capVtx ? tr.capVtx * 2 : 4096;
        while (cap < tr.numVtx + nv) cap *= 2;
        trVtx* p = (trVtx*)realloc(tr.aVtx, cap * sizeof(trVtx));
        if (!p) return 0;
        tr.aVtx = p;
        tr.capVtx = cap;
    }
    if (tr.numIdx + ni > tr.capIdx) {
        int cap = tr.capIdx ? tr.capIdx * 2 : 8192;
        while (cap < tr.numIdx + ni) cap *= 2;
        GLuint* p = (GLuint*)realloc(tr.aIdx, cap * sizeof(GLuint));
        if (!p) return 0;
        tr.aIdx = p;
        tr.capIdx = cap;
    }
    return 1;
}

static void tr_Color(float* out, touchUiColor c)
{
    out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
}

// A quad over the screen box (x0, y0)-(x1, y1); local coordinates are the
// pixel's offset from (cx, cy) turned by the angle (cosine, sine), y flipped if bYUp
static void tr_Quad(float x0, float y0, float x1, float y1, float cx, float cy, float cs, float sn, int bYUp, int kind,
                    const float* a, const float* b, touchUiColor fill, touchUiColor stroke)
{
    if (!tr_Reserve(4, 6)) return;
    const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    const int base = tr.numVtx;
    for (int i = 0; i < 4; i++) {
        trVtx* v = &tr.aVtx[tr.numVtx++];
        float dx = xs[i] - cx, dy = ys[i] - cy;
        if (bYUp) dy = -dy;
        v->px = xs[i];
        v->py = ys[i];
        v->lx = cs * dx + sn * dy;
        v->ly = -sn * dx + cs * dy;
        v->kind = (float)kind;
        memcpy(v->a, a, sizeof(v->a));
        memcpy(v->b, b, sizeof(v->b));
        tr_Color(v->fill, fill);
        tr_Color(v->stroke, stroke);
    }
    static const int q[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) tr.aIdx[tr.numIdx++] = base + q[i];
}

static void tr_Item(const touchUiDrawList* l, const touchUiDrawItem* it, float k)
{
    const float zero[4] = { 0, 0, 0, 0 };
    const touchUiColor none = { 0, 0, 0, 0 };
    switch (it->type) {
        case TOUCHUI_DRAW_CIRCLE: {
            const float x = it->x0 * k, y = it->y0 * k, r = it->r * k, e = r + 1.5f;
            const float a[4] = { r, it->lw * k, 0, 0 };
            tr_Quad(x - e, y - e, x + e, y + e, x, y, 1, 0, 0, TR_CIRCLE, a, zero, it->fill, it->stroke);
            break;
        }
        case TOUCHUI_DRAW_ARC: {
            const float x = it->x0 * k, y = it->y0 * k, r = it->r * k;
            const float e = r + it->lw * k * 0.5f + 1.5f + it->blur * k * 1.5f;
            const float a[4] = { r, it->lw * k, it->sweep, (float)it->bRound };
            const float b[4] = { it->blur * k, it->a0, 0, 0 };
            tr_Quad(x - e, y - e, x + e, y + e, x, y, 1, 0, 0, TR_ARC, a, b, none, it->stroke);
            break;
        }
        case TOUCHUI_DRAW_WEDGE: {
            const float x = it->x0 * k, y = it->y0 * k, r0 = it->r0 * k, r1 = it->r * k;
            const float mid = (it->a0 + it->a1) * 0.5f * 3.14159265f / 180.0f;
            const float half = fabsf(it->a1 - it->a0) * 0.5f * 3.14159265f / 180.0f;
            // the box round the slice: its corners, and the ring's extremes it passes
            float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
            float aAng[8];
            int n = 0;
            aAng[n++] = mid - half;
            aAng[n++] = mid + half;
            for (int q = -4; q <= 4; q++) {
                float qa = q * 1.5707963f;
                if (qa > mid - half && qa < mid + half && n < 8) aAng[n++] = qa;
            }
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < 2; j++) {
                    float rr = j ? r1 : r0;
                    float px = x + rr * cosf(aAng[i]), py = y - rr * sinf(aAng[i]);
                    bx0 = fminf(bx0, px); by0 = fminf(by0, py); bx1 = fmaxf(bx1, px); by1 = fmaxf(by1, py);
                }
            }
            const float e = it->lw * k * 0.5f + 1.5f;
            const float a[4] = { r0, r1, half, it->lw * k };
            tr_Quad(bx0 - e, by0 - e, bx1 + e, by1 + e, x, y, cosf(mid), sinf(mid), 1, TR_WEDGE, a, zero, it->fill, it->stroke);
            break;
        }
        case TOUCHUI_DRAW_LINE: {
            const float x0 = it->x0 * k, y0 = it->y0 * k, x1 = it->x1 * k, y1 = it->y1 * k;
            const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
            const float hw = it->lw * k * 0.5f, e = hw + 1.5f;
            const float cs = len > 0.0f ? dx / len : 1.0f, sn = len > 0.0f ? dy / len : 0.0f;
            const float a[4] = { len, hw, 0, 0 };
            tr_Quad(fminf(x0, x1) - e, fminf(y0, y1) - e, fmaxf(x0, x1) + e, fmaxf(y0, y1) + e, x0, y0, cs, sn, 0, TR_LINE, a, zero, none, it->stroke);
            break;
        }
        case TOUCHUI_DRAW_RRECT: {
            const float x = it->x0 * k, y = it->y0 * k, w = it->x1 * k, h = it->y1 * k;
            const float a[4] = { w * 0.5f, h * 0.5f, fminf(it->r * k, fminf(w, h) * 0.5f), it->lw * k };
            tr_Quad(x - 1.0f, y - 1.0f, x + w + 1.0f, y + h + 1.0f, x + w * 0.5f, y + h * 0.5f, 1, 0, 0, TR_RRECT, a, zero, it->fill, it->stroke);
            break;
        }
        case TOUCHUI_DRAW_TRI: {
            if (!tr_Reserve(3, 3)) return;
            const float xs[3] = { it->x0 * k, it->x1 * k, it->x2 * k }, ys[3] = { it->y0 * k, it->y1 * k, it->y2 * k };
            for (int i = 0; i < 3; i++) {
                trVtx* v = &tr.aVtx[tr.numVtx];
                memset(v, 0, sizeof(*v));
                v->px = xs[i];
                v->py = ys[i];
                v->kind = TR_TRI;
                for (int c = 0; c < 3; c++) v->a[c] = (it->edgeMask & (1 << c)) ? (c == i ? 1.0f : 0.0f) : 1.0f;
                tr_Color(v->fill, it->fill);
                tr.aIdx[tr.numIdx++] = tr.numVtx++;
            }
            break;
        }
        case TOUCHUI_DRAW_TEXT: {
            const char* text = &l->aPool[it->text];
            const float sizePx = it->size * k;
            const float g = sizePx / (float)TOUCHUI_FONT_EM; // screen pixels an atlas pixel
            const float gx = g * it->xscale;
            const float lineH = touchUiDraw_LineHeight(it->size) * k;
            int lines = 1;
            for (const char* p = text; *p; p++) lines += *p == '\n';
            const float top = it->y0 * k - lines * lineH * 0.5f;
            const float pad = (lineH - (TOUCHUI_FONT_ASCENT + TOUCHUI_FONT_DESCENT) * g) * 0.5f;
            const float dscale = g * (0.5f + 0.5f * it->xscale);
            const char* p = text;
            for (int line = 0; line < lines; line++) {
                const char* e = p;
                float w = 0.0f;
                while (*e && *e != '\n') {
                    unsigned char ch = (unsigned char)*e;
                    if (ch < 32 || ch > 126) ch = '?';
                    w += touchUiFont_aGlyphs[ch - 32].adv * gx;
                    e++;
                }
                float penX = it->x0 * k - w * 0.5f;
                const float base = top + line * lineH + pad + TOUCHUI_FONT_ASCENT * g;
                for (const char* c = p; c < e; c++) {
                    unsigned char ch = (unsigned char)*c;
                    if (ch < 32 || ch > 126) ch = '?';
                    const touchUiGlyph* gl = &touchUiFont_aGlyphs[ch - 32];
                    if (gl->w > 0) {
                        const float qx0 = penX + gl->ox * gx, qy0 = base + gl->oy * g;
                        const float qx1 = qx0 + gl->w * gx, qy1 = qy0 + gl->h * g;
                        const float u0 = (float)gl->x / TOUCHUI_FONT_ATLAS_W, v0 = (float)gl->y / TOUCHUI_FONT_ATLAS_H;
                        const float u1 = (float)(gl->x + gl->w) / TOUCHUI_FONT_ATLAS_W, v1 = (float)(gl->y + gl->h) / TOUCHUI_FONT_ATLAS_H;
                        if (!tr_Reserve(4, 6)) return;
                        const float xs[4] = { qx0, qx1, qx1, qx0 }, ys[4] = { qy0, qy0, qy1, qy1 };
                        const float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
                        const int b0 = tr.numVtx;
                        for (int i = 0; i < 4; i++) {
                            trVtx* v = &tr.aVtx[tr.numVtx++];
                            memset(v, 0, sizeof(*v));
                            v->px = xs[i];
                            v->py = ys[i];
                            v->kind = TR_GLYPH;
                            v->a[0] = us[i];
                            v->a[1] = vs[i];
                            v->a[2] = dscale;
                            tr_Color(v->fill, it->fill);
                        }
                        static const int q[6] = { 0, 1, 2, 0, 2, 3 };
                        for (int i = 0; i < 6; i++) tr.aIdx[tr.numIdx++] = b0 + q[i];
                    }
                    penX += gl->adv * gx;
                }
                p = *e ? e + 1 : e;
            }
            break;
        }
        default:
            break;
    }
}

static int tr_Setup(void)
{
    void* ctx = (void*)SDL_GL_GetCurrentContext();
    if (tr.prog && ctx == tr.ctx) return 1;
    if (tr.bFailed && ctx == tr.ctx) return 0;
    // a new context (the first, or the window was made again): everything anew
    tr.prog = tr.vao = tr.vbo = tr.ibo = tr.tex = 0;
    tr.ctx = ctx;
    tr.bFailed = 1;

    // (the fragment shader with the font's spread written in)
    char fs[8192];
    {
        const char* key = "SPREAD";
        const char* src = tr_fs;
        char* o = fs;
        const char* hit = strstr(src, key);
        size_t pre = (size_t)(hit - src);
        memcpy(o, src, pre);
        o += pre;
        o += snprintf(o, 32, "%.1f", (double)TOUCHUI_FONT_SPREAD);
        snprintf(o, sizeof(fs) - (size_t)(o - fs), "%s", hit + strlen(key));
    }
    GLuint vs = create_shader(tr_vs, GL_VERTEX_SHADER);
    GLuint fsh = create_shader(fs, GL_FRAGMENT_SHADER);
    if (!vs || !fsh) {
        stdPlatform_Printf("OpenJKDF2: touch overlay: shaders didn't compile\n");
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fsh);
    static const char* aAttr[7] = { "a_pos", "a_local", "a_kind", "a_a", "a_b", "a_fill", "a_stroke" };
    for (int i = 0; i < 7; i++) glBindAttribLocation(prog, i, aAttr[i]);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fsh);
    if (!ok) {
        print_log(prog);
        glDeleteProgram(prog);
        stdPlatform_Printf("OpenJKDF2: touch overlay: shaders didn't link\n");
        return 0;
    }
    tr.prog = prog;
    tr.uSize = glGetUniformLocation(prog, "u_size");
    tr.uTex = glGetUniformLocation(prog, "u_tex");

    glGenVertexArrays(1, &tr.vao);
    glGenBuffers(1, &tr.vbo);
    glGenBuffers(1, &tr.ibo);
    glBindVertexArray(tr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, tr.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tr.ibo);
    const GLsizei stride = sizeof(trVtx);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, px));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, lx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, kind));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, a));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, b));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, fill));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(trVtx, stroke));
    glBindVertexArray(0);

    glGenTextures(1, &tr.tex);
    glBindTexture(GL_TEXTURE_2D, tr.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, TOUCHUI_FONT_ATLAS_W, TOUCHUI_FONT_ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, touchUiFont_aAtlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    tr.bFailed = 0;
    return 1;
}

void touchUiRender_ContextLost(void)
{
    if (tr.ctx && tr.ctx == (void*)SDL_GL_GetCurrentContext()) {
        if (tr.prog) glDeleteProgram(tr.prog);
        if (tr.vao) glDeleteVertexArrays(1, &tr.vao);
        if (tr.vbo) glDeleteBuffers(1, &tr.vbo);
        if (tr.ibo) glDeleteBuffers(1, &tr.ibo);
        if (tr.tex) glDeleteTextures(1, &tr.tex);
    }
    tr.prog = tr.vao = tr.vbo = tr.ibo = tr.tex = 0;
    tr.ctx = NULL;
    tr.bFailed = 0;
}

void touchUiRender_Draw(const touchUiDrawList* pList, unsigned int fbo, int pxW, int pxH, float pxPerPt)
{
    if (!pList || pList->numItems <= 0 || pxW <= 0 || pxH <= 0) return;

    // The GL state this changes, to put back
    GLint prog, vao, abuf, tex, activeTex, fb, vp[4], bsrc, bdst, bsrcA, bdstA, beq, beqA, unpack;
    GLboolean bBlend, bDepth, bCull, bScissor, bStencil, cmask[4], dmask;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &bdst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &bdstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &beq);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &beqA);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    bBlend = glIsEnabled(GL_BLEND);
    bDepth = glIsEnabled(GL_DEPTH_TEST);
    bCull = glIsEnabled(GL_CULL_FACE);
    bScissor = glIsEnabled(GL_SCISSOR_TEST);
    bStencil = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);

    if (tr_Setup()) {
        tr.numVtx = tr.numIdx = 0;
        for (int i = 0; i < pList->numItems; i++) tr_Item(pList, &pList->aItems[i], pxPerPt);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, pxW, pxH);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); // (the window's alpha stays as it is)
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // (premultiplied)
        glUseProgram(tr.prog);
        glUniform2f(tr.uSize, (float)pxW, (float)pxH);
        glUniform1i(tr.uTex, 0);
        glBindTexture(GL_TEXTURE_2D, tr.tex);
        glBindVertexArray(tr.vao);
        glBindBuffer(GL_ARRAY_BUFFER, tr.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(tr.numVtx * sizeof(trVtx)), tr.aVtx, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(tr.numIdx * sizeof(GLuint)), tr.aIdx, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, tr.numIdx, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray((GLuint)vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
    glUseProgram((GLuint)prog);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    glActiveTexture((GLenum)activeTex);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBlendFuncSeparate((GLenum)bsrc, (GLenum)bdst, (GLenum)bsrcA, (GLenum)bdstA);
    glBlendEquationSeparate((GLenum)beq, (GLenum)beqA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    if (bBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (bDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (bCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (bScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (bStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
    glDepthMask(dmask);
}

unsigned char* touchUiRender_ReadRGBA(unsigned int fbo, int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    const size_t row = (size_t)w * 4;
    unsigned char* p = (unsigned char*)malloc(row * h);
    unsigned char* tmp = (unsigned char*)malloc(row);
    if (!p || !tmp) {
        free(p);
        free(tmp);
        return NULL;
    }
    while (glGetError() != GL_NO_ERROR) {} // (not ours)
    GLint oldFbo = 0, oldPack = 4;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPack);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
    const int bOk = glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, oldPack);
    if (!bOk) {
        free(p);
        free(tmp);
        return NULL;
    }
    for (int y = 0; y < h / 2; y++) { // (GL's rows come bottom up)
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

#endif // TOUCHUI_ENABLED

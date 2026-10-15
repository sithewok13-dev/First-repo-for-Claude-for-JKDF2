// Added: entire file. Draws the touch overlay's draw list (touchUiDraw.h)
// with OpenGL ES 3 / OpenGL 3.3: one small shader that draws every shape
// from its distance to the edge (crisp at any size, no textures but the
// label font's), all in one draw call. It saves the GL state it changes and
// puts it back.

#ifndef _PLATFORM_TOUCH_TOUCHUIRENDER_GL_H
#define _PLATFORM_TOUCH_TOUCHUIRENDER_GL_H

#include "Platform/Touch/touchUiDraw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Draws the list into framebuffer fbo, pxW x pxH pixels, pxPerPt pixels a point
void touchUiRender_Draw(const touchUiDrawList* pList, unsigned int fbo, int pxW, int pxH, float pxPerPt);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_TOUCH_TOUCHUIRENDER_GL_H

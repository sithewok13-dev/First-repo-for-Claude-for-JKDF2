#ifndef _ENHANCED_ENHMENUREAD_H
#define _ENHANCED_ENHMENUREAD_H

// Readable multiplayer menus. The multiplayer screens' text (pink and purple
// on the purple multiplayer art) is hard to read, most of all in Jedi Knight.
// The screens marked here are shown with their text, input boxes and lists
// standing out: a dark panel behind them, the text brighter with a dark
// outline, and a bar on a list's chosen row. It is done on the finished menu
// picture as it goes to the screen (the menu's 8-bit buffer, before the
// renderer turns it into colours), using the game's own palette, fonts and
// background art: nothing about the menus themselves changes (where things
// are, what a tap hits), and other screens are left as they were.

#ifdef JK_ENHANCED

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// A menu (a multiplayer screen) to show the readable way
void enhMenuRead_Mark(jkGuiMenu* pMenu);

// With a marked menu up: its part of the menu buffer (the top left 640x480;
// rows pitch bytes apart), made readable, to show in its place: 640x480
// bytes, rows 640 apart, good until the next call. NULL when there is none:
// the buffer is shown as it is.
#define ENHMENUREAD_FILTER_W 640
#define ENHMENUREAD_FILTER_H 480
const uint8_t* enhMenuRead_Filter(const uint8_t* pSrc, int w, int h, int pitch);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _ENHANCED_ENHMENUREAD_H

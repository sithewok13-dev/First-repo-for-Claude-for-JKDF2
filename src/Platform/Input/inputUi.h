// Added: entire file. What the shared input logic -- game controllers
// (Platform/iOS/iosPad.c) and a keyboard and mouse (iosKbMouse.c), plain C
// built on every platform with the enhanced input -- shows on the screen and
// asks of the platform around it:
//   - the Force, weapon and saber wheels, opened from a pad or the keyboard,
//     pointed at with a stick or the mouse, holding the game still;
//   - the touch controls hidden while another input is played;
//   - the photo modes (a photo, the selfie, Action Freeze) from the keyboard;
//   - the quick save / quick load hold ring, a line of button prompts, the
//     controller options panel, the FPS readout, and where the controller
//     settings are kept.
// Where each comes from:
//   - iOS: the UIKit touch overlay (iosTouch.m) and iosPadPanel.m, as they
//     always have; each inputUi_ name below is that function's own name
//     (macros), so the iOS build calls exactly what it called before.
//   - the desktops and Android (ENH_INPUT): Platform/Input/inputOverlay.c,
//     drawn with the game's own GL (inputWheel.c, and the touch overlay's
//     draw list and renderer, Platform/Touch).
// The wheel kinds, the saber wheel's choices and the hold rings are
// iosPad.h's (IOSPAD_WHEEL_*, IOSPAD_CHIP_*, IOSPAD_RING_*).

#ifndef _PLATFORM_INPUT_INPUTUI_H
#define _PLATFORM_INPUT_INPUTUI_H

#ifdef TARGET_IOS

#include "Platform/iOS/iosTouch.h"
#include "Platform/iOS/iosPadPanel.h"
#include "Platform/iOS/iosKbMouse.h"

#define inputUi_SetPadHud iosTouch_SetPadHud
#define inputUi_SetKbmHud iosTouch_SetKbmHud
#define inputUi_PadWheelOpen iosTouch_PadWheelOpen
#define inputUi_PadWheelIsOpen iosTouch_PadWheelIsOpen
#define inputUi_KbmWheelOpen iosTouch_KbmWheelOpen
#define inputUi_KbmWheelIsOpen iosTouch_KbmWheelIsOpen
#define inputUi_IsWheelOpen iosTouch_IsWheelOpen
#define inputUi_WheelSetTapMode iosTouch_PadWheelSetTapMode
#define inputUi_WheelAim iosTouch_PadWheelAim
#define inputUi_WheelStep iosTouch_PadWheelStep
#define inputUi_WheelHot iosTouch_PadWheelHot
#define inputUi_WheelClose iosTouch_PadWheelClose
#define inputUi_SaberChip iosTouch_PadSaberChip
#define inputUi_IsPhotoModeUp iosTouch_IsPhotoModeUp
#define inputUi_WantsPointer iosTouch_WantsPointer
#define inputUi_Camera iosTouch_KbmCamera
#define INPUTUI_CAM_PHOTO IOSTOUCH_CAM_PHOTO
#define INPUTUI_CAM_SELFIE IOSTOUCH_CAM_SELFIE
#define INPUTUI_CAM_FREEZE IOSTOUCH_CAM_FREEZE
#define INPUTUI_CAM_FREEZE_END IOSTOUCH_CAM_FREEZE_END
#define inputUi_IsFpsShown iosTouch_IsFpsShown
#define inputUi_SetFpsShown iosTouch_SetFpsShown
#define inputUi_PanelShow iosPadPanel_Show
#define inputUi_PanelRefresh iosPadPanel_Refresh
#define inputUi_HoldRing iosPadPanel_HoldRing
#define inputUi_SetPrompt iosPadPanel_SetPrompt
#define inputUi_LoadInt iosPadPanel_LoadInt
#define inputUi_SaveInt iosPadPanel_SaveInt
#define inputUi_KbmViewTick iosKbMouseView_Tick

#else // !TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// The touch controls hidden while a pad (or the keyboard and mouse) is
// played, or shown again (either can hide them; a touch brings them back)
void inputUi_SetPadHud(int bHidden);
void inputUi_SetKbmHud(int bHidden);

// The wheels (kind: IOSPAD_WHEEL_*), opened from a pad or the keyboard, to
// pick by letting go of the button or key, or (bTapMode) with A / a click.
// The game holds still while one is open (single player). 0 if one can't
// open now (another wheel, a photo mode, the saber wheel with no saber).
int inputUi_PadWheelOpen(int kind, int bTapMode);
int inputUi_PadWheelIsOpen(void);
int inputUi_KbmWheelOpen(int kind, int bTapMode);
int inputUi_KbmWheelIsOpen(void);
// Whether any wheel is open (a finger's too, where there are fingers)
int inputUi_IsWheelOpen(void);
// The calls below work the wheel a pad or the keyboard opened
void inputUi_WheelSetTapMode(void);
// Where the stick or the mouse points (degrees counter-clockwise from
// pointing right), or nowhere (bAim 0); the slice it points at holds a
// little past its edges
void inputUi_WheelAim(int bAim, float deg);
// The next slice round (+1 clockwise, -1 the other way)
void inputUi_WheelStep(int dir);
// Whether a slice is pointed at (*pbPickable: one that can be picked)
int inputUi_WheelHot(int* pbPickable);
// Closes it, picking what is pointed at (bSelect) or nothing. A colour on
// the saber wheel is set and the wheel stays open: returns 1 while it is
// still open, else 0.
int inputUi_WheelClose(int bSelect);
// The saber wheel's choices (IOSPAD_CHIP_*)
void inputUi_SaberChip(int chip);

// Whether a photo mode is up (a selfie, Action Freeze, the cinematic burst):
// the pad and the keys leave the game alone then
int inputUi_IsPhotoModeUp(void);
// Whether the game is to let go of the pointer during play
int inputUi_WantsPointer(void);
// The camera from the keyboard: a photo (in Action Freeze, its photo), the
// selfie, Action Freeze on and off, Action Freeze ended. 1 if it went.
#define INPUTUI_CAM_PHOTO 0
#define INPUTUI_CAM_SELFIE 1
#define INPUTUI_CAM_FREEZE 2
#define INPUTUI_CAM_FREEZE_END 3
int inputUi_Camera(int action);

// The FPS readout, on or off (remembered)
int inputUi_IsFpsShown(void);
void inputUi_SetFpsShown(int bShow);

// The controller options (their rows from iosPad_Panel*), shown or hidden,
// or drawn again after a change
void inputUi_PanelShow(int bShow);
void inputUi_PanelRefresh(void);
// The quick save / quick load hold ring (IOSPAD_RING_*), filled this far
void inputUi_HoldRing(int kind, float progress);
// A line of button prompts along the bottom of the screen (NULL: none)
void inputUi_SetPrompt(const char* pText);
// The controller settings, kept between launches
int inputUi_LoadInt(const char* pKey, int valDefault);
void inputUi_SaveInt(const char* pKey, int val);
// Once a frame from the keyboard and mouse (iOS: its diagnostic line)
void inputUi_KbmViewTick(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_INPUT_INPUTUI_H

// Added: entire file. The photo modes from keys (P, O: iosKbMouse.c), where
// there is no touch overlay to start them (the desktops, and Android until
// its touch overlay has CAMERA): a photo, the selfie, Action Freeze -- the
// enhanced gameplay's (src/Enhanced: iosCapture, iosSelfieShots, iosFreeze),
// started and ended as the iOS overlay's CAMERA does. With a mouse, Action
// Freeze's camera turns round what it looks at as the mouse moves, and the
// mouse wheel brings it nearer or further.

#ifndef _PLATFORM_INPUT_INPUTPHOTO_H
#define _PLATFORM_INPUT_INPUTPHOTO_H

#ifdef __cplusplus
extern "C" {
#endif

// Whether a photo mode is up: a selfie, Action Freeze, the cinematic burst
int inputPhoto_IsUp(void);
// Whether it is Action Freeze, started here
int inputPhoto_IsFreezing(void);

// INPUTUI_CAM_*: a photo (in Action Freeze, its photo), the selfie, Action
// Freeze on and off, Action Freeze ended. 1 if it went.
int inputPhoto_Camera(int action);

// What the photos are (IOSCAPTURE_* kinds) and how many a selfie takes (1-5)
void inputPhoto_SetChoices(int kinds, int selfieShots);

// The mouse in Action Freeze: moved (counts), its wheel turned (notches, up +)
void inputPhoto_FreezeMouse(float dx, float dy);
void inputPhoto_FreezeWheel(float notches);

// Once a frame (bGameplay: the game is being played, no menu): the freeze
// camera follows the mouse; a photo no frame took is dropped; a photo mode
// is ended when the game is left
void inputPhoto_Tick(int bGameplay, double now);

// The camera flash over the screen as a photo is taken (0: none)
float inputPhoto_FlashAlpha(double now);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_INPUT_INPUTPHOTO_H

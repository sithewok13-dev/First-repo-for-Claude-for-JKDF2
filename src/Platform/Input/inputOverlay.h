// Added: entire file. The enhanced input's screen where there is no UIKit
// overlay (the desktops and Android: ENH_INPUT): it is inputUi.h's other
// side, for game controllers (iosPad.c) and a keyboard and mouse
// (iosKbMouse.c), drawn over the game and its HUD with the game's own GL
// (Platform/Touch's draw list and renderer):
//   - the Force, weapon and saber wheels (inputWheel.c), opened from a pad
//     or the keys, one at a time, the game held still;
//   - the quick save / quick load hold ring under the top of the screen, the
//     line of button prompts along the bottom, the camera flash;
//   - the controller options (Start held: LOOK X, LOOK Y, CURVE, INVERT,
//     ACCELERATION, VIBRATION, FPS, TYPE), worked with the pad, or clicked
//     with the mouse (the pointer is let go while they are up);
//   - the FPS readout (the options' FPS), top right;
//   - the photo modes from the keys (inputPhoto.c).
// The settings are cvars, kept with the others in openjkdf2_cvars.json and
// settable from the console:
//   enh_padLookX, enh_padLookY  the look stick's speed across and up and
//                               down: 0-7 (0.5x 0.75x 1.0x 1.25x 1.5x 2.0x
//                               2.5x 3.0x; 2, 1.0x, by default)
//   enh_padCurve                0 LINEAR, 1 SMOOTH (default), 2 PRECISE
//   enh_padInvertY, enh_padAccel, enh_padRumble   0 or 1 (off, off, on)
//   enh_showFps                 the FPS readout, 0 or 1
//   enh_saberTarget             with two blades, the one a colour goes to:
//                               0 right, 1 left, 2 both (default)
//   enh_selfieShots             photos a selfie takes, 1-5 (3)
//   enh_photoKinds              what a photo is: 1 the clean view (default),
//                               2 with the HUD, 3 both
// A touch overlay on the same screen (Android's touchUi) hides its controls
// while a pad or the keys are played (inputOverlay_IsTouchHudHidden), and
// says when a finger goes down (inputOverlay_NoteTouch), which brings them
// back.

#ifndef _PLATFORM_INPUT_INPUTOVERLAY_H
#define _PLATFORM_INPUT_INPUTOVERLAY_H

#ifdef ENH_INPUT

#ifdef __cplusplus
extern "C" {
#endif

// sithCvar_Startup: the settings' cvars, before the saved ones are read
void inputOverlay_StartupCvars(void);

// SDL events as they are polled, before the pad's and the keyboard's own
// (Window_SdlUpdate): a click on the controller options; the mouse in
// Action Freeze. Returns 1 if the game is not to see this one.
int inputOverlay_HandleEvent(void* pSdlEvent);

// Once a frame, after the pad and the keyboard have had theirs: the
// settings kept in step with their cvars, the photo modes
void inputOverlay_Update(void);

// Once a frame, the game and its HUD drawn, before the frame is shown
// (Window_SdlVblank): the wheels, the ring, the prompts, the options
void inputOverlay_Render(void);

// The GL context is about to go (and is still current)
void inputOverlay_ContextLost(void);

// For a touch overlay on the same screen: whether its controls are to hide
// for another input, and a finger going down on it (they come back)
int inputOverlay_IsTouchHudHidden(void);
void inputOverlay_NoteTouch(void);

#ifdef __cplusplus
}
#endif

#endif // ENH_INPUT

#endif // _PLATFORM_INPUT_INPUTOVERLAY_H

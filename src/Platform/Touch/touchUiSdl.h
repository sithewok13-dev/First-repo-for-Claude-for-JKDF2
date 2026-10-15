// Added: entire file. The touch overlay's engine side on SDL (Android, and
// the Linux test rig): SDL's finger and sensor events into the core
// (touchUi.c), its keys and look movement into the game's controls, its
// drawing over the game (touchUiRender_gl.c). Window.c and stdControl.c call
// these where iOS calls iosTouch_* (TOUCHUI_ENABLED, set in CMake).

#ifndef _PLATFORM_TOUCH_TOUCHUISDL_H
#define _PLATFORM_TOUCH_TOUCHUISDL_H

#ifdef TOUCHUI_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

// Window_SdlUpdate: before its events are polled; each event; after them
void touchUiSdl_BeforeEvents(void);
void touchUiSdl_HandleEvent(const union SDL_Event* pEvent);
void touchUiSdl_AfterEvents(void);

// Window_SdlVblank: the overlay drawn over the frame, before it is shown
void touchUiSdl_Render(void);

// stdControl_ReadKeyboard: whether the overlay holds this key down
int touchUiSdl_IsScancodeDown(int scancode);

// Where the game's top message text goes, in window units (iosSafeArea.c):
// 0 until the overlay has been laid out
int touchUiSdl_GetTopTextRectWin(float* pTop, float* pLeft, float* pRight);

// Window units (SDL_GetWindowSize's) to the overlay's points
float touchUiSdl_WinToPt(void);

// The platform's haptics (touchUi_android.c; elsewhere none): TOUCHUI_HAPTIC_* bits
void touchUiPlat_Haptic(int kinds);

#ifdef __cplusplus
}
#endif

#endif // TOUCHUI_ENABLED

#endif // _PLATFORM_TOUCH_TOUCHUISDL_H

// Added: entire file. The touch overlay's way into the game: the core
// (touchUi.c) gets what it needs through iosGame.c's helpers, the ones the
// iOS overlay uses.

#include "Platform/Touch/touchUi.h"

#ifdef TOUCHUI_ENABLED

#include "Platform/Touch/touchUiSdl.h"
#include "Enhanced/iosGame.h"
#include "Enhanced/iosSaber.h"
#include "Enhanced/iosCapture.h"
#include "Enhanced/iosSelfieShots.h"
#include "Enhanced/iosFreeze.h"
#include "Enhanced/iosCinema.h"
#include "Enhanced/enhTick.h"
#include "Platform/platServices.h"

extern int jkHud_bChatOpen;
extern int jkHud_IosGetRightGaugeRectPt(float* pX0, float* pY0, float* pX1, float* pY1);
extern int jkHud_IosGetLeftGaugeRectPt(float* pX0, float* pY0, float* pX1, float* pY1);

static int touchUiGame_IsChatOpen(void)
{
    return jkHud_bChatOpen != 0;
}

// The HUD's corner gauge, from window units to the overlay's points
static int touchUiGame_GetGaugeRect(int bLeft, float* pX0, float* pY0, float* pX1, float* pY1)
{
    if (!(bLeft ? jkHud_IosGetLeftGaugeRectPt(pX0, pY0, pX1, pY1) : jkHud_IosGetRightGaugeRectPt(pX0, pY0, pX1, pY1))) return 0;
    const float k = touchUiSdl_WinToPt();
    *pX0 *= k;
    *pY0 *= k;
    *pX1 *= k;
    *pY1 *= k;
    return 1;
}

// The photo modes start on the engine's own clock (enh_Tick runs them)
static int touchUiGame_SelfieStart(int numShots)
{
    int numPeople = 0;
    return iosSelfieShots_Start(numShots, enh_Now(), &numPeople);
}

static int touchUiGame_CinemaStart(void)
{
    return iosCinema_Start(enh_Now());
}

const touchUiGame touchUiGame_engine = {
    iosGame_IsMots,
    iosGame_GetForcePowerName,
    iosGame_IsPowerAvailable,
    iosGame_GetPowerLevel,
    iosGame_GetPowerName,
    iosGame_GetCurPower,
    iosGame_SelectPower,
    iosGame_GetWeaponName,
    iosGame_GetWeapon,
    iosGame_GetCurWeapon,
    iosGame_SelectWeapon,
    iosGame_CancelWeaponPick,
    iosGame_GetItem,
    iosGame_SelectItem,
    iosGame_QuickLoad,
    iosGame_SetHold,
    iosGame_IsHolding,
    touchUiGame_IsChatOpen,
    iosGame_ToggleChat,
    iosGame_GetForceMana,
    iosGame_GetFrameCount,
    iosGame_IsAlwaysRun,
    iosGame_GetMouseLookDegrees,
    iosGame_CanFly,
    iosGame_ToggleFlight,
    iosGame_IsFlying,
    iosGame_CanNoclip,
    iosGame_ToggleNoclip,
    iosGame_NumPickableLevels,
    iosGame_GoToLevel,
    iosGame_IsSithEwokOn,
    iosGame_ToggleSithEwok,
    iosGame_TakeSithEwokTypedOff,
    iosGame_SetCheatShown,
    iosGame_SetThirdPerson,
    iosGame_IsThirdPerson,
    iosGame_SetForceJumpStyle,
    iosGame_GetForceJumpRank,
    iosGame_ShowMessage,
    iosGame_ShowMessageInPlace,
    touchUiGame_GetGaugeRect,
    iosGame_IsSaberButtonShown,
    iosSaber_NumColours,
    iosSaber_ColourName,
    iosSaber_ColourKey,
    iosSaber_ColourByKey,
    iosSaber_ColourRGB,
    iosSaber_StyleAvailable,
    iosSaber_StyleName,
    iosSaber_StyleNumBlades,
    iosSaber_GetStyle,
    iosSaber_SetStyle,
    iosSaber_GetColour,
    iosSaber_SetColour,
    iosCapture_Request,
    iosCapture_Cancel,
    platServices_SetPhotoBatch,
    touchUiGame_SelfieStart,
    iosSelfieShots_Stop,
    iosSelfieShots_IsActive,
    iosFreeze_Begin,
    iosFreeze_End,
    iosFreeze_IsActive,
    iosFreeze_SetSmooth,
    iosFreeze_Orbit,
    iosFreeze_Pan,
    iosFreeze_Zoom,
    touchUiGame_CinemaStart,
    iosCinema_Stop,
    iosCinema_IsActive,
};

// The numbers the overlay shares with the engine (touchUi.h keeps its own copy)
typedef char touchUiGame_assertNumbers[(TOUCHUI_CAPTURE_CLEAN == IOSCAPTURE_CLEAN && TOUCHUI_CAPTURE_HUD == IOSCAPTURE_HUD
                                        && TOUCHUI_CAPTURE_HUDBTNS == IOSCAPTURE_HUDBTNS && TOUCHUI_CAPTURE_BURST == IOSCAPTURE_MODE_BURST
                                        && TOUCHUI_CAPTURE_SELFIE == IOSCAPTURE_MODE_SELFIE && TOUCHUI_PHOTOMODE_STARTED == IOSSELFIESHOTS_STARTED
                                        && TOUCHUI_PHOTOMODE_BUSY == IOSCINEMA_BUSY && TOUCHUI_MODE_SELFIE == PLATSERVICES_MODE_SELFIE
                                        && TOUCHUI_MODE_CINEMA == PLATSERVICES_MODE_CINEMA && TOUCHUI_SABER_DUAL == IOSSABER_STYLE_DUAL) ? 1 : -1];

#endif // TOUCHUI_ENABLED

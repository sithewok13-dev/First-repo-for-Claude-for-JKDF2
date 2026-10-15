// Added: entire file. The touch overlay's way into the game: the core
// (touchUi.c) gets what it needs through iosGame.c's helpers, the ones the
// iOS overlay uses.

#include "Platform/Touch/touchUi.h"

#ifdef TOUCHUI_ENABLED

#include "Platform/Touch/touchUiSdl.h"
#include "Platform/iOS/iosGame.h"

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
};

#endif // TOUCHUI_ENABLED

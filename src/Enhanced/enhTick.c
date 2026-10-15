// Added: entire file. See enhTick.h.

#include "Enhanced/enhTick.h"

#ifdef JK_ENHANCED

#include "Enhanced/iosSelfieShots.h"
#include "Enhanced/iosCinema.h"
#include "Enhanced/iosFreeze.h"
#include "Platform/platServices.h"
#include "SDL2_helper.h"

double enh_Now(void)
{
    return (double)SDL_GetTicksNS() * 1e-9;
}

void enh_Tick(void)
{
    const double now = enh_Now();
    // (one at a time: a selfie, or the burst -- which runs Action Freeze's
    // camera itself -- or Action Freeze moved by hand)
    if (iosSelfieShots_IsActive()) {
        if (iosSelfieShots_Tick(now) == IOSSELFIESHOTS_IDLE)
            platServices_PhotoModeEnded(PLATSERVICES_MODE_SELFIE);
    }
    else if (iosCinema_IsActive()) {
        if (iosCinema_Tick(now) == IOSCINEMA_IDLE)
            platServices_PhotoModeEnded(PLATSERVICES_MODE_CINEMA);
    }
    else if (iosFreeze_IsActive()) {
        iosFreeze_Frame();
    }
    platServices_Tick();
}

#endif // JK_ENHANCED

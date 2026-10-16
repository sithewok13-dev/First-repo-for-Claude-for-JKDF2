#ifndef _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAY_H
#define _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAY_H

// Added: entire file. Experimental: the original 1997 game's own multiplayer connection, DirectPlay
// over TCP/IP, spoken directly (no DirectPlay library), so this build can join a game hosted by the
// original Jedi Knight (and host one it can join). See stdComm_DPlay.c. Built with
// -DOPENJKDF2_NET_DPLAY=ON (PLATFORM_DPLAY), next to ENet.

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_DPLAY

#include "types.h"

void stdComm_DPlay_Startup(void);
void stdComm_DPlay_Shutdown(void);

// Host over DirectPlay rather than ENet (the -dplayHost command line switch; for testing)
extern int stdComm_DPlay_bHostOverDPlay;

// Every session the address last asked about answered with, that one among them (by its
// guidInstance, as stdCommTransport_ProbeAddress gave it): up to max in aOut, how many
int stdComm_DPlay_GetSessions(const GUID_idk* pOne, jkMultiEntry* aOut, int max);
// That session (one of them) the one joined next: 0 if it isn't known
int stdComm_DPlay_SelectSession(const GUID_idk* pGuid);
// Why the last asking found nothing or the last game ended, for the player ("" if nothing to say)
const char* stdComm_DPlay_Problem(void);
void stdComm_DPlay_ClearProblem(void);
// Why the last join failed, for the player, when the router couldn't open this device's ports and
// the host then didn't get through (NULL otherwise: the usual message)
const char* stdComm_DPlay_JoinFailReason(void);

// The player picked a game at that address to join (the Join screen, -join): the next probes of it
// (for a little while) open this device's ports on the router first, if the host needs them to reach
// us -- the answer itself comes back over them. Background asking (games played before) never does.
void stdComm_DPlay_WillJoin(const char* pAddress);

// Another transport's game started: what DirectPlay kept for a join (its thread, the router's
// ports) let go
void stdComm_DPlay_NotNow(void);

// (the Join screen, Gui/jkGUIDPlayPick.c) An address listing several sessions: one picked, in
// *pInOut (which is one of them), and the one joined; 0 if the player went back
int jkGuiDPlayPick_Pick(jkMultiEntry* pInOut);

#endif // PLATFORM_DPLAY

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAY_H

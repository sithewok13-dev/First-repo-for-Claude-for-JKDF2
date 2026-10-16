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

// Host over DirectPlay rather than ENet (the -dplayHost command line switch)
extern int stdComm_DPlay_bHostOverDPlay;

#endif // PLATFORM_DPLAY

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAY_H

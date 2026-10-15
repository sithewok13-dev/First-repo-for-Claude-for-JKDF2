#ifndef _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H
#define _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_ENET

#include "types.h"

void stdComm_ENet_Startup();
void stdComm_ENet_Shutdown();

// The host's address(es) on the local network, for showing to the players who
// will join (IPv4, dotted, comma separated; empty if none found)
void stdComm_ENet_GetLocalAddresses(char* pOut, int outLen);

// Is there a game at that address (a name or IPv4, maybe :port)? A short connection asks it, for up to
// timeoutMs (or until *pbKeepGoing is 0): 1 if a game answered (its description in pOut), 0 if none,
// -1 if the address isn't one. Safe from any thread.
int stdComm_ENet_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing);

#endif // PLATFORM_ENET

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H

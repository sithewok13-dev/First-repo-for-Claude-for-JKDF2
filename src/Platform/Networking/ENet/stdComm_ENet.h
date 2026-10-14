#ifndef _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H
#define _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_ENET

void stdComm_ENet_Startup();
void stdComm_ENet_Shutdown();

// The host's address(es) on the local network, for showing to the players who
// will join (IPv4, dotted, comma separated; empty if none found)
void stdComm_ENet_GetLocalAddresses(char* pOut, int outLen);

#endif // PLATFORM_ENET

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_ENET_STDCOMM_ENET_H

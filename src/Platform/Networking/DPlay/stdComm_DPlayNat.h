#ifndef _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAYNAT_H
#define _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAYNAT_H

// Added: entire file. Opening this device's DirectPlay ports on the router (stdComm_DPlayNat.c):
// a DirectPlay host connects back to the player who joins (TCP) and sends to it (UDP), so a player
// behind a router needs those ports forwarded. Asked of the router with PCP, then NAT-PMP, then
// UPnP-IGD; given back when done.

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_DPLAY

#include <stdint.h>

typedef struct stdCommDPlayNatMapping
{
    uint16_t extTcpPort, extUdpPort; // (the router's ports, forwarded to ours)
    uint32_t extIp;                  // (the router's address, network order; 0 if it didn't say)
    const char* pMethod;             // ("PCP", "NAT-PMP", "UPnP")
} stdCommDPlayNatMapping;

// Our TCP and UDP ports forwarded by the router (up to a few seconds: call off the game's thread).
// 1 if they are (pOut filled), 0 if not (why: stdComm_DPlayNat_Problem)
int stdComm_DPlayNat_Map(uint16_t tcpPort, uint16_t udpPort, stdCommDPlayNatMapping* pOut);
// The router's forwarding given back (if any)
void stdComm_DPlayNat_Unmap(void);
// Why the last stdComm_DPlayNat_Map failed, for the player ("" if it didn't)
const char* stdComm_DPlayNat_Problem(void);

// (an IPv4 address, network order, that is only for a local network: 10/8, 172.16/12, 192.168/16,
// 100.64/10 (a carrier's), 169.254/16, 127/8)
int stdComm_DPlayNat_IsPrivate(uint32_t ipNet);
// Our address on the way to that one (network order; 0 if none)
uint32_t stdComm_DPlayNat_LocalAddressToward(uint32_t ipNet);

#endif // PLATFORM_DPLAY

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_DPLAY_STDCOMM_DPLAYNAT_H

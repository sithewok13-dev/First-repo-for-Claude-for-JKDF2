// Added: entire file. The multiplayer connection's two kinds (transports), side by side:
//
//   ENet (Platform/Networking/ENet): this port's own: what its games are hosted on, with password
//        encryption and games nearby found by themselves.
//   GNS  (Platform/Networking/GNS):  Valve's GameNetworkingSockets: what upstream OpenJKDF2 (the
//        "classic" desktop builds) plays over, for joining their games.
//
// The game sees one connection, as before (Win95/stdComm.h: DirectPlay_*, stdComm_Open, ...);
// stdCommTransport.c passes each call to the transport of the game in progress. Hosting is over
// ENet. Joining goes over whichever the host answered on: asking an address asks both at once
// (stdCommTransport_ProbeAddress), and a host answers only on its own.
//
// A game joined over GNS, in a build with ENet too, is a classic one (stdCommTransport_IsClassic):
// its host is another build, which knows nothing of this port's own messages (Dss/jkNetExt.h), so
// those aren't sent, and co-op isn't joined (only the deathmatch kinds play the same across builds).

#ifndef _PLATFORM_NETWORKING_STDCOMMTRANSPORT_H
#define _PLATFORM_NETWORKING_STDCOMMTRANSPORT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PLATFORM_ENET) || defined(PLATFORM_GNS)
#define STDCOMM_TRANSPORTS

enum stdCommTransportKind
{
    STDCOMM_TRANSPORT_NONE = -1,
    STDCOMM_TRANSPORT_ENET = 0,
    STDCOMM_TRANSPORT_GNS = 1,
    STDCOMM_TRANSPORT_DPLAY = 2, // (experimental: the original game's DirectPlay, Platform/Networking/DPlay)
    STDCOMM_TRANSPORT_NUM = 3,
};

// One transport's side of the connection. The address joined or asked about is as typed: a name or
// IPv4, maybe with :port (27020 if none).
typedef struct stdCommTransport
{
    const char* pName; // (its Startup and Shutdown: stdComm_Startup's, Win95/stdComm.c)
    // DirectPlay_Receive / DirectPlay_Send, for the game in progress
    int (*Receive)(int* pIdOut, int* pMsgIdOut, int* pLenOut);
    BOOL (*Send)(DPID idFrom, DPID idTo, void* lpData, DWORD dwDataSize);
    // Joining the game at jkGuiMultiplayer_ipText (the player's id from the host before returning):
    // 0, or the Join screen's error (0x88770154: wrong password; 0x88770155: can't connect)
    int (*Join)(char16_t* pwPassword);
    // Hosting (pEntry: the session), and the session kept for it (DirectPlay_GetSession_passwordidk)
    int (*OpenHost)(jkMultiEntry* pEntry);
    void (*SetSession)(jkMultiEntry* pEntry);
    void (*SetSessionLevel)(void); // (the level now played: jkMain_aLevelJklFname)
    void (*Close)(void);
    void (*ResetClients)(void);
    // Is there a game at that address? Asked for up to timeoutMs (or until *pbKeepGoing is 0):
    // 1 if one answered (its description in pOut), 0 if none, -1 if the address isn't one.
    // Safe from any thread.
    int (*ProbeAddress)(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing);
    // That game's password: 1 if it is the one (or none is asked for), 0 if not, -1 if no answer.
    int (*CheckPassword)(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing);
} stdCommTransport;

#ifdef PLATFORM_ENET
extern const stdCommTransport stdComm_ENet_transport;
#endif
#ifdef PLATFORM_GNS
extern const stdCommTransport stdComm_GNS_transport;
#endif
#ifdef PLATFORM_DPLAY
extern const stdCommTransport stdComm_DPlay_transport;
#endif

// An address as typed, split: its host (a name, an IPv4, or an IPv6 bare or in [brackets]) and its
// port (after a ':', digits only, 1-65535; defaultPort if none). 0 if it isn't an address. Both
// transports read addresses with this, so they agree on what is one.
int stdCommTransport_SplitAddress(const char* pText, char* pHostOut, int hostLen, int* pPortOut, int defaultPort);

// A transport's asking (ProbeAddress, CheckPassword) runs on other threads, maybe while the game
// shuts the transport down: each asking goes through its gate, and shutting down closes the gate
// and waits (up to timeoutMs) for those inside to leave. Asking with the gate closed: none.
typedef struct stdCommProbeGate
{
    void* pMutex;           // (an SDL_Mutex)
    volatile int numIn;     // (askings under way)
    volatile int bClosing;  // (askings under way: give up now)
} stdCommProbeGate;
void stdCommTransport_GateOpen(stdCommProbeGate* pGate);   // (the transport's Startup)
void stdCommTransport_GateClose(stdCommProbeGate* pGate, int timeoutMs); // (its Shutdown, first)
int stdCommTransport_GateEnter(stdCommProbeGate* pGate);  // 0: closed, don't ask
void stdCommTransport_GateLeave(stdCommProbeGate* pGate);

// The address asked of every transport at once: as a transport's ProbeAddress, the one that answered
// in *pKindOut (ENet's, if both did)
int stdCommTransport_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing, int* pKindOut);
// The password checked over that kind
int stdCommTransport_CheckPassword(int kind, const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing);

// What stdComm_Open joins over next (the kind the host answered on)
void stdCommTransport_SetJoinKind(int kind);
int stdCommTransport_GetJoinKind(void);

// The game in progress's transport (STDCOMM_TRANSPORT_NONE if none)
int stdCommTransport_GetActiveKind(void);
// Joined over GNS (or DirectPlay): a classic host's game
int stdCommTransport_IsClassic(void);
// Can that game (found over that kind) be joined? 0 (and why, a GUIEXT_ string key) if not
int stdCommTransport_CanJoin(int kind, const jkMultiEntry* pEntry, const char** ppWhyKey);

#endif // PLATFORM_ENET || PLATFORM_GNS

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_STDCOMMTRANSPORT_H

// Added: entire file. The multiplayer connection over ENet (lib/enet: plain
// UDP with its own reliable, ordered delivery, connections and timeouts; a
// few C files, so it builds wherever the game does, iOS included).
//
// It works as the GameNetworkingSockets one does (stdComm_GNS.cpp), so the
// game sees no difference: the host listens (jkGuiNetHost_portNum, 27020 by
// default) and gives each player who connects an id (the host is 1), with the
// session's description; each message carries who it is from and to, and the
// host passes on those meant for another player. Looking for a game at an
// address (the Join screen, -join) is a short connection that just reads that
// description. A player gone quiet for STDCOMM_ENET_TIMEOUT_MS is dropped.
//
// ENet is driven by a thread of its own (under a lock): sends go out and
// arrivals are queued even while the game is busy loading a level, so a long
// load never drops a connection; the game reads the queue each frame.

#include "stdComm_ENet.h"

#ifdef PLATFORM_ENET

#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"

#include "SDL2_helper.h"

#include <enet/enet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#define STDCOMM_ENET_DEFAULT_PORT 27020
#define STDCOMM_ENET_MAX_PEERS 32
#define STDCOMM_ENET_CHANNELS 2       // 0: reliable, in order; 1: unreliable, newest only (positions)
#define STDCOMM_ENET_TIMEOUT_MS 8000  // (a player gone quiet this long is dropped)
// (a host gone quiet this long, for a player: longer, as a host sending a
// level's state to several joining players at once can be slow to answer)
#define STDCOMM_ENET_HOST_TIMEOUT_MS 20000
#define STDCOMM_ENET_MAX_MSG 4096

#define stdComm_ENet_infoPrintf(fmt, ...) stdPlatform_Printf(fmt, ##__VA_ARGS__)
#define stdComm_ENet_verbosePrintf(fmt, ...) do { if (Main_bVerboseNetworking) stdPlatform_Printf(fmt, ##__VA_ARGS__); } while (0)

extern int Main_bVerboseNetworking;
extern char16_t jkGuiMultiplayer_ipText[256];
extern int jkGuiNetHost_portNum;

void Hack_ResetClients();

// What a player who connects is told first: their id and the session
#pragma pack(push, 4)
typedef struct stdCommENetInfo
{
    int id;
    jkMultiEntry entry;
} stdCommENetInfo;
#pragma pack(pop)

// Something that arrived, waiting for the game to read it
typedef struct stdCommENetEvent
{
    struct stdCommENetEvent* pNext;
    int kind;     // 0: a message; 2: a player lost (the host: that player; a player: the host)
    int idFrom;
    int len;
    uint8_t data[];
} stdCommENetEvent;

static int stdComm_ENet_bInitted = 0;
static SDL_Mutex* stdComm_ENet_mutex = NULL;
static SDL_Thread* stdComm_ENet_thread = NULL;
static volatile int stdComm_ENet_bThreadRun = 0;

static ENetHost* stdComm_ENet_pHost = NULL;   // the host's, or a player's connection to it
static ENetPeer* stdComm_ENet_pServer = NULL;  // (a player: the host)
static int stdComm_ENet_bServer = 0;
static uint32_t stdComm_ENet_myId = 0xFFFFFFFF;
static uint64_t stdComm_ENet_usedIds = 0x1;    // (the host: ids given out; bit 0, id 1, its own)
static int stdComm_ENet_bHostLost = 0;

// The game's password, checked by the host: a player's connection carries a hash of the one they typed
// (ENet's connect data); one without the right one gets the game's description only (with no player
// id), as finding games does, and is let go; nothing it sends is taken. The description sent out never
// has the password in it. (A check at the door, not encryption: see the docs on playing over the
// internet.)
#define STDCOMM_ENET_REASON_PASSWORD 0x50415353u // ("PASS")
static uint32_t stdComm_ENet_hostPasswordHash = 0; // (the host's; 0: no password)
static uint64_t stdComm_ENet_limitedIds = 0;       // (the host's: connected without the password)
static int stdComm_ENet_bWrongPassword = 0;        // (a player's: the host said so)

static uint32_t stdComm_ENet_PasswordHash(const char16_t* pw)
{
    if (!pw || !pw[0]) return 0;
    uint32_t h = 2166136261u; // (FNV-1a over the UTF-16 units)
    for (int i = 0; i < 32 && pw[i]; i++) {
        h ^= (uint32_t)(pw[i] & 0xFF); h *= 16777619u;
        h ^= (uint32_t)(pw[i] >> 8); h *= 16777619u;
    }
    return h ? h : 1;
}

static stdCommENetEvent* stdComm_ENet_pQueueHead = NULL;
static stdCommENetEvent* stdComm_ENet_pQueueTail = NULL;

static jkMultiEntry stdComm_ENet_storedEntry;      // the host's session
static jkMultiEntry stdComm_ENet_storedEntryEnum;  // the session found at an address
static int stdComm_ENet_numEnumd = 0;

static uint8_t stdComm_ENet_sendBuffer[STDCOMM_ENET_MAX_MSG];

// ------------------------------------------------------------------ queue

static void stdComm_ENet_Push(int kind, int idFrom, const void* pData, int len)
{
    stdCommENetEvent* e = (stdCommENetEvent*)malloc(sizeof(stdCommENetEvent) + (len > 0 ? len : 0));
    if (!e) return;
    e->pNext = NULL;
    e->kind = kind;
    e->idFrom = idFrom;
    e->len = len;
    if (len > 0) memcpy(e->data, pData, len);
    if (stdComm_ENet_pQueueTail) stdComm_ENet_pQueueTail->pNext = e;
    else stdComm_ENet_pQueueHead = e;
    stdComm_ENet_pQueueTail = e;
}

static stdCommENetEvent* stdComm_ENet_Pop(void)
{
    stdCommENetEvent* e = stdComm_ENet_pQueueHead;
    if (!e) return NULL;
    stdComm_ENet_pQueueHead = e->pNext;
    if (!stdComm_ENet_pQueueHead) stdComm_ENet_pQueueTail = NULL;
    return e;
}

static void stdComm_ENet_ClearQueue(void)
{
    stdCommENetEvent* e;
    while ((e = stdComm_ENet_Pop())) free(e);
}

// ------------------------------------------------------------------ helpers

static int stdComm_ENet_PeerId(ENetPeer* pPeer)
{
    return (int)(intptr_t)pPeer->data;
}

// Which channel a message goes on: the game's position updates (a newer one
// replaces an older one) unreliable, everything else reliable and in order
static int stdComm_ENet_ChannelFor(const void* pData, uint32_t len)
{
    uint32_t id = 0;
    if (len < 4) return 0;
    memcpy(&id, pData, 4);
    // (one the game wants to arrive -- teleports, a joining player's copy of
    // the level -- has a message number in the top half: kept reliable)
    return ((id & 0xFF) == DSS_THINGPOS && !(id >> 16)) ? 1 : 0;
}

static void stdComm_ENet_SendRaw(ENetPeer* pPeer, const void* pData, uint32_t len, int channel)
{
    ENetPacket* pkt = enet_packet_create(pData, len, channel == 0 ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!pkt) return;
    if (enet_peer_send(pPeer, (enet_uint8)channel, pkt) < 0) enet_packet_destroy(pkt);
}

// The host: the players with an id
static int stdComm_ENet_RealConnectedPlayers(void)
{
    int amt = 0;
    for (uint32_t i = 0; i < jkPlayer_maxPlayers; i++)
    {
        if (!i && jkGuiNetHost_bIsDedicated) continue;
        if ((jkPlayer_playerInfos[i].flags & 2) != 0 && !jkPlayer_playerInfos[i].playerNetId) continue;
        amt++;
    }
    return amt;
}

// ------------------------------------------------------------------ the host's events

static void stdComm_ENet_ServerEvent(ENetEvent* ev)
{
    switch (ev->type)
    {
        case ENET_EVENT_TYPE_CONNECT:
        {
            int nextId = 0;
            for (int i = 0; i < 64; i++) {
                if (!(stdComm_ENet_usedIds & (1ULL << i))) {
                    stdComm_ENet_usedIds |= (1ULL << i);
                    nextId = i + 1;
                    break;
                }
            }
            if (!nextId) {
                enet_peer_disconnect_now(ev->peer, 0);
                break;
            }
            ev->peer->data = (void*)(intptr_t)nextId;
            enet_peer_timeout(ev->peer, 32, STDCOMM_ENET_TIMEOUT_MS, STDCOMM_ENET_TIMEOUT_MS);
            stdComm_ENet_verbosePrintf("ENet: connection from %x, id %x\n", ev->peer->address.host, nextId);

            stdComm_ENet_storedEntry.multiModeFlags = sithMulti_multiModeFlags;
            stdCommENetInfo info;
            memset(&info, 0, sizeof(info));
            // (without the game's password: the description only, no id, and let go)
            const int bLimited = stdComm_ENet_hostPasswordHash && ev->data != stdComm_ENet_hostPasswordHash;
            if (bLimited) stdComm_ENet_limitedIds |= (1ULL << (nextId - 1));
            info.id = bLimited ? 0 : nextId;
            info.entry = stdComm_ENet_storedEntry;
            info.entry.numPlayers = stdComm_ENet_RealConnectedPlayers();
            info.entry.maxPlayers = stdComm_ENet_storedEntry.maxPlayers;
            jkPlayer_maxPlayers = stdComm_ENet_storedEntry.maxPlayers;
            memset(info.entry.wPassword, 0, sizeof(info.entry.wPassword)); // (never sent)
            stdComm_ENet_SendRaw(ev->peer, &info, sizeof(info), 0);
            if (bLimited) enet_peer_disconnect_later(ev->peer, STDCOMM_ENET_REASON_PASSWORD);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
        {
            int id = stdComm_ENet_PeerId(ev->peer);
            if (id > 0) {
                stdComm_ENet_verbosePrintf("ENet: id %x gone\n", id);
                for (uint32_t i = 0; i < jkPlayer_maxPlayers; i++) {
                    if (!i && jkGuiNetHost_bIsDedicated) continue;
                    if ((jkPlayer_playerInfos[i].flags & 2) != 0 && jkPlayer_playerInfos[i].playerNetId == id) {
                        stdComm_ENet_Push(2, id, NULL, 0);
                        break;
                    }
                }
                stdComm_ENet_usedIds &= ~(1ULL << (id - 1));
                stdComm_ENet_limitedIds &= ~(1ULL << (id - 1));
            }
            ev->peer->data = NULL;
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
            ENetPacket* p = ev->packet;
            const int peerId = stdComm_ENet_PeerId(ev->peer);
            if (peerId > 0 && (stdComm_ENet_limitedIds & (1ULL << (peerId - 1)))) {
                enet_packet_destroy(p); // (connected without the password: nothing taken)
                break;
            }
            if (p->dataLength >= 8 && p->dataLength <= STDCOMM_ENET_MAX_MSG) {
                uint32_t idFrom, idTo;
                memcpy(&idFrom, p->data, 4);
                memcpy(&idTo, p->data + 4, 4);
                if (idTo && idTo != stdComm_ENet_myId) {
                    // for another player: passed on as it came
                    for (size_t k = 0; k < stdComm_ENet_pHost->peerCount; k++) {
                        ENetPeer* q = &stdComm_ENet_pHost->peers[k];
                        if (q->state == ENET_PEER_STATE_CONNECTED && stdComm_ENet_PeerId(q) == (int)idTo)
                            stdComm_ENet_SendRaw(q, p->data, (uint32_t)p->dataLength, ev->channelID);
                    }
                }
                else {
                    stdComm_ENet_Push(0, (int)idFrom, p->data + 8, (int)p->dataLength - 8);
                }
            }
            enet_packet_destroy(p);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------------ a player's events

static void stdComm_ENet_ClientEvent(ENetEvent* ev)
{
    switch (ev->type)
    {
        case ENET_EVENT_TYPE_CONNECT:
            stdComm_ENet_verbosePrintf("ENet: connected to the host\n");
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            stdComm_ENet_infoPrintf("ENet: lost the host\n");
            stdComm_ENet_pServer = NULL;
            stdComm_ENet_bHostLost = 1;
            break;
        case ENET_EVENT_TYPE_RECEIVE:
        {
            ENetPacket* p = ev->packet;
            if (stdComm_ENet_myId == 0xFFFFFFFF && p->dataLength == sizeof(stdCommENetInfo)) {
                // the first: our id, and the session
                stdCommENetInfo info;
                memcpy(&info, p->data, sizeof(info));
                if (!info.id) {
                    // (no id: the host wants the game's password, and this wasn't it)
                    stdComm_ENet_infoPrintf("ENet: wrong password for this game\n");
                    stdComm_ENet_bWrongPassword = 1;
                    enet_packet_destroy(p);
                    break;
                }
                stdComm_ENet_myId = (uint32_t)info.id;
                stdComm_ENet_storedEntryEnum = info.entry;
                stdComm_ENet_storedEntryEnum.field_E0 = 10;
                sithMulti_multiModeFlags = info.entry.multiModeFlags;
                sithNet_MultiModeFlags = info.entry.multiModeFlags;
                stdComm_ENet_numEnumd = 1;
                stdComm_ENet_verbosePrintf("ENet: we are id %x\n", stdComm_ENet_myId);
            }
            else if (p->dataLength >= 8 && p->dataLength <= STDCOMM_ENET_MAX_MSG) {
                uint32_t idFrom, idTo;
                memcpy(&idFrom, p->data, 4);
                memcpy(&idTo, p->data + 4, 4);
                if (!idTo || idTo == stdComm_ENet_myId)
                    stdComm_ENet_Push(0, (int)idFrom, p->data + 8, (int)p->dataLength - 8);
            }
            enet_packet_destroy(p);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------------ the service thread

static int stdComm_ENet_Thread(void* unused)
{
    (void)unused;
    while (stdComm_ENet_bThreadRun)
    {
        SDL_LockMutex(stdComm_ENet_mutex);
        if (stdComm_ENet_pHost) {
            ENetEvent ev;
            int n = 0;
            while (n++ < 256 && stdComm_ENet_pHost && enet_host_service(stdComm_ENet_pHost, &ev, 0) > 0) {
                if (stdComm_ENet_bServer) stdComm_ENet_ServerEvent(&ev);
                else stdComm_ENet_ClientEvent(&ev);
            }
        }
        SDL_UnlockMutex(stdComm_ENet_mutex);
        SDL_Delay(2);
    }
    return 0;
}

static void stdComm_ENet_StartThread(void)
{
    if (stdComm_ENet_thread) return;
    stdComm_ENet_bThreadRun = 1;
    stdComm_ENet_thread = SDL_CreateThread(stdComm_ENet_Thread, "stdComm_ENet", NULL);
}

static void stdComm_ENet_StopThread(void)
{
    if (!stdComm_ENet_thread) return;
    stdComm_ENet_bThreadRun = 0;
    int ret;
    SDL_WaitThread(stdComm_ENet_thread, &ret);
    stdComm_ENet_thread = NULL;
}

// Everything closed: the host's listening, or the connection to it (the
// others told, briefly, when there is time)
static void stdComm_ENet_CloseHost(void)
{
    SDL_LockMutex(stdComm_ENet_mutex);
    if (stdComm_ENet_pHost) {
        for (size_t k = 0; k < stdComm_ENet_pHost->peerCount; k++) {
            ENetPeer* q = &stdComm_ENet_pHost->peers[k];
            if (q->state == ENET_PEER_STATE_CONNECTED) enet_peer_disconnect(q, 0);
        }
        enet_host_flush(stdComm_ENet_pHost);
        ENetEvent ev;
        // (a moment for the goodbyes to go out)
        for (int i = 0; i < 20 && enet_host_service(stdComm_ENet_pHost, &ev, 5) >= 0; i++) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        }
        enet_host_destroy(stdComm_ENet_pHost);
        stdComm_ENet_pHost = NULL;
    }
    stdComm_ENet_pServer = NULL;
    stdComm_ENet_usedIds = 0x1;
    stdComm_ENet_myId = 0xFFFFFFFF;
    stdComm_ENet_bHostLost = 0;
    stdComm_ENet_ClearQueue();
    SDL_UnlockMutex(stdComm_ENet_mutex);
}

// The address typed (jkGuiMultiplayer_ipText: a name or IPv4, maybe :port)
static int stdComm_ENet_ParseAddress(ENetAddress* pAddr)
{
    char text[256];
    stdString_WcharToChar(text, jkGuiMultiplayer_ipText, 255);
    text[255] = 0;
    // (trimmed)
    char* s = text;
    while (*s == ' ') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
    pAddr->port = STDCOMM_ENET_DEFAULT_PORT;
    char* colon = strrchr(s, ':');
    if (colon) {
        *colon = 0;
        int port = atoi(colon + 1);
        if (port > 0 && port < 65536) pAddr->port = (enet_uint16)port;
    }
    if (!*s) return 0;
    return enet_address_set_host(pAddr, s) == 0;
}

// ------------------------------------------------------------------ finding a game at an address

static int stdComm_EnumThread_bForce = 0;
static int stdComm_EnumThread_bInit = 0;
static SDL_Thread* stdComm_EnumThread_thread = NULL;
static SDL_Mutex* stdComm_EnumThread_mutex = NULL;
static char stdComm_ENet_lastAddr[256];

// A short connection to the address: its session read, then closed
static void stdComm_ENet_GetServerInfo(const ENetAddress* pAddr)
{
    ENetHost* h = enet_host_create(NULL, 1, STDCOMM_ENET_CHANNELS, 0, 0);
    if (!h) return;
    ENetPeer* p = enet_host_connect(h, pAddr, STDCOMM_ENET_CHANNELS, 0);
    int bFound = 0;
    if (p) {
        enet_peer_timeout(p, 32, 1500, 1500);
        ENetEvent ev;
        uint32_t start = stdPlatform_GetTimeMsec();
        while (!bFound && stdPlatform_GetTimeMsec() - start < 1500 && stdComm_EnumThread_bInit) {
            if (enet_host_service(h, &ev, 50) <= 0) continue;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                if (ev.packet->dataLength == sizeof(stdCommENetInfo)) {
                    stdCommENetInfo info;
                    memcpy(&info, ev.packet->data, sizeof(info));
                    SDL_LockMutex(stdComm_EnumThread_mutex);
                    stdComm_ENet_storedEntryEnum = info.entry;
                    stdComm_ENet_storedEntryEnum.field_E0 = 10;
                    sithMulti_multiModeFlags = info.entry.multiModeFlags;
                    sithNet_MultiModeFlags = info.entry.multiModeFlags;
                    stdComm_ENet_numEnumd = 1;
                    SDL_UnlockMutex(stdComm_EnumThread_mutex);
                    bFound = 1;
                }
                enet_packet_destroy(ev.packet);
            }
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                break;
            }
        }
        enet_peer_disconnect_now(p, 0);
        enet_host_flush(h);
    }
    enet_host_destroy(h);
    if (!bFound) {
        SDL_LockMutex(stdComm_EnumThread_mutex);
        stdComm_ENet_numEnumd = 0;
        SDL_UnlockMutex(stdComm_EnumThread_mutex);
    }
    // (the Join screen redrawn)
    jkGuiMultiplayer_dword_5564E8 -= 10000;
}

static int stdComm_ENet_EnumThread(void* unused)
{
    (void)unused;
    while (stdComm_EnumThread_bInit)
    {
        char text[256];
        stdString_WcharToChar(text, jkGuiMultiplayer_ipText, 255);
        text[255] = 0;
        int bAgain = stdComm_EnumThread_bForce || strncmp(text, stdComm_ENet_lastAddr, sizeof(text));
        if (bAgain) {
            ENetAddress addr;
            if (stdComm_ENet_ParseAddress(&addr)) stdComm_ENet_GetServerInfo(&addr);
            else {
                SDL_LockMutex(stdComm_EnumThread_mutex);
                stdComm_ENet_numEnumd = 0;
                SDL_UnlockMutex(stdComm_EnumThread_mutex);
            }
            strncpy(stdComm_ENet_lastAddr, text, sizeof(stdComm_ENet_lastAddr));
            stdComm_EnumThread_bForce = 0;
        }
        SDL_Delay(100);
    }
    return 0;
}

// ------------------------------------------------------------------ the game's side

void Hack_ResetClients()
{
    DirectPlay_numPlayers = 32;
    for (int i = 0; i < 32; i++)
    {
        DirectPlay_aPlayers[i].dpId = i + 1;
        jk_snwprintf(DirectPlay_aPlayers[i].waName, 32, u"asdf");
    }
    stdComm_dplayIdSelf = stdComm_bIsServer ? 1 : stdComm_ENet_myId;
}

void stdComm_ENet_Startup()
{
    if (!stdComm_ENet_bInitted) {
        if (enet_initialize() != 0) {
            stdComm_ENet_infoPrintf("ENet: failed to start\n");
            jkGuiMultiplayer_numConnections = 1;
            jk_snwprintf(jkGuiMultiplayer_aConnections[0].name, 0x80, u"No network");
            stdComm_dword_8321E0 = 0;
            memset(jkGuiMultiplayer_aEntries, 0, sizeof(jkMultiEntry) * 32);
            dplay_dword_55D618 = 0;
            return;
        }
        stdComm_ENet_mutex = SDL_CreateMutex();
        stdComm_ENet_bInitted = 1;
    }
    jkGuiMultiplayer_numConnections = 1;
    jk_snwprintf(jkGuiMultiplayer_aConnections[0].name, 0x80, u"Local network / Internet (UDP)");
    stdComm_dword_8321E0 = 0;
    memset(jkGuiMultiplayer_aEntries, 0, sizeof(jkMultiEntry) * 32);
    dplay_dword_55D618 = 0;
    Hack_ResetClients();
}

void stdComm_ENet_Shutdown()
{
    if (!stdComm_ENet_bInitted) return;
    DirectPlay_EnumSessions2();
    stdComm_ENet_StopThread();
    stdComm_ENet_CloseHost();
    enet_deinitialize();
    stdComm_ENet_bInitted = 0;
}

void stdComm_ENet_GetLocalAddresses(char* pOut, int outLen)
{
    if (!pOut || outLen <= 0) return;
    pOut[0] = 0;
#ifndef _WIN32
    struct ifaddrs* pList = NULL;
    if (getifaddrs(&pList) != 0) return;
    for (struct ifaddrs* a = pList; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (a->ifa_flags & IFF_LOOPBACK) continue;
        if (!(a->ifa_flags & IFF_UP)) continue;
        const uint32_t h = ntohl(((struct sockaddr_in*)a->ifa_addr)->sin_addr.s_addr);
        const char* nm = a->ifa_name ? a->ifa_name : "";
        // (not the phone's mobile data, nor iOS's own links; a VPN only if it
        // is Tailscale's, 100.64.0.0/10, which friends elsewhere can reach)
        if (!strncmp(nm, "pdp_ip", 6) || !strncmp(nm, "awdl", 4) || !strncmp(nm, "llw", 3) || !strncmp(nm, "ipsec", 5)) continue;
        if (!strncmp(nm, "utun", 4) && (h & 0xFFC00000u) != 0x64400000u) continue;
        if ((h & 0xFFFF0000u) == 0xA9FE0000u) continue; // (169.254: no network yet)
        char ip[64];
        inet_ntop(AF_INET, &((struct sockaddr_in*)a->ifa_addr)->sin_addr, ip, sizeof(ip));
        int n = (int)strlen(pOut);
        snprintf(pOut + n, outLen - n, "%s%s", n ? ", " : "", ip);
    }
    freeifaddrs(pList);
#endif
}

int DirectPlay_Receive(int *pIdOut, int *pMsgIdOut, int *pLenOut)
{
    int maxLen = *pLenOut;
    *pIdOut = 0;
    *pLenOut = 0;
    if (!stdComm_ENet_bInitted) return -1;

    Hack_ResetClients();

    SDL_LockMutex(stdComm_ENet_mutex);
    stdCommENetEvent* e = stdComm_ENet_Pop();
    int bHostLost = !e && !stdComm_ENet_bServer && stdComm_ENet_bHostLost;
    if (bHostLost) stdComm_ENet_bHostLost = 0;
    SDL_UnlockMutex(stdComm_ENet_mutex);

    if (bHostLost) {
        *pIdOut = 1;
        return 2;
    }
    if (!e) return -1;

    int ret;
    if (e->kind == 2) {
        *pIdOut = e->idFrom;
        ret = 2;
    }
    else {
        int n = e->len < maxLen ? e->len : maxLen;
        memcpy(pMsgIdOut, e->data, n);
        *pLenOut = n;
        *pIdOut = e->idFrom;
        stdComm_ENet_verbosePrintf("Recv %x bytes from %x (%s, %x)\n", n, e->idFrom, sithDSS_IdToStr(*(uint32_t*)pMsgIdOut & 0xFF), *(uint32_t*)pMsgIdOut);
        ret = 0;
    }
    free(e);
    return ret;
}

BOOL DirectPlay_Send(DPID idFrom, DPID idTo, void *lpData, DWORD dwDataSize)
{
    if (!stdComm_ENet_bInitted) return 0;

    Hack_ResetClients();

    if (dwDataSize > STDCOMM_ENET_MAX_MSG - 8) dwDataSize = STDCOMM_ENET_MAX_MSG - 8;
    int ret = 1;
    SDL_LockMutex(stdComm_ENet_mutex);
    uint32_t from = (uint32_t)idFrom, to = (uint32_t)idTo;
    memcpy(&stdComm_ENet_sendBuffer[0], &from, 4);
    memcpy(&stdComm_ENet_sendBuffer[4], &to, 4);
    memcpy(&stdComm_ENet_sendBuffer[8], lpData, dwDataSize);
    const int channel = stdComm_ENet_ChannelFor(lpData, dwDataSize);
    if (!stdComm_ENet_pHost) {
        ret = 0;
    }
    else if (stdComm_ENet_bServer) {
        for (size_t k = 0; k < stdComm_ENet_pHost->peerCount; k++) {
            ENetPeer* q = &stdComm_ENet_pHost->peers[k];
            if (q->state == ENET_PEER_STATE_CONNECTED && stdComm_ENet_PeerId(q) == (int)to)
                stdComm_ENet_SendRaw(q, stdComm_ENet_sendBuffer, dwDataSize + 8, channel);
        }
    }
    else if (stdComm_ENet_pServer && stdComm_ENet_pServer->state == ENET_PEER_STATE_CONNECTED) {
        stdComm_ENet_SendRaw(stdComm_ENet_pServer, stdComm_ENet_sendBuffer, dwDataSize + 8, channel);
    }
    else {
        ret = 0;
    }
    if (stdComm_ENet_pHost) enet_host_flush(stdComm_ENet_pHost);
    SDL_UnlockMutex(stdComm_ENet_mutex);
    stdComm_ENet_verbosePrintf("Sent %x bytes to %x (%s, %x)\n", dwDataSize + 8, idTo, sithDSS_IdToStr(*(uint32_t*)lpData & 0xFF), *(uint32_t*)lpData);
    return ret;
}

int stdComm_OpenConnection(void* a)
{
    stdComm_dword_8321DC = 1;
    return 0;
}

void stdComm_CloseConnection()
{
    if (stdComm_dword_8321DC)
    {
        if (stdComm_dword_8321E0)
        {
            DirectPlay_Close();
            stdComm_dword_8321E0 = 0;
            stdComm_bIsServer = 0;
            stdComm_dplayIdSelf = 0;
        }
        stdComm_dword_8321DC = 0;
    }
}

// Joining the game found at the address typed
int stdComm_Open(int idx, char16_t* pwPassword)
{
    DirectPlay_EnumSessions2();

    stdComm_dword_8321E8 = 0;
    stdComm_dword_8321E0 = 1;
    stdComm_dplayIdSelf = DirectPlay_CreatePlayer(jkPlayer_playerShortName, 0);
    stdComm_bIsServer = 0;
    jkGuiMultiplayer_checksumSeed = jkGuiMultiplayer_aEntries[idx].checksumSeed;

    if (!stdComm_ENet_bInitted) return 0;

    stdComm_ENet_CloseHost();
    ENetAddress addr;
    if (!stdComm_ENet_ParseAddress(&addr)) return 0x88770154 + 1;

    SDL_LockMutex(stdComm_ENet_mutex);
    stdComm_ENet_bServer = 0;
    stdComm_ENet_myId = 0xFFFFFFFF;
    stdComm_ENet_pHost = enet_host_create(NULL, 1, STDCOMM_ENET_CHANNELS, 0, 0);
    stdComm_ENet_bWrongPassword = 0;
    stdComm_ENet_pServer = stdComm_ENet_pHost ? enet_host_connect(stdComm_ENet_pHost, &addr, STDCOMM_ENET_CHANNELS, stdComm_ENet_PasswordHash(pwPassword)) : NULL;
    if (stdComm_ENet_pServer) enet_peer_timeout(stdComm_ENet_pServer, 32, STDCOMM_ENET_HOST_TIMEOUT_MS, STDCOMM_ENET_HOST_TIMEOUT_MS);
    SDL_UnlockMutex(stdComm_ENet_mutex);
    if (!stdComm_ENet_pServer) return 0x88770154 + 1;

    stdComm_ENet_StartThread();
    // (our id from the host, before the game sends anything: up to 5 s)
    for (int i = 0; i < 500; i++) {
        SDL_LockMutex(stdComm_ENet_mutex);
        int bDone = stdComm_ENet_myId != 0xFFFFFFFF || stdComm_ENet_bHostLost || stdComm_ENet_bWrongPassword;
        SDL_UnlockMutex(stdComm_ENet_mutex);
        if (bDone) break;
        SDL_Delay(10);
    }
    if (stdComm_ENet_myId == 0xFFFFFFFF) {
        stdComm_ENet_CloseHost();
        return 0x88770154 + 1;
    }
    Hack_ResetClients();
    return 0;
}

void stdComm_Close()
{
    if (!stdComm_ENet_bInitted) return;
    stdComm_ENet_CloseHost();
    stdComm_ENet_bServer = 0;
}

int DirectPlay_SendLobbyMessage(void* pPkt, uint32_t pktLen)
{
    return 1;
}

void DirectPlay_SetSessionDesc(const char* a1, DWORD maxPlayers)
{
    _strncpy(stdComm_ENet_storedEntry.mapJklFname, jkMain_aLevelJklFname, 0x20);
}

BOOL DirectPlay_SetSessionFlagidk(int a1)
{
    return 1;
}

BOOL DirectPlay_Startup()
{
    jkGuiMultiplayer_numConnections = 0;
    memset(&jkGuiMultiplayer_aConnections, 0, 0x1180u);
    return 1;
}

int DirectPlay_EarlyInit(char16_t* pwIdk, char16_t* pwPlayerName)
{
    return 0;
}

DPID DirectPlay_CreatePlayer(char16_t* pwIdk, int idk2)
{
    return 1;
}

void DirectPlay_Close()
{
}

// Hosting: listening for players
int DirectPlay_OpenHost(jkMultiEntry* pEntry)
{
    stdComm_ENet_storedEntry = *pEntry;
    stdComm_ENet_hostPasswordHash = stdComm_ENet_PasswordHash(pEntry->wPassword);
    stdComm_ENet_limitedIds = 0;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
    stdComm_bIsServer = 1;

    if (!stdComm_ENet_bInitted) return 0;

    stdComm_ENet_CloseHost();
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)(jkGuiNetHost_portNum > 0 ? jkGuiNetHost_portNum : STDCOMM_ENET_DEFAULT_PORT);
    SDL_LockMutex(stdComm_ENet_mutex);
    stdComm_ENet_bServer = 1;
    stdComm_ENet_myId = 1;
    stdComm_ENet_usedIds = 0x1;
    stdComm_ENet_pHost = enet_host_create(&addr, STDCOMM_ENET_MAX_PEERS, STDCOMM_ENET_CHANNELS, 0, 0);
    SDL_UnlockMutex(stdComm_ENet_mutex);
    if (!stdComm_ENet_pHost) {
        stdComm_ENet_infoPrintf("ENet: can't listen on port %d\n", addr.port);
        return 0x88770118 + 1;
    }
    stdComm_ENet_infoPrintf("ENet: hosting on port %d\n", addr.port);
    stdComm_ENet_StartThread();
    Hack_ResetClients();
    return 0;
}

int DirectPlay_GetSession_passwordidk(jkMultiEntry* pEntry)
{
    stdComm_ENet_storedEntry = *pEntry;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
    return 1;
}

int DirectPlay_EnumSessions2()
{
    if (!stdComm_EnumThread_bInit) return 0;
    stdComm_EnumThread_bInit = 0;
    int ret;
    SDL_WaitThread(stdComm_EnumThread_thread, &ret);
    stdComm_EnumThread_thread = NULL;
    return 0;
}

int stdComm_EnumSessions(int a, void* b)
{
    if (!stdComm_ENet_bInitted) return 0;
    if (!stdComm_EnumThread_mutex) stdComm_EnumThread_mutex = SDL_CreateMutex();

    SDL_LockMutex(stdComm_EnumThread_mutex);
    stdComm_EnumThread_bForce = 1;
    jkGuiMultiplayer_aEntries[0] = stdComm_ENet_storedEntryEnum;
    dplay_dword_55D618 = stdComm_ENet_numEnumd;
    SDL_UnlockMutex(stdComm_EnumThread_mutex);

    if (stdComm_EnumThread_bInit) return 0;

    Hack_ResetClients();
    dplay_dword_55D618 = 0;
    stdComm_ENet_numEnumd = 0;
    memset(&stdComm_ENet_storedEntryEnum, 0, sizeof(stdComm_ENet_storedEntryEnum));
    memset(&jkGuiMultiplayer_aEntries[0], 0, sizeof(jkGuiMultiplayer_aEntries[0]));
    memset(stdComm_ENet_lastAddr, 0, sizeof(stdComm_ENet_lastAddr));
    stdComm_EnumThread_bInit = 1;
    stdComm_EnumThread_thread = SDL_CreateThread(stdComm_ENet_EnumThread, "stdComm_ENet_Enum", NULL);
    return 0;
}

void DirectPlay_EnumPlayers(int a)
{
    Hack_ResetClients();
}

int DirectPlay_StartSession(void* a, void* b)
{
    return 1;
}

void DirectPlay_Destroy()
{
}

int DirectPlay_IdkSessionDesc(jkMultiEntry* pEntry)
{
    return 1;
}

#endif // PLATFORM_ENET

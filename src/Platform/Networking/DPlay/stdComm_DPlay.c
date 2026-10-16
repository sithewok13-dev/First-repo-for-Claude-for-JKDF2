// Added: entire file. Experimental: the original 1997 game's multiplayer connection, DirectPlay 4
// over its TCP/IP service provider, spoken directly (no DirectPlay library), as a third transport
// next to ENet and GNS (Platform/Networking/stdCommTransport.h). Built with -DOPENJKDF2_NET_DPLAY=ON.
//
// The wire format is DirectPlay's own (Microsoft's [MS-DPDX] and [MS-DPSP]): every system message is
// "play", a command and a version, after a 20-byte header that carries its size and the TCP port to
// answer on. Looking for a game: an ENUMSESSIONS datagram to the host's UDP port 47624, answered over
// TCP to our own listening port (2300-2349) with the session's description. Joining: two player ids
// asked of the host (REQUESTPLAYERID: our system player, then our player), ADDFORWARDREQUEST (our
// system player and its addresses: the host answers with every player in SUPERENUMPLAYERSREPLY), and
// CREATEPLAYER (our player, told to the others). The game's own messages then go as the original
// sends them (IDirectPlay::Send without DPSEND_GUARANTEED): a UDP datagram to the player's system
// player's port (2350-2399), "from id, to id" and then the message as the game made it.
//
// The original game describes its session in DPSESSIONDESC2 (its DirectPlay_OpenHost and
// DirectPlay_parseSessionDescidk): the name is "game name:episode:level", dwUser1 the world checksum's
// seed, dwUser2 the mode flags, dwUser3 the max rank (high half) and field_E0 (low half), dwUser4 the
// tick rate; its application GUID is {BF0613C0-DE79-11D0-99C9-00A02476AD4B}.
//
// Hosting (-dplayHost) answers the same messages, so the original game can join. Not done yet: games
// of three or more (each player told of the others: ADDFORWARD), groups, keepalive pings sent, the
// host's address behind a router (DirectPlay connects back to the player, so a player behind NAT
// needs its ports open), IPv6, Windows sockets.

#include "stdComm_DPlay.h"
#include "Platform/Networking/stdCommTransport.h"

#ifdef PLATFORM_DPLAY

#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"
#include "Main/Main.h"

#include "SDL2_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define DPLAY_ENUM_PORT       47624
#define DPLAY_TCP_FIRST       2300
#define DPLAY_TCP_LAST        2349
#define DPLAY_UDP_FIRST       2350
#define DPLAY_UDP_LAST        2399
#define DPLAY_MAGIC           0x79616c70u // "play"
#define DPLAY_VERSION         14
#define DPLAY_TOKEN           0xFAB
#define DPLAY_MAX_MSG         (64 * 1024)
#define DPLAY_MAX_PLAYERS     64
#define DPLAY_MAX_CONNS       16

enum
{
    DPCMD_ENUMSESSIONSREPLY = 1,
    DPCMD_ENUMSESSIONS = 2,
    DPCMD_REQUESTPLAYERID = 5,
    DPCMD_REQUESTPLAYERREPLY = 7,
    DPCMD_CREATEPLAYER = 8,
    DPCMD_DELETEPLAYER = 11,
    DPCMD_ADDFORWARDREQUEST = 19,
    DPCMD_PING = 22,
    DPCMD_PINGREPLY = 23,
    DPCMD_ADDFORWARDREPLY = 36,
    DPCMD_SUPERENUMPLAYERSREPLY = 41,
    DPCMD_ADDFORWARD = 46,
    DPCMD_ADDFORWARDACK = 47,
};

#define DPLAYI_PLAYER_SYSPLAYER  0x1
#define DPLAYI_PLAYER_NAMESRVR   0x2
#define DPLAYI_PLAYER_INGROUP    0x4
#define DPLAYI_PLAYER_LOCAL      0x8

#define stdComm_DPlay_infoPrintf(fmt, ...) stdPlatform_Printf(fmt, ##__VA_ARGS__)
#define stdComm_DPlay_verbosePrintf(fmt, ...) do { if (Main_bVerboseNetworking) stdPlatform_Printf(fmt, ##__VA_ARGS__); } while (0)

extern int Main_bVerboseNetworking;

int stdComm_DPlay_bHostOverDPlay = 0;

// The original game's application GUID, as it is on the wire (Windows_DplayGuid)
static const uint8_t stdComm_DPlay_appGuid[16] = {
    0xC0, 0x13, 0x06, 0xBF, 0x79, 0xDE, 0xD0, 0x11, 0x99, 0xC9, 0x00, 0xA0, 0x24, 0x76, 0xAD, 0x4B,
};

typedef struct dpPlayer
{
    uint32_t id;
    uint32_t sysId;          // (a player's system player; a system player's own id)
    uint32_t flags;          // DPLAYI_PLAYER_*
    char16_t name[32];
    uint32_t ip;             // (network order; where its system player is)
    uint16_t tcpPort, udpPort;
    int bUsed;
} dpPlayer;

typedef struct dpConn
{
    int fd;
    uint32_t ip;             // (network order)
    uint16_t port;           // (an outgoing one: the peer's listening port)
    uint8_t* pBuf;
    int len;
} dpConn;

typedef struct dpEvent
{
    struct dpEvent* pNext;
    int kind;                // 0: a message, 2: a player gone
    uint32_t idFrom;
    int len;
    uint8_t data[];
} dpEvent;

typedef struct dpProbe
{
    uint32_t ip;
    int bAnswered;
    uint16_t tcpPort;
    uint8_t dpsd[80];
    char16_t name[128];
} dpProbe;

static int stdComm_DPlay_bInitted = 0;
static SDL_Mutex* stdComm_DPlay_mutex = NULL;
static SDL_Thread* stdComm_DPlay_thread = NULL;
static volatile int stdComm_DPlay_bThreadRun = 0;
static stdCommProbeGate stdComm_DPlay_probeGate;

static int stdComm_DPlay_tcpListen = -1, stdComm_DPlay_udp = -1, stdComm_DPlay_enumUdp = -1;
static uint16_t stdComm_DPlay_tcpPort = 0, stdComm_DPlay_udpPort = 0;
static dpConn stdComm_DPlay_aIn[DPLAY_MAX_CONNS];
static dpConn stdComm_DPlay_aOut[DPLAY_MAX_CONNS];

static dpPlayer stdComm_DPlay_aPlayers[DPLAY_MAX_PLAYERS];
static int stdComm_DPlay_bActive = 0;      // (a game joined or hosted)
static int stdComm_DPlay_bServer = 0;
static uint32_t stdComm_DPlay_mySysId = 0, stdComm_DPlay_myId = 0;
static uint32_t stdComm_DPlay_hostIp = 0;  // (a player: the host's)
static uint16_t stdComm_DPlay_hostTcpPort = 0;
static uint32_t stdComm_DPlay_hostId = 0;  // (the host's player)
static uint32_t stdComm_DPlay_nextId = 0;
static uint32_t stdComm_DPlay_reserved1 = 0; // (hosting: the session's dwReserved1)
static int stdComm_DPlay_bHostLost = 0;

static jkMultiEntry stdComm_DPlay_entry;   // the session (hosting), or the one joined
static uint8_t stdComm_DPlay_dpsd[80];     // its DPSESSIONDESC2, as on the wire
static char16_t stdComm_DPlay_password[33];

// (joining: the host's answers)
static volatile uint32_t stdComm_DPlay_replyId = 0;
static volatile int stdComm_DPlay_bSuperEnum = 0;
static volatile uint32_t stdComm_DPlay_nackResult = 0;

static dpProbe stdComm_DPlay_aProbe[8];

static dpEvent* stdComm_DPlay_pQueueHead = NULL;
static dpEvent* stdComm_DPlay_pQueueTail = NULL;

// ------------------------------------------------------------------ little helpers

static void dp_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void dp_put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint16_t dp_get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t dp_get32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// A SOCKADDR_IN as Windows has it (family little-endian, port and address in network order)
static void dp_putAddr(uint8_t* p, uint16_t port, uint32_t ipNet)
{
    memset(p, 0, 16);
    dp_put16(p, AF_INET);
    p[2] = (uint8_t)(port >> 8); p[3] = (uint8_t)port;
    memcpy(p + 4, &ipNet, 4);
}
static uint16_t dp_addrPort(const uint8_t* p) { return (uint16_t)((p[2] << 8) | p[3]); }
static uint32_t dp_addrIp(const uint8_t* p) { uint32_t ip; memcpy(&ip, p + 4, 4); return ip; }

// The 20-byte header every TCP message (and an ENUMSESSIONS) starts with: size, token, our TCP port
static void dp_putSpHeader(uint8_t* p, uint32_t size)
{
    dp_put32(p, (DPLAY_TOKEN << 20) | (size & 0xFFFFF));
    dp_putAddr(p + 4, stdComm_DPlay_tcpPort, 0);
}

static int dp_putMsgHeader(uint8_t* p, uint16_t cmd)
{
    dp_put32(p, DPLAY_MAGIC);
    dp_put16(p + 4, cmd);
    dp_put16(p + 6, DPLAY_VERSION);
    return 8;
}

static int dp_wlen(const char16_t* s, int max)
{
    int n = 0;
    while (s && n < max && s[n]) n++;
    return n;
}

// A string as DirectPlay sends it: UTF-16LE, ended
static int dp_putWstr(uint8_t* p, const char16_t* s, int max)
{
    const int n = dp_wlen(s, max);
    for (int i = 0; i < n; i++) dp_put16(p + i * 2, (uint16_t)s[i]);
    dp_put16(p + n * 2, 0);
    return (n + 1) * 2;
}

static int dp_getWstr(char16_t* pOut, int outLen, const uint8_t* p, int avail)
{
    int i = 0, used = 0;
    while (used + 2 <= avail) {
        const uint16_t c = dp_get16(p + used);
        used += 2;
        if (!c) break;
        if (i < outLen - 1) pOut[i++] = c;
    }
    if (outLen > 0) pOut[i] = 0;
    return used;
}

static void dp_queuePush(int kind, uint32_t idFrom, const void* pData, int len)
{
    dpEvent* e = (dpEvent*)malloc(sizeof(dpEvent) + (len > 0 ? len : 0));
    if (!e) return;
    e->pNext = NULL;
    e->kind = kind;
    e->idFrom = idFrom;
    e->len = len;
    if (len > 0) memcpy(e->data, pData, len);
    if (stdComm_DPlay_pQueueTail) stdComm_DPlay_pQueueTail->pNext = e;
    else stdComm_DPlay_pQueueHead = e;
    stdComm_DPlay_pQueueTail = e;
}

static dpEvent* dp_queuePop(void)
{
    dpEvent* e = stdComm_DPlay_pQueueHead;
    if (!e) return NULL;
    stdComm_DPlay_pQueueHead = e->pNext;
    if (!stdComm_DPlay_pQueueHead) stdComm_DPlay_pQueueTail = NULL;
    return e;
}

static void dp_queueClear(void)
{
    dpEvent* e;
    while ((e = dp_queuePop())) free(e);
}

// ------------------------------------------------------------------ players

static dpPlayer* dp_findPlayer(uint32_t id)
{
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++)
        if (stdComm_DPlay_aPlayers[i].bUsed && stdComm_DPlay_aPlayers[i].id == id) return &stdComm_DPlay_aPlayers[i];
    return NULL;
}

static dpPlayer* dp_addPlayer(uint32_t id)
{
    dpPlayer* p = dp_findPlayer(id);
    if (p) return p;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        if (!stdComm_DPlay_aPlayers[i].bUsed) {
            p = &stdComm_DPlay_aPlayers[i];
            memset(p, 0, sizeof(*p));
            p->bUsed = 1;
            p->id = id;
            return p;
        }
    }
    return NULL;
}

// A player gone, and the players of a system player gone with it: the game told of each
static void dp_removePlayer(uint32_t id)
{
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        dpPlayer* p = &stdComm_DPlay_aPlayers[i];
        if (!p->bUsed) continue;
        if (p->id == id || (p->sysId == id && !(p->flags & DPLAYI_PLAYER_SYSPLAYER))) {
            if (!(p->flags & DPLAYI_PLAYER_SYSPLAYER)) dp_queuePush(2, p->id, NULL, 0);
            p->bUsed = 0;
        }
    }
}

static int dp_numGamePlayers(void)
{
    int n = 0;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++)
        if (stdComm_DPlay_aPlayers[i].bUsed && !(stdComm_DPlay_aPlayers[i].flags & DPLAYI_PLAYER_SYSPLAYER)) n++;
    return n;
}

// ------------------------------------------------------------------ sockets

static void dp_nonblock(int fd)
{
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int dp_bindRange(int type, uint16_t first, uint16_t last, uint16_t* pPortOut)
{
    for (uint32_t port = first; port <= last; port++) {
        int fd = socket(AF_INET, type, 0);
        if (fd < 0) return -1;
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
            if (type == SOCK_STREAM && listen(fd, 8) != 0) { close(fd); continue; }
            dp_nonblock(fd);
            *pPortOut = (uint16_t)port;
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void dp_closeConn(dpConn* c)
{
    if (c->fd >= 0) close(c->fd);
    free(c->pBuf);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static void dp_closeAll(void)
{
    for (int i = 0; i < DPLAY_MAX_CONNS; i++) {
        if (stdComm_DPlay_aIn[i].fd >= 0) dp_closeConn(&stdComm_DPlay_aIn[i]);
        if (stdComm_DPlay_aOut[i].fd >= 0) dp_closeConn(&stdComm_DPlay_aOut[i]);
    }
}

// Our listening sockets (TCP 2300-2349, UDP 2350-2399), made once
static int dp_openSockets(void)
{
    if (stdComm_DPlay_tcpListen < 0)
        stdComm_DPlay_tcpListen = dp_bindRange(SOCK_STREAM, DPLAY_TCP_FIRST, DPLAY_TCP_LAST, &stdComm_DPlay_tcpPort);
    if (stdComm_DPlay_udp < 0) {
        stdComm_DPlay_udp = dp_bindRange(SOCK_DGRAM, DPLAY_UDP_FIRST, DPLAY_UDP_LAST, &stdComm_DPlay_udpPort);
        if (stdComm_DPlay_udp >= 0) {
            int one = 1;
            setsockopt(stdComm_DPlay_udp, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        }
    }
    if (stdComm_DPlay_tcpListen < 0 || stdComm_DPlay_udp < 0) {
        stdComm_DPlay_infoPrintf("DPlay: no free ports (TCP %d-%d, UDP %d-%d)\n", DPLAY_TCP_FIRST, DPLAY_TCP_LAST, DPLAY_UDP_FIRST, DPLAY_UDP_LAST);
        return 0;
    }
    return 1;
}

// A connection to a peer's listening port, made when first needed (DirectPlay sends each way on a
// connection of its own: what we send goes on ours, what we're sent comes on the peer's)
static dpConn* dp_outConn(uint32_t ip, uint16_t port)
{
    dpConn* pFree = NULL;
    for (int i = 0; i < DPLAY_MAX_CONNS; i++) {
        dpConn* c = &stdComm_DPlay_aOut[i];
        if (c->fd >= 0 && c->ip == ip && c->port == port) return c;
        if (c->fd < 0 && !pFree) pFree = c;
    }
    if (!pFree) return NULL;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    dp_nonblock(fd);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0 && errno != EINPROGRESS) {
        close(fd);
        return NULL;
    }
    // (up to 2 s to connect: on a local network, at once)
    struct pollfd pf = { fd, POLLOUT, 0 };
    int err = 0;
    socklen_t errLen = sizeof(err);
    if (poll(&pf, 1, 2000) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errLen) != 0 || err) {
        close(fd);
        return NULL;
    }
    pFree->fd = fd;
    pFree->ip = ip;
    pFree->port = port;
    return pFree;
}

static int dp_sendAll(int fd, const uint8_t* p, int len)
{
    int sent = 0;
    uint32_t start = stdPlatform_GetTimeMsec();
    while (sent < len) {
#ifdef MSG_NOSIGNAL
        const int n = (int)send(fd, p + sent, len - sent, MSG_NOSIGNAL);
#else
        const int n = (int)send(fd, p + sent, len - sent, 0);
#endif
        if (n > 0) { sent += n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && stdPlatform_GetTimeMsec() - start < 2000) {
            struct pollfd pf = { fd, POLLOUT, 0 };
            poll(&pf, 1, 50);
            continue;
        }
        return 0;
    }
    return 1;
}

// A system message (its 20-byte header filled in here) to a peer's TCP port
static int dp_sendTcp(uint32_t ip, uint16_t port, uint8_t* pMsg, int len)
{
    dp_putSpHeader(pMsg, (uint32_t)len);
    dpConn* c = dp_outConn(ip, port);
    if (!c) return 0;
    if (!dp_sendAll(c->fd, pMsg, len)) {
        dp_closeConn(c);
        return 0;
    }
    return 1;
}

static void dp_sendUdp(uint32_t ip, uint16_t port, const uint8_t* p, int len)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    sendto(stdComm_DPlay_udp, p, len, 0, (struct sockaddr*)&a, sizeof(a));
}

// ------------------------------------------------------------------ the session's description

static void dp_makeSessionDesc(void)
{
    const jkMultiEntry* e = &stdComm_DPlay_entry;
    uint8_t* d = stdComm_DPlay_dpsd;
    memset(d, 0, 80);
    dp_put32(d + 0x00, 80);
    // (as the original's DirectPlay_OpenHost: keepalive, no data messages, no host migration)
    dp_put32(d + 0x04, ((uint32_t)e->sessionFlags | 0xC0) & ~4u);
    memcpy(d + 0x08, &e->guidInstance, 16);
    memcpy(d + 0x18, stdComm_DPlay_appGuid, 16);
    dp_put32(d + 0x28, (uint32_t)e->maxPlayers);
    dp_put32(d + 0x2C, (uint32_t)dp_numGamePlayers());
    dp_put32(d + 0x38, stdComm_DPlay_reserved1); // (dwReserved1: echoed by those joining)
    dp_put32(d + 0x40, (uint32_t)e->checksumSeed);
    dp_put32(d + 0x44, (uint32_t)e->multiModeFlags);
    dp_put32(d + 0x48, ((uint32_t)e->maxRank << 16) | ((uint32_t)e->field_E0 & 0xFFFF));
    dp_put32(d + 0x4C, (uint32_t)e->tickRateMs);
}

// "game name:episode:level", as the original names its session
static int dp_putSessionName(uint8_t* p)
{
    char16_t w[128];
    char16_t ep[33], map[33];
    stdString_CharToWchar(ep, stdComm_DPlay_entry.episodeGobName, 32);
    ep[32] = 0;
    stdString_CharToWchar(map, stdComm_DPlay_entry.mapJklFname, 32);
    map[32] = 0;
    int n = 0;
    for (int i = 0; i < 32 && stdComm_DPlay_entry.serverName[i] && n < 120; i++) w[n++] = stdComm_DPlay_entry.serverName[i];
    w[n++] = ':';
    for (int i = 0; ep[i] && n < 124; i++) w[n++] = ep[i];
    w[n++] = ':';
    for (int i = 0; map[i] && n < 127; i++) w[n++] = map[i];
    w[n] = 0;
    return dp_putWstr(p, w, 127);
}

// A session's description (and name) as the original reads it (DirectPlay_parseSessionDescidk)
static void dp_parseSessionDesc(const uint8_t* d, const char16_t* pName, jkMultiEntry* pOut)
{
    memset(pOut, 0, sizeof(*pOut));
    memcpy(&pOut->guidInstance, d + 0x08, 16);
    pOut->maxPlayers = (int32_t)dp_get32(d + 0x28);
    pOut->numPlayers = (int32_t)dp_get32(d + 0x2C);
    pOut->sessionFlags = (int32_t)dp_get32(d + 0x04);
    pOut->checksumSeed = (int32_t)dp_get32(d + 0x40);
    pOut->multiModeFlags = (int32_t)dp_get32(d + 0x44);
    pOut->field_E0 = (int32_t)(dp_get32(d + 0x48) & 0xFFFF);
    pOut->maxRank = (int32_t)(dp_get32(d + 0x48) >> 16);
    pOut->tickRateMs = (int32_t)dp_get32(d + 0x4C);
    // (split at ':' -- the game name, the episode, the level)
    char16_t part[3][128];
    memset(part, 0, sizeof(part));
    int k = 0, n = 0;
    for (int i = 0; pName[i] && i < 127; i++) {
        if ((pName[i] == ':' && k < 2) || pName[i] == '\n') { k++; n = 0; if (k > 2) break; continue; }
        if (n < 127) part[k][n++] = pName[i];
    }
    for (int i = 0; i < 31 && part[0][i]; i++) pOut->serverName[i] = part[0][i];
    stdString_WcharToChar(pOut->episodeGobName, part[1], 31);
    stdString_WcharToChar(pOut->mapJklFname, part[2], 31);
}

// ------------------------------------------------------------------ what comes in

static void dp_sendPlayerReply(uint32_t ip, uint16_t port, uint32_t id)
{
    uint8_t m[20 + 104];
    memset(m, 0, sizeof(m));
    int o = 20 + dp_putMsgHeader(m + 20, DPCMD_REQUESTPLAYERREPLY);
    dp_put32(m + o, id); // (then a DPSECURITYDESC, two offsets, the result and two empty names: 0)
    dp_sendTcp(ip, port, m, sizeof(m));
}

// Every player, to a system player that has just joined
static void dp_sendSuperEnum(uint32_t ip, uint16_t port)
{
    static uint8_t m[8192];
    memset(m, 0, 20 + 36);
    int o = 20 + dp_putMsgHeader(m + 20, DPCMD_SUPERENUMPLAYERSREPLY);
    const int fixed = 8 + 7 * 4;
    int numPlayers = 0;
    dp_makeSessionDesc();
    memcpy(m + 20 + fixed, stdComm_DPlay_dpsd, 80);
    const int nameLen = dp_putSessionName(m + 20 + fixed + 80);
    int p = 20 + fixed + 80 + nameLen;
    for (int i = 0; i < DPLAY_MAX_PLAYERS && p < (int)sizeof(m) - 256; i++) {
        const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
        if (!pl->bUsed) continue;
        const int bSys = (pl->flags & DPLAYI_PLAYER_SYSPLAYER) != 0;
        const int bName = !bSys && pl->name[0];
        uint32_t flags = pl->flags;
        if (pl->ip == ip && bSys) flags &= ~DPLAYI_PLAYER_LOCAL; // (its own system player: not ours)
        dp_put32(m + p, 16);
        dp_put32(m + p + 4, flags);
        dp_put32(m + p + 8, pl->id);
        dp_put32(m + p + 12, (bName ? 0x3 : 0) | 0x4);
        dp_put32(m + p + 16, bSys ? DPLAY_VERSION : pl->sysId);
        p += 20;
        if (bName) {
            p += dp_putWstr(m + p, pl->name, 31);
            p += dp_putWstr(m + p, pl->name, 31);
        }
        m[p++] = 32;
        // (our own players: address 0, "where this came from"; the others': where they are)
        const uint32_t addrIp = (pl->ip == 0) ? 0 : pl->ip;
        dp_putAddr(m + p, pl->tcpPort, addrIp);
        dp_putAddr(m + p + 16, pl->udpPort, addrIp);
        p += 32;
        numPlayers++;
    }
    dp_put32(m + o, (uint32_t)numPlayers);
    dp_put32(m + o + 4, 0);                            // groups
    dp_put32(m + o + 8, (uint32_t)(fixed + 80 + nameLen)); // packed players
    dp_put32(m + o + 12, 0);                           // shortcuts
    dp_put32(m + o + 16, (uint32_t)fixed);             // session description
    dp_put32(m + o + 20, (uint32_t)(fixed + 80));      // session name
    dp_put32(m + o + 24, 0);                           // password
    dp_sendTcp(ip, port, m, p);
}

// (a length coded in infoMask: 1, 2 or 4 bytes)
static uint32_t dp_getCoded(const uint8_t* p, int* pOff, int code)
{
    uint32_t v = 0;
    if (code == 1) { v = p[*pOff]; *pOff += 1; }
    else if (code == 2) { v = dp_get16(p + *pOff); *pOff += 2; }
    else if (code == 3) { v = dp_get32(p + *pOff); *pOff += 4; }
    return v;
}

static void dp_parseSuperEnum(const uint8_t* b, int len, uint32_t srcIp)
{
    if (len < 36) return;
    const uint32_t numPlayers = dp_get32(b + 8), numGroups = dp_get32(b + 12), packedOff = dp_get32(b + 16);
    const uint32_t descOff = dp_get32(b + 24), nameOff = dp_get32(b + 28);
    if (descOff + 80 <= (uint32_t)len) memcpy(stdComm_DPlay_dpsd, b + descOff, 80);
    if (nameOff && nameOff < (uint32_t)len) {
        char16_t name[128];
        dp_getWstr(name, 128, b + nameOff, len - nameOff);
        dp_parseSessionDesc(stdComm_DPlay_dpsd, name, &stdComm_DPlay_entry);
    }
    int p = (int)packedOff;
    for (uint32_t i = 0; i < numPlayers + numGroups && p + 20 <= len; i++) {
        const uint32_t size = dp_get32(b + p), flags = dp_get32(b + p + 4), id = dp_get32(b + p + 8);
        const uint32_t mask = dp_get32(b + p + 12), verOrSys = dp_get32(b + p + 16);
        p += 4 + (int)(size ? size : 16); // (size: the fixed part after the size itself)
        char16_t sn[32] = { 0 }, ln[32] = { 0 };
        if (mask & 1) p += dp_getWstr(sn, 32, b + p, len - p);
        if (mask & 2) p += dp_getWstr(ln, 32, b + p, len - p);
        const uint32_t dataLen = dp_getCoded(b, &p, (mask >> 4) & 3);
        p += (int)dataLen;
        const uint32_t spLen = dp_getCoded(b, &p, (mask >> 2) & 3);
        const int spOff = p;
        p += (int)spLen;
        const uint32_t count = dp_getCoded(b, &p, (mask >> 6) & 3);
        p += (int)count * 4;
        if (mask & 0x100) p += 4;
        const uint32_t shortcuts = dp_getCoded(b, &p, (mask >> 9) & 3);
        p += (int)shortcuts * 4;
        if (p > len) break;
        if (i >= numPlayers) continue; // (groups: not used by the game)
        if (id == stdComm_DPlay_mySysId) continue;
        dpPlayer* pl = dp_addPlayer(id);
        if (!pl) continue;
        pl->flags = flags & ~DPLAYI_PLAYER_LOCAL;
        pl->sysId = (flags & DPLAYI_PLAYER_SYSPLAYER) ? id : verOrSys;
        memcpy(pl->name, sn, sizeof(sn));
        if (spLen >= 32) {
            const uint32_t aip = dp_addrIp(b + spOff);
            pl->ip = aip ? aip : srcIp;
            pl->tcpPort = dp_addrPort(b + spOff);
            pl->udpPort = dp_addrPort(b + spOff + 16);
        }
        if (!(flags & DPLAYI_PLAYER_SYSPLAYER) && (flags & DPLAYI_PLAYER_LOCAL) && !stdComm_DPlay_hostId)
            stdComm_DPlay_hostId = id; // (the host's own player)
        stdComm_DPlay_verbosePrintf("DPlay: player %x (%s, of %x) at %x:%d/%d\n", id,
            (flags & DPLAYI_PLAYER_SYSPLAYER) ? "system" : "game", pl->sysId, ntohl(pl->ip), pl->tcpPort, pl->udpPort);
    }
    // (the host's player: the one with the host's system player, if none was marked)
    if (!stdComm_DPlay_hostId) {
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (pl->bUsed && !(pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->ip == srcIp) { stdComm_DPlay_hostId = pl->id; break; }
        }
    }
}

// A player made (CREATEPLAYER from a peer)
static void dp_parseCreatePlayer(const uint8_t* b, int len, uint32_t srcIp)
{
    if (len < 28 + 48) return;
    const uint8_t* pp = b + 28;
    const uint32_t flags = dp_get32(pp + 4), id = dp_get32(pp + 8);
    const uint32_t snLen = dp_get32(pp + 12), lnLen = dp_get32(pp + 16), spLen = dp_get32(pp + 20);
    const uint32_t sysId = dp_get32(pp + 32);
    if (id == stdComm_DPlay_myId || id == stdComm_DPlay_mySysId) return;
    dpPlayer* pl = dp_addPlayer(id);
    if (!pl) return;
    int o = 28 + 48;
    if (snLen && o + (int)snLen <= len) dp_getWstr(pl->name, 32, b + o, snLen);
    o += (int)(snLen + lnLen);
    pl->flags = flags & ~DPLAYI_PLAYER_LOCAL;
    pl->sysId = (flags & DPLAYI_PLAYER_SYSPLAYER) ? id : sysId;
    pl->ip = srcIp;
    if (spLen >= 32 && o + 32 <= len) {
        const uint32_t aip = dp_addrIp(b + o);
        if (aip) pl->ip = aip;
        pl->tcpPort = dp_addrPort(b + o);
        pl->udpPort = dp_addrPort(b + o + 16);
    }
    // (its system player's addresses, if we didn't know it)
    const dpPlayer* sp = dp_findPlayer(pl->sysId);
    if (sp && !pl->udpPort) { pl->tcpPort = sp->tcpPort; pl->udpPort = sp->udpPort; pl->ip = sp->ip; }
    stdComm_DPlay_infoPrintf("DPlay: player %x joined\n", id);
}

// A game message (from id, to id, then the game's own bytes)
static void dp_gameMessage(const uint8_t* p, int len)
{
    if (len < 8 + 4) return;
    const uint32_t from = dp_get32(p);
    dp_queuePush(0, from, p + 8, len - 8);
}

static void dp_systemMessage(const uint8_t* m, int len, uint32_t srcIp)
{
    const uint16_t replyPort = dp_addrPort(m + 4);
    const uint8_t* b = m + 20;
    const int bl = len - 20;
    const uint16_t cmd = dp_get16(b + 4);
    stdComm_DPlay_verbosePrintf("DPlay: message %d (v%d) from %x:%d, %d bytes\n", cmd, dp_get16(b + 6), ntohl(srcIp), replyPort, len);
    switch (cmd)
    {
    case DPCMD_ENUMSESSIONSREPLY: {
        if (bl < 8 + 80 + 4) break;
        for (int i = 0; i < 8; i++) {
            dpProbe* pr = &stdComm_DPlay_aProbe[i];
            if (pr->ip != srcIp || pr->bAnswered) continue;
            memcpy(pr->dpsd, b + 8, 80);
            const uint32_t nameOff = dp_get32(b + 88);
            pr->name[0] = 0;
            if (nameOff && nameOff < (uint32_t)bl) dp_getWstr(pr->name, 128, b + nameOff, bl - nameOff);
            pr->tcpPort = replyPort;
            pr->bAnswered = 1;
        }
        break;
    }
    case DPCMD_REQUESTPLAYERID: {
        if (!stdComm_DPlay_bServer) break;
        const uint32_t id = stdComm_DPlay_nextId++;
        dp_sendPlayerReply(srcIp, replyPort, id);
        break;
    }
    case DPCMD_REQUESTPLAYERREPLY:
        if (bl >= 12) stdComm_DPlay_replyId = dp_get32(b + 8);
        break;
    case DPCMD_ADDFORWARDREQUEST: {
        if (!stdComm_DPlay_bServer || bl < 28 + 48 + 32) break;
        const uint32_t id = dp_get32(b + 12), pwOff = dp_get32(b + 24);
        const uint8_t* sp = b + 28 + 48;
        // (the password, if the game has one)
        char16_t pw[33] = { 0 };
        if (pwOff && pwOff < (uint32_t)bl) dp_getWstr(pw, 33, b + pwOff, bl - pwOff);
        if (stdComm_DPlay_password[0] && memcmp(pw, stdComm_DPlay_password, sizeof(pw))) {
            uint8_t r[20 + 12];
            memset(r, 0, sizeof(r));
            dp_putMsgHeader(r + 20, DPCMD_ADDFORWARDREPLY);
            dp_put32(r + 28, 0x8877015E); // DPERR_INVALIDPASSWORD
            dp_sendTcp(srcIp, replyPort, r, sizeof(r));
            stdComm_DPlay_infoPrintf("DPlay: a player gave the wrong password\n");
            break;
        }
        dpPlayer* pl = dp_addPlayer(id);
        if (!pl) break;
        pl->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_INGROUP;
        pl->sysId = id;
        pl->ip = dp_addrIp(sp) ? dp_addrIp(sp) : srcIp;
        pl->tcpPort = dp_addrPort(sp);
        pl->udpPort = dp_addrPort(sp + 16);
        dp_sendSuperEnum(srcIp, replyPort);
        stdComm_DPlay_infoPrintf("DPlay: system player %x joining from %x\n", id, ntohl(srcIp));
        break;
    }
    case DPCMD_ADDFORWARDREPLY:
        stdComm_DPlay_nackResult = bl >= 12 ? dp_get32(b + 8) : 0x80004005;
        break;
    case DPCMD_SUPERENUMPLAYERSREPLY:
        dp_parseSuperEnum(b, bl, srcIp);
        stdComm_DPlay_bSuperEnum = 1;
        break;
    case DPCMD_CREATEPLAYER:
        dp_parseCreatePlayer(b, bl, srcIp);
        break;
    case DPCMD_DELETEPLAYER:
        if (bl >= 16) dp_removePlayer(dp_get32(b + 12));
        break;
    case DPCMD_PING: {
        if (bl < 16) break;
        uint8_t r[20 + 16];
        memset(r, 0, sizeof(r));
        dp_putMsgHeader(r + 20, DPCMD_PINGREPLY);
        dp_put32(r + 28, stdComm_DPlay_mySysId);
        dp_put32(r + 32, dp_get32(b + 12));
        dp_sendTcp(srcIp, replyPort, r, sizeof(r));
        break;
    }
    case DPCMD_ADDFORWARD: {
        // (the host telling us of another player's system player)
        if (bl < 28 + 48 + 32) break;
        const uint32_t id = dp_get32(b + 12);
        const uint8_t* sp = b + 28 + 48;
        dpPlayer* pl = dp_addPlayer(id);
        if (pl) {
            pl->flags = DPLAYI_PLAYER_SYSPLAYER;
            pl->sysId = id;
            pl->ip = dp_addrIp(sp) ? dp_addrIp(sp) : srcIp;
            pl->tcpPort = dp_addrPort(sp);
            pl->udpPort = dp_addrPort(sp + 16);
        }
        uint8_t r[20 + 12];
        memset(r, 0, sizeof(r));
        dp_putMsgHeader(r + 20, DPCMD_ADDFORWARDACK);
        dp_put32(r + 28, id);
        dp_sendTcp(srcIp, replyPort, r, sizeof(r));
        break;
    }
    default:
        break;
    }
}

// One message off a TCP connection
static void dp_tcpMessage(const uint8_t* m, int len, uint32_t srcIp)
{
    if (len >= 28 && dp_get32(m + 20) == DPLAY_MAGIC) dp_systemMessage(m, len, srcIp);
    else if (len > 20) dp_gameMessage(m + 20, len - 20);
}

// An ENUMSESSIONS (hosting): answered over TCP, to the port it gives, at the address it came from
static void dp_enumRequest(const uint8_t* m, int len, uint32_t srcIp)
{
    if (!stdComm_DPlay_bServer || len < 20 + 32) return;
    const uint8_t* b = m + 20;
    if (dp_get32(b) != DPLAY_MAGIC || dp_get16(b + 4) != DPCMD_ENUMSESSIONS) return;
    static const uint8_t zero[16] = { 0 };
    if (memcmp(b + 8, stdComm_DPlay_appGuid, 16) && memcmp(b + 8, zero, 16)) return;
    uint8_t r[20 + 8 + 80 + 4 + 260];
    memset(r, 0, sizeof(r));
    int o = 20 + dp_putMsgHeader(r + 20, DPCMD_ENUMSESSIONSREPLY);
    dp_makeSessionDesc();
    memcpy(r + o, stdComm_DPlay_dpsd, 80);
    dp_put32(r + o + 80, 8 + 80 + 4);
    const int n = dp_putSessionName(r + o + 84);
    dp_sendTcp(srcIp, dp_addrPort(m + 4), r, o + 84 + n);
}

static void dp_poll(int timeoutMs)
{
    struct pollfd pf[3 + DPLAY_MAX_CONNS];
    int map[3 + DPLAY_MAX_CONNS];
    int n = 0;
    if (stdComm_DPlay_tcpListen >= 0) { pf[n].fd = stdComm_DPlay_tcpListen; pf[n].events = POLLIN; map[n++] = -1; }
    if (stdComm_DPlay_udp >= 0) { pf[n].fd = stdComm_DPlay_udp; pf[n].events = POLLIN; map[n++] = -2; }
    if (stdComm_DPlay_enumUdp >= 0) { pf[n].fd = stdComm_DPlay_enumUdp; pf[n].events = POLLIN; map[n++] = -3; }
    for (int i = 0; i < DPLAY_MAX_CONNS; i++)
        if (stdComm_DPlay_aIn[i].fd >= 0) { pf[n].fd = stdComm_DPlay_aIn[i].fd; pf[n].events = POLLIN; map[n++] = i; }
    if (!n || poll(pf, n, timeoutMs) <= 0) return;

    static uint8_t buf[DPLAY_MAX_MSG];
    for (int k = 0; k < n; k++) {
        if (!(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        if (map[k] == -1) {
            struct sockaddr_in a;
            socklen_t al = sizeof(a);
            const int fd = accept(stdComm_DPlay_tcpListen, (struct sockaddr*)&a, &al);
            if (fd < 0) continue;
            int slot = -1;
            for (int i = 0; i < DPLAY_MAX_CONNS; i++) if (stdComm_DPlay_aIn[i].fd < 0) { slot = i; break; }
            if (slot < 0) { close(fd); continue; }
            dp_nonblock(fd);
            stdComm_DPlay_aIn[slot].fd = fd;
            stdComm_DPlay_aIn[slot].ip = a.sin_addr.s_addr;
            stdComm_DPlay_aIn[slot].len = 0;
            stdComm_DPlay_aIn[slot].port = 0;
            stdComm_DPlay_aIn[slot].pBuf = (uint8_t*)malloc(DPLAY_MAX_MSG);
            if (!stdComm_DPlay_aIn[slot].pBuf) dp_closeConn(&stdComm_DPlay_aIn[slot]);
        }
        else if (map[k] == -2 || map[k] == -3) {
            struct sockaddr_in a;
            socklen_t al = sizeof(a);
            const int got = (int)recvfrom(pf[k].fd, buf, sizeof(buf), 0, (struct sockaddr*)&a, &al);
            if (got <= 0) continue;
            if (map[k] == -3) dp_enumRequest(buf, got, a.sin_addr.s_addr);
            else if (got >= 28 && dp_get32(buf + 20) == DPLAY_MAGIC) dp_systemMessage(buf, got, a.sin_addr.s_addr);
            else dp_gameMessage(buf, got);
        }
        else {
            dpConn* c = &stdComm_DPlay_aIn[map[k]];
            const int got = (int)recv(c->fd, c->pBuf + c->len, DPLAY_MAX_MSG - c->len, 0);
            if (got <= 0) {
                if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                // (a peer's connection closed: its players gone; a player: the host gone)
                const uint32_t ip = c->ip;
                const uint16_t port = c->port;
                dp_closeConn(c);
                if (stdComm_DPlay_bActive && port) {
                    if (!stdComm_DPlay_bServer && ip == stdComm_DPlay_hostIp && port == stdComm_DPlay_hostTcpPort) stdComm_DPlay_bHostLost = 1;
                    for (int i = 0; i < DPLAY_MAX_PLAYERS && stdComm_DPlay_bServer; i++) {
                        dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
                        if (pl->bUsed && (pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->ip == ip && pl->tcpPort == port && pl->id != stdComm_DPlay_mySysId)
                            dp_removePlayer(pl->id);
                    }
                }
                continue;
            }
            c->len += got;
            while (c->len >= 20) {
                const int size = (int)(dp_get32(c->pBuf) & 0xFFFFF);
                if (size < 20 || size > DPLAY_MAX_MSG) { c->len = 0; break; }
                if (c->len < size) break;
                c->port = dp_addrPort(c->pBuf + 4); // (the peer's listening port: who this connection is)
                dp_tcpMessage(c->pBuf, size, c->ip);
                memmove(c->pBuf, c->pBuf + size, c->len - size);
                c->len -= size;
            }
        }
    }
}

static int stdComm_DPlay_Thread(void* unused)
{
    (void)unused;
    while (stdComm_DPlay_bThreadRun) {
        SDL_LockMutex(stdComm_DPlay_mutex);
        dp_poll(0);
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        SDL_Delay(2);
    }
    return 0;
}

static int dp_start(void)
{
    if (!stdComm_DPlay_bInitted) return 0;
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int ok = dp_openSockets();
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (!ok) return 0;
    if (!stdComm_DPlay_thread) {
        stdComm_DPlay_bThreadRun = 1;
        stdComm_DPlay_thread = SDL_CreateThread(stdComm_DPlay_Thread, "stdComm_DPlay", NULL);
    }
    return stdComm_DPlay_thread != NULL;
}

// ------------------------------------------------------------------ the transport

static void stdComm_DPlay_ResetClients(void)
{
    if (!stdComm_DPlay_bActive) return;
    SDL_LockMutex(stdComm_DPlay_mutex);
    DirectPlay_numPlayers = 0;
    for (int i = 0; i < DPLAY_MAX_PLAYERS && DirectPlay_numPlayers < 32; i++) {
        const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
        if (!pl->bUsed || (pl->flags & DPLAYI_PLAYER_SYSPLAYER)) continue;
        memset(&DirectPlay_aPlayers[DirectPlay_numPlayers], 0, sizeof(DirectPlay_aPlayers[0]));
        DirectPlay_aPlayers[DirectPlay_numPlayers].dpId = pl->id;
        memcpy(DirectPlay_aPlayers[DirectPlay_numPlayers].waName, pl->name, sizeof(pl->name));
        DirectPlay_numPlayers++;
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    stdComm_dplayIdSelf = stdComm_DPlay_myId;
}

static int stdComm_DPlay_Receive(int* pIdOut, int* pMsgIdOut, int* pLenOut)
{
    const int maxLen = *pLenOut;
    *pIdOut = 0;
    *pLenOut = 0;
    if (!stdComm_DPlay_bInitted) return -1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    dpEvent* e = dp_queuePop();
    const int bHostLost = !e && stdComm_DPlay_bHostLost;
    if (bHostLost) stdComm_DPlay_bHostLost = 0;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (bHostLost) {
        *pIdOut = (int)stdComm_DPlay_hostId;
        return 2;
    }
    if (!e) return -1;
    int ret = 0;
    *pIdOut = (int)e->idFrom;
    if (e->kind == 2) {
        ret = 2;
    }
    else {
        const int n = e->len < maxLen ? e->len : maxLen;
        memcpy(pMsgIdOut, e->data, n);
        *pLenOut = n;
        stdComm_DPlay_verbosePrintf("DPlay: recv %x bytes from %x (%s)\n", n, e->idFrom, sithDSS_IdToStr(*(uint32_t*)pMsgIdOut & 0xFF));
    }
    free(e);
    return ret;
}

static BOOL stdComm_DPlay_Send(DPID idFrom, DPID idTo, void* lpData, DWORD dwDataSize)
{
    (void)idFrom;
    if (!stdComm_DPlay_bActive || dwDataSize > DPLAY_MAX_MSG - 8) return 0;
    static uint8_t m[DPLAY_MAX_MSG];
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_put32(m, stdComm_DPlay_myId);
    dp_put32(m + 4, (uint32_t)idTo);
    memcpy(m + 8, lpData, dwDataSize);
    int bSent = 0;
    if (idTo == 0) {
        // (everyone: once to each other system player, "to 0", as DirectPlay sends it)
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (!pl->bUsed || !(pl->flags & DPLAYI_PLAYER_SYSPLAYER) || pl->id == stdComm_DPlay_mySysId || !pl->udpPort) continue;
            dp_sendUdp(pl->ip, pl->udpPort, m, (int)dwDataSize + 8);
            bSent = 1;
        }
    }
    else if ((uint32_t)idTo != stdComm_DPlay_myId) {
        const dpPlayer* pl = dp_findPlayer((uint32_t)idTo);
        const dpPlayer* sp = pl ? dp_findPlayer(pl->sysId) : NULL;
        if (sp && sp->udpPort && sp->id != stdComm_DPlay_mySysId) {
            dp_sendUdp(sp->ip, sp->udpPort, m, (int)dwDataSize + 8);
            bSent = 1;
        }
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_verbosePrintf("DPlay: sent %x bytes to %x (%s)\n", (int)dwDataSize, idTo, sithDSS_IdToStr(*(uint32_t*)lpData & 0xFF));
    return bSent;
}

// A player id asked of the host (flags 0x9: a system player, 0x8: a player); 0 if no answer
static uint32_t dp_requestId(uint32_t flags)
{
    uint8_t m[20 + 12];
    memset(m, 0, sizeof(m));
    dp_putMsgHeader(m + 20, DPCMD_REQUESTPLAYERID);
    dp_put32(m + 28, flags);
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_replyId = 0;
    const int ok = dp_sendTcp(stdComm_DPlay_hostIp, stdComm_DPlay_hostTcpPort, m, sizeof(m));
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (!ok) return 0;
    for (int i = 0; i < 300 && !stdComm_DPlay_replyId; i++) SDL_Delay(10);
    return stdComm_DPlay_replyId;
}

// A PackedPlayer (48 bytes) and what follows it, for ADDFORWARDREQUEST and CREATEPLAYER
static int dp_putPackedPlayer(uint8_t* p, uint32_t flags, uint32_t id, const char16_t* pName, uint32_t sysId)
{
    uint8_t names[70];
    const int snLen = pName ? dp_putWstr(names, pName, 31) : 0;
    memset(p, 0, 48);
    dp_put32(p, 48 + snLen + 32);
    dp_put32(p + 4, flags);
    dp_put32(p + 8, id);
    dp_put32(p + 12, (uint32_t)snLen);
    dp_put32(p + 16, 0);
    dp_put32(p + 20, 32);
    dp_put32(p + 24, 0);
    dp_put32(p + 28, 0);
    dp_put32(p + 32, sysId);
    dp_put32(p + 36, 48);
    dp_put32(p + 40, DPLAY_VERSION);
    dp_put32(p + 44, 0);
    int o = 48;
    if (snLen) { memcpy(p + o, names, snLen); o += snLen; }
    dp_putAddr(p + o, stdComm_DPlay_tcpPort, 0);
    dp_putAddr(p + o + 16, stdComm_DPlay_udpPort, 0);
    return o + 32;
}

static void dp_resetSession(void)
{
    memset(stdComm_DPlay_aPlayers, 0, sizeof(stdComm_DPlay_aPlayers));
    stdComm_DPlay_mySysId = stdComm_DPlay_myId = 0;
    stdComm_DPlay_hostId = 0;
    stdComm_DPlay_bHostLost = 0;
    stdComm_DPlay_bSuperEnum = 0;
    stdComm_DPlay_nackResult = 0;
    dp_queueClear();
}

static void stdComm_DPlay_Close(void);

// Joining the game the Join screen found (stdComm_DPlay_ProbeAddress kept its address)
static int stdComm_DPlay_Join(char16_t* pwPassword)
{
    if (!stdComm_DPlay_hostIp || !stdComm_DPlay_hostTcpPort || !dp_start()) return 0x88770154 + 1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_resetSession();
    stdComm_DPlay_bServer = 0;
    stdComm_DPlay_bActive = 0;
    SDL_UnlockMutex(stdComm_DPlay_mutex);

    // our system player
    const uint32_t sysId = dp_requestId(0x9);
    if (!sysId) { stdComm_DPlay_infoPrintf("DPlay: the host gave no player id\n"); return 0x88770154 + 1; }
    stdComm_DPlay_mySysId = sysId;

    // ...told to the host, with the password: every player in answer
    static uint8_t m[1024];
    memset(m, 0, sizeof(m));
    int o = 20 + dp_putMsgHeader(m + 20, DPCMD_ADDFORWARDREQUEST);
    dp_put32(m + o, 0);          // to
    dp_put32(m + o + 4, sysId);  // player
    dp_put32(m + o + 8, 0);      // group
    dp_put32(m + o + 12, 28);    // create offset
    o += 20;
    o += dp_putPackedPlayer(m + o, 0x9, sysId, NULL, sysId);
    dp_put32(m + 20 + 24, (uint32_t)(o - 20));  // password offset
    o += dp_putWstr(m + o, pwPassword, 31);
    memcpy(m + o, stdComm_DPlay_dpsd + 0x38, 4); // (the session's dwReserved1, as DirectPlay sends it)
    o += 4;
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int ok = dp_sendTcp(stdComm_DPlay_hostIp, stdComm_DPlay_hostTcpPort, m, o);
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (!ok) return 0x88770154 + 1;
    for (int i = 0; i < 500 && !stdComm_DPlay_bSuperEnum && !stdComm_DPlay_nackResult; i++) SDL_Delay(10);
    if (!stdComm_DPlay_bSuperEnum) {
        stdComm_DPlay_infoPrintf("DPlay: the host refused (%x)\n", stdComm_DPlay_nackResult);
        return stdComm_DPlay_nackResult ? 0x88770154 : 0x88770154 + 1;
    }

    // our player, made and told to the others
    const uint32_t id = dp_requestId(0x8);
    if (!id) return 0x88770154 + 1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_myId = id;
    dpPlayer* pSys = dp_addPlayer(sysId);
    if (pSys) { pSys->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_LOCAL; pSys->sysId = sysId; }
    dpPlayer* pMe = dp_addPlayer(id);
    if (pMe) {
        pMe->flags = DPLAYI_PLAYER_LOCAL;
        pMe->sysId = sysId;
        memcpy(pMe->name, jkPlayer_playerShortName, sizeof(pMe->name));
        pMe->name[31] = 0;
    }
    memset(m, 0, 20 + 28);
    o = 20 + dp_putMsgHeader(m + 20, DPCMD_CREATEPLAYER);
    dp_put32(m + o, 0);
    dp_put32(m + o + 4, id);
    dp_put32(m + o + 8, 0);
    dp_put32(m + o + 12, 28);
    dp_put32(m + o + 16, 0);
    o += 20;
    o += dp_putPackedPlayer(m + o, 0x8, id, jkPlayer_playerShortName, sysId);
    memset(m + o, 0, 6); // (two reserved fields)
    o += 6;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
        if (pl->bUsed && (pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->id != sysId && pl->tcpPort)
            dp_sendTcp(pl->ip, pl->tcpPort, m, o);
    }
    stdComm_DPlay_bActive = 1;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_infoPrintf("DPlay: joined as %x (system player %x), the host's player %x\n", id, sysId, stdComm_DPlay_hostId);
    stdComm_DPlay_ResetClients();
    return 0;
}

static void stdComm_DPlay_Close(void)
{
    if (!stdComm_DPlay_bInitted) return;
    SDL_LockMutex(stdComm_DPlay_mutex);
    // (a player: the host told we're leaving)
    if (stdComm_DPlay_bActive && !stdComm_DPlay_bServer && stdComm_DPlay_hostIp) {
        const uint32_t ids[2] = { stdComm_DPlay_myId, stdComm_DPlay_mySysId };
        for (int k = 0; k < 2; k++) {
            uint8_t m[20 + 28];
            memset(m, 0, sizeof(m));
            dp_putMsgHeader(m + 20, DPCMD_DELETEPLAYER);
            dp_put32(m + 32, ids[k]);
            dp_sendTcp(stdComm_DPlay_hostIp, stdComm_DPlay_hostTcpPort, m, sizeof(m));
        }
    }
    stdComm_DPlay_bActive = 0;
    stdComm_DPlay_bServer = 0;
    if (stdComm_DPlay_enumUdp >= 0) { close(stdComm_DPlay_enumUdp); stdComm_DPlay_enumUdp = -1; }
    dp_closeAll();
    dp_resetSession();
    SDL_UnlockMutex(stdComm_DPlay_mutex);
}

static void stdComm_DPlay_SetSessionLevel(void)
{
    SDL_LockMutex(stdComm_DPlay_mutex);
    _strncpy(stdComm_DPlay_entry.mapJklFname, jkMain_aLevelJklFname, 0x20);
    SDL_UnlockMutex(stdComm_DPlay_mutex);
}

static void stdComm_DPlay_SetSession(jkMultiEntry* pEntry)
{
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_entry = *pEntry;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    jkPlayer_maxPlayers = pEntry->maxPlayers;
}

// Hosting: answering on UDP 47624, our system player and player made
static int stdComm_DPlay_OpenHost(jkMultiEntry* pEntry)
{
    stdComm_bIsServer = 1;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
    if (!dp_start()) return 0x88770118 + 1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_resetSession();
    stdComm_DPlay_entry = *pEntry;
    if (!stdComm_DPlay_entry.guidInstance.a) stdComm_DPlay_entry.guidInstance.a = (uint32_t)stdPlatform_GetTimeMsec() ^ 0x4A4B4446;
    memset(stdComm_DPlay_password, 0, sizeof(stdComm_DPlay_password));
    for (int i = 0; i < 32 && pEntry->wPassword[i]; i++) stdComm_DPlay_password[i] = pEntry->wPassword[i];
    if (stdComm_DPlay_enumUdp < 0) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(DPLAY_ENUM_PORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (fd >= 0 && bind(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
            dp_nonblock(fd);
            stdComm_DPlay_enumUdp = fd;
        }
        else if (fd >= 0) close(fd);
    }
    if (stdComm_DPlay_enumUdp < 0) {
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        stdComm_DPlay_infoPrintf("DPlay: can't listen on UDP %d\n", DPLAY_ENUM_PORT);
        return 0x88770118 + 1;
    }
    stdComm_DPlay_nextId = 0x00100000u | ((stdPlatform_GetTimeMsec() & 0xFFF) << 8) | 1u;
    stdComm_DPlay_reserved1 = stdPlatform_GetTimeMsec() ^ 0x5A5A5A5Au;
    stdComm_DPlay_mySysId = stdComm_DPlay_nextId++;
    stdComm_DPlay_myId = stdComm_DPlay_nextId++;
    stdComm_DPlay_hostId = stdComm_DPlay_myId;
    dpPlayer* pSys = dp_addPlayer(stdComm_DPlay_mySysId);
    if (pSys) {
        pSys->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_NAMESRVR | DPLAYI_PLAYER_INGROUP | DPLAYI_PLAYER_LOCAL;
        pSys->sysId = pSys->id;
        pSys->tcpPort = stdComm_DPlay_tcpPort;
        pSys->udpPort = stdComm_DPlay_udpPort;
    }
    dpPlayer* pMe = dp_addPlayer(stdComm_DPlay_myId);
    if (pMe) {
        pMe->flags = DPLAYI_PLAYER_LOCAL;
        pMe->sysId = stdComm_DPlay_mySysId;
        memcpy(pMe->name, jkPlayer_playerShortName, sizeof(pMe->name));
        pMe->name[31] = 0;
        pMe->tcpPort = stdComm_DPlay_tcpPort;
        pMe->udpPort = stdComm_DPlay_udpPort;
    }
    stdComm_DPlay_bServer = 1;
    stdComm_DPlay_bActive = 1;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_infoPrintf("DPlay: hosting (UDP %d, TCP %d, UDP %d), player %x\n", DPLAY_ENUM_PORT, stdComm_DPlay_tcpPort, stdComm_DPlay_udpPort, stdComm_DPlay_myId);
    stdComm_DPlay_ResetClients();
    return 0;
}

static int dp_resolve(const char* pAddress, uint32_t* pIpOut)
{
    char host[256];
    int port = DPLAY_ENUM_PORT;
    if (!stdCommTransport_SplitAddress(pAddress, host, sizeof(host), &port, DPLAY_ENUM_PORT)) return 0;
    if (port != DPLAY_ENUM_PORT) return 0; // (another transport's address)
    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) == 1) { *pIpOut = a.s_addr; return 1; }
    struct addrinfo hints, *pRes = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &pRes) != 0 || !pRes) return 0;
    *pIpOut = ((struct sockaddr_in*)pRes->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(pRes);
    return 1;
}

// Is there a DirectPlay game at that address? An ENUMSESSIONS to its UDP 47624; the answer comes to
// our TCP port
static int stdComm_DPlay_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    if (!stdCommTransport_GateEnter(&stdComm_DPlay_probeGate)) return 0;
    uint32_t ip = 0;
    int r = 0;
    if (!dp_resolve(pAddress, &ip)) { stdCommTransport_GateLeave(&stdComm_DPlay_probeGate); return -1; }
    if (!dp_start()) { stdCommTransport_GateLeave(&stdComm_DPlay_probeGate); return 0; }
    SDL_LockMutex(stdComm_DPlay_mutex);
    dpProbe* pr = NULL;
    for (int i = 0; i < 8 && !pr; i++) if (stdComm_DPlay_aProbe[i].ip == ip) pr = &stdComm_DPlay_aProbe[i];
    for (int i = 0; i < 8 && !pr; i++) if (!stdComm_DPlay_aProbe[i].ip) pr = &stdComm_DPlay_aProbe[i];
    if (!pr) pr = &stdComm_DPlay_aProbe[0];
    memset(pr, 0, sizeof(*pr));
    pr->ip = ip;
    SDL_UnlockMutex(stdComm_DPlay_mutex);

    uint8_t m[20 + 32];
    memset(m, 0, sizeof(m));
    uint32_t start = stdPlatform_GetTimeMsec(), lastSent = 0;
    int bSentOnce = 0;
    while (stdPlatform_GetTimeMsec() - start < (uint32_t)timeoutMs && (!pbKeepGoing || *pbKeepGoing) && !stdComm_DPlay_probeGate.bClosing) {
        if (!bSentOnce || stdPlatform_GetTimeMsec() - lastSent > 500) {
            dp_putSpHeader(m, sizeof(m));
            dp_putMsgHeader(m + 20, DPCMD_ENUMSESSIONS);
            memcpy(m + 28, stdComm_DPlay_appGuid, 16);
            dp_put32(m + 44, 0);    // (no password)
            dp_put32(m + 48, 0x52); // (as the original asks: all, password-protected too)
            SDL_LockMutex(stdComm_DPlay_mutex);
            {
                struct sockaddr_in a;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_port = htons(DPLAY_ENUM_PORT);
                a.sin_addr.s_addr = ip;
                sendto(stdComm_DPlay_udp, m, sizeof(m), 0, (struct sockaddr*)&a, sizeof(a));
            }
            SDL_UnlockMutex(stdComm_DPlay_mutex);
            lastSent = stdPlatform_GetTimeMsec();
            bSentOnce = 1;
        }
        SDL_LockMutex(stdComm_DPlay_mutex);
        if (pr->bAnswered) {
            dp_parseSessionDesc(pr->dpsd, pr->name, pOut);
            // (kept for joining it)
            memcpy(stdComm_DPlay_dpsd, pr->dpsd, 80);
            stdComm_DPlay_hostIp = ip;
            stdComm_DPlay_hostTcpPort = pr->tcpPort;
            r = 1;
        }
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        if (r) break;
        SDL_Delay(20);
    }
    stdCommTransport_GateLeave(&stdComm_DPlay_probeGate);
    if (r) stdComm_DPlay_verbosePrintf("DPlay: game at %s (TCP %d)\n", pAddress, stdComm_DPlay_hostTcpPort);
    return r;
}

// (asked when joining: the host checks it in ADDFORWARDREQUEST, so it is only known then)
static int stdComm_DPlay_CheckPassword(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing)
{
    (void)pAddress; (void)pwPassword; (void)timeoutMs; (void)pbKeepGoing;
    return 1;
}

void stdComm_DPlay_Startup(void)
{
    if (stdComm_DPlay_bInitted) return;
    stdComm_DPlay_mutex = SDL_CreateMutex();
    if (!stdComm_DPlay_mutex) return;
    for (int i = 0; i < DPLAY_MAX_CONNS; i++) {
        memset(&stdComm_DPlay_aIn[i], 0, sizeof(dpConn));
        memset(&stdComm_DPlay_aOut[i], 0, sizeof(dpConn));
        stdComm_DPlay_aIn[i].fd = stdComm_DPlay_aOut[i].fd = -1;
    }
    stdComm_DPlay_bInitted = 1;
    stdCommTransport_GateOpen(&stdComm_DPlay_probeGate);
}

void stdComm_DPlay_Shutdown(void)
{
    if (!stdComm_DPlay_bInitted) return;
    stdCommTransport_GateClose(&stdComm_DPlay_probeGate, 5000);
    if (stdComm_DPlay_thread) {
        stdComm_DPlay_bThreadRun = 0;
        int ret;
        SDL_WaitThread(stdComm_DPlay_thread, &ret);
        stdComm_DPlay_thread = NULL;
    }
    stdComm_DPlay_Close();
    if (stdComm_DPlay_tcpListen >= 0) close(stdComm_DPlay_tcpListen);
    if (stdComm_DPlay_udp >= 0) close(stdComm_DPlay_udp);
    stdComm_DPlay_tcpListen = stdComm_DPlay_udp = -1;
    stdComm_DPlay_bInitted = 0;
}

const stdCommTransport stdComm_DPlay_transport = {
    "DirectPlay",
    stdComm_DPlay_Receive,
    stdComm_DPlay_Send,
    stdComm_DPlay_Join,
    stdComm_DPlay_OpenHost,
    stdComm_DPlay_SetSession,
    stdComm_DPlay_SetSessionLevel,
    stdComm_DPlay_Close,
    stdComm_DPlay_ResetClients,
    stdComm_DPlay_ProbeAddress,
    stdComm_DPlay_CheckPassword,
};

#endif // PLATFORM_DPLAY

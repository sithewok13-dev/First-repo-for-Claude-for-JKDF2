// Added: entire file. Experimental: the original 1997 game's multiplayer connection, DirectPlay 4
// over its TCP/IP service provider, spoken directly (no DirectPlay library), as a third transport
// next to ENet and GNS (Platform/Networking/stdCommTransport.h), for joining a game the original
// game hosts. Built with -DOPENJKDF2_NET_DPLAY=ON.
//
// The wire format is DirectPlay's own (Microsoft's [MS-DPDX] and [MS-DPSP]): every system message is
// "play", a command and a version, after a 20-byte header that carries its size and the TCP port to
// answer on. Looking for a game: an ENUMSESSIONS datagram to the host's UDP port 47624, answered over
// TCP to our own listening port (2300-2349) with the session's description (one answer per session:
// a computer hosting several games answers for each, from its own TCP port). Joining: two player ids
// asked of the host (REQUESTPLAYERID: our system player, then our player), ADDFORWARDREQUEST (our
// system player and its ports: the host answers with every player in SUPERENUMPLAYERSREPLY), and
// CREATEPLAYER (our player, told to each other system player). ADDFORWARD and CREATEPLAYER tell of
// those who join later, DELETEPLAYER of those who leave. The game's messages go as the original
// sends them (IDirectPlay::Send without DPSEND_GUARANTEED): a UDP datagram straight to each player's
// system player's port (2350-2399), "from id, to id" and then the message as the game made it.
//
// The original game describes its session in DPSESSIONDESC2 (its DirectPlay_OpenHost and
// DirectPlay_parseSessionDescidk): the name is "game name:episode:level", dwUser1 the world checksum's
// seed, dwUser2 the mode flags, dwUser3 the max rank (high half) and field_E0 (low half), dwUser4 the
// tick rate; its application GUID is {BF0613C0-DE79-11D0-99C9-00A02476AD4B}.
//
// What arrives is checked before it is believed: everything read within its message's bounds; the
// players and the session only from the host (over TCP, from its address), a player's own players
// from that player, an ENUMSESSIONS answer only from the address asked, a game message only from the
// address of the system player of the player it says it is from; no more system players than the
// session holds; no peer on a private address when the host is on the internet; nothing sent to a
// peer that hasn't been heard from beyond a trickle; queues and connections capped.
//
// Threads and sockets exist only while they are needed: from the start of a probe (or a join) to its
// end, and through a game until it is left (stdComm_DPlay_Close). One thread does all the network's
// work (connecting, sending, receiving): the game's side only queues what it sends and reads what was
// received, so the lock is never held across a send, a connect or a wait.
//
// The host is pinged every few seconds (and answered when it pings): no word at all from its address
// for DPLAY_HOST_TIMEOUT_MS (or for a few seconds after its connection closed) and it is gone.
//
// A host connects back to the player who joins: its answers come over TCP to our listening port,
// and the game's messages over UDP to ours. That can't be avoided (its host opens a connection of its
// own, from a port of its choosing). So when the player picks a game on the internet to join from
// behind a router (stdComm_DPlay_WillJoin), our two ports are opened on the router first
// (stdComm_DPlayNat: PCP, NAT-PMP, UPnP), until the game is left or the app goes to the background.
// The address in what we send stays 0, "where this came from", as Windows' DirectPlay sends it: the
// host puts in the address it saw, which is the right one even behind a carrier's NAT.
//
// Hosting (-dplayHost) is for testing on the rig only (the original game can join it): it answers the
// same messages and tells its players of each other. Not done: groups, IPv6, Windows sockets.

#include "stdComm_DPlay.h"
#include "stdComm_DPlayNat.h"
#include "Platform/Networking/stdCommTransport.h"

#ifdef PLATFORM_DPLAY

#ifndef STDCOMM_DPLAY_FUZZ
#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"
#include "Main/Main.h"
#include "SDL2_helper.h"
#endif

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
#define DPLAY_TOKEN           0xFABu
#define DPLAY_MAX_MSG         (64 * 1024)
#define DPLAY_MAX_PLAYERS     64
#define DPLAY_MAX_CONNS       16
#define DPLAY_MAX_IN_PER_IP   4        // (incoming TCP connections one address may hold)
#define DPLAY_IN_FIRST_MS     10000    // (an incoming connection with nothing whole in this long: closed)
#define DPLAY_IN_IDLE_MS      120000   // (one quiet this long: closed)
#define DPLAY_MAX_EVENTS      256      // (what the game hasn't read yet: at most this many,
#define DPLAY_MAX_EVENT_BYTES (1024 * 1024) // and this much)
#define DPLAY_MAX_OUT_BYTES   (1024 * 1024) // (queued to send)
#define DPLAY_MAX_PENDING     (256 * 1024)  // (one connection's unsent bytes)
#define DPLAY_MAX_SESSIONS    16
#define DPLAY_HOST_TIMEOUT_MS 25000    // (no word from the host this long: it is gone)
#define DPLAY_HOST_CLOSED_MS  5000     // (its connection closed, and no word since, this long: gone)
#define DPLAY_PING_EVERY_MS   4000
// (datagrams to a peer not heard from yet: a bucket of this many, refilled at this many a second.
// Big enough for what the game sends a player who has just joined, all at once -- the original game
// may well not send to it first -- and small enough that a host naming someone else's address can't
// make this device flood it)
#define DPLAY_UNHEARD_BURST   64
#define DPLAY_UNHEARD_PER_S   32
#define DPLAY_JOIN_INTENT_MS  30000    // (a game picked: the router's ports opened for probes this long)
#define DPLAY_UNMAP_STALE_MS  120000   // (ports opened for a join that never came: given back after)
#define DPLAY_RENEW_EVERY_MS  (45 * 60 * 1000) // (the router's ports renewed while a game goes on)

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
    int bHeard;              // (a system player: something has come from its address)
    uint32_t budgetMs;       // (and, until then, when its bucket of datagrams was last filled,
    int budget;              // and what is left in it; DPLAY_UNHEARD_BURST + 1 for a new one)
} dpPlayer;

// (the network thread's own) A TCP connection: one we made to a peer's listening port (what we
// send), or one a peer made to ours (what it sends)
typedef struct dpConn
{
    int fd;
    int bOut;
    int bConnecting;
    uint32_t ip;             // (network order)
    uint16_t port;           // (the peer's listening port: ours to it, or the one its messages give)
    uint8_t* pBuf;           // (in: what has come so far; out: what is still to send)
    size_t len;
    uint32_t lastMs;         // (in: when something last came; out: when it was opened)
    int bGotOne;             // (in: a whole message has come)
} dpConn;

typedef struct dpEvent
{
    struct dpEvent* pNext;
    int kind;                // 0: a message, 2: a player gone
    uint32_t idFrom;
    int len;
    uint8_t data[];
} dpEvent;

// What the game's side asks to send (sent by the network thread)
typedef struct dpOut
{
    struct dpOut* pNext;
    int bTcp;
    uint32_t ip;
    uint16_t port;
    int len;
    uint8_t data[];
} dpOut;

// A session an address answered with
typedef struct dpSession
{
    int bUsed;
    uint32_t ip;             // (its host: the address that answered)
    uint16_t tcpPort;        // (and its TCP port)
    uint8_t dpsd[80];
    char16_t name[128];
    uint32_t probeGen;       // (the asking it answered)
    uint32_t seenMs;
} dpSession;

// ------------------------------------------------------------------ state

static int stdComm_DPlay_bInitted = 0;
static SDL_Mutex* stdComm_DPlay_mutex = NULL;     // (what the network thread and the game share)
static SDL_Mutex* stdComm_DPlay_lifeMutex = NULL; // (starting and stopping the thread and sockets)
static SDL_Thread* stdComm_DPlay_thread = NULL;
static volatile int stdComm_DPlay_bThreadRun = 0;
static int stdComm_DPlay_numUsers = 0;            // (probes under way, and a game: under lifeMutex)
static stdCommProbeGate stdComm_DPlay_probeGate;

// (the network thread's own, made before it starts and closed after it ends)
static int stdComm_DPlay_tcpListen = -1, stdComm_DPlay_udp = -1, stdComm_DPlay_enumUdp = -1;
static dpConn stdComm_DPlay_aConn[2 * DPLAY_MAX_CONNS];
static volatile uint16_t stdComm_DPlay_tcpPort = 0, stdComm_DPlay_udpPort = 0;

// (under stdComm_DPlay_mutex from here on)
static dpPlayer stdComm_DPlay_aPlayers[DPLAY_MAX_PLAYERS];
static int stdComm_DPlay_bActive = 0;      // (a game joined or hosted)
static int stdComm_DPlay_bServer = 0;
static uint32_t stdComm_DPlay_mySysId = 0, stdComm_DPlay_myId = 0;
static uint32_t stdComm_DPlay_hostIp = 0;  // (a player: the host's)
static uint16_t stdComm_DPlay_hostTcpPort = 0;
static uint32_t stdComm_DPlay_hostSysId = 0;
static uint32_t stdComm_DPlay_hostId = 0;  // (the host's player)
static int stdComm_DPlay_maxSysPlayers = 32;
static uint32_t stdComm_DPlay_nextId = 0;
static uint32_t stdComm_DPlay_reserved1 = 0;
static int stdComm_DPlay_bHostLost = 0, stdComm_DPlay_bHostLostTold = 0;
static uint32_t stdComm_DPlay_lastHostMs = 0, stdComm_DPlay_hostClosedMs = 0, stdComm_DPlay_lastPingMs = 0;

static jkMultiEntry stdComm_DPlay_entry;   // the session (hosting), or the one joined
static uint8_t stdComm_DPlay_dpsd[80];     // its DPSESSIONDESC2, as on the wire
static char16_t stdComm_DPlay_password[33];

// (joining: the host's answers)
static volatile uint32_t stdComm_DPlay_replyId = 0;
static volatile int stdComm_DPlay_bSuperEnum = 0;
static volatile uint32_t stdComm_DPlay_nackResult = 0;

static dpSession stdComm_DPlay_aSessions[DPLAY_MAX_SESSIONS];
typedef struct dpAsk { uint32_t ip, gen; } dpAsk;
static dpAsk stdComm_DPlay_aAsk[8];        // (the addresses being asked now, and by which asking: answers from them kept)
static uint32_t stdComm_DPlay_probeGen = 0;
static uint32_t stdComm_DPlay_lastGoodGen = 0; // (the last asking that found something)

static dpEvent* stdComm_DPlay_pQueueHead = NULL;
static dpEvent* stdComm_DPlay_pQueueTail = NULL;
static int stdComm_DPlay_numEvents = 0;
static size_t stdComm_DPlay_eventBytes = 0;
static dpOut* stdComm_DPlay_pOutHead = NULL;
static dpOut* stdComm_DPlay_pOutTail = NULL;
static size_t stdComm_DPlay_outBytes = 0;
static volatile int stdComm_DPlay_numUnsent = 0; // (the network thread's: connections with bytes still to go)

// (the router: under its own lock, stdComm_DPlay_natMutex)
static SDL_Mutex* stdComm_DPlay_natMutex = NULL;
static volatile uint16_t stdComm_DPlay_advTcpPort = 0, stdComm_DPlay_advUdpPort = 0; // (what others are told)
static int stdComm_DPlay_bMapped = 0;
static uint32_t stdComm_DPlay_mappedMs = 0;
static char stdComm_DPlay_intentHost[256] = "";
static uint32_t stdComm_DPlay_intentMs = 0;

static char stdComm_DPlay_problem[384] = "";
static uint32_t stdComm_DPlay_problemGen = 0; // (the asking the problem is from; 0: a game's)

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

static uint16_t dp_myTcpPort(void) { return stdComm_DPlay_advTcpPort ? stdComm_DPlay_advTcpPort : stdComm_DPlay_tcpPort; }
static uint16_t dp_myUdpPort(void) { return stdComm_DPlay_advUdpPort ? stdComm_DPlay_advUdpPort : stdComm_DPlay_udpPort; }

// The 20-byte header every TCP message (and an ENUMSESSIONS) starts with: size, token, our TCP port
static void dp_putSpHeader(uint8_t* p, uint32_t size)
{
    dp_put32(p, (DPLAY_TOKEN << 20) | (size & 0xFFFFF));
    dp_putAddr(p + 4, dp_myTcpPort(), 0);
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

// A reader that stays inside its message: each read checked, the first bad one makes it bad (and
// every read after it 0)
typedef struct dpRd
{
    const uint8_t* p;
    size_t len, off;
    int bBad;
} dpRd;

static void rd_init(dpRd* r, const uint8_t* p, size_t len, size_t off)
{
    r->p = p; r->len = len; r->off = off; r->bBad = off > len;
}
static int rd_has(dpRd* r, size_t n)
{
    if (r->bBad || r->off > r->len || n > r->len - r->off) { r->bBad = 1; return 0; }
    return 1;
}
static uint32_t rd_u8(dpRd* r) { if (!rd_has(r, 1)) return 0; return r->p[r->off++]; }
static uint32_t rd_u16(dpRd* r) { if (!rd_has(r, 2)) return 0; const uint32_t v = dp_get16(r->p + r->off); r->off += 2; return v; }
static uint32_t rd_u32(dpRd* r) { if (!rd_has(r, 4)) return 0; const uint32_t v = dp_get32(r->p + r->off); r->off += 4; return v; }
static const uint8_t* rd_bytes(dpRd* r, size_t n) { if (!rd_has(r, n)) return NULL; const uint8_t* q = r->p + r->off; r->off += n; return q; }
// (a length coded in infoMask: 0, 1, 2 or 4 bytes)
static uint32_t rd_coded(dpRd* r, uint32_t code)
{
    if (code == 1) return rd_u8(r);
    if (code == 2) return rd_u16(r);
    if (code == 3) return rd_u32(r);
    return 0;
}
// An ended UTF-16 string, kept up to outLen - 1 characters; bad if it doesn't end in the message
static void rd_wstr(dpRd* r, char16_t* pOut, int outLen)
{
    int i = 0;
    for (;;) {
        if (!rd_has(r, 2)) break;
        const uint16_t c = dp_get16(r->p + r->off);
        r->off += 2;
        if (!c) break;
        if (i < outLen - 1) pOut[i++] = c;
    }
    if (outLen > 0) pOut[i] = 0;
}
// A string of exactly nBytes (UTF-16, maybe ended inside)
static void rd_wstrN(dpRd* r, char16_t* pOut, int outLen, size_t nBytes)
{
    if (outLen > 0) pOut[0] = 0;
    const uint8_t* q = rd_bytes(r, nBytes);
    if (!q) return;
    int i = 0;
    for (size_t k = 0; k + 1 < nBytes && i < outLen - 1; k += 2) {
        const uint16_t c = dp_get16(q + k);
        if (!c) break;
        pOut[i++] = c;
    }
    if (outLen > 0) pOut[i] = 0;
}

static void dp_queuePush(int kind, uint32_t idFrom, const void* pData, int len)
{
    if (len < 0) len = 0;
    if (stdComm_DPlay_numEvents >= DPLAY_MAX_EVENTS || stdComm_DPlay_eventBytes + (size_t)len > DPLAY_MAX_EVENT_BYTES) {
        if (kind == 0) return; // (the game behind: messages dropped, as a datagram may be; a player gone kept)
    }
    dpEvent* e = (dpEvent*)malloc(sizeof(dpEvent) + (size_t)len);
    if (!e) return;
    e->pNext = NULL;
    e->kind = kind;
    e->idFrom = idFrom;
    e->len = len;
    if (len > 0) memcpy(e->data, pData, (size_t)len);
    if (stdComm_DPlay_pQueueTail) stdComm_DPlay_pQueueTail->pNext = e;
    else stdComm_DPlay_pQueueHead = e;
    stdComm_DPlay_pQueueTail = e;
    stdComm_DPlay_numEvents++;
    stdComm_DPlay_eventBytes += (size_t)len;
}

static dpEvent* dp_queuePop(void)
{
    dpEvent* e = stdComm_DPlay_pQueueHead;
    if (!e) return NULL;
    stdComm_DPlay_pQueueHead = e->pNext;
    if (!stdComm_DPlay_pQueueHead) stdComm_DPlay_pQueueTail = NULL;
    stdComm_DPlay_numEvents--;
    stdComm_DPlay_eventBytes -= (size_t)e->len;
    return e;
}

static void dp_queueClear(void)
{
    dpEvent* e;
    while ((e = dp_queuePop())) free(e);
}

// (under the lock) Something to send, for the network thread
static void dp_outPush(int bTcp, uint32_t ip, uint16_t port, const uint8_t* p, int len)
{
    if (!ip || !port || len <= 0 || stdComm_DPlay_outBytes + (size_t)len > DPLAY_MAX_OUT_BYTES) return;
    dpOut* o = (dpOut*)malloc(sizeof(dpOut) + (size_t)len);
    if (!o) return;
    o->pNext = NULL;
    o->bTcp = bTcp;
    o->ip = ip;
    o->port = port;
    o->len = len;
    memcpy(o->data, p, (size_t)len);
    if (stdComm_DPlay_pOutTail) stdComm_DPlay_pOutTail->pNext = o;
    else stdComm_DPlay_pOutHead = o;
    stdComm_DPlay_pOutTail = o;
    stdComm_DPlay_outBytes += (size_t)len;
}

static void dp_outClear(void)
{
    while (stdComm_DPlay_pOutHead) {
        dpOut* o = stdComm_DPlay_pOutHead;
        stdComm_DPlay_pOutHead = o->pNext;
        free(o);
    }
    stdComm_DPlay_pOutTail = NULL;
    stdComm_DPlay_outBytes = 0;
}

// (under the lock) A system message to a peer's TCP port (its 20-byte header filled in here)
static void dp_sendTcp(uint32_t ip, uint16_t port, uint8_t* pMsg, int len)
{
    dp_putSpHeader(pMsg, (uint32_t)len);
    dp_outPush(1, ip, port, pMsg, len);
}

static void dp_sendUdp(uint32_t ip, uint16_t port, const uint8_t* p, int len)
{
    dp_outPush(0, ip, port, p, len);
}

void stdComm_DPlay_ClearProblem(void);
static void dp_holdExpired(uint32_t now);

static void dp_setProblem(const char* pText, uint32_t gen)
{
    snprintf(stdComm_DPlay_problem, sizeof(stdComm_DPlay_problem), "%s", pText);
    stdComm_DPlay_problemGen = gen;
    stdComm_DPlay_infoPrintf("DPlay: %s\n", pText);
}

// ------------------------------------------------------------------ players

static dpPlayer* dp_findPlayer(uint32_t id)
{
    if (!id) return NULL;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++)
        if (stdComm_DPlay_aPlayers[i].bUsed && stdComm_DPlay_aPlayers[i].id == id) return &stdComm_DPlay_aPlayers[i];
    return NULL;
}

static int dp_numSysPlayers(void)
{
    int n = 0;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++)
        n += stdComm_DPlay_aPlayers[i].bUsed && (stdComm_DPlay_aPlayers[i].flags & DPLAYI_PLAYER_SYSPLAYER);
    return n;
}

// A player kept (or the one already kept with that id); NULL if there is no room, or it would be one
// system player more than the session holds
static dpPlayer* dp_addPlayer(uint32_t id, int bSys)
{
    if (!id) return NULL;
    dpPlayer* p = dp_findPlayer(id);
    if (p) return p;
    if (bSys && dp_numSysPlayers() >= stdComm_DPlay_maxSysPlayers) return NULL;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        if (!stdComm_DPlay_aPlayers[i].bUsed) {
            p = &stdComm_DPlay_aPlayers[i];
            memset(p, 0, sizeof(*p));
            p->bUsed = 1;
            p->id = id;
            p->flags = bSys ? DPLAYI_PLAYER_SYSPLAYER : 0;
            p->sysId = bSys ? id : 0;
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
            if (!(p->flags & DPLAYI_PLAYER_SYSPLAYER) && stdComm_DPlay_bActive) dp_queuePush(2, p->id, NULL, 0);
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

// (a player) A peer's address believable: not a private one when the host is on the internet
static int dp_peerAddressOk(uint32_t ip)
{
    if (!ip) return 0;
    if (stdComm_DPlay_bServer || stdComm_DPlayNat_IsPrivate(stdComm_DPlay_hostIp)) return 1;
    return !stdComm_DPlayNat_IsPrivate(ip);
}

// The system player at that address (a peer: the host, or another player)
static dpPlayer* dp_sysPlayerAt(uint32_t ip)
{
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        dpPlayer* p = &stdComm_DPlay_aPlayers[i];
        if (p->bUsed && (p->flags & DPLAYI_PLAYER_SYSPLAYER) && p->ip == ip && p->id != stdComm_DPlay_mySysId) return p;
    }
    return NULL;
}

// Something came from that address: its system players heard from
static void dp_heardFrom(uint32_t ip)
{
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        dpPlayer* p = &stdComm_DPlay_aPlayers[i];
        if (p->bUsed && (p->flags & DPLAYI_PLAYER_SYSPLAYER) && p->ip == ip) p->bHeard = 1;
    }
    if (ip == stdComm_DPlay_hostIp) stdComm_DPlay_lastHostMs = stdPlatform_GetTimeMsec();
}

// A datagram to a system player: freely once heard from; before that, from a bucket (it may not be
// where it says)
static void dp_sendToSys(dpPlayer* sp, const uint8_t* p, int len)
{
    if (!sp->udpPort || !sp->ip) return;
    if (!sp->bHeard) {
        const uint32_t now = stdPlatform_GetTimeMsec();
        if (!sp->budgetMs) { sp->budgetMs = now; sp->budget = DPLAY_UNHEARD_BURST; }
        const uint32_t refill = (now - sp->budgetMs) * DPLAY_UNHEARD_PER_S / 1000;
        if (refill) {
            sp->budget = sp->budget + (int)refill > DPLAY_UNHEARD_BURST ? DPLAY_UNHEARD_BURST : sp->budget + (int)refill;
            sp->budgetMs = now;
        }
        if (sp->budget <= 0) {
            stdComm_DPlay_verbosePrintf("DPlay: a datagram to %x not sent: not heard from yet\n", sp->id);
            return;
        }
        sp->budget--;
    }
    dp_sendUdp(sp->ip, sp->udpPort, p, len);
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
    pOut->episodeGobName[31] = pOut->mapJklFname[31] = 0;
}

// (a session's dwMaxPlayers: how many system players there may be)
static void dp_setMaxPlayers(const uint8_t* dpsd)
{
    uint32_t n = dp_get32(dpsd + 0x28);
    if (n < 2) n = 2;
    if (n > 32) n = 32;
    stdComm_DPlay_maxSysPlayers = (int)n;
}

// ------------------------------------------------------------------ reading what comes

// SUPERENUMPLAYERSREPLY (b: from its "play"): the session and every player. Stops at the first field
// that doesn't fit in the message.
static void dp_parseSuperEnum(const uint8_t* b, int lenIn, uint32_t srcIp)
{
    if (lenIn < 36) return;
    const size_t len = (size_t)lenIn;
    dpRd r;
    rd_init(&r, b, len, 8);
    const uint32_t numPlayers = rd_u32(&r), numGroups = rd_u32(&r), packedOff = rd_u32(&r);
    rd_u32(&r); // (shortcuts)
    const uint32_t descOff = rd_u32(&r), nameOff = rd_u32(&r);
    if (r.bBad) return;
    if (descOff >= 36 && descOff <= len && 80 <= len - descOff) {
        memcpy(stdComm_DPlay_dpsd, b + descOff, 80);
        dp_setMaxPlayers(stdComm_DPlay_dpsd);
        if (nameOff >= 36 && nameOff < len) {
            char16_t name[128];
            dpRd rn;
            rd_init(&rn, b, len, nameOff);
            rd_wstr(&rn, name, 128);
            if (!rn.bBad) dp_parseSessionDesc(stdComm_DPlay_dpsd, name, &stdComm_DPlay_entry);
        }
    }
    if (packedOff < 36 || packedOff > len) return;
    rd_init(&r, b, len, packedOff);
    const uint32_t numAll = (numPlayers > DPLAY_MAX_PLAYERS ? DPLAY_MAX_PLAYERS : numPlayers) + (numGroups > DPLAY_MAX_PLAYERS ? DPLAY_MAX_PLAYERS : numGroups);
    for (uint32_t i = 0; i < numAll; i++) {
        const size_t start = r.off;
        const uint32_t size = rd_u32(&r), flags = rd_u32(&r), id = rd_u32(&r), mask = rd_u32(&r), verOrSys = rd_u32(&r);
        // (size: the fixed part after the size itself, 16; anything more skipped)
        if (r.bBad || size < 16 || size > len) return;
        r.off = start + 4;
        if (!rd_bytes(&r, size)) return;
        char16_t sn[32] = { 0 }, ln[32] = { 0 };
        if (mask & 1) rd_wstr(&r, sn, 32);
        if (mask & 2) rd_wstr(&r, ln, 32);
        const uint32_t dataLen = rd_coded(&r, (mask >> 4) & 3);
        rd_bytes(&r, dataLen);
        const uint32_t spLen = rd_coded(&r, (mask >> 2) & 3);
        const uint8_t* sp = rd_bytes(&r, spLen);
        const uint32_t count = rd_coded(&r, (mask >> 6) & 3);
        if (count > DPLAY_MAX_PLAYERS) return;
        rd_bytes(&r, (size_t)count * 4);
        if (mask & 0x100) rd_u32(&r);
        const uint32_t shortcuts = rd_coded(&r, (mask >> 9) & 3);
        if (shortcuts > DPLAY_MAX_PLAYERS) return;
        rd_bytes(&r, (size_t)shortcuts * 4);
        if (r.bBad) return;
        if (i >= numPlayers) continue; // (groups: not used by the game)
        if (!id || id == stdComm_DPlay_mySysId || id == stdComm_DPlay_myId) continue;
        const int bSys = (flags & DPLAYI_PLAYER_SYSPLAYER) != 0;
        uint32_t ip = srcIp;
        uint16_t tcp = 0, udp = 0;
        if (sp && spLen >= 32) {
            if (dp_addrIp(sp)) ip = dp_addrIp(sp);
            tcp = dp_addrPort(sp);
            udp = dp_addrPort(sp + 16);
        }
        if (!dp_peerAddressOk(ip)) continue;
        dpPlayer* pl = dp_addPlayer(id, bSys);
        if (!pl) continue;
        pl->flags = flags & ~DPLAYI_PLAYER_LOCAL;
        pl->sysId = bSys ? id : verOrSys;
        memcpy(pl->name, sn, sizeof(sn));
        pl->ip = ip;
        pl->tcpPort = tcp;
        pl->udpPort = udp;
        if (bSys && ip == stdComm_DPlay_hostIp && tcp == stdComm_DPlay_hostTcpPort) {
            stdComm_DPlay_hostSysId = id; // (the host's system player)
            pl->bHeard = 1;
        }
        if (!bSys && (flags & DPLAYI_PLAYER_LOCAL) && !stdComm_DPlay_hostId)
            stdComm_DPlay_hostId = id; // (the host's own player)
        stdComm_DPlay_verbosePrintf("DPlay: player %x (%s, of %x) at %x:%d/%d\n", id, bSys ? "system" : "game", pl->sysId, ntohl(ip), tcp, udp);
    }
    // (the host's player: one of the host's system player, if none was marked)
    if (!stdComm_DPlay_hostId && stdComm_DPlay_hostSysId) {
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (pl->bUsed && !(pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->sysId == stdComm_DPlay_hostSysId) { stdComm_DPlay_hostId = pl->id; break; }
        }
    }
}

// The PackedPlayer after a CREATEPLAYER's (or ADDFORWARD's) 28 bytes (b: from its "play"): its
// fields, read within the message. 0 if it doesn't fit.
typedef struct dpPacked
{
    uint32_t flags, id, sysId;
    char16_t name[32];
    int bSp;
    uint32_t ip;
    uint16_t tcpPort, udpPort;
} dpPacked;

static int dp_readPacked(const uint8_t* b, size_t len, dpPacked* pOut)
{
    memset(pOut, 0, sizeof(*pOut));
    if (len < 28 + 48) return 0;
    const uint8_t* pp = b + 28;
    pOut->flags = dp_get32(pp + 4);
    pOut->id = dp_get32(pp + 8);
    const uint32_t snLen = dp_get32(pp + 12), lnLen = dp_get32(pp + 16), spLen = dp_get32(pp + 20);
    pOut->sysId = dp_get32(pp + 32);
    size_t o = 28 + 48;
    if (snLen > len - o) return 0;
    if (snLen) {
        dpRd r;
        rd_init(&r, b, len, o);
        rd_wstrN(&r, pOut->name, 32, snLen);
    }
    o += snLen;
    if (lnLen > len - o) return 0;
    o += lnLen;
    if (spLen >= 32 && spLen <= len - o) {
        pOut->bSp = 1;
        pOut->ip = dp_addrIp(b + o);
        pOut->tcpPort = dp_addrPort(b + o);
        pOut->udpPort = dp_addrPort(b + o + 16);
    }
    return 1;
}

// A player made (CREATEPLAYER), at srcIp's word
static void dp_parseCreatePlayer(const uint8_t* b, int lenIn, uint32_t srcIp)
{
    if (lenIn < 28 + 48) return;
    dpPacked pk;
    if (!dp_readPacked(b, (size_t)lenIn, &pk)) return;
    if (!pk.id || pk.id == stdComm_DPlay_myId || pk.id == stdComm_DPlay_mySysId) return;
    const int bSys = (pk.flags & DPLAYI_PLAYER_SYSPLAYER) != 0;
    const uint32_t sysId = bSys ? pk.id : pk.sysId;
    if (!sysId) return;
    const uint32_t ip = (pk.bSp && pk.ip) ? pk.ip : srcIp;
    if (!dp_peerAddressOk(ip)) return;
    // (its system player: known (SUPERENUMPLAYERSREPLY, ADDFORWARD), or made from this)
    dpPlayer* sp = dp_findPlayer(sysId);
    if (!sp && !bSys) {
        if (!pk.bSp || !pk.udpPort) return;
        sp = dp_addPlayer(sysId, 1);
        if (!sp) return;
        sp->ip = ip;
        sp->tcpPort = pk.tcpPort;
        sp->udpPort = pk.udpPort;
    }
    dpPlayer* pl = dp_addPlayer(pk.id, bSys);
    if (!pl) return;
    pl->flags = pk.flags & ~DPLAYI_PLAYER_LOCAL;
    pl->sysId = sysId;
    memcpy(pl->name, pk.name, sizeof(pk.name));
    if (sp && sp != pl) { pl->ip = sp->ip; pl->tcpPort = sp->tcpPort; pl->udpPort = sp->udpPort; }
    else { pl->ip = ip; pl->tcpPort = pk.tcpPort; pl->udpPort = pk.udpPort; }
    if (!bSys) stdComm_DPlay_infoPrintf("DPlay: player %x joined\n", pk.id);
}

// (hosting) A player's CREATEPLAYER or DELETEPLAYER passed on to the other players (m: from its
// 20-byte header), the address in a CREATEPLAYER's SP data put in (0 in it is "where this came
// from", which is us when passed on)
static void dp_forward(const uint8_t* m, int lenIn, uint32_t srcIp, int bCreate)
{
    static uint8_t f[4096];
    if (lenIn < 20 + 8 || lenIn > (int)sizeof(f)) return;
    const size_t len = (size_t)lenIn;
    memcpy(f, m, len);
    if (bCreate) {
        if (len < 20 + 28 + 48) return;
        const uint8_t* pp = f + 20 + 28;
        const uint32_t snLen = dp_get32(pp + 12), lnLen = dp_get32(pp + 16), spLen = dp_get32(pp + 20);
        const size_t base = 20 + 28 + 48;
        if (snLen > len - base || lnLen > len - base - snLen) return;
        const size_t spOff = base + snLen + lnLen;
        if (spLen >= 32 && spLen <= len - spOff && 32 <= len - spOff) {
            if (!dp_addrIp(f + spOff)) memcpy(f + spOff + 4, &srcIp, 4);
            if (!dp_addrIp(f + spOff + 16)) memcpy(f + spOff + 20, &srcIp, 4);
        }
    }
    const uint16_t fromPort = dp_addrPort(m + 4);
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        const dpPlayer* q = &stdComm_DPlay_aPlayers[i];
        if (q->bUsed && (q->flags & DPLAYI_PLAYER_SYSPLAYER) && q->id != stdComm_DPlay_mySysId && q->tcpPort
            && !(q->ip == srcIp && q->tcpPort == fromPort))
            dp_sendTcp(q->ip, q->tcpPort, f, (int)len);
    }
}

// A game message (from id, to id, then the game's own bytes), at srcIp: kept only from where the
// player it says it is from is
static void dp_gameMessage(const uint8_t* p, int len, uint32_t srcIp)
{
    if (len < 8 + 4 || !stdComm_DPlay_bActive) return;
    const uint32_t from = dp_get32(p);
    const dpPlayer* pl = dp_findPlayer(from);
    if (!pl || (pl->flags & DPLAYI_PLAYER_SYSPLAYER)) return;
    const dpPlayer* sp = dp_findPlayer(pl->sysId);
    if (!sp || sp->ip != srcIp || sp->id == stdComm_DPlay_mySysId) return;
    dp_heardFrom(srcIp);
    dp_queuePush(0, from, p + 8, len - 8);
}

// ------------------------------------------------------------------ hosting (for testing)

static void dp_sendPlayerReply(uint32_t ip, uint16_t port, uint32_t id)
{
    uint8_t m[20 + 104];
    memset(m, 0, sizeof(m));
    const int o = 20 + dp_putMsgHeader(m + 20, DPCMD_REQUESTPLAYERREPLY);
    dp_put32(m + o, id); // (then a DPSECURITYDESC, two offsets, the result and two empty names: 0)
    dp_sendTcp(ip, port, m, sizeof(m));
}

// Every player, to a system player that has just joined
static void dp_sendSuperEnum(uint32_t ip, uint16_t port)
{
    static uint8_t m[8192];
    memset(m, 0, 20 + 36);
    const int o = 20 + dp_putMsgHeader(m + 20, DPCMD_SUPERENUMPLAYERSREPLY);
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
        if (pl->ip == ip && bSys) flags &= ~DPLAYI_PLAYER_LOCAL;
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
        dp_putAddr(m + p, pl->tcpPort, pl->ip);
        dp_putAddr(m + p + 16, pl->udpPort, pl->ip);
        p += 32;
        numPlayers++;
    }
    dp_put32(m + o, (uint32_t)numPlayers);
    dp_put32(m + o + 4, 0);
    dp_put32(m + o + 8, (uint32_t)(fixed + 80 + nameLen));
    dp_put32(m + o + 12, 0);
    dp_put32(m + o + 16, (uint32_t)fixed);
    dp_put32(m + o + 20, (uint32_t)(fixed + 80));
    dp_put32(m + o + 24, 0);
    dp_sendTcp(ip, port, m, p);
}

static void dp_hostMessage(const uint8_t* m, size_t len, uint32_t srcIp)
{
    const uint16_t replyPort = dp_addrPort(m + 4);
    const uint8_t* b = m + 20;
    const size_t bl = len - 20;
    const uint16_t cmd = dp_get16(b + 4);
    switch (cmd)
    {
    case DPCMD_REQUESTPLAYERID:
        if (dp_numSysPlayers() < stdComm_DPlay_maxSysPlayers || (bl >= 12 && dp_get32(b + 8) == 0x8))
            dp_sendPlayerReply(srcIp, replyPort, stdComm_DPlay_nextId++);
        break;
    case DPCMD_ADDFORWARDREQUEST: {
        if (bl < 28 + 48 + 32) break;
        const uint32_t id = dp_get32(b + 12), pwOff = dp_get32(b + 24);
        const uint8_t* sp = b + 28 + 48;
        char16_t pw[33] = { 0 };
        if (pwOff >= 28 && pwOff < bl) { dpRd r; rd_init(&r, b, bl, pwOff); rd_wstr(&r, pw, 33); }
        if (stdComm_DPlay_password[0] && memcmp(pw, stdComm_DPlay_password, sizeof(pw))) {
            uint8_t rr[20 + 12];
            memset(rr, 0, sizeof(rr));
            dp_putMsgHeader(rr + 20, DPCMD_ADDFORWARDREPLY);
            dp_put32(rr + 28, 0x8877015E); // DPERR_INVALIDPASSWORD
            dp_sendTcp(srcIp, replyPort, rr, sizeof(rr));
            break;
        }
        dpPlayer* pl = dp_addPlayer(id, 1);
        if (!pl) break;
        pl->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_INGROUP;
        pl->ip = srcIp;
        pl->tcpPort = dp_addrPort(sp);
        pl->udpPort = dp_addrPort(sp + 16);
        pl->bHeard = 1;
        // (the players already in told of it: ADDFORWARD, as DirectPlay's host does)
        uint8_t f[20 + 28 + 48 + 32];
        memset(f, 0, sizeof(f));
        int o = 20 + dp_putMsgHeader(f + 20, DPCMD_ADDFORWARD);
        dp_put32(f + o + 4, id);
        dp_put32(f + o + 12, 28);
        o += 20;
        dp_put32(f + o, 48 + 32); dp_put32(f + o + 4, DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_INGROUP); dp_put32(f + o + 8, id);
        dp_put32(f + o + 20, 32); dp_put32(f + o + 32, id); dp_put32(f + o + 36, 48); dp_put32(f + o + 40, DPLAY_VERSION);
        dp_putAddr(f + o + 48, pl->tcpPort, pl->ip);
        dp_putAddr(f + o + 64, pl->udpPort, pl->ip);
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            const dpPlayer* q = &stdComm_DPlay_aPlayers[i];
            if (q->bUsed && (q->flags & DPLAYI_PLAYER_SYSPLAYER) && q->id != id && q->id != stdComm_DPlay_mySysId && q->tcpPort)
                dp_sendTcp(q->ip, q->tcpPort, f, sizeof(f));
        }
        dp_sendSuperEnum(srcIp, replyPort);
        stdComm_DPlay_infoPrintf("DPlay: system player %x joining from %x\n", id, ntohl(srcIp));
        break;
    }
    case DPCMD_CREATEPLAYER: {
        // (a player's own players only, from where its system player is)
        dpPacked pk;
        if (!dp_readPacked(b, bl, &pk)) break;
        const dpPlayer* sp = dp_findPlayer(pk.sysId);
        if (!sp || sp->ip != srcIp) break;
        dp_parseCreatePlayer(b, (int)bl, srcIp);
        dp_forward(m, (int)len, srcIp, 1);
        break;
    }
    case DPCMD_DELETEPLAYER: {
        if (bl < 16) break;
        const dpPlayer* pl = dp_findPlayer(dp_get32(b + 12));
        const dpPlayer* sp = pl ? dp_findPlayer(pl->sysId ? pl->sysId : pl->id) : NULL;
        if (!sp || sp->ip != srcIp) break;
        dp_removePlayer(pl->id);
        dp_forward(m, (int)len, srcIp, 0);
        break;
    }
    case DPCMD_PING: {
        if (bl < 16 || !dp_sysPlayerAt(srcIp)) break;
        uint8_t rr[20 + 16];
        memset(rr, 0, sizeof(rr));
        dp_putMsgHeader(rr + 20, DPCMD_PINGREPLY);
        dp_put32(rr + 28, stdComm_DPlay_mySysId);
        dp_put32(rr + 32, dp_get32(b + 12));
        dp_sendTcp(srcIp, replyPort, rr, sizeof(rr));
        break;
    }
    default:
        break;
    }
}

// An ENUMSESSIONS (hosting): answered over TCP, to the port it gives, at the address it came from
static void dp_enumRequest(const uint8_t* m, size_t len, uint32_t srcIp)
{
    if (!stdComm_DPlay_bServer || len < 20 + 32) return;
    const uint8_t* b = m + 20;
    if (dp_get32(b) != DPLAY_MAGIC || dp_get16(b + 4) != DPCMD_ENUMSESSIONS) return;
    static const uint8_t zero[16] = { 0 };
    if (memcmp(b + 8, stdComm_DPlay_appGuid, 16) && memcmp(b + 8, zero, 16)) return;
    uint8_t r[20 + 8 + 80 + 4 + 260];
    memset(r, 0, sizeof(r));
    const int o = 20 + dp_putMsgHeader(r + 20, DPCMD_ENUMSESSIONSREPLY);
    dp_makeSessionDesc();
    memcpy(r + o, stdComm_DPlay_dpsd, 80);
    dp_put32(r + o + 80, 8 + 80 + 4);
    const int n = dp_putSessionName(r + o + 84);
    dp_sendTcp(srcIp, dp_addrPort(m + 4), r, o + 84 + n);
}

// ------------------------------------------------------------------ a player: what comes in

// (the asking that address is being asked by; 0 if none)
static uint32_t dp_askedBy(uint32_t ip)
{
    for (int i = 0; i < 8; i++) if (stdComm_DPlay_aAsk[i].gen && stdComm_DPlay_aAsk[i].ip == ip) return stdComm_DPlay_aAsk[i].gen;
    return 0;
}

static void dp_askSet(const uint32_t* aIp, int n, uint32_t gen)
{
    for (int k = 0; k < n; k++) {
        int slot = -1;
        for (int i = 0; i < 8 && slot < 0; i++) if (stdComm_DPlay_aAsk[i].gen && stdComm_DPlay_aAsk[i].ip == aIp[k]) slot = i;
        for (int i = 0; i < 8 && slot < 0; i++) if (!stdComm_DPlay_aAsk[i].gen) slot = i;
        if (slot < 0) slot = 0;
        stdComm_DPlay_aAsk[slot].ip = aIp[k];
        stdComm_DPlay_aAsk[slot].gen = gen;
    }
}

static void dp_askClear(uint32_t gen)
{
    for (int i = 0; i < 8; i++) if (stdComm_DPlay_aAsk[i].gen == gen) stdComm_DPlay_aAsk[i].gen = 0;
}

// (under the lock) One system message (over TCP: m from its 20-byte header), from srcIp
static void dp_systemMessage(const uint8_t* m, size_t len, uint32_t srcIp)
{
    if (len < 28) return;
    if (stdComm_DPlay_bServer) { dp_hostMessage(m, len, srcIp); return; }
    const uint16_t replyPort = dp_addrPort(m + 4);
    const uint8_t* b = m + 20;
    const size_t bl = len - 20;
    const uint16_t cmd = dp_get16(b + 4);
    // (the host's word: what tells of the game and its players comes from its address only; its
    // port may be another than the one it gave, through a router)
    const int bFromHost = stdComm_DPlay_hostIp && srcIp == stdComm_DPlay_hostIp;
    if (bFromHost) dp_heardFrom(srcIp);
    stdComm_DPlay_verbosePrintf("DPlay: message %d (v%d) from %x:%d, %d bytes\n", cmd, dp_get16(b + 6), ntohl(srcIp), replyPort, (int)len);
    switch (cmd)
    {
    case DPCMD_ENUMSESSIONSREPLY: {
        // (only from an address being asked; each session by its address, port and GUID)
        const uint32_t gen = dp_askedBy(srcIp);
        if (bl < 8 + 80 + 4 || !gen) break;
        if (memcmp(b + 8 + 0x18, stdComm_DPlay_appGuid, 16)) break; // (another game's)
        dpSession* se = NULL;
        for (int i = 0; i < DPLAY_MAX_SESSIONS && !se; i++) {
            dpSession* s = &stdComm_DPlay_aSessions[i];
            if (s->bUsed && s->ip == srcIp && s->tcpPort == replyPort && !memcmp(s->dpsd + 8, b + 8 + 8, 16)) se = s;
        }
        for (int i = 0; i < DPLAY_MAX_SESSIONS && !se; i++)
            if (!stdComm_DPlay_aSessions[i].bUsed) se = &stdComm_DPlay_aSessions[i];
        if (!se) break;
        memset(se, 0, sizeof(*se));
        se->bUsed = 1;
        memcpy(se->dpsd, b + 8, 80);
        const uint32_t nameOff = dp_get32(b + 88);
        if (nameOff >= 92 && nameOff < bl) { dpRd r; rd_init(&r, b, bl, nameOff); rd_wstr(&r, se->name, 128); }
        se->ip = srcIp;
        se->tcpPort = replyPort;
        se->probeGen = gen;
        se->seenMs = stdPlatform_GetTimeMsec();
        break;
    }
    case DPCMD_REQUESTPLAYERREPLY:
        if (bFromHost && bl >= 12) stdComm_DPlay_replyId = dp_get32(b + 8);
        break;
    case DPCMD_ADDFORWARDREPLY:
        if (bFromHost) stdComm_DPlay_nackResult = bl >= 12 ? dp_get32(b + 8) : 0x80004005;
        break;
    case DPCMD_SUPERENUMPLAYERSREPLY:
        if (!bFromHost || stdComm_DPlay_bSuperEnum) break;
        dp_parseSuperEnum(b, (int)bl, srcIp);
        stdComm_DPlay_bSuperEnum = 1;
        break;
    case DPCMD_ADDFORWARD: {
        // (the host telling us of another player's system player)
        if (!bFromHost || !stdComm_DPlay_bSuperEnum) break;
        dpPacked pk;
        if (!dp_readPacked(b, bl, &pk) || !pk.bSp || !(pk.flags & DPLAYI_PLAYER_SYSPLAYER)) break;
        const uint32_t ip = pk.ip ? pk.ip : srcIp;
        if (!dp_peerAddressOk(ip) || pk.id == stdComm_DPlay_mySysId) break;
        dpPlayer* pl = dp_addPlayer(pk.id, 1);
        if (!pl) break;
        pl->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_INGROUP;
        pl->ip = ip;
        pl->tcpPort = pk.tcpPort;
        pl->udpPort = pk.udpPort;
        uint8_t r[20 + 12];
        memset(r, 0, sizeof(r));
        dp_putMsgHeader(r + 20, DPCMD_ADDFORWARDACK);
        dp_put32(r + 28, pk.id);
        dp_sendTcp(srcIp, replyPort, r, sizeof(r));
        break;
    }
    case DPCMD_CREATEPLAYER: {
        // (from the host, or from a player telling of its own players)
        if (!stdComm_DPlay_bSuperEnum) break;
        dpPacked pk;
        if (!dp_readPacked(b, bl, &pk)) break;
        if (!bFromHost) {
            const dpPlayer* sp = dp_findPlayer((pk.flags & DPLAYI_PLAYER_SYSPLAYER) ? pk.id : pk.sysId);
            if (!sp || !(sp->flags & DPLAYI_PLAYER_SYSPLAYER) || sp->ip != srcIp) break;
            dp_heardFrom(srcIp);
        }
        dp_parseCreatePlayer(b, (int)bl, srcIp);
        break;
    }
    case DPCMD_DELETEPLAYER: {
        if (bl < 16) break;
        const dpPlayer* pl = dp_findPlayer(dp_get32(b + 12));
        if (!pl || pl->id == stdComm_DPlay_myId || pl->id == stdComm_DPlay_mySysId) break;
        if (!bFromHost) {
            const dpPlayer* sp = dp_findPlayer(pl->sysId ? pl->sysId : pl->id);
            if (!sp || sp->ip != srcIp) break;
        }
        if (!(pl->flags & DPLAYI_PLAYER_SYSPLAYER)) stdComm_DPlay_infoPrintf("DPlay: player %x left\n", pl->id);
        dp_removePlayer(pl->id);
        break;
    }
    case DPCMD_PING: {
        // (answered for those in the game only)
        if (bl < 16 || !(bFromHost || dp_sysPlayerAt(srcIp))) break;
        uint8_t r[20 + 16];
        memset(r, 0, sizeof(r));
        dp_putMsgHeader(r + 20, DPCMD_PINGREPLY);
        dp_put32(r + 28, stdComm_DPlay_mySysId);
        dp_put32(r + 32, dp_get32(b + 12));
        dp_sendTcp(srcIp, replyPort, r, sizeof(r));
        break;
    }
    default:
        break;
    }
}

// (under the lock) One message off a TCP connection
static void dp_tcpMessage(const uint8_t* m, size_t len, uint32_t srcIp)
{
    if (len >= 28 && dp_get32(m + 20) == DPLAY_MAGIC) dp_systemMessage(m, len, srcIp);
    else if (len > 20) dp_gameMessage(m + 20, (int)(len - 20), srcIp);
}

// ------------------------------------------------------------------ the network thread

static void dp_nonblock(int fd)
{
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void dp_noSigPipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

static int dp_bindRange(int type, uint16_t first, uint16_t last, volatile uint16_t* pPortOut)
{
    for (uint32_t port = first; port <= last; port++) {
        int fd = socket(AF_INET, type, 0);
        if (fd < 0) return -1;
        dp_noSigPipe(fd);
        if (type == SOCK_STREAM) { // (our listening port again after a probe: not held by connections closing)
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
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

static dpConn* dp_freeConn(int bOut)
{
    const int first = bOut ? DPLAY_MAX_CONNS : 0;
    for (int i = first; i < first + DPLAY_MAX_CONNS; i++)
        if (stdComm_DPlay_aConn[i].fd < 0) return &stdComm_DPlay_aConn[i];
    return NULL;
}

// (the network thread) Our connection to a peer's listening port: made (without waiting) if there is
// none, what is to go on it kept until it is up
static dpConn* dp_outConn(uint32_t ip, uint16_t port)
{
    for (int i = DPLAY_MAX_CONNS; i < 2 * DPLAY_MAX_CONNS; i++) {
        dpConn* c = &stdComm_DPlay_aConn[i];
        if (c->fd >= 0 && c->ip == ip && c->port == port) return c;
    }
    dpConn* c = dp_freeConn(1);
    if (!c) return NULL;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    dp_noSigPipe(fd);
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    dp_nonblock(fd);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    const int r = connect(fd, (struct sockaddr*)&a, sizeof(a));
    if (r != 0 && errno != EINPROGRESS) { close(fd); return NULL; }
    c->fd = fd;
    c->bOut = 1;
    c->bConnecting = r != 0;
    c->ip = ip;
    c->port = port;
    c->lastMs = stdPlatform_GetTimeMsec();
    return c;
}

static void dp_connAppend(dpConn* c, const uint8_t* p, size_t n)
{
    if (c->len + n > DPLAY_MAX_PENDING) { dp_closeConn(c); return; } // (a peer that doesn't read: dropped)
    uint8_t* q = (uint8_t*)realloc(c->pBuf, c->len + n);
    if (!q) return;
    c->pBuf = q;
    memcpy(c->pBuf + c->len, p, n);
    c->len += n;
}

static void dp_connFlush(dpConn* c)
{
    while (c->fd >= 0 && !c->bConnecting && c->len) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = send(c->fd, c->pBuf, c->len, MSG_NOSIGNAL);
#else
        const ssize_t n = send(c->fd, c->pBuf, c->len, 0);
#endif
        if (n > 0) {
            memmove(c->pBuf, c->pBuf + n, c->len - (size_t)n);
            c->len -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        dp_closeConn(c);
    }
}

// (the network thread) What the game's side queued, sent
static void dp_sendQueued(void)
{
    SDL_LockMutex(stdComm_DPlay_mutex);
    dpOut* o = stdComm_DPlay_pOutHead;
    stdComm_DPlay_pOutHead = stdComm_DPlay_pOutTail = NULL;
    stdComm_DPlay_outBytes = 0;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    while (o) {
        dpOut* next = o->pNext;
        if (o->bTcp) {
            dpConn* c = dp_outConn(o->ip, o->port);
            if (c) { dp_connAppend(c, o->data, (size_t)o->len); if (c->fd >= 0) dp_connFlush(c); }
        }
        else if (stdComm_DPlay_udp >= 0) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_port = htons(o->port);
            a.sin_addr.s_addr = o->ip;
            sendto(stdComm_DPlay_udp, o->data, (size_t)o->len, 0, (struct sockaddr*)&a, sizeof(a));
        }
        free(o);
        o = next;
    }
}

static int dp_inConnsFrom(uint32_t ip)
{
    int n = 0;
    for (int i = 0; i < DPLAY_MAX_CONNS; i++) n += stdComm_DPlay_aConn[i].fd >= 0 && stdComm_DPlay_aConn[i].ip == ip;
    return n;
}

// (the network thread) The host's connection to us closed: gone if nothing more comes soon
static void dp_inClosed(uint32_t ip)
{
    SDL_LockMutex(stdComm_DPlay_mutex);
    if (stdComm_DPlay_bActive && !stdComm_DPlay_bServer && ip == stdComm_DPlay_hostIp) stdComm_DPlay_hostClosedMs = stdPlatform_GetTimeMsec() | 1;
    if (stdComm_DPlay_bActive && stdComm_DPlay_bServer) {
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (pl->bUsed && (pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->ip == ip && pl->id != stdComm_DPlay_mySysId) dp_removePlayer(pl->id);
        }
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
}

static void dp_poll(int timeoutMs)
{
    struct pollfd pf[3 + 2 * DPLAY_MAX_CONNS];
    int map[3 + 2 * DPLAY_MAX_CONNS];
    int n = 0;
    if (stdComm_DPlay_tcpListen >= 0) { pf[n].fd = stdComm_DPlay_tcpListen; pf[n].events = POLLIN; map[n++] = -1; }
    if (stdComm_DPlay_udp >= 0) { pf[n].fd = stdComm_DPlay_udp; pf[n].events = POLLIN; map[n++] = -2; }
    if (stdComm_DPlay_enumUdp >= 0) { pf[n].fd = stdComm_DPlay_enumUdp; pf[n].events = POLLIN; map[n++] = -3; }
    for (int i = 0; i < 2 * DPLAY_MAX_CONNS; i++) {
        const dpConn* c = &stdComm_DPlay_aConn[i];
        if (c->fd < 0) continue;
        pf[n].fd = c->fd;
        pf[n].events = c->bOut ? ((c->bConnecting || c->len) ? POLLOUT : POLLIN) : POLLIN;
        map[n++] = i;
    }
    if (!n || poll(pf, n, timeoutMs) <= 0) return;

    static uint8_t buf[DPLAY_MAX_MSG];
    const uint32_t now = stdPlatform_GetTimeMsec();
    for (int k = 0; k < n; k++) {
        if (!(pf[k].revents & (POLLIN | POLLOUT | POLLHUP | POLLERR))) continue;
        if (map[k] == -1) {
            struct sockaddr_in a;
            socklen_t al = sizeof(a);
            const int fd = accept(stdComm_DPlay_tcpListen, (struct sockaddr*)&a, &al);
            if (fd < 0) continue;
            dpConn* c = dp_inConnsFrom(a.sin_addr.s_addr) < DPLAY_MAX_IN_PER_IP ? dp_freeConn(0) : NULL;
            if (!c) { close(fd); continue; }
            dp_nonblock(fd);
            dp_noSigPipe(fd);
            c->fd = fd;
            c->ip = a.sin_addr.s_addr;
            c->lastMs = now;
            c->pBuf = (uint8_t*)malloc(DPLAY_MAX_MSG);
            if (!c->pBuf) dp_closeConn(c);
        }
        else if (map[k] == -2 || map[k] == -3) {
            struct sockaddr_in a;
            socklen_t al = sizeof(a);
            const ssize_t got = recvfrom(pf[k].fd, buf, sizeof(buf), 0, (struct sockaddr*)&a, &al);
            if (got <= 0) continue;
            SDL_LockMutex(stdComm_DPlay_mutex);
            if (map[k] == -3) dp_enumRequest(buf, (size_t)got, a.sin_addr.s_addr);
            // (system messages come over TCP: one in a datagram is no one's)
            else if (!(got >= 4 && dp_get32(buf) == DPLAY_MAGIC) && !(got >= 24 && dp_get32(buf + 20) == DPLAY_MAGIC))
                dp_gameMessage(buf, (int)got, a.sin_addr.s_addr);
            SDL_UnlockMutex(stdComm_DPlay_mutex);
        }
        else {
            dpConn* c = &stdComm_DPlay_aConn[map[k]];
            if (c->bOut) {
                if (c->bConnecting) {
                    int err = 0;
                    socklen_t el = sizeof(err);
                    if ((pf[k].revents & (POLLERR | POLLHUP)) || getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err) { dp_closeConn(c); continue; }
                    c->bConnecting = 0;
                }
                if (pf[k].revents & (POLLHUP | POLLERR)) { dp_closeConn(c); continue; }
                if (pf[k].revents & POLLIN) { // (nothing is meant to come this way: the peer closed it)
                    const ssize_t got = recv(c->fd, buf, sizeof(buf), 0);
                    if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { dp_closeConn(c); continue; }
                }
                dp_connFlush(c);
                continue;
            }
            const ssize_t got = recv(c->fd, c->pBuf + c->len, DPLAY_MAX_MSG - c->len, 0);
            if (got <= 0) {
                if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                const uint32_t ip = c->ip;
                dp_closeConn(c);
                dp_inClosed(ip);
                continue;
            }
            c->len += (size_t)got;
            c->lastMs = now;
            while (c->fd >= 0 && c->len >= 20) {
                const size_t size = dp_get32(c->pBuf) & 0xFFFFF;
                if (size < 20 || size > DPLAY_MAX_MSG) { // (out of step: what follows can't be read, so the connection is dropped)
                    const uint32_t ip = c->ip;
                    dp_closeConn(c);
                    dp_inClosed(ip);
                    break;
                }
                if (c->len < size) break;
                c->port = dp_addrPort(c->pBuf + 4);
                c->bGotOne = 1;
                SDL_LockMutex(stdComm_DPlay_mutex);
                dp_tcpMessage(c->pBuf, size, c->ip);
                SDL_UnlockMutex(stdComm_DPlay_mutex);
                memmove(c->pBuf, c->pBuf + size, c->len - size);
                c->len -= size;
            }
        }
    }
}

// (the network thread) Connections that said nothing, or nothing for long, closed; connects that
// never finished given up
static void dp_reapConns(uint32_t now)
{
    int numUnsent = 0;
    for (int i = 0; i < 2 * DPLAY_MAX_CONNS; i++) {
        dpConn* c = &stdComm_DPlay_aConn[i];
        if (c->fd < 0) continue;
        if (c->bOut) {
            if (c->bConnecting && now - c->lastMs > 3000) { dp_closeConn(c); continue; }
            numUnsent += c->len > 0;
            continue;
        }
        if ((!c->bGotOne && now - c->lastMs > DPLAY_IN_FIRST_MS) || now - c->lastMs > DPLAY_IN_IDLE_MS) {
            const uint32_t ip = c->ip;
            dp_closeConn(c);
            dp_inClosed(ip);
        }
    }
    stdComm_DPlay_numUnsent = numUnsent;
}

// (under the lock; a player) The host pinged, and given up on if it has said nothing for a while
static void dp_keepAlive(uint32_t now)
{
    if (!stdComm_DPlay_bActive || stdComm_DPlay_bServer || stdComm_DPlay_bHostLost) return;
    if (now - stdComm_DPlay_lastPingMs >= DPLAY_PING_EVERY_MS) {
        stdComm_DPlay_lastPingMs = now;
        uint8_t m[20 + 16];
        memset(m, 0, sizeof(m));
        dp_putMsgHeader(m + 20, DPCMD_PING);
        dp_put32(m + 28, stdComm_DPlay_mySysId);
        dp_put32(m + 32, now);
        dp_sendTcp(stdComm_DPlay_hostIp, stdComm_DPlay_hostTcpPort, m, sizeof(m));
    }
    const int bClosedQuiet = stdComm_DPlay_hostClosedMs && now - stdComm_DPlay_hostClosedMs > DPLAY_HOST_CLOSED_MS
                             && now - stdComm_DPlay_lastHostMs > DPLAY_HOST_CLOSED_MS;
    if (bClosedQuiet || now - stdComm_DPlay_lastHostMs > DPLAY_HOST_TIMEOUT_MS) {
        stdComm_DPlay_bHostLost = 1;
        char t[160];
        if (bClosedQuiet) snprintf(t, sizeof(t), "lost the host: its connection closed");
        else snprintf(t, sizeof(t), "lost the host: no answer for %d seconds", DPLAY_HOST_TIMEOUT_MS / 1000);
        dp_setProblem(t, 0);
    }
    // (it spoke well after: still there, on another connection)
    if (stdComm_DPlay_hostClosedMs && stdComm_DPlay_lastHostMs - stdComm_DPlay_hostClosedMs > 1000 && stdComm_DPlay_lastHostMs - stdComm_DPlay_hostClosedMs < 0x80000000u)
        stdComm_DPlay_hostClosedMs = 0;
}

static int dp_remapThread(void* p);

// (under the lock) The router's ports renewed now and then while a game goes on (on a thread of its
// own: it may take seconds)
static void dp_renew(uint32_t now)
{
    if (!stdComm_DPlay_bActive || stdComm_DPlay_bServer || !stdComm_DPlay_bMapped || now - stdComm_DPlay_mappedMs < DPLAY_RENEW_EVERY_MS) return;
    stdComm_DPlay_mappedMs = now;
    SDL_Thread* t = SDL_CreateThread(dp_remapThread, "stdComm_DPlay_renew", NULL);
    if (t) SDL_DetachThread(t);
}

static int stdComm_DPlay_Thread(void* unused)
{
    (void)unused;
    while (stdComm_DPlay_bThreadRun) {
        dp_sendQueued();
        dp_poll(5);
        const uint32_t now = stdPlatform_GetTimeMsec();
        dp_reapConns(now);
        SDL_LockMutex(stdComm_DPlay_mutex);
        dp_keepAlive(now);
        dp_renew(now);
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        dp_holdExpired(now);
    }
    // (the last of what was queued, sent: the goodbyes)
    dp_sendQueued();
    return 0;
}

// ------------------------------------------------------------------ starting and stopping

static void dp_closeSockets(void)
{
    for (int i = 0; i < 2 * DPLAY_MAX_CONNS; i++) if (stdComm_DPlay_aConn[i].fd >= 0) dp_closeConn(&stdComm_DPlay_aConn[i]);
    if (stdComm_DPlay_tcpListen >= 0) close(stdComm_DPlay_tcpListen);
    if (stdComm_DPlay_udp >= 0) close(stdComm_DPlay_udp);
    if (stdComm_DPlay_enumUdp >= 0) close(stdComm_DPlay_enumUdp);
    stdComm_DPlay_tcpListen = stdComm_DPlay_udp = stdComm_DPlay_enumUdp = -1;
}

// A user of the thread and sockets (a probe, a game): they are made for the first
static int dp_acquire(void)
{
    if (!stdComm_DPlay_bInitted) return 0;
    SDL_LockMutex(stdComm_DPlay_lifeMutex);
    int ok = 1;
    if (!stdComm_DPlay_numUsers) {
        stdComm_DPlay_tcpListen = dp_bindRange(SOCK_STREAM, DPLAY_TCP_FIRST, DPLAY_TCP_LAST, &stdComm_DPlay_tcpPort);
        stdComm_DPlay_udp = dp_bindRange(SOCK_DGRAM, DPLAY_UDP_FIRST, DPLAY_UDP_LAST, &stdComm_DPlay_udpPort);
        if (stdComm_DPlay_tcpListen < 0 || stdComm_DPlay_udp < 0) {
            stdComm_DPlay_infoPrintf("DPlay: no free ports (TCP %d-%d, UDP %d-%d)\n", DPLAY_TCP_FIRST, DPLAY_TCP_LAST, DPLAY_UDP_FIRST, DPLAY_UDP_LAST);
            dp_closeSockets();
            ok = 0;
        }
        else {
            stdComm_DPlay_bThreadRun = 1;
            stdComm_DPlay_thread = SDL_CreateThread(stdComm_DPlay_Thread, "stdComm_DPlay", NULL);
            if (!stdComm_DPlay_thread) { stdComm_DPlay_bThreadRun = 0; dp_closeSockets(); ok = 0; }
        }
    }
    if (ok) stdComm_DPlay_numUsers++;
    SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
    return ok;
}

// That user done: the last one stops the thread and closes the sockets (the queued sent first)
static void dp_release(void)
{
    SDL_LockMutex(stdComm_DPlay_lifeMutex);
    if (stdComm_DPlay_numUsers > 0 && --stdComm_DPlay_numUsers == 0) {
        stdComm_DPlay_bThreadRun = 0;
        if (stdComm_DPlay_thread) {
            int ret;
            SDL_WaitThread(stdComm_DPlay_thread, &ret);
            stdComm_DPlay_thread = NULL;
        }
        dp_closeSockets();
        SDL_LockMutex(stdComm_DPlay_mutex);
        dp_outClear();
        SDL_UnlockMutex(stdComm_DPlay_mutex);
    }
    SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
}

// A game found: the thread and sockets kept until it is joined (or DPLAY_HOLD_MS after the last
// probe that found one), as they are -- the host keeps the connection it answered on, to our listening port,
// and answers the join over it; a new listening socket, even on the same port, wouldn't get that.
// Under lifeMutex.
#define DPLAY_HOLD_MS 60000
static int stdComm_DPlay_bHold = 0;
static uint32_t stdComm_DPlay_holdMs = 0;

static void dp_hold(void)
{
    SDL_LockMutex(stdComm_DPlay_lifeMutex);
    const int bHad = stdComm_DPlay_bHold;
    stdComm_DPlay_bHold = 1;
    stdComm_DPlay_holdMs = stdPlatform_GetTimeMsec();
    SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
    if (!bHad && !dp_acquire()) {
        SDL_LockMutex(stdComm_DPlay_lifeMutex);
        stdComm_DPlay_bHold = 0;
        SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
    }
}

// The hold let go (joined, or given up waiting): 1 if there was one
static int dp_unhold(void)
{
    SDL_LockMutex(stdComm_DPlay_lifeMutex);
    const int bHad = stdComm_DPlay_bHold;
    stdComm_DPlay_bHold = 0;
    SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
    if (bHad) dp_release();
    return bHad;
}

static int dp_unholdThread(void* p) { (void)p; dp_unhold(); return 0; }

// (the network thread) A hold waited out: let go from a thread of its own (the network thread can't
// wait for itself to end)
static void dp_holdExpired(uint32_t now)
{
    static uint32_t lastToldMs = 0;
    if (!stdComm_DPlay_bHold || now - stdComm_DPlay_holdMs < DPLAY_HOLD_MS || stdComm_DPlay_bActive || now - lastToldMs < 5000) return;
    lastToldMs = now;
    SDL_Thread* t = SDL_CreateThread(dp_unholdThread, "stdComm_DPlay_unhold", NULL);
    if (t) SDL_DetachThread(t);
}

// ------------------------------------------------------------------ the router's ports

static void dp_unmap(void)
{
    SDL_LockMutex(stdComm_DPlay_natMutex);
    if (stdComm_DPlay_bMapped) {
        stdComm_DPlayNat_Unmap();
        stdComm_DPlay_bMapped = 0;
    }
    stdComm_DPlay_advTcpPort = stdComm_DPlay_advUdpPort = 0;
    SDL_UnlockMutex(stdComm_DPlay_natMutex);
}

// Our ports (as bound now) opened on the router, if joining that host needs it: a host on the
// internet, and us behind a router (OPENJKDF2_DPLAY_NAT=force: whatever the addresses, for testing;
// =off: never). Called off the network thread, the main lock not held. 0 if they had to be and couldn't.
static int dp_mapFor(uint32_t hostIp, uint32_t gen)
{
    const char* pEnv = getenv("OPENJKDF2_DPLAY_NAT");
    if (pEnv && !strcmp(pEnv, "off")) return 1;
    const int bForce = pEnv && !strcmp(pEnv, "force");
    const uint32_t local = stdComm_DPlayNat_LocalAddressToward(hostIp);
    if (!bForce && (stdComm_DPlayNat_IsPrivate(hostIp) || !local || !stdComm_DPlayNat_IsPrivate(local))) return 1;
    SDL_LockMutex(stdComm_DPlay_natMutex);
    stdCommDPlayNatMapping mp;
    const int ok = stdComm_DPlayNat_Map(stdComm_DPlay_tcpPort, stdComm_DPlay_udpPort, &mp);
    if (ok) {
        stdComm_DPlay_bMapped = 1;
        stdComm_DPlay_mappedMs = stdPlatform_GetTimeMsec();
        stdComm_DPlay_advTcpPort = mp.extTcpPort;
        stdComm_DPlay_advUdpPort = mp.extUdpPort;
    }
    SDL_UnlockMutex(stdComm_DPlay_natMutex);
    char t[384];
    if (ok) {
        char ip[32] = "?";
        if (mp.extIp) inet_ntop(AF_INET, &mp.extIp, ip, sizeof(ip));
        stdComm_DPlay_infoPrintf("DPlay: the router (%s) forwards TCP %d and UDP %d to us (its address %s)\n", mp.pMethod, mp.extTcpPort, mp.extUdpPort, ip);
        if (stdComm_DPlayNat_Problem()[0]) {
            snprintf(t, sizeof(t), "warning: %s; the host may not reach this device", stdComm_DPlayNat_Problem());
            SDL_LockMutex(stdComm_DPlay_mutex);
            dp_setProblem(t, gen);
            SDL_UnlockMutex(stdComm_DPlay_mutex);
        }
        return 1;
    }
    snprintf(t, sizeof(t), "Couldn't open this device's ports on the router (%s). On mobile data this game can't be joined; "
             "on Wi-Fi, turn on UPnP or NAT-PMP on the router, or forward TCP %d and UDP %d to this device.",
             stdComm_DPlayNat_Problem(), stdComm_DPlay_tcpPort, stdComm_DPlay_udpPort);
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_setProblem(t, gen);
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    return 0;
}

void stdComm_DPlay_WillJoin(const char* pAddress)
{
    if (!stdComm_DPlay_bInitted) return;
    SDL_LockMutex(stdComm_DPlay_mutex);
    snprintf(stdComm_DPlay_intentHost, sizeof(stdComm_DPlay_intentHost), "%s", pAddress ? pAddress : "");
    stdComm_DPlay_intentMs = stdPlatform_GetTimeMsec();
    SDL_UnlockMutex(stdComm_DPlay_mutex);
}

// The app going to the background: the router's ports given back (asked again when it comes back,
// if a game is still on)
static void* dp_bgThreadArg = NULL;
static int dp_unmapThread(void* p) { (void)p; dp_unmap(); return 0; }
static int dp_remapThread(void* p)
{
    (void)p;
    // (the router's ports opened again for the game going on, or renewed)
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int bActive = stdComm_DPlay_bActive && !stdComm_DPlay_bServer;
    const uint32_t hostIp = stdComm_DPlay_hostIp;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (bActive) dp_mapFor(hostIp, 0);
    return 0;
}
static bool dp_appEvent(void* userdata, SDL_Event* e)
{
    (void)userdata;
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND && stdComm_DPlay_bMapped) {
        SDL_Thread* t = SDL_CreateThread(dp_unmapThread, "stdComm_DPlay_unmap", dp_bgThreadArg);
        if (t) SDL_DetachThread(t);
    }
    else if (e->type == SDL_EVENT_DID_ENTER_FOREGROUND && stdComm_DPlay_bActive && !stdComm_DPlay_bServer) {
        SDL_Thread* t = SDL_CreateThread(dp_remapThread, "stdComm_DPlay_remap", dp_bgThreadArg);
        if (t) SDL_DetachThread(t);
    }
    return true;
}

// ------------------------------------------------------------------ the transport

static void stdComm_DPlay_ResetClients(void)
{
    if (!stdComm_DPlay_bInitted) return;
    SDL_LockMutex(stdComm_DPlay_mutex);
    if (stdComm_DPlay_bActive) {
        DirectPlay_numPlayers = 0;
        for (int i = 0; i < DPLAY_MAX_PLAYERS && DirectPlay_numPlayers < 32; i++) {
            const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (!pl->bUsed || (pl->flags & DPLAYI_PLAYER_SYSPLAYER)) continue;
            memset(&DirectPlay_aPlayers[DirectPlay_numPlayers], 0, sizeof(DirectPlay_aPlayers[0]));
            DirectPlay_aPlayers[DirectPlay_numPlayers].dpId = pl->id;
            memcpy(DirectPlay_aPlayers[DirectPlay_numPlayers].waName, pl->name, sizeof(pl->name));
            DirectPlay_numPlayers++;
        }
        stdComm_dplayIdSelf = stdComm_DPlay_myId;
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
}

static int stdComm_DPlay_Receive(int* pIdOut, int* pMsgIdOut, int* pLenOut)
{
    const int maxLen = *pLenOut;
    *pIdOut = 0;
    *pLenOut = 0;
    if (!stdComm_DPlay_bInitted) return -1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    dpEvent* e = dp_queuePop();
    const int bHostLost = !e && stdComm_DPlay_bHostLost && !stdComm_DPlay_bHostLostTold;
    if (bHostLost) stdComm_DPlay_bHostLostTold = 1;
    const uint32_t hostId = stdComm_DPlay_hostId;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (bHostLost) {
        stdComm_DPlay_infoPrintf("DPlay: the game's host is gone\n");
        *pIdOut = (int)hostId;
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
        memcpy(pMsgIdOut, e->data, (size_t)n);
        *pLenOut = n;
    }
    free(e);
    return ret;
}

static BOOL stdComm_DPlay_Send(DPID idFrom, DPID idTo, void* lpData, DWORD dwDataSize)
{
    (void)idFrom;
    if (!stdComm_DPlay_bInitted || dwDataSize > DPLAY_MAX_MSG - 8) return 0;
    static uint8_t m[DPLAY_MAX_MSG];
    int bSent = 0;
    SDL_LockMutex(stdComm_DPlay_mutex);
    if (stdComm_DPlay_bActive) {
        dp_put32(m, stdComm_DPlay_myId);
        dp_put32(m + 4, (uint32_t)idTo);
        memcpy(m + 8, lpData, dwDataSize);
        if (idTo == 0) {
            // (everyone: once to each other system player, "to 0", as DirectPlay sends it)
            for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
                dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
                if (!pl->bUsed || !(pl->flags & DPLAYI_PLAYER_SYSPLAYER) || pl->id == stdComm_DPlay_mySysId) continue;
                dp_sendToSys(pl, m, (int)dwDataSize + 8);
                bSent = 1;
            }
        }
        else if ((uint32_t)idTo != stdComm_DPlay_myId) {
            const dpPlayer* pl = dp_findPlayer((uint32_t)idTo);
            dpPlayer* sp = pl ? dp_findPlayer(pl->sysId) : NULL;
            if (sp && sp->id != stdComm_DPlay_mySysId) {
                dp_sendToSys(sp, m, (int)dwDataSize + 8);
                bSent = 1;
            }
        }
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
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
    dp_sendTcp(stdComm_DPlay_hostIp, stdComm_DPlay_hostTcpPort, m, sizeof(m));
    SDL_UnlockMutex(stdComm_DPlay_mutex);
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
    dp_put32(p + 20, 32);
    dp_put32(p + 32, sysId);
    dp_put32(p + 36, 48);
    dp_put32(p + 40, DPLAY_VERSION);
    int o = 48;
    if (snLen) { memcpy(p + o, names, snLen); o += snLen; }
    dp_putAddr(p + o, dp_myTcpPort(), 0);
    dp_putAddr(p + o + 16, dp_myUdpPort(), 0);
    return o + 32;
}

static void dp_resetSession(void)
{
    memset(stdComm_DPlay_aPlayers, 0, sizeof(stdComm_DPlay_aPlayers));
    stdComm_DPlay_mySysId = stdComm_DPlay_myId = 0;
    stdComm_DPlay_hostId = stdComm_DPlay_hostSysId = 0;
    stdComm_DPlay_bHostLost = stdComm_DPlay_bHostLostTold = 0;
    stdComm_DPlay_hostClosedMs = 0;
    stdComm_DPlay_bSuperEnum = 0;
    stdComm_DPlay_nackResult = 0;
    stdComm_DPlay_maxSysPlayers = 32;
    dp_queueClear();
}

static void dp_joinFailed(const char* pWhy)
{
    SDL_LockMutex(stdComm_DPlay_mutex);
    if (pWhy) dp_setProblem(pWhy, 0);
    dp_resetSession();
    stdComm_DPlay_bActive = 0;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    dp_unmap();
    dp_release();
}

// Joining the game picked (stdComm_DPlay_SelectSession, or the probe's first answer)
static int stdComm_DPlay_Join(char16_t* pwPassword)
{
    if (!stdComm_DPlay_bInitted) return 0x88770154 + 1;
    SDL_LockMutex(stdComm_DPlay_mutex);
    const uint32_t hostIp = stdComm_DPlay_hostIp;
    const uint16_t hostPort = stdComm_DPlay_hostTcpPort;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (!hostIp || !hostPort) {
        SDL_LockMutex(stdComm_DPlay_mutex);
        dp_setProblem("that game isn't listed any more: look for it again", 0);
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        return 0x88770154 + 1;
    }
    if (!dp_acquire()) return 0x88770154 + 1;
    dp_unhold(); // (the probe's sockets, kept for this join: ours now)
    char hostText[32];
    inet_ntop(AF_INET, &hostIp, hostText, sizeof(hostText));
    stdComm_DPlay_infoPrintf("DPlay: joining the game at %s (TCP %d)\n", hostText, hostPort);
    // (the router's ports, if the host needs them to reach us: opened for the probe already when
    // this game was picked (stdComm_DPlay_WillJoin); again for these sockets otherwise)
    if (!dp_mapFor(hostIp, 0)) { dp_joinFailed(NULL); return 0x88770154 + 1; }

    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_resetSession();
    stdComm_DPlay_bServer = 0;
    stdComm_DPlay_bActive = 0;
    dp_setMaxPlayers(stdComm_DPlay_dpsd);
    stdComm_DPlay_lastHostMs = stdPlatform_GetTimeMsec();
    SDL_UnlockMutex(stdComm_DPlay_mutex);

    // our system player
    const uint32_t sysId = dp_requestId(0x9);
    if (!sysId) { dp_joinFailed("the host didn't answer the join (it may not reach this device's ports)"); return 0x88770154 + 1; }
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_mySysId = sysId;
    SDL_UnlockMutex(stdComm_DPlay_mutex);

    // ...told to the host, with the password: every player in answer
    static uint8_t m[1024];
    memset(m, 0, sizeof(m));
    int o = 20 + dp_putMsgHeader(m + 20, DPCMD_ADDFORWARDREQUEST);
    dp_put32(m + o + 4, sysId);  // player
    dp_put32(m + o + 12, 28);    // create offset
    o += 20;
    o += dp_putPackedPlayer(m + o, 0x9, sysId, NULL, sysId);
    dp_put32(m + 20 + 24, (uint32_t)(o - 20));  // password offset
    o += dp_putWstr(m + o, pwPassword, 31);
    SDL_LockMutex(stdComm_DPlay_mutex);
    memcpy(m + o, stdComm_DPlay_dpsd + 0x38, 4); // (the session's dwReserved1, as DirectPlay sends it)
    o += 4;
    dp_sendTcp(hostIp, hostPort, m, o);
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    for (int i = 0; i < 500 && !stdComm_DPlay_bSuperEnum && !stdComm_DPlay_nackResult; i++) SDL_Delay(10);
    if (!stdComm_DPlay_bSuperEnum) {
        const uint32_t nack = stdComm_DPlay_nackResult;
        dp_joinFailed(nack ? "the host refused: wrong password?" : "the host didn't answer the join");
        return nack ? 0x88770154 : 0x88770154 + 1;
    }

    // our player, made and told to each other system player
    const uint32_t id = dp_requestId(0x8);
    if (!id) { dp_joinFailed("the host gave no player id"); return 0x88770154 + 1; }
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_myId = id;
    dpPlayer* pSys = dp_addPlayer(sysId, 1);
    if (pSys) pSys->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_LOCAL;
    dpPlayer* pMe = dp_addPlayer(id, 0);
    if (pMe) {
        pMe->flags = DPLAYI_PLAYER_LOCAL;
        pMe->sysId = sysId;
        memcpy(pMe->name, jkPlayer_playerShortName, sizeof(pMe->name));
        pMe->name[31] = 0;
    }
    memset(m, 0, 20 + 28);
    o = 20 + dp_putMsgHeader(m + 20, DPCMD_CREATEPLAYER);
    dp_put32(m + o + 4, id);
    dp_put32(m + o + 12, 28);
    o += 20;
    o += dp_putPackedPlayer(m + o, 0x8, id, jkPlayer_playerShortName, sysId);
    memset(m + o, 0, 6); // (two reserved fields)
    o += 6;
    for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
        const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
        if (pl->bUsed && (pl->flags & DPLAYI_PLAYER_SYSPLAYER) && pl->id != sysId && pl->tcpPort)
            dp_sendTcp(pl->ip, pl->tcpPort, m, o);
    }
    stdComm_DPlay_lastHostMs = stdComm_DPlay_lastPingMs = stdPlatform_GetTimeMsec();
    stdComm_DPlay_bActive = 1;
    const uint32_t hostPlayer = stdComm_DPlay_hostId;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_infoPrintf("DPlay: joined as %x (system player %x), the host's player %x\n", id, sysId, hostPlayer);
    stdComm_DPlay_ResetClients();
    return 0;
}

static void stdComm_DPlay_Close(void)
{
    if (!stdComm_DPlay_bInitted) return;
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int bWasActive = stdComm_DPlay_bActive;
    // (a player: everyone told we're leaving, as DirectPlay does: DELETEPLAYER for our player and our
    // system player, to each other system player)
    if (bWasActive && !stdComm_DPlay_bServer) {
        const uint32_t ids[2] = { stdComm_DPlay_myId, stdComm_DPlay_mySysId };
        for (int i = 0; i < DPLAY_MAX_PLAYERS; i++) {
            const dpPlayer* pl = &stdComm_DPlay_aPlayers[i];
            if (!pl->bUsed || !(pl->flags & DPLAYI_PLAYER_SYSPLAYER) || pl->id == stdComm_DPlay_mySysId || !pl->tcpPort) continue;
            if (pl->ip == stdComm_DPlay_hostIp && stdComm_DPlay_bHostLost) continue; // (no one there)
            for (int k = 0; k < 2; k++) {
                uint8_t m[20 + 28];
                memset(m, 0, sizeof(m));
                dp_putMsgHeader(m + 20, DPCMD_DELETEPLAYER);
                dp_put32(m + 32, ids[k]);
                dp_sendTcp(pl->ip, pl->tcpPort, m, sizeof(m));
            }
        }
    }
    stdComm_DPlay_bActive = 0;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (!bWasActive) return;
    // (a short while for the goodbyes to go: never longer, whoever is or isn't there)
    for (int i = 0; i < 30; i++) {
        SDL_LockMutex(stdComm_DPlay_mutex);
        const int bQueued = stdComm_DPlay_pOutHead != NULL;
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        if (!bQueued && !stdComm_DPlay_numUnsent) break;
        SDL_Delay(10);
    }
    SDL_LockMutex(stdComm_DPlay_mutex);
    stdComm_DPlay_bServer = 0;
    dp_resetSession();
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    dp_unmap(); // (the router's ports given back: the game is over)
    dp_release();
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

// Hosting (for testing): answering on UDP 47624, our system player and player made
static int stdComm_DPlay_OpenHost(jkMultiEntry* pEntry)
{
    stdComm_bIsServer = 1;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
    if (!dp_acquire()) return 0x88770118 + 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    if (fd >= 0) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(DPLAY_ENUM_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd < 0 || bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0) {
        if (fd >= 0) close(fd);
        stdComm_DPlay_infoPrintf("DPlay: can't listen on UDP %d\n", DPLAY_ENUM_PORT);
        dp_release();
        return 0x88770118 + 1;
    }
    dp_nonblock(fd);
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_resetSession();
    stdComm_DPlay_enumUdp = fd; // (polled from the thread's next round)
    stdComm_DPlay_entry = *pEntry;
    if (!stdComm_DPlay_entry.guidInstance.a) stdComm_DPlay_entry.guidInstance.a = (uint32_t)stdPlatform_GetTimeMsec() ^ 0x4A4B4446;
    memset(stdComm_DPlay_password, 0, sizeof(stdComm_DPlay_password));
    for (int i = 0; i < 32 && pEntry->wPassword[i]; i++) stdComm_DPlay_password[i] = pEntry->wPassword[i];
    stdComm_DPlay_maxSysPlayers = pEntry->maxPlayers < 2 ? 2 : (pEntry->maxPlayers > 32 ? 32 : pEntry->maxPlayers);
    stdComm_DPlay_nextId = 0x00100000u | ((stdPlatform_GetTimeMsec() & 0xFFF) << 8) | 1u;
    stdComm_DPlay_reserved1 = stdPlatform_GetTimeMsec() ^ 0x5A5A5A5Au;
    stdComm_DPlay_mySysId = stdComm_DPlay_nextId++;
    stdComm_DPlay_myId = stdComm_DPlay_nextId++;
    stdComm_DPlay_hostId = stdComm_DPlay_myId;
    dpPlayer* pSys = dp_addPlayer(stdComm_DPlay_mySysId, 1);
    if (pSys) {
        pSys->flags = DPLAYI_PLAYER_SYSPLAYER | DPLAYI_PLAYER_NAMESRVR | DPLAYI_PLAYER_INGROUP | DPLAYI_PLAYER_LOCAL;
        pSys->tcpPort = stdComm_DPlay_tcpPort;
        pSys->udpPort = stdComm_DPlay_udpPort;
    }
    dpPlayer* pMe = dp_addPlayer(stdComm_DPlay_myId, 0);
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

// The address typed (a name or IPv4, with no port or DirectPlay's 47624: another port is another
// transport's): its IPv4 addresses, up to max; how many (0: not an address for DirectPlay)
static int dp_resolve(const char* pAddress, uint32_t* aIpOut, int max)
{
    char host[256];
    int port = DPLAY_ENUM_PORT;
    if (!stdCommTransport_SplitAddress(pAddress, host, sizeof(host), &port, DPLAY_ENUM_PORT)) return 0;
    if (port != DPLAY_ENUM_PORT) return 0;
    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) == 1) { aIpOut[0] = a.s_addr; return 1; }
    struct addrinfo hints, *pRes = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &pRes) != 0 || !pRes) return 0;
    int n = 0;
    for (struct addrinfo* r = pRes; r && n < max; r = r->ai_next) {
        const uint32_t ip = ((struct sockaddr_in*)r->ai_addr)->sin_addr.s_addr;
        int dup = 0;
        for (int i = 0; i < n; i++) dup |= aIpOut[i] == ip;
        if (!dup) aIpOut[n++] = ip;
    }
    freeaddrinfo(pRes);
    return n;
}

static int dp_sessionIndex(const GUID_idk* pGuid, uint32_t gen)
{
    for (int i = 0; i < DPLAY_MAX_SESSIONS; i++)
        if (stdComm_DPlay_aSessions[i].bUsed && (!gen || stdComm_DPlay_aSessions[i].probeGen == gen)
            && !memcmp(stdComm_DPlay_aSessions[i].dpsd + 8, pGuid, 16)) return i;
    return -1;
}

// (under the lock) That session's host kept for joining it
static void dp_useSession(const dpSession* se)
{
    memcpy(stdComm_DPlay_dpsd, se->dpsd, 80);
    stdComm_DPlay_hostIp = se->ip;
    stdComm_DPlay_hostTcpPort = se->tcpPort;
}

// Is there a DirectPlay game at that address (a name, maybe of several addresses, or an IPv4)? An
// ENUMSESSIONS to its UDP 47624; the answers (one per session) come to our TCP port. The first in
// pOut; all of them: stdComm_DPlay_GetSessions
static int stdComm_DPlay_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    uint32_t aIp[4];
    const int numIp = dp_resolve(pAddress, aIp, 4);
    if (!numIp) return -1;
    if (!stdCommTransport_GateEnter(&stdComm_DPlay_probeGate)) return 0;
    if (!dp_acquire()) { stdCommTransport_GateLeave(&stdComm_DPlay_probeGate); return 0; }
    // (a game being looked for, not just listed: the router's ports opened first, as the answer
    // comes back over them)
    SDL_LockMutex(stdComm_DPlay_mutex);
    const uint32_t gen = ++stdComm_DPlay_probeGen;
    if (!stdComm_DPlay_bActive) stdComm_DPlay_ClearProblem(); // (what was said of an earlier game or asking: old)
    const int bIntent = stdComm_DPlay_intentHost[0] && !strcmp(stdComm_DPlay_intentHost, pAddress) && stdPlatform_GetTimeMsec() - stdComm_DPlay_intentMs < DPLAY_JOIN_INTENT_MS;
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (bIntent) dp_mapFor(aIp[0], gen);

    SDL_LockMutex(stdComm_DPlay_mutex);
    const uint32_t start = stdPlatform_GetTimeMsec();
    dp_askSet(aIp, numIp, gen);
    SDL_UnlockMutex(stdComm_DPlay_mutex);

    uint8_t m[20 + 32];
    uint32_t lastSent = 0, firstAnswerMs = 0;
    int bSentOnce = 0, r = 0;
    while (stdPlatform_GetTimeMsec() - start < (uint32_t)timeoutMs && (!pbKeepGoing || *pbKeepGoing) && !stdComm_DPlay_probeGate.bClosing) {
        const uint32_t now = stdPlatform_GetTimeMsec();
        SDL_LockMutex(stdComm_DPlay_mutex);
        if (!bSentOnce || now - lastSent > 500) {
            for (int k = 0; k < numIp; k++) {
                memset(m, 0, sizeof(m));
                dp_putSpHeader(m, sizeof(m));
                dp_putMsgHeader(m + 20, DPCMD_ENUMSESSIONS);
                memcpy(m + 28, stdComm_DPlay_appGuid, 16);
                dp_put32(m + 48, 0x52); // (as the original asks: all, password-protected too)
                dp_sendUdp(aIp[k], DPLAY_ENUM_PORT, m, sizeof(m));
            }
            lastSent = now;
            bSentOnce = 1;
        }
        int n = 0;
        for (int i = 0; i < DPLAY_MAX_SESSIONS; i++) n += stdComm_DPlay_aSessions[i].bUsed && stdComm_DPlay_aSessions[i].probeGen == gen;
        SDL_UnlockMutex(stdComm_DPlay_mutex);
        // (the first answer: a little longer, for the others)
        if (n && !firstAnswerMs) firstAnswerMs = now;
        if (firstAnswerMs && now - firstAnswerMs > 600) break;
        SDL_Delay(20);
    }
    SDL_LockMutex(stdComm_DPlay_mutex);
    dp_askClear(gen);
    // (what was found earlier at these addresses and not now, forgotten)
    const dpSession* pFirst = NULL;
    int numFound = 0;
    for (int i = 0; i < DPLAY_MAX_SESSIONS; i++) {
        dpSession* se = &stdComm_DPlay_aSessions[i];
        if (!se->bUsed) continue;
        int bOurs = 0;
        for (int k = 0; k < numIp; k++) bOurs |= se->ip == aIp[k];
        if (!bOurs) continue;
        if (se->probeGen != gen) { se->bUsed = 0; continue; }
        numFound++;
        if (!pFirst) pFirst = se;
    }
    if (pFirst) {
        dp_parseSessionDesc(pFirst->dpsd, pFirst->name, pOut);
        pOut->field_E0 = 10;
        dp_useSession(pFirst);
        stdComm_DPlay_lastGoodGen = gen;
        r = 1;
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (r) dp_hold(); // (for joining it: the sockets the host answered kept a while)
    dp_release();
    stdCommTransport_GateLeave(&stdComm_DPlay_probeGate);
    if (r) stdComm_DPlay_verbosePrintf("DPlay: %d game(s) at %s\n", numFound, pAddress);
    // (ports opened for a game picked that wasn't there, or long ago and never joined: given back)
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int bStale = !stdComm_DPlay_bActive && ((bIntent && !r) || (!bIntent && stdPlatform_GetTimeMsec() - stdComm_DPlay_mappedMs > DPLAY_UNMAP_STALE_MS));
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    if (bStale && stdComm_DPlay_bMapped) dp_unmap();
    return r;
}

int stdComm_DPlay_GetSessions(const GUID_idk* pOne, jkMultiEntry* aOut, int max)
{
    if (!stdComm_DPlay_bInitted) return 0;
    SDL_LockMutex(stdComm_DPlay_mutex);
    int n = 0;
    const int k = pOne ? dp_sessionIndex(pOne, 0) : -1;
    if (k >= 0) {
        const uint32_t gen = stdComm_DPlay_aSessions[k].probeGen;
        for (int i = 0; i < DPLAY_MAX_SESSIONS && n < max; i++) {
            const dpSession* se = &stdComm_DPlay_aSessions[i];
            if (!se->bUsed || se->probeGen != gen) continue;
            dp_parseSessionDesc(se->dpsd, se->name, &aOut[n]);
            aOut[n].field_E0 = 10;
            n++;
        }
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    return n;
}

int stdComm_DPlay_SelectSession(const GUID_idk* pGuid)
{
    if (!stdComm_DPlay_bInitted) return 0;
    SDL_LockMutex(stdComm_DPlay_mutex);
    const int k = dp_sessionIndex(pGuid, 0);
    if (k >= 0) dp_useSession(&stdComm_DPlay_aSessions[k]);
    else {
        stdComm_DPlay_hostIp = 0;
        stdComm_DPlay_hostTcpPort = 0;
    }
    SDL_UnlockMutex(stdComm_DPlay_mutex);
    return k >= 0;
}

void stdComm_DPlay_ClearProblem(void);

const char* stdComm_DPlay_Problem(void)
{
    // (only a problem of the last asking, or of the game now)
    if (stdComm_DPlay_problemGen && stdComm_DPlay_problemGen != stdComm_DPlay_probeGen) return "";
    return stdComm_DPlay_problem;
}

void stdComm_DPlay_ClearProblem(void)
{
    stdComm_DPlay_problem[0] = 0;
    stdComm_DPlay_problemGen = 0;
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
    stdComm_DPlay_lifeMutex = SDL_CreateMutex();
    stdComm_DPlay_natMutex = SDL_CreateMutex();
    if (!stdComm_DPlay_mutex || !stdComm_DPlay_lifeMutex || !stdComm_DPlay_natMutex) return;
    for (int i = 0; i < 2 * DPLAY_MAX_CONNS; i++) {
        memset(&stdComm_DPlay_aConn[i], 0, sizeof(dpConn));
        stdComm_DPlay_aConn[i].fd = -1;
    }
    stdComm_DPlay_bInitted = 1;
    stdCommTransport_GateOpen(&stdComm_DPlay_probeGate);
    SDL_AddEventWatch(dp_appEvent, NULL);
}

void stdComm_DPlay_Shutdown(void)
{
    if (!stdComm_DPlay_bInitted) return;
    SDL_RemoveEventWatch(dp_appEvent, NULL);
    stdCommTransport_GateClose(&stdComm_DPlay_probeGate, 5000);
    stdComm_DPlay_Close();
    dp_unhold();
    dp_unmap();
    // (anything still holding the thread: let go)
    SDL_LockMutex(stdComm_DPlay_lifeMutex);
    const int n = stdComm_DPlay_numUsers;
    SDL_UnlockMutex(stdComm_DPlay_lifeMutex);
    for (int i = 0; i < n; i++) dp_release();
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

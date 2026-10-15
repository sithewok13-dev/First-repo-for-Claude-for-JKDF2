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
#include "stdComm_ENetCrypto.h"
#include "stdComm_ENetDiscover.h"
#include "Platform/Networking/stdCommTransport.h"

#ifdef PLATFORM_ENET

#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"
#include "Main/Main.h"
#include "Main/jkCoopSave.h"
#include "Gui/jkGUITitle.h"
#include "Gui/jkGUINetHost.h"

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

static void stdComm_ENet_ResetClients(void);

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

// The game's password: a game with one is encrypted (stdComm_ENetCrypto.h), and only a player who
// knows it gets in. A player who connects gets the game's description (with no player id: as finding
// games does) and a challenge (the host's salt and a random number for this connection); the player
// answers with a random number of its own and a message sealed with the key both make from the
// password and those numbers. The right one: the player's id comes sealed, and everything from then
// on, both ways, is sealed with that connection's key (the host opens what it passes on and seals it
// again for the player it is for). The wrong one, or none within STDCOMM_ENET_CHALLENGE_MS: let go
// (REASON_PASSWORD). The password is never sent; the description sent out never has it in it.
#define STDCOMM_ENET_REASON_PASSWORD 0x50415353u // ("PASS")
#define STDCOMM_ENET_MAGIC_CHALLENGE 0x434B4A4Fu // ("OJKC")
#define STDCOMM_ENET_MAGIC_HELLO 0x484B4A4Fu     // ("OJKH")
#define STDCOMM_ENET_PEERS_PER_ADDRESS 8
#define STDCOMM_ENET_CHALLENGE_MS 6000 // (a little more than a player waits for its id)
#define STDCOMM_ENET_HELLO_TEXT "OJK-HELLO"
#define STDCOMM_ENET_HELLO_TEXT_LEN 9
#define STDCOMM_ENET_CHALLENGE_LEN (4 + STDCOMM_ENETCRYPTO_SALT_LEN + STDCOMM_ENETCRYPTO_NONCE_LEN)
#define STDCOMM_ENET_HELLO_LEN (4 + STDCOMM_ENETCRYPTO_NONCE_LEN + STDCOMM_ENET_HELLO_TEXT_LEN + STDCOMM_ENETCRYPTO_OVERHEAD)

// The host's: its password's key (and its salt), and each player's connection (by id - 1)
typedef struct stdCommENetPeerCrypto
{
    uint8_t hostNonce[STDCOMM_ENETCRYPTO_NONCE_LEN];
    uint32_t sinceMs; // (when it connected: not yet answered, let go after STDCOMM_ENET_CHALLENGE_MS)
    stdCommENetCryptoSession session;
} stdCommENetPeerCrypto;
static int stdComm_ENet_bHostCrypto = 0;
static uint8_t stdComm_ENet_hostSalt[STDCOMM_ENETCRYPTO_SALT_LEN];
static uint8_t stdComm_ENet_hostKey[STDCOMM_ENETCRYPTO_KEY_LEN];
static stdCommENetPeerCrypto stdComm_ENet_aPeerCrypto[64];
static uint64_t stdComm_ENet_limitedIds = 0;       // (the host's: connected, the password not yet shown)

// A player's: the password typed, and its connection to the host
static uint16_t stdComm_ENet_joinPassword[33];
static stdCommENetCryptoSession stdComm_ENet_clientCrypto;
static int stdComm_ENet_bWrongPassword = 0;        // (the host said so)

static stdCommENetEvent* stdComm_ENet_pQueueHead = NULL;
static stdCommENetEvent* stdComm_ENet_pQueueTail = NULL;

static jkMultiEntry stdComm_ENet_storedEntry;      // the host's session

static uint8_t stdComm_ENet_sendBuffer[STDCOMM_ENET_MAX_MSG];
static uint8_t stdComm_ENet_sealBuffer[STDCOMM_ENET_MAX_MSG + STDCOMM_ENETCRYPTO_OVERHEAD];
static uint8_t stdComm_ENet_openBuffer[STDCOMM_ENET_MAX_MSG];

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

// As it is (the description, the challenge, the answer)
static void stdComm_ENet_SendPlain(ENetPeer* pPeer, const void* pData, uint32_t len, int channel)
{
    ENetPacket* pkt = enet_packet_create(pData, len, channel == 0 ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!pkt) return;
    if (enet_peer_send(pPeer, (enet_uint8)channel, pkt) < 0) enet_packet_destroy(pkt);
}

// The connection's session, if it is encrypted (the host's with that player, or a player's with the
// host)
static stdCommENetCryptoSession* stdComm_ENet_SessionFor(ENetPeer* pPeer)
{
    if (!stdComm_ENet_bServer) return stdComm_ENet_clientCrypto.bOn ? &stdComm_ENet_clientCrypto : NULL;
    const int id = stdComm_ENet_PeerId(pPeer);
    if (!stdComm_ENet_bHostCrypto || id <= 0 || id > 64) return NULL;
    stdCommENetCryptoSession* pSession = &stdComm_ENet_aPeerCrypto[id - 1].session;
    return pSession->bOn ? pSession : NULL;
}

// A message to that connection: sealed if it is encrypted; never sent plain to one that should be
static void stdComm_ENet_SendRaw(ENetPeer* pPeer, const void* pData, uint32_t len, int channel)
{
    if (stdComm_ENet_bServer ? stdComm_ENet_bHostCrypto : stdComm_ENet_clientCrypto.bOn) {
        stdCommENetCryptoSession* pSession = stdComm_ENet_SessionFor(pPeer);
        if (!pSession) return; // (a player who hasn't shown the password: nothing)
        int n = stdComm_ENetCrypto_Seal(pSession, channel, pData, len, stdComm_ENet_sealBuffer, sizeof(stdComm_ENet_sealBuffer));
        if (n < 0) return;
        stdComm_ENet_SendPlain(pPeer, stdComm_ENet_sealBuffer, (uint32_t)n, channel);
        return;
    }
    stdComm_ENet_SendPlain(pPeer, pData, len, channel);
}

// A message that came on that connection: opened if it is encrypted (into stdComm_ENet_openBuffer);
// its data and length, or NULL if it isn't to be taken
static const uint8_t* stdComm_ENet_Received(ENetPeer* pPeer, ENetPacket* p, int channel, uint32_t* pLenOut)
{
    if (stdComm_ENet_bServer ? stdComm_ENet_bHostCrypto : stdComm_ENet_clientCrypto.bOn) {
        stdCommENetCryptoSession* pSession = stdComm_ENet_SessionFor(pPeer);
        if (!pSession) return NULL;
        int n = stdComm_ENetCrypto_Open(pSession, channel, p->data, p->dataLength, stdComm_ENet_openBuffer, sizeof(stdComm_ENet_openBuffer));
        if (n < 0) {
            stdComm_ENet_verbosePrintf("ENet: a message that wouldn't open, dropped\n");
            return NULL;
        }
        *pLenOut = (uint32_t)n;
        return stdComm_ENet_openBuffer;
    }
    *pLenOut = (uint32_t)p->dataLength;
    return p->data;
}

// The host: the players in the game (as the game's own scores count them: flag 1), the host itself
// counted while its level is still loading
static int stdComm_ENet_RealConnectedPlayers(void)
{
    int amt = 0;
    for (uint32_t i = 0; i < jkPlayer_maxPlayers; i++)
    {
        if (!i && jkGuiNetHost_bIsDedicated) continue;
        if (jkPlayer_playerInfos[i].flags & 1) amt++;
    }
    if (!amt && !jkGuiNetHost_bIsDedicated) amt = 1;
    return amt;
}

// ------------------------------------------------------------------ the host's events

// The player's id (0: none yet) and the session, sent (sealed, in a game with a password, once the
// player has shown it)
static void stdComm_ENet_SendInfo(ENetPeer* pPeer, int id)
{
    stdComm_ENet_storedEntry.multiModeFlags = sithMulti_multiModeFlags;
    stdCommENetInfo info;
    memset(&info, 0, sizeof(info));
    info.id = id;
    info.entry = stdComm_ENet_storedEntry;
    info.entry.numPlayers = stdComm_ENet_RealConnectedPlayers();
    info.entry.maxPlayers = stdComm_ENet_storedEntry.maxPlayers;
    jkPlayer_maxPlayers = stdComm_ENet_storedEntry.maxPlayers;
    memset(info.entry.wPassword, 0, sizeof(info.entry.wPassword)); // (never sent)
    if (id) stdComm_ENet_SendRaw(pPeer, &info, sizeof(info), 0);
    else stdComm_ENet_SendPlain(pPeer, &info, sizeof(info), 0);
}

// A player who hasn't yet shown the password: its answer to the challenge checked (1: the right
// password, the connection encrypted from now on)
static int stdComm_ENet_CheckHello(int id, const ENetPacket* p)
{
    stdCommENetPeerCrypto* pc = &stdComm_ENet_aPeerCrypto[id - 1];
    uint32_t magic = 0;
    if (p->dataLength != STDCOMM_ENET_HELLO_LEN) return 0;
    memcpy(&magic, p->data, 4);
    if (magic != STDCOMM_ENET_MAGIC_HELLO) return 0;
    stdComm_ENetCrypto_StartSession(&pc->session, stdComm_ENet_hostKey, pc->hostNonce, p->data + 4, STDCOMM_ENETCRYPTO_FROM_HOST);
    uint8_t text[STDCOMM_ENET_HELLO_TEXT_LEN];
    int n = stdComm_ENetCrypto_Open(&pc->session, 0, p->data + 4 + STDCOMM_ENETCRYPTO_NONCE_LEN,
                                    STDCOMM_ENET_HELLO_TEXT_LEN + STDCOMM_ENETCRYPTO_OVERHEAD, text, sizeof(text));
    if (n != STDCOMM_ENET_HELLO_TEXT_LEN || memcmp(text, STDCOMM_ENET_HELLO_TEXT, STDCOMM_ENET_HELLO_TEXT_LEN)) {
        stdComm_ENetCrypto_EndSession(&pc->session);
        return 0;
    }
    return 1;
}

// The host's: addresses that lately gave a wrong password. One guessing it over and over is slowed
// down: after STDCOMM_ENET_FREE_TRIES wrong ones in a row, one try per STDCOMM_ENET_RETRY_MS (a typo,
// or a friend's on the same home network, isn't held against the next player)
#define STDCOMM_ENET_RETRY_MS 2000
#define STDCOMM_ENET_FREE_TRIES 3
#define STDCOMM_ENET_FORGET_MS 60000
#define STDCOMM_ENET_NUM_WRONG 16
static uint32_t stdComm_ENet_aWrongHost[STDCOMM_ENET_NUM_WRONG];
static uint32_t stdComm_ENet_aWrongMs[STDCOMM_ENET_NUM_WRONG];
static int stdComm_ENet_aWrongCount[STDCOMM_ENET_NUM_WRONG];

static int stdComm_ENet_WrongSlot(uint32_t host)
{
    const uint32_t now = stdPlatform_GetTimeMsec();
    for (int i = 0; i < STDCOMM_ENET_NUM_WRONG; i++) {
        if (stdComm_ENet_aWrongMs[i] && stdComm_ENet_aWrongHost[i] == host && now - stdComm_ENet_aWrongMs[i] < STDCOMM_ENET_FORGET_MS)
            return i;
    }
    return -1;
}

static void stdComm_ENet_NoteWrongPassword(uint32_t host)
{
    int slot = stdComm_ENet_WrongSlot(host);
    if (slot < 0) {
        slot = 0;
        for (int i = 0; i < STDCOMM_ENET_NUM_WRONG; i++) {
            if (stdComm_ENet_aWrongMs[i] < stdComm_ENet_aWrongMs[slot]) slot = i; // (the oldest)
        }
        stdComm_ENet_aWrongHost[slot] = host;
        stdComm_ENet_aWrongCount[slot] = 0;
    }
    stdComm_ENet_aWrongCount[slot]++;
    stdComm_ENet_aWrongMs[slot] = stdPlatform_GetTimeMsec() | 1;
}

// A right password: that address's wrong ones forgotten
static void stdComm_ENet_ForgetWrongPasswords(uint32_t host)
{
    int slot = stdComm_ENet_WrongSlot(host);
    if (slot >= 0) stdComm_ENet_aWrongMs[slot] = 0;
}

// Too many wrong ones from there, the last too lately: not even looked at
static int stdComm_ENet_TriedLately(uint32_t host)
{
    int slot = stdComm_ENet_WrongSlot(host);
    return slot >= 0 && stdComm_ENet_aWrongCount[slot] >= STDCOMM_ENET_FREE_TRIES
        && stdPlatform_GetTimeMsec() - stdComm_ENet_aWrongMs[slot] < STDCOMM_ENET_RETRY_MS;
}

// The host, now and then: players that haven't answered the challenge in time let go
static void stdComm_ENet_DropSlowAnswers(void)
{
    if (!stdComm_ENet_bServer || !stdComm_ENet_bHostCrypto || !stdComm_ENet_limitedIds || !stdComm_ENet_pHost) return;
    const uint32_t now = stdPlatform_GetTimeMsec();
    for (size_t k = 0; k < stdComm_ENet_pHost->peerCount; k++) {
        ENetPeer* q = &stdComm_ENet_pHost->peers[k];
        const int id = stdComm_ENet_PeerId(q);
        if (q->state != ENET_PEER_STATE_CONNECTED || id <= 0 || id > 64) continue;
        if (!(stdComm_ENet_limitedIds & (1ULL << (id - 1)))) continue;
        if (now - stdComm_ENet_aPeerCrypto[id - 1].sinceMs > STDCOMM_ENET_CHALLENGE_MS)
            enet_peer_disconnect_later(q, STDCOMM_ENET_REASON_PASSWORD);
    }
}

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

            if (!stdComm_ENet_bHostCrypto) {
                stdComm_ENet_SendInfo(ev->peer, nextId);
                break;
            }
            // (a game with a password: the description with no id, and the challenge)
            stdCommENetPeerCrypto* pc = &stdComm_ENet_aPeerCrypto[nextId - 1];
            stdComm_ENetCrypto_EndSession(&pc->session);
            if (!stdComm_ENetCrypto_Random(pc->hostNonce, sizeof(pc->hostNonce))) {
                enet_peer_disconnect_now(ev->peer, 0);
                stdComm_ENet_usedIds &= ~(1ULL << (nextId - 1));
                ev->peer->data = NULL;
                break;
            }
            pc->sinceMs = stdPlatform_GetTimeMsec();
            stdComm_ENet_limitedIds |= (1ULL << (nextId - 1));
            stdComm_ENet_SendInfo(ev->peer, 0);
            uint8_t challenge[STDCOMM_ENET_CHALLENGE_LEN];
            const uint32_t magic = STDCOMM_ENET_MAGIC_CHALLENGE;
            memcpy(challenge, &magic, 4);
            memcpy(challenge + 4, stdComm_ENet_hostSalt, STDCOMM_ENETCRYPTO_SALT_LEN);
            memcpy(challenge + 4 + STDCOMM_ENETCRYPTO_SALT_LEN, pc->hostNonce, STDCOMM_ENETCRYPTO_NONCE_LEN);
            stdComm_ENet_SendPlain(ev->peer, challenge, sizeof(challenge), 0);
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
                stdComm_ENetCrypto_EndSession(&stdComm_ENet_aPeerCrypto[id - 1].session);
            }
            ev->peer->data = NULL;
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
            ENetPacket* p = ev->packet;
            const int peerId = stdComm_ENet_PeerId(ev->peer);
            if (peerId <= 0 || peerId > 64) {
                enet_packet_destroy(p);
                break;
            }
            if (stdComm_ENet_limitedIds & (1ULL << (peerId - 1))) {
                // (the password not yet shown: only its answer to the challenge taken, once)
                if (ev->peer->state != ENET_PEER_STATE_CONNECTED) {
                    enet_packet_destroy(p);
                    break;
                }
                if (stdComm_ENet_TriedLately(ev->peer->address.host)) {
                    enet_peer_disconnect_later(ev->peer, STDCOMM_ENET_REASON_PASSWORD);
                }
                else if (stdComm_ENet_CheckHello(peerId, p)) {
                    stdComm_ENet_limitedIds &= ~(1ULL << (peerId - 1));
                    stdComm_ENet_ForgetWrongPasswords(ev->peer->address.host);
                    stdComm_ENet_verbosePrintf("ENet: id %x has the password\n", peerId);
                    stdComm_ENet_SendInfo(ev->peer, peerId);
                }
                else {
                    stdComm_ENet_infoPrintf("ENet: a player with the wrong password let go\n");
                    stdComm_ENet_NoteWrongPassword(ev->peer->address.host);
                    enet_peer_disconnect_later(ev->peer, STDCOMM_ENET_REASON_PASSWORD);
                }
                enet_packet_destroy(p);
                break;
            }
            uint32_t len = 0;
            const uint8_t* pData = stdComm_ENet_Received(ev->peer, p, ev->channelID, &len);
            if (pData && len >= 8 && len <= STDCOMM_ENET_MAX_MSG) {
                uint32_t idFrom, idTo;
                memcpy(&idFrom, pData, 4);
                memcpy(&idTo, pData + 4, 4);
                if (idTo && idTo != stdComm_ENet_myId) {
                    // for another player: passed on as it came (sealed again for that player)
                    int bPassed = 0;
                    for (size_t k = 0; k < stdComm_ENet_pHost->peerCount; k++) {
                        ENetPeer* q = &stdComm_ENet_pHost->peers[k];
                        if (q->state == ENET_PEER_STATE_CONNECTED && stdComm_ENet_PeerId(q) == (int)idTo) {
                            stdComm_ENet_SendRaw(q, pData, len, ev->channelID);
                            bPassed = 1;
                        }
                    }
                    stdComm_ENet_verbosePrintf("Passed on %x bytes from %x to %x%s (%s, %x)\n", len, idFrom, idTo, bPassed ? "" : ": not here",
                                               len >= 12 ? sithDSS_IdToStr(pData[8]) : "", len >= 12 ? *(const uint32_t*)(pData + 8) : 0);
                }
                else {
                    stdComm_ENet_Push(0, (int)idFrom, pData + 8, (int)len - 8);
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

// The answer to a host's challenge (a game with a password) made: the key from the password, the session
// (pSession) started, and the HELLO to send; 0 if no random numbers
static int stdComm_ENet_MakeHello(stdCommENetCryptoSession* pSession, const uint16_t* pPassword, const uint8_t* pChallenge,
                                  uint8_t hello[STDCOMM_ENET_HELLO_LEN])
{
    uint8_t passwordKey[STDCOMM_ENETCRYPTO_KEY_LEN];
    const uint32_t magic = STDCOMM_ENET_MAGIC_HELLO;
    memcpy(hello, &magic, 4);
    if (!stdComm_ENetCrypto_Random(hello + 4, STDCOMM_ENETCRYPTO_NONCE_LEN))
        return 0;
    stdComm_ENetCrypto_PasswordKey(pPassword, pChallenge + 4, passwordKey);
    stdComm_ENetCrypto_StartSession(pSession, passwordKey, pChallenge + 4 + STDCOMM_ENETCRYPTO_SALT_LEN,
                                    hello + 4, STDCOMM_ENETCRYPTO_FROM_PLAYER);
    memset(passwordKey, 0, sizeof(passwordKey));
    stdComm_ENetCrypto_Seal(pSession, 0, STDCOMM_ENET_HELLO_TEXT, STDCOMM_ENET_HELLO_TEXT_LEN,
                            hello + 4 + STDCOMM_ENETCRYPTO_NONCE_LEN, STDCOMM_ENET_HELLO_TEXT_LEN + STDCOMM_ENETCRYPTO_OVERHEAD);
    return 1;
}

// The host's challenge (a game with a password) answered: the key made from the password typed, and
// the connection encrypted from now on; none typed: the wrong password
static void stdComm_ENet_AnswerChallenge(ENetPeer* pPeer, const uint8_t* pChallenge)
{
    if (!stdComm_ENet_joinPassword[0]) {
        stdComm_ENet_infoPrintf("ENet: this game has a password\n");
        stdComm_ENet_bWrongPassword = 1;
        enet_peer_disconnect(pPeer, 0);
        return;
    }
    uint8_t hello[STDCOMM_ENET_HELLO_LEN];
    if (!stdComm_ENet_MakeHello(&stdComm_ENet_clientCrypto, stdComm_ENet_joinPassword, pChallenge, hello)) {
        enet_peer_disconnect(pPeer, 0);
        return;
    }
    stdComm_ENet_SendPlain(pPeer, hello, sizeof(hello), 0);
    stdComm_ENet_verbosePrintf("ENet: answered the game's password challenge\n");
}

static void stdComm_ENet_ClientEvent(ENetEvent* ev)
{
    switch (ev->type)
    {
        case ENET_EVENT_TYPE_CONNECT:
            stdComm_ENet_verbosePrintf("ENet: connected to the host\n");
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (ev->data == STDCOMM_ENET_REASON_PASSWORD) {
                stdComm_ENet_infoPrintf("ENet: wrong password for this game\n");
                stdComm_ENet_bWrongPassword = 1;
            }
            else {
                stdComm_ENet_infoPrintf("ENet: lost the host\n");
            }
            stdComm_ENet_pServer = NULL;
            stdComm_ENet_bHostLost = 1;
            break;
        case ENET_EVENT_TYPE_RECEIVE:
        {
            ENetPacket* p = ev->packet;
            uint32_t magic = 0;
            if (p->dataLength >= 4) memcpy(&magic, p->data, 4);
            if (stdComm_ENet_myId == 0xFFFFFFFF && !stdComm_ENet_clientCrypto.bOn
                && p->dataLength == STDCOMM_ENET_CHALLENGE_LEN && magic == STDCOMM_ENET_MAGIC_CHALLENGE) {
                // (the game has a password: our answer)
                stdComm_ENet_AnswerChallenge(ev->peer, p->data);
                enet_packet_destroy(p);
                break;
            }
            uint32_t len = 0;
            const uint8_t* pData = stdComm_ENet_Received(ev->peer, p, ev->channelID, &len);
            if (!pData) {
                enet_packet_destroy(p);
                break;
            }
            if (stdComm_ENet_myId == 0xFFFFFFFF && len == sizeof(stdCommENetInfo)) {
                // the first: our id, and the session
                stdCommENetInfo info;
                memcpy(&info, pData, sizeof(info));
                if (!info.id) {
                    // (no id: the game has a password; its challenge comes next)
                    enet_packet_destroy(p);
                    break;
                }
                stdComm_ENet_myId = (uint32_t)info.id;
                sithMulti_multiModeFlags = info.entry.multiModeFlags;
                sithNet_MultiModeFlags = info.entry.multiModeFlags;
                stdComm_ENet_verbosePrintf("ENet: we are id %x\n", stdComm_ENet_myId);
            }
            else if (stdComm_ENet_myId != 0xFFFFFFFF && len >= 8 && len <= STDCOMM_ENET_MAX_MSG) {
                uint32_t idFrom, idTo;
                memcpy(&idFrom, pData, 4);
                memcpy(&idTo, pData + 4, 4);
                if (!idTo || idTo == stdComm_ENet_myId)
                    stdComm_ENet_Push(0, (int)idFrom, pData + 8, (int)len - 8);
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
            stdComm_ENet_DropSlowAnswers();
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
    stdComm_ENet_limitedIds = 0;
    for (int i = 0; i < 64; i++) stdComm_ENetCrypto_EndSession(&stdComm_ENet_aPeerCrypto[i].session);
    memset(stdComm_ENet_hostKey, 0, sizeof(stdComm_ENet_hostKey));
    stdComm_ENetCrypto_EndSession(&stdComm_ENet_clientCrypto);
    stdComm_ENet_ClearQueue();
    SDL_UnlockMutex(stdComm_ENet_mutex);
}

// An address as typed (a name or IPv4, maybe :port: stdCommTransport_SplitAddress)
static int stdComm_ENet_ParseAddressText(const char* pText, ENetAddress* pAddr)
{
    char host[256];
    int port = STDCOMM_ENET_DEFAULT_PORT;
    if (!stdCommTransport_SplitAddress(pText, host, sizeof(host), &port, STDCOMM_ENET_DEFAULT_PORT)) return 0;
    pAddr->port = (enet_uint16)port;
    return enet_address_set_host(pAddr, host) == 0;
}

// The address typed (jkGuiMultiplayer_ipText)
static int stdComm_ENet_ParseAddress(ENetAddress* pAddr)
{
    char text[256];
    stdString_WcharToChar(text, jkGuiMultiplayer_ipText, 255);
    text[255] = 0;
    return stdComm_ENet_ParseAddressText(text, pAddr);
}

// ------------------------------------------------------------------ finding a game at an address

// (the asking below, on other threads: let in and waited for by Startup / Shutdown)
static stdCommProbeGate stdComm_ENet_probeGate;

// A short connection to the address: its session read (into pOut), then closed; 1 if it answered.
// pbKeepGoing: while that is 1 (or no flag)
static int stdComm_ENet_QueryServer(const ENetAddress* pAddr, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    ENetHost* h = enet_host_create(NULL, 1, STDCOMM_ENET_CHANNELS, 0, 0);
    if (!h) return 0;
    ENetPeer* p = enet_host_connect(h, pAddr, STDCOMM_ENET_CHANNELS, 0);
    int bFound = 0;
    if (p) {
        enet_peer_timeout(p, 32, timeoutMs, timeoutMs);
        ENetEvent ev;
        uint32_t start = stdPlatform_GetTimeMsec();
        while (!bFound && stdPlatform_GetTimeMsec() - start < (uint32_t)timeoutMs && (!pbKeepGoing || *pbKeepGoing) && !stdComm_ENet_probeGate.bClosing) {
            if (enet_host_service(h, &ev, 50) <= 0) continue;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                if (ev.packet->dataLength == sizeof(stdCommENetInfo)) {
                    stdCommENetInfo info;
                    memcpy(&info, ev.packet->data, sizeof(info));
                    *pOut = info.entry;
                    pOut->field_E0 = 10;
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
    return bFound;
}

int stdComm_ENet_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    if (!stdCommTransport_GateEnter(&stdComm_ENet_probeGate)) return 0;
    ENetAddress addr;
    int r;
    if (!stdComm_ENet_bInitted) r = 0;
    else if (!stdComm_ENet_ParseAddressText(pAddress, &addr)) r = -1;
    else r = stdComm_ENet_QueryServer(&addr, pOut, timeoutMs, pbKeepGoing);
    stdCommTransport_GateLeave(&stdComm_ENet_probeGate);
    return r;
}

static int stdComm_ENet_CheckPasswordNow(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing);

int stdComm_ENet_CheckPassword(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing)
{
    if (!stdCommTransport_GateEnter(&stdComm_ENet_probeGate)) return -1;
    const int r = stdComm_ENet_CheckPasswordNow(pAddress, pwPassword, timeoutMs, pbKeepGoing);
    stdCommTransport_GateLeave(&stdComm_ENet_probeGate);
    return r;
}

static int stdComm_ENet_CheckPasswordNow(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing)
{
    ENetAddress addr;
    if (!stdComm_ENet_bInitted) return -1;
    if (!stdComm_ENet_ParseAddressText(pAddress, &addr)) return -1;
    uint16_t password[33];
    memset(password, 0, sizeof(password));
    for (int i = 0; pwPassword && i < 32 && pwPassword[i]; i++) password[i] = (uint16_t)pwPassword[i];

    ENetHost* h = enet_host_create(NULL, 1, STDCOMM_ENET_CHANNELS, 0, 0);
    if (!h) return -1;
    ENetPeer* p = enet_host_connect(h, &addr, STDCOMM_ENET_CHANNELS, 0);
    stdCommENetCryptoSession session;
    memset(&session, 0, sizeof(session));
    int result = -1, bDone = 0;
    if (p) {
        enet_peer_timeout(p, 32, timeoutMs, timeoutMs);
        const uint32_t start = stdPlatform_GetTimeMsec();
        while (!bDone && stdPlatform_GetTimeMsec() - start < (uint32_t)timeoutMs && (!pbKeepGoing || *pbKeepGoing) && !stdComm_ENet_probeGate.bClosing) {
            ENetEvent ev;
            if (enet_host_service(h, &ev, 50) <= 0) continue;
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                const ENetPacket* pk = ev.packet;
                uint32_t magic = 0;
                if (pk->dataLength >= 4) memcpy(&magic, pk->data, 4);
                if (!session.bOn && pk->dataLength == STDCOMM_ENET_CHALLENGE_LEN && magic == STDCOMM_ENET_MAGIC_CHALLENGE) {
                    // (the challenge: answered as joining would)
                    uint8_t hello[STDCOMM_ENET_HELLO_LEN];
                    if (stdComm_ENet_MakeHello(&session, password, pk->data, hello))
                        stdComm_ENet_SendPlain(p, hello, sizeof(hello), 0);
                }
                else if (!session.bOn && pk->dataLength == sizeof(stdCommENetInfo)) {
                    stdCommENetInfo info;
                    memcpy(&info, pk->data, sizeof(info));
                    if (info.id) { result = 1; bDone = 1; } // (no password asked for)
                }
                else if (session.bOn) {
                    // (an id, sealed: the password was right)
                    uint8_t buf[sizeof(stdCommENetInfo)];
                    if (stdComm_ENetCrypto_Open(&session, ev.channelID, pk->data, pk->dataLength, buf, sizeof(buf)) == (int)sizeof(buf)) {
                        result = 1;
                        bDone = 1;
                    }
                }
                enet_packet_destroy(ev.packet);
            }
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                result = ev.data == STDCOMM_ENET_REASON_PASSWORD ? 0 : -1;
                bDone = 1;
                p = NULL;
            }
        }
        if (p) {
            enet_peer_disconnect_now(p, 0);
            enet_host_flush(h);
        }
    }
    enet_host_destroy(h);
    stdComm_ENetCrypto_EndSession(&session);
    memset(password, 0, sizeof(password));
    return result;
}

// ------------------------------------------------------------------ games nearby

static int stdComm_ENet_bAdvertising = 0;
static uint32_t stdComm_ENet_advertisedMs = 0;

// The host's game, as games nearby see it (stdComm_ENetDiscover): told now, and again when it changes
static void stdComm_ENet_Advertise(void)
{
    if (!stdComm_ENet_bServer || !stdComm_ENet_pHost) return;
    stdCommENetDiscoverGame g;
    memset(&g, 0, sizeof(g));
    // (plain text: a phone's curly apostrophe made straight, anything else not ASCII a '?')
    for (int i = 0, n = 0; stdComm_ENet_storedEntry.serverName[i] && i < 32 && n < (int)sizeof(g.name) - 1; i++) {
        const char16_t c = stdComm_ENet_storedEntry.serverName[i];
        g.name[n++] = (c == 0x2018 || c == 0x2019) ? '\'' : (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    if (!g.name[0]) {
        // (no name given: the host's)
        char me[32];
        int n = 0;
        for (int i = 0; jkPlayer_playerShortName[i] && n < (int)sizeof(me) - 1; i++) {
            const char16_t c = jkPlayer_playerShortName[i];
            me[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        me[n] = 0;
        snprintf(g.name, sizeof(g.name), "%s's game", me[0] ? me : "OpenJKDF2");
    }
#ifdef QOL_IMPROVEMENTS
    snprintf(g.hostId, sizeof(g.hostId), "%s", jkCoopSave_PlayerId());
#endif
    snprintf(g.game, sizeof(g.game), "%s", Main_bMotsCompat ? "mots" : "jk");
    const int flags = sithMulti_multiModeFlags ? sithMulti_multiModeFlags : stdComm_ENet_storedEntry.multiModeFlags;
#ifdef QOL_IMPROVEMENTS
    snprintf(g.mode, sizeof(g.mode), "%s", jkGuiNetHost_ModeName(stdComm_ENet_storedEntry.episodeGobName, flags));
#else
    snprintf(g.mode, sizeof(g.mode), "%s", (flags & MULTIMODEFLAG_COOP) ? "coop" : (flags & MULTIMODEFLAG_TEAMS) ? "team" : "dm");
#endif
    snprintf(g.level, sizeof(g.level), "%s", stdComm_ENet_storedEntry.mapJklFname);
#ifdef QOL_IMPROVEMENTS
    // (and its title, as the loading screen shows it, plain text)
    char16_t wTitle[64];
    if (jkGuiTitle_LevelTitle(&jkCog_strings, stdComm_ENet_storedEntry.mapJklFname, wTitle, 64)) {
        int n = 0;
        for (int i = 0; wTitle[i] && n < (int)sizeof(g.title) - 1; i++) {
            const char16_t c = wTitle[i];
            g.title[n++] = (c == 0x2018 || c == 0x2019) ? '\'' : (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        g.title[n] = 0;
    }
#endif
    g.numPlayers = stdComm_ENet_RealConnectedPlayers();
    g.maxPlayers = (int)stdComm_ENet_storedEntry.maxPlayers;
    if (jkGuiNetHost_bIsDedicated && g.maxPlayers > 1) g.maxPlayers--;
    g.bPassword = stdComm_ENet_bHostCrypto;
    stdComm_ENetDiscover_Advertise(&g, stdComm_ENet_pHost->address.port);
    stdComm_ENet_bAdvertising = 1;
    stdComm_ENet_advertisedMs = stdPlatform_GetTimeMsec();
}

static void stdComm_ENet_StopAdvertising(void)
{
    if (!stdComm_ENet_bAdvertising) return;
    stdComm_ENetDiscover_StopAdvertising();
    stdComm_ENet_bAdvertising = 0;
}

// ------------------------------------------------------------------ the game's side

static void stdComm_ENet_ResetClients(void)
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
        stdCommTransport_GateOpen(&stdComm_ENet_probeGate);
    }
    jkGuiMultiplayer_numConnections = 1;
    jk_snwprintf(jkGuiMultiplayer_aConnections[0].name, 0x80, u"Local network / Internet (UDP)");
    stdComm_dword_8321E0 = 0;
    memset(jkGuiMultiplayer_aEntries, 0, sizeof(jkMultiEntry) * 32);
    dplay_dword_55D618 = 0;
    stdComm_ENet_ResetClients();
}

void stdComm_ENet_Shutdown()
{
    if (!stdComm_ENet_bInitted) return;
    DirectPlay_EnumSessions2();
    stdCommTransport_GateClose(&stdComm_ENet_probeGate, 5000); // (games being asked about: given up first)
    stdComm_ENet_StopThread();
    stdComm_ENet_StopAdvertising();
    stdComm_ENet_CloseHost();
    stdComm_ENetDiscover_Shutdown();
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

static int stdComm_ENet_Receive(int *pIdOut, int *pMsgIdOut, int *pLenOut)
{
    int maxLen = *pLenOut;
    *pIdOut = 0;
    *pLenOut = 0;
    if (!stdComm_ENet_bInitted) return -1;

    stdComm_ENet_ResetClients();

    // (hosting: games nearby told the player count now and then)
    if (stdComm_ENet_bAdvertising && stdPlatform_GetTimeMsec() - stdComm_ENet_advertisedMs > 2000)
        stdComm_ENet_Advertise();

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

static BOOL stdComm_ENet_Send(DPID idFrom, DPID idTo, void *lpData, DWORD dwDataSize)
{
    if (!stdComm_ENet_bInitted) return 0;

    stdComm_ENet_ResetClients();

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

// Joining the game found at the address typed
static int stdComm_ENet_Join(char16_t* pwPassword)
{
    if (!stdComm_ENet_bInitted) return 0;

    stdComm_ENet_CloseHost();
    ENetAddress addr;
    if (!stdComm_ENet_ParseAddress(&addr)) return 0x88770154 + 1;

    SDL_LockMutex(stdComm_ENet_mutex);
    stdComm_ENet_bServer = 0;
    stdComm_ENet_myId = 0xFFFFFFFF;
    stdComm_ENet_pHost = enet_host_create(NULL, 1, STDCOMM_ENET_CHANNELS, 0, 0);
    stdComm_ENet_bWrongPassword = 0;
    memset(stdComm_ENet_joinPassword, 0, sizeof(stdComm_ENet_joinPassword));
    for (int i = 0; pwPassword && i < 32 && pwPassword[i]; i++) stdComm_ENet_joinPassword[i] = (uint16_t)pwPassword[i];
    stdComm_ENet_pServer = stdComm_ENet_pHost ? enet_host_connect(stdComm_ENet_pHost, &addr, STDCOMM_ENET_CHANNELS, 0) : NULL;
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
        const int bWrongPassword = stdComm_ENet_bWrongPassword;
        stdComm_ENet_CloseHost();
        return bWrongPassword ? 0x88770154 : 0x88770154 + 1; // (the Join screen: "wrong password", or can't connect)
    }
    stdComm_ENet_ResetClients();
    return 0;
}

static void stdComm_ENet_Close(void)
{
    if (!stdComm_ENet_bInitted) return;
    stdComm_ENet_StopAdvertising();
    stdComm_ENet_CloseHost();
    stdComm_ENet_bServer = 0;
    stdComm_ENet_bHostCrypto = 0;
}

// The level now played, for the session told to players who join
static void stdComm_ENet_SetSessionLevel(void)
{
    _strncpy(stdComm_ENet_storedEntry.mapJklFname, jkMain_aLevelJklFname, 0x20);
}

// Hosting: listening for players
static int stdComm_ENet_OpenHost(jkMultiEntry* pEntry)
{
    stdComm_ENet_storedEntry = *pEntry;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
    stdComm_bIsServer = 1;

    if (!stdComm_ENet_bInitted) return 0;

    stdComm_ENet_CloseHost();
    // (a password: the game encrypted, its key made now, salted afresh)
    stdComm_ENet_bHostCrypto = 0;
    if (pEntry->wPassword[0]) {
        uint16_t pw[33];
        memset(pw, 0, sizeof(pw));
        for (int i = 0; i < 32 && pEntry->wPassword[i]; i++) pw[i] = (uint16_t)pEntry->wPassword[i];
        if (!stdComm_ENetCrypto_Random(stdComm_ENet_hostSalt, sizeof(stdComm_ENet_hostSalt))) {
            stdComm_ENet_infoPrintf("ENet: no random numbers for the game's password\n");
            return 0x88770118 + 1;
        }
        stdComm_ENetCrypto_PasswordKey(pw, stdComm_ENet_hostSalt, stdComm_ENet_hostKey);
        memset(pw, 0, sizeof(pw));
        stdComm_ENet_bHostCrypto = 1;
        stdComm_ENet_infoPrintf("ENet: the game has a password: encrypted\n");
    }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)(jkGuiNetHost_portNum > 0 ? jkGuiNetHost_portNum : STDCOMM_ENET_DEFAULT_PORT);
    SDL_LockMutex(stdComm_ENet_mutex);
    stdComm_ENet_bServer = 1;
    stdComm_ENet_myId = 1;
    stdComm_ENet_usedIds = 0x1;
    stdComm_ENet_pHost = enet_host_create(&addr, STDCOMM_ENET_MAX_PEERS, STDCOMM_ENET_CHANNELS, 0, 0);
    // (a game with a password: no one address may hold all the places with unanswered challenges)
    if (stdComm_ENet_pHost && stdComm_ENet_bHostCrypto) stdComm_ENet_pHost->duplicatePeers = STDCOMM_ENET_PEERS_PER_ADDRESS;
    SDL_UnlockMutex(stdComm_ENet_mutex);
    if (!stdComm_ENet_pHost) {
        stdComm_ENet_infoPrintf("ENet: can't listen on port %d\n", addr.port);
        return 0x88770118 + 1;
    }
    stdComm_ENet_infoPrintf("ENet: hosting on port %d\n", addr.port);
    stdComm_ENet_StartThread();
    stdComm_ENet_Advertise();
    stdComm_ENet_ResetClients();
    return 0;
}

// The session to host (before stdComm_ENet_OpenHost)
static void stdComm_ENet_SetSession(jkMultiEntry* pEntry)
{
    stdComm_ENet_storedEntry = *pEntry;
    jkPlayer_maxPlayers = pEntry->maxPlayers;
}

const stdCommTransport stdComm_ENet_transport = {
    "ENet",
    stdComm_ENet_Receive,
    stdComm_ENet_Send,
    stdComm_ENet_Join,
    stdComm_ENet_OpenHost,
    stdComm_ENet_SetSession,
    stdComm_ENet_SetSessionLevel,
    stdComm_ENet_Close,
    stdComm_ENet_ResetClients,
    stdComm_ENet_ProbeAddress,
    stdComm_ENet_CheckPassword,
};

#endif // PLATFORM_ENET

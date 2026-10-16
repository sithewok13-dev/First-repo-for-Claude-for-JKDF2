// Added: entire file. See stdComm_ENetDiscover.h.

#include "stdComm_ENetDiscover.h"

#ifdef PLATFORM_ENET

#include "stdComm_ENet.h"

#if defined(__APPLE__) || defined(STDCOMM_ENET_HAVE_DNSSD)
#define STDCOMM_ENETDISCOVER_BONJOUR
#endif

#ifdef STDCOMM_ENETDISCOVER_BONJOUR

#include "stdPlatform.h"
#include "SDL2_helper.h"

#include <dns_sd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <arpa/inet.h>

#define STDCOMM_ENETDISCOVER_REFRESH_MS 4000  // (a game heard of asked again for its details this often)
#define STDCOMM_ENETDISCOVER_RESOLVE_MS 3000  // (and given up on, this time, after this long)

// A game heard of (by its Bonjour name), and what it said
typedef struct stdCommENetDiscoverSeen
{
    int bUsed;
    char service[64];
    char type[64];
    char domain[64];
    uint32_t interfaceIndex;
    int bResolved;
    uint32_t askedMs;        // (when its details were last asked for)
    DNSServiceRef resolve;   // (asking now)
    int missed;              // (askings in a row that got no answer: two, and it is no longer listed)
    stdCommENetDiscoverGame game;
} stdCommENetDiscoverSeen;

static SDL_Mutex* stdComm_ENetDiscover_mutex = NULL;
static SDL_Thread* stdComm_ENetDiscover_thread = NULL;
static volatile int stdComm_ENetDiscover_bRun = 0;

// (the thread's: what is open now)
static DNSServiceRef stdComm_ENetDiscover_register = NULL;
static DNSServiceRef stdComm_ENetDiscover_browse = NULL;

// (asked for: under the lock)
static int stdComm_ENetDiscover_bWantAdvertise = 0;
static int stdComm_ENetDiscover_advertiseSerial = 0;   // (changed whenever what is said changes)
static stdCommENetDiscoverGame stdComm_ENetDiscover_mine;
static int stdComm_ENetDiscover_minePort = 0;
static int stdComm_ENetDiscover_bWantBrowse = 0;
static int stdComm_ENetDiscover_bRefsClosed = 0;        // (the thread's: a ref closed while answering the others)
static uint32_t stdComm_ENetDiscover_retryMs = 0;        // (Bonjour itself gone (restarted): advertising not tried again before)
static uint32_t stdComm_ENetDiscover_browseRetryMs = 0;  // (and browsing)
static uint32_t stdComm_ENetDiscover_browseOpenMs = 0;   // (when the browse was opened)
#define STDCOMM_ENETDISCOVER_RETRY_MS 3000
// (iOS: the Local Network permission refused; not every dns_sd.h has the name)
#define STDCOMM_ENETDISCOVER_ERR_POLICYDENIED (-65570)
static volatile int stdComm_ENetDiscover_bBlocked = 0;  // (the last browse refused that way)
static int stdComm_ENetDiscover_bBrowseFailed = 0;       // (the thread's: the browse callback said it failed)
#define STDCOMM_ENETDISCOVER_ALLOWED_MS 2000             // (a browse open this long without a refusal: allowed)
static char stdComm_ENetDiscover_browseGame[8];
static stdCommENetDiscoverSeen stdComm_ENetDiscover_aSeen[STDCOMM_ENETDISCOVER_MAX_GAMES];

static uint32_t stdComm_ENetDiscover_Now(void)
{
    return (uint32_t)SDL_GetTicks();
}

// ------------------------------------------------------------------ TXT records

static void stdComm_ENetDiscover_TxtSet(TXTRecordRef* pTxt, const char* key, const char* value)
{
    size_t n = strlen(value);
    if (n > 250) n = 250;
    TXTRecordSetValue(pTxt, key, (uint8_t)n, value);
}

static void stdComm_ENetDiscover_TxtGet(uint16_t txtLen, const unsigned char* pTxt, const char* key, char* pOut, int outLen)
{
    uint8_t n = 0;
    const void* p = TXTRecordGetValuePtr(txtLen, pTxt, key, &n);
    pOut[0] = 0;
    if (!p) return;
    if (n >= outLen) n = (uint8_t)(outLen - 1);
    memcpy(pOut, p, n);
    pOut[n] = 0;
}

// The host's addresses: the one to join on is the one on the same network as one of ours (the first
// three numbers alike), else the first
static void stdComm_ENetDiscover_PickAddress(const char* list, char* pOut, int outLen)
{
    char mine[256];
    stdComm_ENet_GetLocalAddresses(mine, sizeof(mine));
    char first[48] = "";
    char best[48] = "";
    const char* s = list;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        int n = 0;
        char one[48];
        while (s[n] && s[n] != ',' && n < (int)sizeof(one) - 1) { one[n] = s[n]; n++; }
        one[n] = 0;
        s += n;
        while (*s && *s != ',') s++;
        struct in_addr a;
        if (!one[0] || inet_pton(AF_INET, one, &a) != 1) continue;
        if (!first[0]) snprintf(first, sizeof(first), "%s", one);
        // (same /24 as one of ours)
        const char* lastDot = strrchr(one, '.');
        if (!best[0] && lastDot) {
            size_t pre = (size_t)(lastDot - one) + 1;
            for (const char* m = mine; *m; ) {
                while (*m == ' ' || *m == ',') m++;
                if (!strncmp(m, one, pre)) { snprintf(best, sizeof(best), "%s", one); break; }
                while (*m && *m != ',') m++;
            }
        }
    }
    snprintf(pOut, outLen, "%s", best[0] ? best : first);
}

// ------------------------------------------------------------------ hosting

static void DNSSD_API stdComm_ENetDiscover_Registered(DNSServiceRef ref, DNSServiceFlags flags, DNSServiceErrorType err,
                                                      const char* name, const char* type, const char* domain, void* ctx)
{
    (void)ref; (void)flags; (void)type; (void)domain; (void)ctx;
    if (err == kDNSServiceErr_NoError) stdPlatform_Printf("ENet: games nearby can find this one (%s)\n", name);
    else stdPlatform_Printf("ENet: couldn't tell games nearby about this one (%d)\n", (int)err);
}

// What the host says, as a TXT record (into pTxt, its buffer buf)
static void stdComm_ENetDiscover_MakeTxt(TXTRecordRef* pTxt, char* buf, int bufLen, const stdCommENetDiscoverGame* g, int port)
{
    char ips[256], num[16];
    stdComm_ENet_GetLocalAddresses(ips, sizeof(ips));
    TXTRecordCreate(pTxt, (uint16_t)bufLen, buf);
    stdComm_ENetDiscover_TxtSet(pTxt, "v", "1");
    stdComm_ENetDiscover_TxtSet(pTxt, "g", g->game);
    stdComm_ENetDiscover_TxtSet(pTxt, "nm", g->name);
    stdComm_ENetDiscover_TxtSet(pTxt, "id", g->hostId);
    stdComm_ENetDiscover_TxtSet(pTxt, "ip", ips);
    snprintf(num, sizeof(num), "%d", port);
    stdComm_ENetDiscover_TxtSet(pTxt, "p", num);
    stdComm_ENetDiscover_TxtSet(pTxt, "m", g->mode);
    stdComm_ENetDiscover_TxtSet(pTxt, "l", g->level);
    stdComm_ENetDiscover_TxtSet(pTxt, "t", g->title);
    snprintf(num, sizeof(num), "%d", g->numPlayers);
    stdComm_ENetDiscover_TxtSet(pTxt, "n", num);
    snprintf(num, sizeof(num), "%d", g->maxPlayers);
    stdComm_ENetDiscover_TxtSet(pTxt, "x", num);
    stdComm_ENetDiscover_TxtSet(pTxt, "pw", g->bPassword ? "1" : "0");
}

// (the thread's) The host's record brought in line with what was asked for
static void stdComm_ENetDiscover_UpdateAdvertising(void)
{
    static int doneSerial = -1;
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    const int bWant = stdComm_ENetDiscover_bWantAdvertise;
    const int serial = stdComm_ENetDiscover_advertiseSerial;
    stdCommENetDiscoverGame g = stdComm_ENetDiscover_mine;
    const int port = stdComm_ENetDiscover_minePort;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);

    if (!bWant) {
        if (stdComm_ENetDiscover_register) {
            DNSServiceRefDeallocate(stdComm_ENetDiscover_register);
            stdComm_ENetDiscover_register = NULL;
        }
        doneSerial = -1;
        return;
    }
    if (stdComm_ENetDiscover_register && serial == doneSerial) return;
    if (!stdComm_ENetDiscover_register && (int32_t)(stdComm_ENetDiscover_Now() - stdComm_ENetDiscover_retryMs) < 0) return;

    char buf[1024];
    TXTRecordRef txt;
    stdComm_ENetDiscover_MakeTxt(&txt, buf, sizeof(buf), &g, port);
    int bOk = 0;
    if (stdComm_ENetDiscover_register) {
        // (the details changed: said again)
        bOk = DNSServiceUpdateRecord(stdComm_ENetDiscover_register, NULL, 0, TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt), 0) == kDNSServiceErr_NoError;
        if (!bOk) {
            DNSServiceRefDeallocate(stdComm_ENetDiscover_register);
            stdComm_ENetDiscover_register = NULL;
        }
    }
    if (!stdComm_ENetDiscover_register) {
        // (the Bonjour name: the game's, which Bonjour makes unique if another has it)
        char service[64];
        snprintf(service, sizeof(service), "%s", g.name[0] ? g.name : "OpenJKDF2 game");
        bOk = DNSServiceRegister(&stdComm_ENetDiscover_register, 0, 0, service, STDCOMM_ENETDISCOVER_SERVICE, NULL, NULL,
                                 htons((uint16_t)port), TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
                                 stdComm_ENetDiscover_Registered, NULL) == kDNSServiceErr_NoError;
        if (!bOk) stdComm_ENetDiscover_register = NULL;
    }
    TXTRecordDeallocate(&txt);
    if (bOk) doneSerial = serial;
}

// ------------------------------------------------------------------ joining

static void stdComm_ENetDiscover_Forget(stdCommENetDiscoverSeen* s)
{
    if (s->resolve) {
        DNSServiceRefDeallocate(s->resolve);
        stdComm_ENetDiscover_bRefsClosed = 1;
    }
    memset(s, 0, sizeof(*s));
}

static void DNSSD_API stdComm_ENetDiscover_Resolved(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interfaceIndex,
                                                    DNSServiceErrorType err, const char* fullname, const char* hosttarget,
                                                    uint16_t portNet, uint16_t txtLen, const unsigned char* txtRecord, void* ctx)
{
    (void)ref; (void)flags; (void)interfaceIndex; (void)fullname; (void)hosttarget;
    stdCommENetDiscoverSeen* s = (stdCommENetDiscoverSeen*)ctx;
    if (err != kDNSServiceErr_NoError) return;

    stdCommENetDiscoverGame g;
    char v[8], ips[256], port[16], num[16];
    memset(&g, 0, sizeof(g));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "v", v, sizeof(v));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "g", g.game, sizeof(g.game));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "nm", g.name, sizeof(g.name));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "id", g.hostId, sizeof(g.hostId));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "ip", ips, sizeof(ips));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "p", port, sizeof(port));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "m", g.mode, sizeof(g.mode));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "l", g.level, sizeof(g.level));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "t", g.title, sizeof(g.title));
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "n", num, sizeof(num));
    g.numPlayers = atoi(num);
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "x", num, sizeof(num));
    g.maxPlayers = atoi(num);
    stdComm_ENetDiscover_TxtGet(txtLen, txtRecord, "pw", num, sizeof(num));
    g.bPassword = num[0] == '1';
    char ip[48];
    stdComm_ENetDiscover_PickAddress(ips, ip, sizeof(ip));
    int p = atoi(port);
    if (p <= 0 || p > 65535) p = ntohs(portNet);

    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    if (s->bUsed) {
        if (strcmp(v, "1") || !ip[0] || strcmp(g.game, stdComm_ENetDiscover_browseGame)) {
            s->bResolved = 0; // (another game's, or one that can't be joined: not listed)
        }
        else {
            snprintf(g.address, sizeof(g.address), "%s:%d", ip, p);
            if (!g.name[0]) snprintf(g.name, sizeof(g.name), "%s", s->service);
            s->game = g;
            s->bResolved = 1;
            s->missed = 0;
        }
    }
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
    // (the asking done: closed by the thread)
}

static void DNSSD_API stdComm_ENetDiscover_Browsed(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interfaceIndex,
                                                   DNSServiceErrorType err, const char* name, const char* type,
                                                   const char* domain, void* ctx)
{
    (void)ref; (void)ctx;
    if (err != kDNSServiceErr_NoError) {
        // (refused: the browse closed and tried again in a while; the Join screen says why)
        if (err == STDCOMM_ENETDISCOVER_ERR_POLICYDENIED) stdComm_ENetDiscover_bBlocked = 1;
        stdComm_ENetDiscover_bBrowseFailed = 1;
        return;
    }
    stdComm_ENetDiscover_bBlocked = 0;
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    stdCommENetDiscoverSeen* pFree = NULL;
    stdCommENetDiscoverSeen* pFound = NULL;
    for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES; i++) {
        stdCommENetDiscoverSeen* s = &stdComm_ENetDiscover_aSeen[i];
        if (!s->bUsed) { if (!pFree) pFree = s; continue; }
        if (!strcmp(s->service, name) && s->interfaceIndex == interfaceIndex) pFound = s;
    }
    if (flags & kDNSServiceFlagsAdd) {
        if (!pFound && pFree) {
            pFound = pFree;
            memset(pFound, 0, sizeof(*pFound));
            pFound->bUsed = 1;
            snprintf(pFound->service, sizeof(pFound->service), "%s", name);
            snprintf(pFound->type, sizeof(pFound->type), "%s", type);
            snprintf(pFound->domain, sizeof(pFound->domain), "%s", domain);
            pFound->interfaceIndex = interfaceIndex;
        }
    }
    else if (pFound) {
        stdComm_ENetDiscover_Forget(pFound); // (gone)
    }
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
}

// (the thread's) Browsing started or stopped as asked, and the games heard of asked for their details
static void stdComm_ENetDiscover_UpdateBrowsing(void)
{
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    const int bWant = stdComm_ENetDiscover_bWantBrowse;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);

    if (!bWant) {
        if (stdComm_ENetDiscover_browse) {
            DNSServiceRefDeallocate(stdComm_ENetDiscover_browse);
            stdComm_ENetDiscover_browse = NULL;
            SDL_LockMutex(stdComm_ENetDiscover_mutex);
            for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES; i++) stdComm_ENetDiscover_Forget(&stdComm_ENetDiscover_aSeen[i]);
            SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
        }
        return;
    }
    if (!stdComm_ENetDiscover_browse) {
        if ((int32_t)(stdComm_ENetDiscover_Now() - stdComm_ENetDiscover_browseRetryMs) < 0) return;
        DNSServiceErrorType err = DNSServiceBrowse(&stdComm_ENetDiscover_browse, 0, 0, STDCOMM_ENETDISCOVER_SERVICE, NULL,
                                                   stdComm_ENetDiscover_Browsed, NULL);
        if (err != kDNSServiceErr_NoError) {
            if (err == STDCOMM_ENETDISCOVER_ERR_POLICYDENIED) stdComm_ENetDiscover_bBlocked = 1;
            stdComm_ENetDiscover_browse = NULL;
            stdComm_ENetDiscover_browseRetryMs = stdComm_ENetDiscover_Now() + STDCOMM_ENETDISCOVER_RETRY_MS;
            return;
        }
        stdComm_ENetDiscover_browseOpenMs = stdComm_ENetDiscover_Now();
    }
    else if (stdComm_ENetDiscover_bBlocked && stdComm_ENetDiscover_Now() - stdComm_ENetDiscover_browseOpenMs > STDCOMM_ENETDISCOVER_ALLOWED_MS) {
        stdComm_ENetDiscover_bBlocked = 0; // (open a while, not refused: allowed now)
    }
    const uint32_t now = stdComm_ENetDiscover_Now();
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES; i++) {
        stdCommENetDiscoverSeen* s = &stdComm_ENetDiscover_aSeen[i];
        if (!s->bUsed) continue;
        if (s->resolve && now - s->askedMs > STDCOMM_ENETDISCOVER_RESOLVE_MS) {
            DNSServiceRefDeallocate(s->resolve);
            s->resolve = NULL;
            // (no answer: a host gone without saying, its record still about; not listed after two)
            if (++s->missed >= 2) s->bResolved = 0;
        }
        if (!s->resolve && (!s->askedMs || now - s->askedMs > STDCOMM_ENETDISCOVER_REFRESH_MS)) {
            s->askedMs = now ? now : 1;
            if (DNSServiceResolve(&s->resolve, 0, s->interfaceIndex, s->service, s->type, s->domain,
                                  stdComm_ENetDiscover_Resolved, s) != kDNSServiceErr_NoError)
                s->resolve = NULL;
        }
    }
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
}

// ------------------------------------------------------------------ the thread

static int stdComm_ENetDiscover_Thread(void* unused)
{
    (void)unused;
    while (stdComm_ENetDiscover_bRun) {
        stdComm_ENetDiscover_UpdateAdvertising();
        stdComm_ENetDiscover_UpdateBrowsing();

        // (whatever is open answered, for a tenth of a second)
        DNSServiceRef aRef[2 + STDCOMM_ENETDISCOVER_MAX_GAMES];
        int n = 0;
        if (stdComm_ENetDiscover_register) aRef[n++] = stdComm_ENetDiscover_register;
        if (stdComm_ENetDiscover_browse) aRef[n++] = stdComm_ENetDiscover_browse;
        SDL_LockMutex(stdComm_ENetDiscover_mutex);
        for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES; i++)
            if (stdComm_ENetDiscover_aSeen[i].resolve) aRef[n++] = stdComm_ENetDiscover_aSeen[i].resolve;
        SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
        if (!n) {
            SDL_Delay(100);
            continue;
        }
        fd_set fds;
        FD_ZERO(&fds);
        int maxFd = -1;
        for (int i = 0; i < n; i++) {
            int fd = DNSServiceRefSockFD(aRef[i]);
            if (fd < 0) continue;
            FD_SET(fd, &fds);
            if (fd > maxFd) maxFd = fd;
        }
        struct timeval tv = { 0, 100000 };
        if (maxFd < 0 || select(maxFd + 1, &fds, NULL, NULL, &tv) <= 0) continue;
        stdComm_ENetDiscover_bRefsClosed = 0;
        for (int i = 0; i < n && !stdComm_ENetDiscover_bRefsClosed; i++) {
            // (one closed meanwhile, by a game gone: the rest left for the next time round)
            int fd = DNSServiceRefSockFD(aRef[i]);
            if (fd >= 0 && FD_ISSET(fd, &fds)) {
                stdComm_ENetDiscover_bBrowseFailed = 0;
                DNSServiceErrorType err = DNSServiceProcessResult(aRef[i]);
                if (aRef[i] == stdComm_ENetDiscover_browse && stdComm_ENetDiscover_bBrowseFailed && err == kDNSServiceErr_NoError)
                    err = kDNSServiceErr_Unknown; // (the browse said it failed: closed as below)
                if (err != kDNSServiceErr_NoError && (aRef[i] == stdComm_ENetDiscover_register || aRef[i] == stdComm_ENetDiscover_browse)) {
                    // (Bonjour itself gone, restarted, or the app put away: closed, opened again in a while,
                    // never spun on a socket that has nothing more to say)
                    if (aRef[i] == stdComm_ENetDiscover_register) {
                        stdComm_ENetDiscover_register = NULL;
                        stdComm_ENetDiscover_retryMs = stdComm_ENetDiscover_Now() + STDCOMM_ENETDISCOVER_RETRY_MS;
                    }
                    else {
                        // (and what it had heard forgotten: heard again when it works)
                        stdComm_ENetDiscover_browse = NULL;
                        stdComm_ENetDiscover_browseRetryMs = stdComm_ENetDiscover_Now() + STDCOMM_ENETDISCOVER_RETRY_MS;
                        SDL_LockMutex(stdComm_ENetDiscover_mutex);
                        for (int k = 0; k < STDCOMM_ENETDISCOVER_MAX_GAMES; k++) stdComm_ENetDiscover_Forget(&stdComm_ENetDiscover_aSeen[k]);
                        SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
                    }
                    DNSServiceRefDeallocate(aRef[i]);
                    stdComm_ENetDiscover_bRefsClosed = 1;
                    continue;
                }
                // (a game's details came: that asking done)
                SDL_LockMutex(stdComm_ENetDiscover_mutex);
                for (int k = 0; k < STDCOMM_ENETDISCOVER_MAX_GAMES; k++) {
                    stdCommENetDiscoverSeen* s = &stdComm_ENetDiscover_aSeen[k];
                    if (s->resolve == aRef[i] && (s->bResolved || err != kDNSServiceErr_NoError)) {
                        DNSServiceRefDeallocate(s->resolve);
                        s->resolve = NULL;
                    }
                }
                SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
            }
        }
    }
    // (everything closed)
    if (stdComm_ENetDiscover_register) DNSServiceRefDeallocate(stdComm_ENetDiscover_register);
    if (stdComm_ENetDiscover_browse) DNSServiceRefDeallocate(stdComm_ENetDiscover_browse);
    stdComm_ENetDiscover_register = NULL;
    stdComm_ENetDiscover_browse = NULL;
    for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES; i++) stdComm_ENetDiscover_Forget(&stdComm_ENetDiscover_aSeen[i]);
    return 0;
}

static void stdComm_ENetDiscover_EnsureThread(void)
{
    if (!stdComm_ENetDiscover_mutex) stdComm_ENetDiscover_mutex = SDL_CreateMutex();
    if (stdComm_ENetDiscover_thread) return;
    stdComm_ENetDiscover_bRun = 1;
    stdComm_ENetDiscover_thread = SDL_CreateThread(stdComm_ENetDiscover_Thread, "stdComm_ENetDiscover", NULL);
}

// The thread ended (what it had open closed), and waited for (up to a tenth of a second). Only from
// the game's side, never under the lock.
static void stdComm_ENetDiscover_StopThread(void)
{
    if (!stdComm_ENetDiscover_thread) return;
    stdComm_ENetDiscover_bRun = 0;
    int ret;
    SDL_WaitThread(stdComm_ENetDiscover_thread, &ret);
    stdComm_ENetDiscover_thread = NULL;
}

// Neither advertising nor browsing now: the thread ended (started again when asked for)
static void stdComm_ENetDiscover_StopIfIdle(void)
{
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    const int bIdle = !stdComm_ENetDiscover_bWantAdvertise && !stdComm_ENetDiscover_bWantBrowse;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
    if (bIdle) stdComm_ENetDiscover_StopThread();
}

// ------------------------------------------------------------------ the game's side

int stdComm_ENetDiscover_IsAvailable(void)
{
    return 1;
}

int stdComm_ENetDiscover_IsBlocked(void)
{
    return stdComm_ENetDiscover_bBlocked;
}

void stdComm_ENetDiscover_Advertise(const stdCommENetDiscoverGame* pGame, int port)
{
    stdComm_ENetDiscover_EnsureThread();
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    if (!stdComm_ENetDiscover_bWantAdvertise || port != stdComm_ENetDiscover_minePort
        || memcmp(pGame, &stdComm_ENetDiscover_mine, sizeof(*pGame))) {
        stdComm_ENetDiscover_mine = *pGame;
        stdComm_ENetDiscover_minePort = port;
        stdComm_ENetDiscover_bWantAdvertise = 1;
        stdComm_ENetDiscover_advertiseSerial++;
    }
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
}

void stdComm_ENetDiscover_StopAdvertising(void)
{
    if (!stdComm_ENetDiscover_mutex) return;
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    stdComm_ENetDiscover_bWantAdvertise = 0;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
    stdComm_ENetDiscover_StopIfIdle();
}

void stdComm_ENetDiscover_StartBrowsing(const char* game)
{
    stdComm_ENetDiscover_EnsureThread();
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    snprintf(stdComm_ENetDiscover_browseGame, sizeof(stdComm_ENetDiscover_browseGame), "%s", game ? game : "jk");
    stdComm_ENetDiscover_bWantBrowse = 1;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
}

void stdComm_ENetDiscover_StopBrowsing(void)
{
    if (!stdComm_ENetDiscover_mutex) return;
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    stdComm_ENetDiscover_bWantBrowse = 0;
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
    stdComm_ENetDiscover_StopIfIdle();
}

int stdComm_ENetDiscover_GetGames(stdCommENetDiscoverGame* aOut, int max)
{
    if (!stdComm_ENetDiscover_mutex) return 0;
    int n = 0;
    SDL_LockMutex(stdComm_ENetDiscover_mutex);
    for (int i = 0; i < STDCOMM_ENETDISCOVER_MAX_GAMES && n < max; i++) {
        const stdCommENetDiscoverSeen* s = &stdComm_ENetDiscover_aSeen[i];
        if (!s->bUsed || !s->bResolved) continue;
        // (one host heard on two networks: listed once)
        int bDup = 0;
        for (int k = 0; k < n && !bDup; k++)
            bDup = s->game.hostId[0] ? !strcmp(aOut[k].hostId, s->game.hostId) : !strcmp(aOut[k].address, s->game.address);
        if (!bDup) aOut[n++] = s->game;
    }
    SDL_UnlockMutex(stdComm_ENetDiscover_mutex);
    return n;
}

void stdComm_ENetDiscover_Shutdown(void)
{
    if (!stdComm_ENetDiscover_thread) return;
    stdComm_ENetDiscover_StopThread();
    stdComm_ENetDiscover_bWantAdvertise = 0;
    stdComm_ENetDiscover_bWantBrowse = 0;
}

#else // !STDCOMM_ENETDISCOVER_BONJOUR

// (no Bonjour here: nothing is found, and an address is typed)
int stdComm_ENetDiscover_IsAvailable(void) { return 0; }
int stdComm_ENetDiscover_IsBlocked(void) { return 0; }
void stdComm_ENetDiscover_Advertise(const stdCommENetDiscoverGame* pGame, int port) { (void)pGame; (void)port; }
void stdComm_ENetDiscover_StopAdvertising(void) {}
void stdComm_ENetDiscover_StartBrowsing(const char* game) { (void)game; }
void stdComm_ENetDiscover_StopBrowsing(void) {}
int stdComm_ENetDiscover_GetGames(stdCommENetDiscoverGame* aOut, int max) { (void)aOut; (void)max; return 0; }
void stdComm_ENetDiscover_Shutdown(void) {}

#endif // STDCOMM_ENETDISCOVER_BONJOUR

#endif // PLATFORM_ENET

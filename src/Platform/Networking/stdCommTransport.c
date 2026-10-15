// Added: entire file. See stdCommTransport.h: the game's one connection (Win95/stdComm.h), passed
// to the transport of the game in progress (ENet, GNS), and finding a game at an address over all
// of them at once.

#include "stdCommTransport.h"

#ifdef STDCOMM_TRANSPORTS

#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"

#include "SDL2_helper.h"

#include <stdio.h>
#include <string.h>

extern char16_t jkGuiMultiplayer_ipText[256];

static const stdCommTransport* const stdCommTransport_apAll[STDCOMM_TRANSPORT_NUM] = {
#ifdef PLATFORM_ENET
    &stdComm_ENet_transport,
#else
    NULL,
#endif
#ifdef PLATFORM_GNS
    &stdComm_GNS_transport,
#else
    NULL,
#endif
};

// (hosting, and anything before a game: ENet where it is built in)
#ifdef PLATFORM_ENET
#define STDCOMM_TRANSPORT_DEFAULT STDCOMM_TRANSPORT_ENET
#else
#define STDCOMM_TRANSPORT_DEFAULT STDCOMM_TRANSPORT_GNS
#endif

static int stdCommTransport_joinKind = STDCOMM_TRANSPORT_DEFAULT;
static int stdCommTransport_activeKind = STDCOMM_TRANSPORT_NONE;

static const stdCommTransport* stdCommTransport_Get(int kind)
{
    if (kind >= 0 && kind < STDCOMM_TRANSPORT_NUM && stdCommTransport_apAll[kind])
        return stdCommTransport_apAll[kind];
    return stdCommTransport_apAll[STDCOMM_TRANSPORT_DEFAULT];
}

// The game in progress's (the default's if none)
static const stdCommTransport* stdCommTransport_Current(void)
{
    return stdCommTransport_Get(stdCommTransport_activeKind);
}

void stdCommTransport_SetJoinKind(int kind)
{
    if (kind >= 0 && kind < STDCOMM_TRANSPORT_NUM && stdCommTransport_apAll[kind])
        stdCommTransport_joinKind = kind;
    else
        stdCommTransport_joinKind = STDCOMM_TRANSPORT_DEFAULT;
}

int stdCommTransport_GetJoinKind(void)
{
    return stdCommTransport_joinKind;
}

int stdCommTransport_GetActiveKind(void)
{
    return stdCommTransport_activeKind;
}

// (only where ENet is built in too: a build with GNS alone plays its own games over it)
int stdCommTransport_IsClassic(void)
{
#ifdef PLATFORM_ENET
    return stdCommTransport_activeKind == STDCOMM_TRANSPORT_GNS;
#else
    return 0;
#endif
}

int stdCommTransport_CanJoin(int kind, const jkMultiEntry* pEntry, const char** ppWhyKey)
{
    if (ppWhyKey) *ppWhyKey = NULL;
    // (co-op over a classic host: its co-op isn't this port's, and this port's isn't its)
    if (stdCommTransport_apAll[STDCOMM_TRANSPORT_ENET] && kind == STDCOMM_TRANSPORT_GNS && pEntry && (pEntry->multiModeFlags & MULTIMODEFLAG_COOP)) {
        if (ppWhyKey) *ppWhyKey = "GUIEXT_MP_ERR_CLASSIC_COOP";
        return 0;
    }
    return 1;
}

// ------------------------------------------------------------------ asking an address

// One transport asking, on a thread of its own
typedef struct stdCommTransportAsk
{
    const stdCommTransport* pTransport;
    char address[256];
    int timeoutMs;
    volatile int* pbKeepGoing; // (shared by all the asking: 0 once one has answered, or the caller gave up)
    jkMultiEntry entry;
    volatile int result;
    volatile int bDone;
    SDL_Thread* pThread;
} stdCommTransportAsk;

static int stdCommTransport_AskThread(void* pArg)
{
    stdCommTransportAsk* pAsk = (stdCommTransportAsk*)pArg;
    memset(&pAsk->entry, 0, sizeof(pAsk->entry));
    pAsk->result = pAsk->pTransport->ProbeAddress(pAsk->address, &pAsk->entry, pAsk->timeoutMs, pAsk->pbKeepGoing);
    pAsk->bDone = 1;
    return 0;
}

int stdCommTransport_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing, int* pKindOut)
{
    if (pKindOut) *pKindOut = STDCOMM_TRANSPORT_NONE;

    int numTransports = 0, onlyKind = STDCOMM_TRANSPORT_NONE;
    for (int k = 0; k < STDCOMM_TRANSPORT_NUM; k++) {
        if (stdCommTransport_apAll[k]) {
            numTransports++;
            onlyKind = k;
        }
    }
    if (numTransports == 1) {
        const int r = stdCommTransport_apAll[onlyKind]->ProbeAddress(pAddress, pOut, timeoutMs, pbKeepGoing);
        if (r > 0 && pKindOut) *pKindOut = onlyKind;
        return r;
    }

    // (all at once: a host answers on its own transport only, so the others wait out the time;
    // the first answer stops them)
    volatile int bKeepGoing = 1;
    stdCommTransportAsk aAsk[STDCOMM_TRANSPORT_NUM];
    memset(aAsk, 0, sizeof(aAsk));
    for (int k = 0; k < STDCOMM_TRANSPORT_NUM; k++) {
        stdCommTransportAsk* a = &aAsk[k];
        a->pTransport = stdCommTransport_apAll[k];
        if (!a->pTransport) continue;
        snprintf(a->address, sizeof(a->address), "%s", pAddress ? pAddress : "");
        a->timeoutMs = timeoutMs;
        a->pbKeepGoing = &bKeepGoing;
        a->result = 0;
        a->bDone = 0;
        a->pThread = SDL_CreateThread(stdCommTransport_AskThread, "stdCommTransport_Ask", a);
        if (!a->pThread) stdCommTransport_AskThread(a); // (no thread: asked here, one after the other)
    }
    for (;;) {
        if (pbKeepGoing && !*pbKeepGoing) bKeepGoing = 0;
        int bAllDone = 1;
        for (int k = 0; k < STDCOMM_TRANSPORT_NUM; k++) {
            const stdCommTransportAsk* a = &aAsk[k];
            if (!a->pThread) continue;
            if (!a->bDone) bAllDone = 0;
            else if (a->result > 0) bKeepGoing = 0;
        }
        if (bAllDone) break;
        SDL_Delay(10);
    }
    for (int k = 0; k < STDCOMM_TRANSPORT_NUM; k++) {
        if (!aAsk[k].pThread) continue;
        int unused;
        SDL_WaitThread(aAsk[k].pThread, &unused);
        aAsk[k].pThread = NULL;
    }
    // (ENet's answer first, if both answered)
    int result = 0, bAnyAddress = 0;
    for (int k = 0; k < STDCOMM_TRANSPORT_NUM; k++) {
        const stdCommTransportAsk* a = &aAsk[k];
        if (!a->pTransport) continue;
        if (a->result >= 0) bAnyAddress = 1;
        if (a->result > 0 && result <= 0) {
            *pOut = a->entry;
            if (pKindOut) *pKindOut = k;
            result = 1;
        }
    }
    if (result <= 0) result = bAnyAddress ? 0 : -1;
    return result;
}

int stdCommTransport_CheckPassword(int kind, const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing)
{
    return stdCommTransport_Get(kind)->CheckPassword(pAddress, pwPassword, timeoutMs, pbKeepGoing);
}

// ------------------------------------------------------------------ the game's side (Win95/stdComm.h)

int stdComm_OpenConnection(void* a)
{
    (void)a;
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

// Joining the game found at the address typed, over the transport it answered on
int stdComm_Open(int idx, char16_t* pwPassword)
{
    DirectPlay_EnumSessions2();

    stdComm_dword_8321E8 = 0;
    stdComm_dword_8321E0 = 1;
    stdComm_dplayIdSelf = DirectPlay_CreatePlayer(jkPlayer_playerShortName, 0);
    stdComm_bIsServer = 0;
    jkGuiMultiplayer_checksumSeed = jkGuiMultiplayer_aEntries[idx].checksumSeed;

    const int kind = stdCommTransport_apAll[stdCommTransport_joinKind] ? stdCommTransport_joinKind : STDCOMM_TRANSPORT_DEFAULT;
    stdPlatform_Printf("Joining over %s\n", stdCommTransport_Get(kind)->pName);
    const int r = stdCommTransport_Get(kind)->Join(pwPassword);
    if (!r) stdCommTransport_activeKind = kind;
    return r;
}

void stdComm_Close()
{
    stdCommTransport_Current()->Close();
    stdCommTransport_activeKind = STDCOMM_TRANSPORT_NONE;
}

int DirectPlay_Receive(int *pIdOut, int *pMsgIdOut, int *pLenOut)
{
    return stdCommTransport_Current()->Receive(pIdOut, pMsgIdOut, pLenOut);
}

BOOL DirectPlay_Send(DPID idFrom, DPID idTo, void *lpData, DWORD dwDataSize)
{
    return stdCommTransport_Current()->Send(idFrom, idTo, lpData, dwDataSize);
}

int DirectPlay_SendLobbyMessage(void* pPkt, uint32_t pktLen)
{
    (void)pPkt; (void)pktLen;
    return 1;
}

void DirectPlay_SetSessionDesc(const char* a1, DWORD maxPlayers)
{
    (void)a1; (void)maxPlayers;
    stdCommTransport_Current()->SetSessionLevel();
}

BOOL DirectPlay_SetSessionFlagidk(int a1)
{
    (void)a1;
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
    (void)pwIdk; (void)pwPlayerName;
    return 0;
}

DPID DirectPlay_CreatePlayer(char16_t* pwIdk, int idk2)
{
    (void)pwIdk; (void)idk2;
    return 1;
}

void DirectPlay_Close()
{
}

// Hosting: over the default transport (ENet)
int DirectPlay_OpenHost(jkMultiEntry* pEntry)
{
    const int r = stdCommTransport_Get(STDCOMM_TRANSPORT_DEFAULT)->OpenHost(pEntry);
    if (!r) stdCommTransport_activeKind = STDCOMM_TRANSPORT_DEFAULT;
    return r;
}

int DirectPlay_GetSession_passwordidk(jkMultiEntry* pEntry)
{
    stdCommTransport_Get(STDCOMM_TRANSPORT_DEFAULT)->SetSession(pEntry);
    return 1;
}

void DirectPlay_EnumPlayers(int a)
{
    (void)a;
    stdCommTransport_Current()->ResetClients();
}

int DirectPlay_StartSession(void* a, void* b)
{
    (void)a; (void)b;
    return 1;
}

void DirectPlay_Destroy()
{
}

int DirectPlay_IdkSessionDesc(jkMultiEntry* pEntry)
{
    (void)pEntry;
    return 1;
}

// ------------------------------------------------------------------ the old Join screen, and -join

// The game at the address typed (jkGuiMultiplayer_ipText), asked again whenever the address changes
// or the screen asks (stdComm_EnumSessions), on a thread until DirectPlay_EnumSessions2
static int stdCommTransport_enumForce = 0;
static volatile int stdCommTransport_bEnumRun = 0;
static SDL_Thread* stdCommTransport_enumThread = NULL;
static SDL_Mutex* stdCommTransport_enumMutex = NULL;
static char stdCommTransport_enumLastAddr[256];
static jkMultiEntry stdCommTransport_enumEntry;
static int stdCommTransport_enumNum = 0;
static int stdCommTransport_enumKind = STDCOMM_TRANSPORT_NONE;

static int stdCommTransport_EnumThread(void* unused)
{
    (void)unused;
    while (stdCommTransport_bEnumRun)
    {
        char text[256];
        stdString_WcharToChar(text, jkGuiMultiplayer_ipText, 255);
        text[255] = 0;
        SDL_LockMutex(stdCommTransport_enumMutex);
        const int bAgain = stdCommTransport_enumForce || strncmp(text, stdCommTransport_enumLastAddr, sizeof(text));
        stdCommTransport_enumForce = 0;
        SDL_UnlockMutex(stdCommTransport_enumMutex);
        if (bAgain) {
            jkMultiEntry entry;
            memset(&entry, 0, sizeof(entry));
            int kind = STDCOMM_TRANSPORT_NONE;
            const int r = stdCommTransport_ProbeAddress(text, &entry, 1500, &stdCommTransport_bEnumRun, &kind);
            SDL_LockMutex(stdCommTransport_enumMutex);
            if (r > 0) {
                stdCommTransport_enumEntry = entry;
                stdCommTransport_enumKind = kind;
                sithMulti_multiModeFlags = entry.multiModeFlags;
                sithNet_MultiModeFlags = entry.multiModeFlags;
                stdCommTransport_enumNum = 1;
            }
            else {
                stdCommTransport_enumNum = 0;
            }
            snprintf(stdCommTransport_enumLastAddr, sizeof(stdCommTransport_enumLastAddr), "%s", text);
            SDL_UnlockMutex(stdCommTransport_enumMutex);
            // (the Join screen redrawn)
            jkGuiMultiplayer_dword_5564E8 -= 10000;
        }
        SDL_Delay(100);
    }
    return 0;
}

int DirectPlay_EnumSessions2()
{
    if (!stdCommTransport_bEnumRun) return 0;
    stdCommTransport_bEnumRun = 0;
    int ret;
    SDL_WaitThread(stdCommTransport_enumThread, &ret);
    stdCommTransport_enumThread = NULL;
    return 0;
}

int stdComm_EnumSessions(int a, void* b)
{
    (void)a; (void)b;
    if (!stdCommTransport_enumMutex) stdCommTransport_enumMutex = SDL_CreateMutex();

    SDL_LockMutex(stdCommTransport_enumMutex);
    stdCommTransport_enumForce = 1;
    jkGuiMultiplayer_aEntries[0] = stdCommTransport_enumEntry;
    dplay_dword_55D618 = stdCommTransport_enumNum;
    // (what joining it goes over)
    if (stdCommTransport_enumNum) stdCommTransport_SetJoinKind(stdCommTransport_enumKind);
    SDL_UnlockMutex(stdCommTransport_enumMutex);

    if (stdCommTransport_bEnumRun) return 0;

    stdCommTransport_Current()->ResetClients();
    SDL_LockMutex(stdCommTransport_enumMutex);
    dplay_dword_55D618 = 0;
    stdCommTransport_enumNum = 0;
    stdCommTransport_enumKind = STDCOMM_TRANSPORT_NONE;
    memset(&stdCommTransport_enumEntry, 0, sizeof(stdCommTransport_enumEntry));
    memset(&jkGuiMultiplayer_aEntries[0], 0, sizeof(jkGuiMultiplayer_aEntries[0]));
    memset(stdCommTransport_enumLastAddr, 0, sizeof(stdCommTransport_enumLastAddr));
    SDL_UnlockMutex(stdCommTransport_enumMutex);
    stdCommTransport_bEnumRun = 1;
    stdCommTransport_enumThread = SDL_CreateThread(stdCommTransport_EnumThread, "stdCommTransport_Enum", NULL);
    if (!stdCommTransport_enumThread) stdCommTransport_bEnumRun = 0;
    return 0;
}

#endif // STDCOMM_TRANSPORTS

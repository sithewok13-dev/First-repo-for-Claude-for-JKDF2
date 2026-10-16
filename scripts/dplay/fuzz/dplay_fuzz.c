// A fuzz harness for the DirectPlay transport's readers (src/Platform/Networking/DPlay): the real
// source, compiled on its own with the engine's few functions it calls stubbed (STDCOMM_DPLAY_FUZZ),
// fed (for libFuzzer, or by dplay_fuzz_main.c) to SUPERENUMPLAYERSREPLY, CREATEPLAYER and, as a
// host, a CREATEPLAYER passed on. Build: build_fuzz.sh.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "types.h"
#include "Platform/Networking/stdCommTransport.h"
#include "Platform/Networking/DPlay/stdComm_DPlayNat.h"

// ---- what the engine and SDL would give
typedef struct SDL_Mutex SDL_Mutex;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Event { uint32_t type; } SDL_Event;
enum { SDL_EVENT_WILL_ENTER_BACKGROUND = 0x103, SDL_EVENT_DID_ENTER_FOREGROUND = 0x106 };
static SDL_Mutex* SDL_CreateMutex(void) { return (SDL_Mutex*)1; }
static void SDL_LockMutex(SDL_Mutex* m) { (void)m; }
static void SDL_UnlockMutex(SDL_Mutex* m) { (void)m; }
static SDL_Thread* SDL_CreateThread(int (*f)(void*), const char* n, void* p) { (void)f; (void)n; (void)p; return NULL; }
static void SDL_WaitThread(SDL_Thread* t, int* r) { (void)t; (void)r; }
static void SDL_DetachThread(SDL_Thread* t) { (void)t; }
static void SDL_Delay(uint32_t ms) { (void)ms; }
static bool SDL_AddEventWatch(bool (*f)(void*, SDL_Event*), void* u) { (void)f; (void)u; return true; }
static void SDL_RemoveEventWatch(bool (*f)(void*, SDL_Event*), void* u) { (void)f; (void)u; }
static int stdPlatform_Printf(const char* f, ...) { (void)f; return 0; }
static uint32_t stdPlatform_GetTimeMsec(void) { static uint32_t t = 1000; return t += 3; }
static void stdString_CharToWchar(char16_t* d, const char* s, int n) { int i = 0; for (; i < n && s[i]; i++) d[i] = (unsigned char)s[i]; if (i < n) d[i] = 0; }
static void stdString_WcharToChar(char* d, const char16_t* s, int n) { int i = 0; for (; i < n && s[i]; i++) d[i] = (char)s[i]; if (i < n) d[i] = 0; }
static char* _strncpy(char* d, const char* s, size_t n) { return strncpy(d, s, n); }
int Main_bVerboseNetworking = 1;
static sithDplayPlayer DirectPlay_aPlayers[32];
static int DirectPlay_numPlayers, stdComm_dplayIdSelf, stdComm_bIsServer, jkPlayer_maxPlayers;
static char16_t jkPlayer_playerShortName[32];
static char jkMain_aLevelJklFname[128];
void stdCommTransport_GateOpen(stdCommProbeGate* g) { (void)g; }
void stdCommTransport_GateClose(stdCommProbeGate* g, int t) { (void)g; (void)t; }
int stdCommTransport_GateEnter(stdCommProbeGate* g) { (void)g; return 1; }
void stdCommTransport_GateLeave(stdCommProbeGate* g) { (void)g; }
int stdCommTransport_SplitAddress(const char* t, char* h, int hl, int* p, int d) { snprintf(h, hl, "%s", t); *p = d; return 1; }
int stdComm_DPlayNat_IsPrivate(uint32_t ip) { const uint32_t h = __builtin_bswap32(ip); return (h >> 24) == 10 || (h >> 24) == 127 || (h >> 16) == 0xC0A8 || !h; }
uint32_t stdComm_DPlayNat_LocalAddressToward(uint32_t ip) { (void)ip; return 0; }
int stdComm_DPlayNat_Map(uint16_t a, uint16_t b, stdCommDPlayNatMapping* o) { (void)a; (void)b; memset(o, 0, sizeof(*o)); return 0; }
void stdComm_DPlayNat_Unmap(void) {}
const char* stdComm_DPlayNat_Problem(void) { return ""; }

#define STDCOMM_DPLAY_FUZZ
#include "Platform/Networking/DPlay/stdComm_DPlay.c"

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (n < 2 || n > 4000) return 0;
    memset(stdComm_DPlay_aPlayers, 0, sizeof(stdComm_DPlay_aPlayers));
    dp_queueClear();
    dp_outClear();
    stdComm_DPlay_bServer = 0;
    const int sel = d[0] % 3;
    d++; n--;
    uint8_t* b = (uint8_t*)malloc(n);
    memcpy(b, d, n);
    if (sel == 0) dp_parseSuperEnum(b, (int)n, 0x0100007f);
    else if (sel == 1) dp_parseCreatePlayer(b, (int)n, 0x0100007f);
    else {
        stdComm_DPlay_bServer = 1;
        if (n >= 20) { dp_parseCreatePlayer(b + 20, (int)n - 20, 1); dp_forward(b, (int)n, 1, 1); }
        if (n >= 28) dp_systemMessage(b, n, 0x0100007f); // (the dispatcher too, as a player and as a host)
        stdComm_DPlay_bServer = 0;
        if (n >= 28) dp_systemMessage(b, n, 0x0100007f);
    }
    free(b);
    return 0;
}

int fwd_only(uint8_t* b, int n)
{
    stdComm_DPlay_bServer = 1;
    dp_forward(b, n, 0x01020304, 1);
    stdComm_DPlay_bServer = 0;
    return 0;
}

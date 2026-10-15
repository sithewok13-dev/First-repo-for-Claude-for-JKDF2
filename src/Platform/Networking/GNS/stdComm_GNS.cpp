#include "stdComm_GNS.h"
#include "Platform/Networking/stdCommTransport.h"

#include "Win95/stdComm.h"
#include "Dss/sithMulti.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "jk.h"

#include "SDL2_helper.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <cctype>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingmessages.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h> // Ug, for NukeProcess -- see below
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <sys/ioctl.h>
    typedef int SOCKET;
    constexpr SOCKET INVALID_SOCKET = -1;
    #include <signal.h>
    #include <dlfcn.h>
#endif

#define sithDplayGNS_infoPrintf(fmt, ...) stdPlatform_Printf(fmt, ##__VA_ARGS__)
#define sithDplayGNS_verbosePrintf(fmt, ...) if (Main_bVerboseNetworking) \
    { \
        stdPlatform_Printf(fmt, ##__VA_ARGS__);  \
    } \
    ;

extern "C"
{
static void stdComm_GNS_ResetClients(void);

#pragma pack(push, 4)
typedef struct GNSInfoPacket
{
    int id;
    jkMultiEntry entry;
} GNSInfoPacket;
#pragma pack(pop)

static jkMultiEntry sithDplayGNS_storedEntry;
extern char16_t jkGuiMultiplayer_ipText[256];
extern int Main_bVerboseNetworking;

typedef bool (*GameNetworkingSockets_Init_t)( const SteamNetworkingIdentity *pIdentity, SteamNetworkingErrMsg &errMsg );
typedef ISteamNetworkingUtils* (*SteamNetworkingUtils_t)(void);
typedef void (*GameNetworkingSockets_Kill_t)(void);
typedef ISteamNetworkingSockets* (*SteamNetworkingSockets_t)(void);
typedef void (*SteamNetworkingIPAddr_ToString_t)( const SteamNetworkingIPAddr *pAddr, char *buf, size_t cbBuf, bool bWithPort );
typedef bool (*SteamNetworkingIPAddr_ParseString_t)( SteamNetworkingIPAddr *pAddr, const char *pszStr );

GameNetworkingSockets_Init_t g_GameNetworkingSockets_Init = NULL;
SteamNetworkingUtils_t g_SteamNetworkingUtils = NULL;
GameNetworkingSockets_Kill_t g_GameNetworkingSockets_Kill = NULL;
SteamNetworkingSockets_t g_SteamNetworkingSockets = NULL;
SteamNetworkingIPAddr_ToString_t g_SteamNetworkingIPAddr_ToString = NULL;
SteamNetworkingIPAddr_ParseString_t g_SteamNetworkingIPAddr_ParseString = NULL;

}
/////////////////////////////////////////////////////////////////////////////
//
// Common stuff
//
/////////////////////////////////////////////////////////////////////////////

bool g_bQuit = false;

SteamNetworkingMicroseconds g_logTimeZero;

#ifdef WIN32
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>   // for nanosleep
#endif

void sleep_ms(int milliseconds){ // cross-platform sleep function
    SDL_Delay(milliseconds);
}

// We do this because I won't want to figure out how to cleanly shut
// down the thread that is reading from stdin.
static void NukeProcess( int rc )
{
    #ifdef _WIN32
        ExitProcess( rc );
    #else
        (void)rc; // Unused formal parameter
        kill( getpid(), SIGKILL );
    #endif
}

static void DebugOutput( ESteamNetworkingSocketsDebugOutputType eType, const char *pszMsg )
{
    //SteamNetworkingMicroseconds time = g_SteamNetworkingUtils()->GetLocalTimestamp() - g_logTimeZero;
    //sithDplayGNS_verbosePrintf( "%10.6f %s\n", time*1e-6, pszMsg );
    sithDplayGNS_verbosePrintf( "%s\n", pszMsg );
    fflush(stdout);
    if ( eType == k_ESteamNetworkingSocketsDebugOutputType_Bug )
    {
        fflush(stdout);
        fflush(stderr);
        //NukeProcess(1);
    }
}

static void FatalError( const char *fmt, ... )
{
    char text[ 2048 ];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( text, sizeof(text), fmt, ap );
    va_end(ap);
    char *nl = strchr( text, '\0' ) - 1;
    if ( nl >= text && *nl == '\n' )
        *nl = '\0';
    DebugOutput( k_ESteamNetworkingSocketsDebugOutputType_Bug, text );
}

static void Printf( const char *fmt, ... )
{
    char text[ 2048 ];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( text, sizeof(text), fmt, ap );
    va_end(ap);
    char *nl = strchr( text, '\0' ) - 1;
    if ( nl >= text && *nl == '\n' )
        *nl = '\0';
    DebugOutput( k_ESteamNetworkingSocketsDebugOutputType_Msg, text );
}

static int stdComm_GNS_bInitted = 0;
static int stdComm_GNS_bForceStubs = 0;
static int stdComm_GNS_bSymbolsLoaded = 0;

static void stdComm_GNS_LoadSymbols()
{
    if (stdComm_GNS_bSymbolsLoaded) return;

    static const char pszExportFunc[] = "GameNetworkingSockets_Init";

    #if defined(STDCOMM_GNS_STATIC)
        // Added: linked into the program itself (iOS, where an app can't load a library of its
        // own at run time)
        static const char pszModule[] = "GameNetworkingSockets (built in)";
        g_GameNetworkingSockets_Init = GameNetworkingSockets_Init;
        g_SteamNetworkingUtils = SteamNetworkingUtils_LibV4;
        g_GameNetworkingSockets_Kill = GameNetworkingSockets_Kill;
        g_SteamNetworkingSockets = SteamNetworkingSockets_LibV12;
        g_SteamNetworkingIPAddr_ToString = SteamNetworkingIPAddr_ToString;
        g_SteamNetworkingIPAddr_ParseString = SteamNetworkingIPAddr_ParseString;
    #elif defined( WIN32 )
        static const char pszModule[] = "libGameNetworkingSockets.dll";
        HMODULE h = ::LoadLibraryA( pszModule );
        if ( h == NULL )
        {
            stdPlatform_Printf("Failed to load %s.\n", pszModule );
            stdComm_GNS_bForceStubs = 1;
            
            return;
        }
        g_GameNetworkingSockets_Init = (GameNetworkingSockets_Init_t)::GetProcAddress(h, "GameNetworkingSockets_Init");
        g_SteamNetworkingUtils = (SteamNetworkingUtils_t)::GetProcAddress(h, "SteamNetworkingUtils_LibV4");
        g_GameNetworkingSockets_Kill = (GameNetworkingSockets_Kill_t)::GetProcAddress(h, "GameNetworkingSockets_Kill");
        g_SteamNetworkingSockets = (SteamNetworkingSockets_t)::GetProcAddress(h, "SteamNetworkingSockets_LibV12");
        g_SteamNetworkingIPAddr_ToString = (SteamNetworkingIPAddr_ToString_t)::GetProcAddress(h, "SteamNetworkingIPAddr_ToString");
        g_SteamNetworkingIPAddr_ParseString = (SteamNetworkingIPAddr_ParseString_t)::GetProcAddress(h, "SteamNetworkingIPAddr_ParseString");
    #elif defined(LINUX) | defined(MACOS)
        #if defined(MACOS)
            static const char pszModule[] = "libGameNetworkingSockets.dylib";
        #else
            static const char pszModule[] = "libGameNetworkingSockets.so";
        #endif
        void* h = dlopen(pszModule, RTLD_LAZY);
        if ( h == NULL )
        {
            stdPlatform_Printf("Failed to dlopen %s.  %s\n", pszModule, dlerror() );
            stdComm_GNS_bForceStubs = 1;
            return;
        }
        g_GameNetworkingSockets_Init = (GameNetworkingSockets_Init_t)dlsym(h, "GameNetworkingSockets_Init");
        g_SteamNetworkingUtils = (SteamNetworkingUtils_t)dlsym(h, "SteamNetworkingUtils_LibV4");
        g_GameNetworkingSockets_Kill = (GameNetworkingSockets_Kill_t)dlsym(h, "GameNetworkingSockets_Kill");
        g_SteamNetworkingSockets = (SteamNetworkingSockets_t)dlsym(h, "SteamNetworkingSockets_LibV12");
        g_SteamNetworkingIPAddr_ToString = (SteamNetworkingIPAddr_ToString_t)dlsym(h, "SteamNetworkingIPAddr_ToString");
        g_SteamNetworkingIPAddr_ParseString = (SteamNetworkingIPAddr_ParseString_t)dlsym(h, "SteamNetworkingIPAddr_ParseString");
    #else
        
    #endif

    if (!g_SteamNetworkingIPAddr_ToString || !g_GameNetworkingSockets_Init 
        || !g_GameNetworkingSockets_Kill || !g_SteamNetworkingSockets 
        || !g_SteamNetworkingIPAddr_ToString || !g_SteamNetworkingIPAddr_ParseString)
    {
        stdPlatform_Printf("Failed to load %s, reverting to stubs.\n", pszModule);
        stdComm_GNS_bForceStubs = 1;
        return;
    }
    stdPlatform_Printf("Loaded %s successfully.\n", pszModule);

    stdComm_GNS_bSymbolsLoaded = 1;
}

static void InitSteamDatagramConnectionSockets()
{
    if (stdComm_GNS_bInitted) return;

    stdComm_GNS_LoadSymbols();

    #ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
        SteamDatagramErrMsg errMsg;
        if ( !g_GameNetworkingSockets_Init( nullptr, errMsg ) ) {
            FatalError( "GameNetworkingSockets_Init failed.  %s", errMsg );
            stdPlatform_Printf("GNS: failed to start (%s)\n", errMsg);
            stdComm_GNS_bForceStubs = 1; // Added: no GNS rather than a crash later
            return;
        }
    #else
        SteamDatagram_SetAppID( 570 ); // Just set something, doesn't matter what
        SteamDatagram_SetUniverse( false, k_EUniverseDev );

        SteamDatagramErrMsg errMsg;
        if ( !SteamDatagramClient_Init( errMsg ) )
            FatalError( "SteamDatagramClient_Init failed.  %s", errMsg );

        // Disable authentication when running with Steam, for this
        // example, since we're not a real app.
        //
        // Authentication is disabled automatically in the open-source
        // version since we don't have a trusted third party to issue
        // certs.
        g_SteamNetworkingUtils()->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1 );
    #endif

    g_logTimeZero = g_SteamNetworkingUtils()->GetLocalTimestamp();

    // Added: a player whose game has gone away (closed, crashed, out of Wi-Fi)
    // is dropped after 8 s rather than the library's 10 s plus retries
    g_SteamNetworkingUtils()->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_TimeoutConnected, 8000 );

    g_SteamNetworkingUtils()->SetDebugOutputFunction( k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput );
    stdComm_GNS_bInitted = 1;
}

static void ShutdownSteamDatagramConnectionSockets()
{
    if (!stdComm_GNS_bInitted) return;

    // Give connections time to finish up.  This is an application layer protocol
    // here, it's not TCP.  Note that if you have an application and you need to be
    // more sure about cleanup, you won't be able to do this.  You will need to send
    // a message and then either wait for the peer to close the connection, or
    // you can pool the connection to see if any reliable data is pending.
    sleep_ms( 500 );

    #ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
        g_GameNetworkingSockets_Kill();
    #else
        SteamDatagramClient_Kill();
    #endif

    stdComm_GNS_bInitted = 0;
}


/////////////////////////////////////////////////////////////////////////////
//
// GNSServer
//
/////////////////////////////////////////////////////////////////////////////

class GNSServer
{
public:
    void Init( uint16 nPort )
    {
        // Select instance to use.  For now we'll always use the default.
        // But we could use SteamGameServerNetworkingSockets() on Steam.
        m_pInterface = g_SteamNetworkingSockets();

        // Start listening
        SteamNetworkingIPAddr serverLocalAddr;
        serverLocalAddr.Clear();
        serverLocalAddr.m_port = nPort;
        SteamNetworkingConfigValue_t opt;
        opt.SetPtr( k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback );
        //opt.SetPtr( k_ESteamNetworkingConfig_Callback_CreateConnectionSignaling, (void*)SteamNetCreateConnectionSignalingCallback);
        m_hListenSock = m_pInterface->CreateListenSocketIP( serverLocalAddr, 1, &opt );
        if ( m_hListenSock == k_HSteamListenSocket_Invalid )
            Printf( "[1] Failed to listen on port %d", nPort );
        m_hPollGroup = m_pInterface->CreatePollGroup();
        if ( m_hPollGroup == k_HSteamNetPollGroup_Invalid )
            Printf( "[2] Failed to listen on port %d", nPort );
        Printf( "Server listening on port %d\n", nPort );

        //m_pBcastInterface = SteamNetworkingMessages();

        m_identity.Clear();
        m_identity.SetGenericString("OpenJKDF2");

        id = 1;
        availableIds = 0x1;
        // Drop any disconnect events left over from a previous session. Otherwise a
        // stale id (e.g. a client that dis/reconnected last game) gets replayed during
        // the next host's join handshake, which aborts the new client's sync (matching
        // sithMulti_sendto_id) and removes them -> they time out and never appear.
        std::queue<int>().swap(m_DisconnectedPeers);
    }

    void Shutdown()
    {
        // Close all the connections
        Printf( "Closing connections...\n" );
        for ( auto it: m_mapClients )
        {
            // TODO: Send a proper shutdown message

            // Close the connection.  We use "linger mode" to ask SteamNetworkingSockets
            // to flush this out and close gracefully.
            m_pInterface->CloseConnection( it.first, 0, "Server Shutdown", true );
        }
        m_mapClients.clear();

        m_pInterface->CloseListenSocket( m_hListenSock );
        m_hListenSock = k_HSteamListenSocket_Invalid;

        m_pInterface->DestroyPollGroup( m_hPollGroup );
        m_hPollGroup = k_HSteamNetPollGroup_Invalid;

        availableIds = 0x1;
        std::queue<int>().swap(m_DisconnectedPeers);
    }

    void RunStep()
    {
        //printf("Server runstep\n");
        //PollIncomingMessages();
        PollConnectionStateChanges();
        //TickBroadcastOut();
    }

    void Run()
    {
        while ( !g_bQuit )
        {
            RunStep();
            sleep_ms(10);
        }

        Shutdown();
    }

    int Receive(int *pIdOut, void *pMsg, int *pLenInOut)
    {
        int maxLen = *pLenInOut;
        *pIdOut = 0;
        *pLenInOut = 0;

        if (m_DisconnectedPeers.size())
        {
            int dis_id = m_DisconnectedPeers.front();
            m_DisconnectedPeers.pop();

            *pIdOut = dis_id;

            return 2;
        }

        ISteamNetworkingMessage *pIncomingMsg = nullptr;
        int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup( m_hPollGroup, &pIncomingMsg, 1 );
        if ( numMsgs == 0 )
            return -1;
        if ( numMsgs < 0 ) {
            sithDplayGNS_infoPrintf( "Error checking for messages (%d)\n", numMsgs);
            return -1;
        }
        assert( numMsgs == 1 && pIncomingMsg );
        auto itClient = m_mapClients.find( pIncomingMsg->m_conn );
        assert( itClient != m_mapClients.end() );

        if (pIncomingMsg->m_cbSize < 8) {
            sithDplayGNS_infoPrintf("Bad packet size %x\n", pIncomingMsg->m_cbSize);
            pIncomingMsg->Release();
            return -1;
        }

        uint8_t* dataBuf = (uint8_t*)pIncomingMsg->m_pData;
        int idFrom = *(uint32_t*)&dataBuf[0];//itClient->second.m_id;
        int idTo = *(uint32_t*)&dataBuf[4];
        *pIdOut = idFrom;

        // If we get a packet intended for another client, forward it to them.
        if (idTo && idTo != id)
        {
            Send(idFrom, idTo, &dataBuf[8], pIncomingMsg->m_cbSize-8);
            pIncomingMsg->Release();

            *pLenInOut = maxLen;
            return Receive(pIdOut, pMsg, pLenInOut);
        }

        int outsize = maxLen;
        if (outsize > pIncomingMsg->m_cbSize-8)
            outsize = pIncomingMsg->m_cbSize-8;

        memcpy(pMsg, &dataBuf[8], outsize);
        *pLenInOut = outsize;

        sithDplayGNS_verbosePrintf("Recv %x bytes from %x %x (%s, %x)\n", outsize, idFrom, idTo, sithDSS_IdToStr(*(uint32_t*)pMsg & 0xFF), *(uint32_t*)pMsg);

        // We don't need this anymore.
        pIncomingMsg->Release();

        return 0;
    }

    int Send(uint32_t idFrom, uint32_t idTo, void *lpData, uint32_t dwDataSize)
    {
        if (dwDataSize > 4096-8) dwDataSize = 4096-8;
        *(uint32_t*)&sendBuffer[0] = idFrom;
        *(uint32_t*)&sendBuffer[4] = idTo;

        memcpy(&sendBuffer[8], lpData, dwDataSize);

        HSteamNetConnection except = k_HSteamNetConnection_Invalid;
        for ( auto &c: m_mapClients )
        {
            if ( c.first != except && c.second.m_id == idTo ) {
                sithDplayGNS_verbosePrintf("Sent %x bytes to %x (%s, %x)\n", dwDataSize+8, idTo, sithDSS_IdToStr(*(uint32_t*)lpData & 0xFF), *(uint32_t*)lpData);
                SendBytesToClient( c.first, sendBuffer, dwDataSize+8 );
            }
        }

        return 1;
    }

    uint32_t id;
private:

    HSteamListenSocket m_hListenSock;
    HSteamNetPollGroup m_hPollGroup;
    ISteamNetworkingSockets *m_pInterface;
    //ISteamNetworkingMessages *m_pBcastInterface;
    uint64_t availableIds = 0x1;
    uint8_t sendBuffer[4096];
    uint8_t sendBuffer2[4096];
    SteamNetworkingIdentity m_identity;

    struct Client_t
    {
        uint32_t m_id;
        std::string m_sNick;
    };

    std::map< HSteamNetConnection, Client_t > m_mapClients;
    std::queue<int> m_DisconnectedPeers;

    void SendBytesToClient( HSteamNetConnection conn, void *pData, uint32_t len)
    {
        m_pInterface->SendMessageToConnection( conn, pData, len, k_nSteamNetworkingSend_Reliable, nullptr );
    }

    void SendBytesToAllClients( void *pData, uint32_t len, HSteamNetConnection except = k_HSteamNetConnection_Invalid )
    {
        for ( auto &c: m_mapClients )
        {
            if ( c.first != except )
                SendBytesToClient( c.first, pData, len );
        }
    }

    void SetClientNick( HSteamNetConnection hConn, const char *nick )
    {

        // Remember their nick
        m_mapClients[hConn].m_sNick = nick;

        // Set the connection name, too, which is useful for debugging
        m_pInterface->SetConnectionName( hConn, nick );
    }

    void SetClientId( HSteamNetConnection hConn, int id )
    {
        m_mapClients[hConn].m_id = id;
    }

#if 0
    void TickBroadcastOut()
    {
        uint8_t tmp[8] = {0};
        m_pBcastInterface->SendMessageToUser(m_identity, tmp, 8, k_nSteamNetworkingSend_Unreliable, 1337);
    }
#endif

    void OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *pInfo )
    {
        char temp[1024];

        // What's the state of the connection?
        switch ( pInfo->m_info.m_eState )
        {
            case k_ESteamNetworkingConnectionState_None:
                // NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
                break;

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            {
                // Ignore if they were not previously connected.  (If they disconnected
                // before we accepted the connection.)
                if ( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected )
                {

                    // Locate the client.  Note that it should have been found, because this
                    // is the only codepath where we remove clients (except on shutdown),
                    // and connection change callbacks are dispatched in queue order.
                    auto itClient = m_mapClients.find( pInfo->m_hConn );
                    assert( itClient != m_mapClients.end() );

                    // Select appropriate log messages
                    const char *pszDebugLogAction;
                    if ( pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally )
                    {
                        pszDebugLogAction = "problem detected locally";
                        snprintf( temp, sizeof(temp), "Problem detected with client %x (%s)", itClient->second.m_id, pInfo->m_info.m_szEndDebug );
                    }
                    else
                    {
                        // Note that here we could check the reason code to see if
                        // it was a "usual" connection or an "unusual" one.
                        pszDebugLogAction = "closed by peer";
                        snprintf( temp, sizeof(temp), "Client id %x has left.", itClient->second.m_id );
                    }

                    // Spew something to our own log.  Note that because we put their nick
                    // as the connection description, it will show up, along with their
                    // transport-specific data (e.g. their IP address)
                    Printf( "Connection %s %s, reason %d: %s\n",
                        pInfo->m_info.m_szConnectionDescription,
                        pszDebugLogAction,
                        pInfo->m_info.m_eEndReason,
                        pInfo->m_info.m_szEndDebug
                    );

                    // Only send disconnect messages to fully connected clients.
                    for (int i = 0; i < jkPlayer_maxPlayers; i++)
                    {
                        if (!i && jkGuiNetHost_bIsDedicated) continue;

                        if ( (jkPlayer_playerInfos[i].flags & 2) != 0 && jkPlayer_playerInfos[i].playerNetId == itClient->second.m_id) {
                            m_DisconnectedPeers.push(itClient->second.m_id);
                            break;
                        }
                    }

                    availableIds &= ~(1ULL << (itClient->second.m_id-1));
                    m_mapClients.erase( itClient );
                }
                else
                {
                    assert( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting );
                }

                // Clean up the connection.  This is important!
                // The connection is "closed" in the network sense, but
                // it has not been destroyed.  We must close it on our end, too
                // to finish up.  The reason information do not matter in this case,
                // and we cannot linger because it's already closed on the other end,
                // so we just pass 0's.
                m_pInterface->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
                break;
            }

            case k_ESteamNetworkingConnectionState_Connecting:
            {
                // This must be a new connection
                assert( m_mapClients.find( pInfo->m_hConn ) == m_mapClients.end() );

                Printf( "Connection request from %s", pInfo->m_info.m_szConnectionDescription );

                // Don't accept if we can't allocate an ID
                if (ConnectedPlayers() >= 64) {
                    stdPlatform_Printf("Rejecting request too many players connected (%u)\n", ConnectedPlayers());
                    break;
                }

                // A client is attempting to connect
                // Try to accept the connection.
                if ( m_pInterface->AcceptConnection( pInfo->m_hConn ) != k_EResultOK )
                {
                    // This could fail.  If the remote host tried to connect, but then
                    // disconnected, the connection may already be half closed.  Just
                    // destroy whatever we have on our side.
                    m_pInterface->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
                    Printf( "Can't accept connection.  (It was already closed?)" );
                    break;
                }

                // Assign the poll group
                if ( !m_pInterface->SetConnectionPollGroup( pInfo->m_hConn, m_hPollGroup ) )
                {
                    m_pInterface->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
                    Printf( "Failed to set poll group?" );
                    break;
                }

                int nextId = 0;
                for (int i = 0; i < 64; i++)
                {
                    if (!(availableIds & (1ULL << i))) {
                        availableIds |= (1ULL << i);
                        nextId = i+1;
                        break;
                    }
                }

                sithDplayGNS_verbosePrintf("Assigning ID: %x\n", nextId);

                sithDplayGNS_storedEntry.multiModeFlags = sithMulti_multiModeFlags;

                GNSInfoPacket infoPkt = {0};
                infoPkt.id = nextId;
                infoPkt.entry = sithDplayGNS_storedEntry;
                infoPkt.entry.numPlayers = RealConnectedPlayers();
                infoPkt.entry.maxPlayers = sithDplayGNS_storedEntry.maxPlayers;

                jkPlayer_maxPlayers = sithDplayGNS_storedEntry.maxPlayers; // Hack?

                memcpy(sendBuffer2, &infoPkt, sizeof(infoPkt));
                SendBytesToClient( pInfo->m_hConn, sendBuffer2, sizeof(infoPkt)); 

                // Add them to the client list, using std::map wacky syntax
                m_mapClients[ pInfo->m_hConn ];
                SetClientNick( pInfo->m_hConn, "asdf" );
                SetClientId( pInfo->m_hConn, nextId);

                nextId++;
                break;
            }

            case k_ESteamNetworkingConnectionState_Connected:
                // We will get a callback immediately after accepting the connection.
                // Since we are the server, we can ignore this, it's not news to us.
                break;

            default:
                // Silences -Wswitch
                break;
        }
    }

    static GNSServer *s_pCallbackInstance;
    static void SteamNetConnectionStatusChangedCallback( SteamNetConnectionStatusChangedCallback_t *pInfo )
    {
        s_pCallbackInstance->OnSteamNetConnectionStatusChanged( pInfo );
    }

    static ISteamNetworkingConnectionSignaling* SteamNetCreateConnectionSignalingCallback( ISteamNetworkingSockets *pLocalInterface, const SteamNetworkingIdentity &identityPeer, int nLocalVirtualPort, int nRemoteVirtualPort )
    {
        //s_pCallbackInstance->OnSteamNetConnectionStatusChanged( pInfo );
        sithDplayGNS_verbosePrintf("incoming!\n");
        return nullptr;
    }

    void PollConnectionStateChanges()
    {
        s_pCallbackInstance = this;
        m_pInterface->RunCallbacks();
    }

    int ConnectedPlayers()
    {
        int ret = 0;
        //RealConnectedPlayers();
        for (int i = 0; i < 64; i++)
        {
            if (availableIds & (1ULL << i)) {
                ret++;
            }
        }
        return ret;
    }

    int RealConnectedPlayers()
    {
        int amt = 0;
        //availableIds = 3;
        for (int i = 0; i < jkPlayer_maxPlayers; i++)
        {
            if (!i && jkGuiNetHost_bIsDedicated) continue;


            if ( (jkPlayer_playerInfos[i].flags & 2) != 0 && !jkPlayer_playerInfos[i].playerNetId ){
                
            }
            else {
                //availableIds |= (1ULL << (jkPlayer_playerInfos[i].playerNetId-1));
                amt++;
            }
        }
        return amt;
    }
};

GNSServer *GNSServer::s_pCallbackInstance = nullptr;

/////////////////////////////////////////////////////////////////////////////
//
// GNSClient
//
/////////////////////////////////////////////////////////////////////////////

class GNSClient
{
public:
    void Init( const SteamNetworkingIPAddr &serverAddr )
    {
        id = 0xFFFFFFFF;
        m_closed = 0;

        // Select instance to use.  For now we'll always use the default.
        m_pInterface = g_SteamNetworkingSockets();

        // Start connecting
        char szAddr[ SteamNetworkingIPAddr::k_cchMaxString ];
        g_SteamNetworkingIPAddr_ToString(&serverAddr, szAddr, sizeof(szAddr), true );
        Printf( "Connecting to server at %s", szAddr );
        SteamNetworkingConfigValue_t opt;
        opt.SetPtr( k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback );
        //opt.SetPtr( k_ESteamNetworkingConfig_Callback_CreateConnectionSignaling, (void*)SteamNetCreateConnectionSignalingCallback);
        m_hConnection = m_pInterface->ConnectByIPAddress( serverAddr, 1, &opt );
        if ( m_hConnection == k_HSteamNetConnection_Invalid ) {
            Printf( "Failed to create connection" );
            m_closed = 1;
        }

        //m_pBcastInterface = SteamNetworkingMessages();

        m_identity.Clear();
        m_identity.SetGenericString("OpenJKDF2");

        m_hostDisconnected = 0;
    }

    void Shutdown()
    {
        if ( m_pInterface ) // Added: (never connected: nothing to close)
            m_pInterface->CloseConnection( m_hConnection, 0, nullptr, false );
        m_hConnection = k_HSteamNetConnection_Invalid;
        id = 0xFFFFFFFF;
        m_closed = 1;

        m_hostDisconnected = 0;
    }

    void RunStep()
    {
        //printf("Client runstep\n");
        //PollIncomingMessages();
        PollConnectionStateChanges();
        //TickBroadcastIn();
    }

    void Run()
    {
        while ( !g_bQuit )
        {
            RunStep();
            sleep_ms(10);
        }
    }

    int Receive(int *pIdOut, void *pMsg, int *pLenInOut)
    {
        int maxLen = *pLenInOut;
        *pIdOut = 0;
        *pLenInOut = 0;

        if ( m_hostDisconnected ) {
            sithDplayGNS_infoPrintf( "Host is disconnected, forcing exit...\n");
            Shutdown();
            m_closed = 1;
            *pIdOut = 1;
            return 2;
        }

        // Added: messages too short, or meant for another player, are let go (released) and the
        // next one read, rather than kept (a leak) and the reading stopped there
        for (;;)
        {
            ISteamNetworkingMessage *pIncomingMsg = nullptr;
            int numMsgs = m_pInterface->ReceiveMessagesOnConnection( m_hConnection, &pIncomingMsg, 1 );
            if ( numMsgs == 0 )
                return -1;
            if ( numMsgs < 0 ) {
                sithDplayGNS_infoPrintf( "Error checking for messages (%d)\n", numMsgs);
                Shutdown();
                m_closed = 1;
                *pIdOut = 1;
                m_hackFallback = !m_hackFallback;
                return m_hackFallback ? 2 : -1;
            }

            if (pIncomingMsg->m_cbSize < 8) {
                sithDplayGNS_infoPrintf("Bad packet size %x\n", pIncomingMsg->m_cbSize);
                pIncomingMsg->Release();
                continue;
            }

            uint8_t* dataBuf = (uint8_t*)pIncomingMsg->m_pData;
            uint32_t idFrom, idTo;
            memcpy(&idFrom, &dataBuf[0], 4);
            memcpy(&idTo, &dataBuf[4], 4);

            // Not intended for us
            if (idTo && idTo != id) {
                pIncomingMsg->Release();
                continue;
            }
            *pIdOut = (int)idFrom;

            int outsize = maxLen;
            if (outsize > pIncomingMsg->m_cbSize-8)
                outsize = pIncomingMsg->m_cbSize-8;

            memcpy(pMsg, &dataBuf[8], outsize);
            *pLenInOut = outsize;

            sithDplayGNS_verbosePrintf("Recv %x bytes from %x %x (%s, %x)\n", pIncomingMsg->m_cbSize, idFrom, idTo, sithDSS_IdToStr(*(uint32_t*)pMsg & 0xFF), *(uint32_t*)pMsg);

            // We don't need this anymore.
            pIncomingMsg->Release();
            return 0;
        }
    }

    int Send(uint32_t idFrom, uint32_t idTo, void *lpData, uint32_t dwDataSize)
    {
        if (dwDataSize > 4096-8) dwDataSize = 4096-8;
        *(uint32_t*)&sendBuffer[0] = idFrom;
        *(uint32_t*)&sendBuffer[4] = idTo;

        memcpy(&sendBuffer[8], lpData, dwDataSize);

        sithDplayGNS_verbosePrintf("Sent %x bytes to %x (%s, %x)\n", dwDataSize+8, idTo, sithDSS_IdToStr(*(uint32_t*)lpData & 0xFF), *(uint32_t*)lpData);

        EResult ret = m_pInterface->SendMessageToConnection( m_hConnection, sendBuffer, dwDataSize+8, k_nSteamNetworkingSend_Reliable, nullptr );
        if (ret < 0) {
            sithDplayGNS_infoPrintf( "Error sending message (%d)\n", ret);
        }
        if (ret == k_EResultNoConnection || ret == k_EResultInvalidParam) {
            return 0;
        }

        return 1;
    }

    // Added: connecting or connected
    bool IsOpen() const { return !m_closed && m_hConnection != k_HSteamNetConnection_Invalid; }

    uint32_t id = 0xFFFFFFFF;
    jkMultiEntry hostEntry; // Added: the session in the host's first message (with our id)
private:

    HSteamNetConnection m_hConnection = k_HSteamNetConnection_Invalid;
    ISteamNetworkingSockets *m_pInterface = nullptr;
    //ISteamNetworkingMessages *m_pBcastInterface;
    SteamNetworkingIdentity m_identity;
    uint8_t sendBuffer[4096];
    int m_closed = 0;
    int m_hostDisconnected = 0;
    int m_hackFallback = 0;

    void PollIncomingMessages()
    {
        ISteamNetworkingMessage *pIncomingMsg = nullptr;
        int numMsgs = m_pInterface->ReceiveMessagesOnConnection( m_hConnection, &pIncomingMsg, 1 );
        if ( numMsgs == 0 )
            return;
        if ( numMsgs < 0 ) {
            sithDplayGNS_infoPrintf( "Error checking for messages (%d)\n", numMsgs);
            return;
        }

        // Just echo anything we get from the server
        sithDplayGNS_verbosePrintf("Received %x bytes (%x)\n", pIncomingMsg->m_cbSize, sizeof(GNSInfoPacket));

        if (id == 0xFFFFFFFF && pIncomingMsg->m_cbSize == sizeof(GNSInfoPacket)) {
            GNSInfoPacket* pPkt = (GNSInfoPacket*)pIncomingMsg->m_pData;
            id = pPkt->id;
            memcpy(&hostEntry, &pPkt->entry, sizeof(hostEntry)); // Added: (the game joined, as its host tells it)
            sithDplayGNS_verbosePrintf("We are ID %x\n", id);


            // Hack?
            sithMulti_multiModeFlags = pPkt->entry.multiModeFlags;
            sithNet_MultiModeFlags = pPkt->entry.multiModeFlags;
        }

        // We don't need this anymore.
        pIncomingMsg->Release();
    }

#if 0
    int TickBroadcastIn()
    {
        SteamNetworkingMessage_t *pMsg = nullptr;
        int numMsgs = m_pBcastInterface->ReceiveMessagesOnChannel(1337, &pMsg, 1);
        if ( numMsgs == 0 )
            return -1;
        if ( numMsgs < 0 ) {
            sithDplayGNS_infoPrintf( "Error checking for messages (%d)\n", numMsgs);
            Shutdown();
            m_closed = 1;
            return 2;
        }

        if (pMsg->m_cbSize < 8) {
            sithDplayGNS_infoPrintf("Bad packet size %x\n", pMsg->m_cbSize);
            Shutdown();
            m_closed = 1;
            return -1;
        }

        sithDplayGNS_infoPrintf("Got broadcast %x\n", pMsg->m_cbSize);
        return 0;
    }
#endif

    void OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *pInfo )
    {
        if (m_hConnection == k_HSteamNetConnection_Invalid )
            return;

        // What's the state of the connection?
        switch ( pInfo->m_info.m_eState )
        {
            case k_ESteamNetworkingConnectionState_None:
                // NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
                break;

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            {
                g_bQuit = true;

                // Print an appropriate message
                if ( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting )
                {
                    // Note: we could distinguish between a timeout, a rejected connection,
                    // or some other transport problem.
                    Printf( "Couldn't connect to host. (%s)", pInfo->m_info.m_szEndDebug );
                }
                else if ( pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally )
                {
                    Printf( "Lost contact with the host. (%s)", pInfo->m_info.m_szEndDebug );
                }
                else
                {
                    // NOTE: We could check the reason code for a normal disconnection
                    Printf( "Host has disconnected. (%s)", pInfo->m_info.m_szEndDebug );
                    m_hostDisconnected = 1;
                }

                // Clean up the connection.  This is important!
                // The connection is "closed" in the network sense, but
                // it has not been destroyed.  We must close it on our end, too
                // to finish up.  The reason information do not matter in this case,
                // and we cannot linger because it's already closed on the other end,
                // so we just pass 0's.
                m_pInterface->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
                m_hConnection = k_HSteamNetConnection_Invalid;
                break;
            }

            case k_ESteamNetworkingConnectionState_Connecting:
                // We will get this callback when we start connecting.
                // We can ignore this.
                break;

            case k_ESteamNetworkingConnectionState_Connected:
                Printf( "Connected to server OK" );
                // (the host's first message: our id. Added: waited for 3 s at most, not for ever)
                for (int i = 0; i < 3000 && id == 0xFFFFFFFF; i++) {
                    PollIncomingMessages();
                    if (id == 0xFFFFFFFF) sleep_ms(1);
                }
                stdComm_GNS_ResetClients();
                break;

            default:
                // Silences -Wswitch
                break;
        }
    }

    static GNSClient *s_pCallbackInstance;
    static void SteamNetConnectionStatusChangedCallback( SteamNetConnectionStatusChangedCallback_t *pInfo )
    {
        s_pCallbackInstance->OnSteamNetConnectionStatusChanged( pInfo );
    }

    static ISteamNetworkingConnectionSignaling* SteamNetCreateConnectionSignalingCallback( ISteamNetworkingSockets *pLocalInterface, const SteamNetworkingIdentity &identityPeer, int nLocalVirtualPort, int nRemoteVirtualPort )
    {
        //s_pCallbackInstance->OnSteamNetConnectionStatusChanged( pInfo );
        sithDplayGNS_infoPrintf("incoming!\n");
        return nullptr;
    }

    void PollConnectionStateChanges()
    {
        s_pCallbackInstance = this;
        m_pInterface->RunCallbacks();
    }
};

GNSClient *GNSClient::s_pCallbackInstance = nullptr;

const uint16 DEFAULT_SERVER_PORT = 27020;

extern "C"
{

extern int jkGuiNetHost_portNum;
static GNSClient client;
static GNSServer server;

static void stdComm_GNS_ResetClients(void)
{
    DirectPlay_numPlayers = 32;
    for (int i = 0; i < 32; i++)
    {
        DirectPlay_aPlayers[i].dpId = i+1;
        jk_snwprintf(DirectPlay_aPlayers[i].waName, 32, u"asdf");
    }

    if (stdComm_bIsServer)
        stdComm_dplayIdSelf = server.id;
    else
        stdComm_dplayIdSelf = client.id;
}

void stdComm_GNS_Startup()
{
    stdComm_GNS_LoadSymbols();

    if (stdComm_GNS_bForceStubs)
    {
        jkGuiMultiplayer_numConnections = 1;
        jk_snwprintf(jkGuiMultiplayer_aConnections[0].name, 0x80, u"Screaming Into The Void (GNS Failed)");
        stdComm_dword_8321E0 = 0;

        memset(jkGuiMultiplayer_aEntries, 0, sizeof(jkMultiEntry) * 32);
        dplay_dword_55D618 = 0;
        return;
    }

    jkGuiMultiplayer_numConnections = 1;
    jk_snwprintf(jkGuiMultiplayer_aConnections[0].name, 0x80, u"Valve GNS");
    stdComm_dword_8321E0 = 0;

    memset(jkGuiMultiplayer_aEntries, 0, sizeof(jkMultiEntry) * 32);
    dplay_dword_55D618 = 0;

    stdComm_GNS_ResetClients();

    // Create client and server sockets
    InitSteamDatagramConnectionSockets();
}

void stdComm_GNS_Shutdown()
{
    if (stdComm_GNS_bForceStubs)
        return;

    DirectPlay_EnumSessions2(); // Added: (looking for a game, over GNS too: stopped first)

    ShutdownSteamDatagramConnectionSockets();
}

static int stdComm_GNS_Receive(int *pIdOut, int *pMsgIdOut, int *pLenOut)
{
    if (stdComm_GNS_bForceStubs)
        return -1;

    stdComm_GNS_ResetClients();

    if (stdComm_bIsServer)
    {
        server.RunStep();
        return server.Receive(pIdOut, (void*)pMsgIdOut, pLenOut);
    }
    else 
    {
        client.RunStep();
        return client.Receive(pIdOut, (void*)pMsgIdOut, pLenOut);
    }

    return -1;
}

static BOOL stdComm_GNS_Send(DPID idFrom, DPID idTo, void *lpData, DWORD dwDataSize)
{
    if (stdComm_GNS_bForceStubs)
        return 0;

    stdComm_GNS_ResetClients();

    if (stdComm_bIsServer)
    {
        server.RunStep();
        return server.Send(idFrom, idTo, lpData, dwDataSize);
    }
    else 
    {
        client.RunStep();
        return client.Send(idFrom, idTo, lpData, dwDataSize);
    }

    return 1;
}

// Added: an address as typed (a name, IPv4 or IPv6, maybe a port: stdCommTransport_SplitAddress;
// 27020 if none), looked up. 0 if it isn't one, or the name is unknown.
static int stdComm_GNS_ParseAddress(const char* pText, SteamNetworkingIPAddr* pAddr)
{
    char host[256];
    int port = DEFAULT_SERVER_PORT;
    if (!stdCommTransport_SplitAddress(pText, host, sizeof(host), &port, DEFAULT_SERVER_PORT)) return 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* pList = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &pList) != 0 || !pList)
        return 0;
    // (IPv4 first, if the name has both)
    const struct addrinfo* pUse = nullptr;
    for (const struct addrinfo* a = pList; a; a = a->ai_next) {
        if (a->ai_family == AF_INET) { pUse = a; break; }
        if (a->ai_family == AF_INET6 && !pUse) pUse = a;
    }
    int bOk = 0;
    pAddr->Clear();
    if (pUse && pUse->ai_family == AF_INET) {
        const struct sockaddr_in* sin = (const struct sockaddr_in*)pUse->ai_addr;
        pAddr->SetIPv4(ntohl(sin->sin_addr.s_addr), (uint16)port);
        bOk = 1;
    }
    else if (pUse && pUse->ai_family == AF_INET6) {
        const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)pUse->ai_addr;
        pAddr->SetIPv6((const uint8*)&sin6->sin6_addr, (uint16)port);
        bOk = 1;
    }
    freeaddrinfo(pList);
    return bOk;
}

// Added: what a host told about its game, as a player sees it: its password not shown (a classic
// host sends it as it is, and the player checks it: stdComm_GNS_CheckPassword), only that it has one
static void stdComm_GNS_PublicEntry(jkMultiEntry* pEntry)
{
    pEntry->field_E0 = 10;
    if (pEntry->wPassword[0]) pEntry->sessionFlags |= SESSIONFLAG_PASSWORD;
    memset(pEntry->wPassword, 0, sizeof(pEntry->wPassword));
}

// Added: a short connection to the address, as upstream's Join screen makes: the host's first
// message (our id, and its session) read, then closed. 1 if it answered. Its own connection, with
// no callbacks: safe from any thread, and alongside a game's.
static int stdComm_GNS_Query(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    if (!stdComm_GNS_bInitted || stdComm_GNS_bForceStubs) return 0;
    SteamNetworkingIPAddr addr;
    if (!stdComm_GNS_ParseAddress(pAddress, &addr)) return -1;
    ISteamNetworkingSockets* pInterface = g_SteamNetworkingSockets();
    HSteamNetConnection h = pInterface->ConnectByIPAddress(addr, 0, nullptr);
    if (h == k_HSteamNetConnection_Invalid) return 0;

    int bFound = 0;
    const uint32_t start = stdPlatform_GetTimeMsec();
    while (!bFound && stdPlatform_GetTimeMsec() - start < (uint32_t)timeoutMs && (!pbKeepGoing || *pbKeepGoing)) {
        ISteamNetworkingMessage* pMsg = nullptr;
        const int n = pInterface->ReceiveMessagesOnConnection(h, &pMsg, 1);
        if (n > 0) {
            if (pMsg->m_cbSize == (int)sizeof(GNSInfoPacket)) {
                GNSInfoPacket info;
                memcpy(&info, pMsg->m_pData, sizeof(info));
                *pOut = info.entry;
                bFound = 1;
            }
            pMsg->Release();
            continue;
        }
        if (n < 0) break;
        SteamNetConnectionInfo_t ci;
        if (!pInterface->GetConnectionInfo(h, &ci)) break;
        if (ci.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
            || ci.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            break;
        sleep_ms(10);
    }
    pInterface->CloseConnection(h, 0, nullptr, false);
    return bFound;
}

static int stdComm_GNS_ProbeAddress(const char* pAddress, jkMultiEntry* pOut, int timeoutMs, volatile int* pbKeepGoing)
{
    jkMultiEntry entry;
    memset(&entry, 0, sizeof(entry));
    const int r = stdComm_GNS_Query(pAddress, &entry, timeoutMs, pbKeepGoing);
    if (r > 0) {
        stdComm_GNS_PublicEntry(&entry);
        *pOut = entry;
    }
    return r;
}

static int stdComm_GNS_CheckPassword(const char* pAddress, const char16_t* pwPassword, int timeoutMs, volatile int* pbKeepGoing)
{
    jkMultiEntry entry;
    memset(&entry, 0, sizeof(entry));
    const int r = stdComm_GNS_Query(pAddress, &entry, timeoutMs, pbKeepGoing);
    if (r <= 0) return -1;
    int bSame = 1;
    for (int i = 0; i < 32; i++) {
        const char16_t a = pwPassword ? pwPassword[i] : 0;
        if (a != entry.wPassword[i]) { bSame = 0; break; }
        if (!a) break;
    }
    memset(&entry, 0, sizeof(entry));
    return bSame;
}

// Joining the game at the address typed (jkGuiMultiplayer_ipText). Added: our id waited for (up to
// 5 s) before the game sends anything, as over ENet, and a host not answering is told
static int stdComm_GNS_Join(char16_t* pwPassword)
{
    // (a classic host never asks for the password: the player checks it, below)
    stdComm_dplayIdSelf = 2; // HACK

    if (stdComm_GNS_bForceStubs)
        return 0;

    char text[256];
    stdString_WcharToChar(text, jkGuiMultiplayer_ipText, 255);
    text[255] = 0;
    SteamNetworkingIPAddr addr;
    if (!stdComm_GNS_ParseAddress(text, &addr)) return 0x88770154 + 1;

    memset(&client.hostEntry, 0, sizeof(client.hostEntry));
    client.Init(addr);
    for (int i = 0; i < 500 && client.id == 0xFFFFFFFF && client.IsOpen(); i++) {
        client.RunStep();
        if (client.id == 0xFFFFFFFF) sleep_ms(10);
    }
    if (client.id == 0xFFFFFFFF) {
        client.Shutdown();
        return 0x88770154 + 1;
    }
    // (checked against the game joined, not only the one asked about before: -join and the old Join
    // screen ask nothing first)
#ifdef PLATFORM_ENET
    if (client.hostEntry.multiModeFlags & MULTIMODEFLAG_COOP) {
        stdPlatform_Printf("GNS: a classic host's co-op game: not joined\n");
        client.Shutdown();
        return 0x88770154 + 1;
    }
#endif
    if (client.hostEntry.wPassword[0]) {
        int bSame = 1;
        for (int i = 0; i < 32; i++) {
            const char16_t a = pwPassword ? pwPassword[i] : 0;
            if (a != client.hostEntry.wPassword[i]) { bSame = 0; break; }
            if (!a) break;
        }
        memset(client.hostEntry.wPassword, 0, sizeof(client.hostEntry.wPassword));
        if (!bSame) {
            stdPlatform_Printf("GNS: wrong password for this game\n");
            client.Shutdown();
            return 0x88770154;
        }
    }
    stdComm_GNS_ResetClients();
    return 0;
}

static void stdComm_GNS_Close(void)
{
    if (stdComm_GNS_bForceStubs)
        return;

    if (stdComm_bIsServer)
    {
        server.Shutdown();
    }
    else 
    {
        client.Shutdown();
    }
}

static void stdComm_GNS_SetSessionLevel(void)
{
    _strncpy(sithDplayGNS_storedEntry.mapJklFname, jkMain_aLevelJklFname, 0x20);
}

static int stdComm_GNS_OpenHost(jkMultiEntry* pEntry)
{
    sithDplayGNS_storedEntry = *pEntry;

    jkPlayer_maxPlayers = pEntry->maxPlayers; // Hack?

    stdComm_bIsServer = 1;

    if (stdComm_GNS_bForceStubs)
        return 0;
    server.Init(jkGuiNetHost_portNum);

    return 0;
}

static void stdComm_GNS_SetSession(jkMultiEntry* pEntry)
{
    sithDplayGNS_storedEntry = *pEntry;

    jkPlayer_maxPlayers = pEntry->maxPlayers; // Hack?
}

const stdCommTransport stdComm_GNS_transport = {
    "GNS",
    stdComm_GNS_Receive,
    stdComm_GNS_Send,
    stdComm_GNS_Join,
    stdComm_GNS_OpenHost,
    stdComm_GNS_SetSession,
    stdComm_GNS_SetSessionLevel,
    stdComm_GNS_Close,
    stdComm_GNS_ResetClients,
    stdComm_GNS_ProbeAddress,
    stdComm_GNS_CheckPassword,
};

}

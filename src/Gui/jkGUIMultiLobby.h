// Added: entire file. The Multiplayer screens made simple (over ENet, the iOS apps' and desktops'):
//
// - Multiplayer: HOST GAME, JOIN GAME, RESUME CO-OP (and the multiplayer characters). The old
//   "choose a network provider" screen, with its one choice, is gone.
// - Host Game: DEATHMATCH or CO-OP CAMPAIGN, then the host settings for that (jkGuiNetHost: only
//   that kind's episodes, and only the settings that matter to it).
// - Resume Co-op: the episodes with a co-op save; the one picked is hosted from where it was.
// - Join Game: the games on this Wi-Fi, found by themselves (stdComm_ENetDiscover); the hosts
//   played with before (each checked: found, or not answering); and an address to type. A tap
//   joins. A host found nearby comes first: a saved address that has since changed is shown as
//   the game found, not as a stale address. Joining asks first (stdComm_ENet_ProbeAddress), so a
//   wrong address, no game there, a full game, and a password are each told apart.
//
// Everything else (picking a character, the game's own joining) is as it was (jkGUIMultiplayer.c).

#ifndef _GUI_JKGUIMULTILOBBY_H
#define _GUI_JKGUIMULTILOBBY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(QOL_IMPROVEMENTS) && defined(PLATFORM_ENET)
#define JKGUI_MULTI_LOBBY
#endif

#ifdef JKGUI_MULTI_LOBBY

// What the Multiplayer screen chose
enum jkGuiMultiLobbyChoice
{
    JKGUIMULTILOBBY_BACK = -1,
    JKGUIMULTILOBBY_HOST = 1,
    JKGUIMULTILOBBY_JOIN = 2,
    JKGUIMULTILOBBY_RESUME = 3,
    JKGUIMULTILOBBY_CHARACTERS = 4,
};

// What kind of game is being hosted (jkGuiNetHost_lobbyMode)
enum jkGuiMultiLobbyMode
{
    JKGUIMULTILOBBY_MODE_CLASSIC = 0,    // (the old screens: every kind together)
    JKGUIMULTILOBBY_MODE_DEATHMATCH = 1,
    JKGUIMULTILOBBY_MODE_COOP = 2,
};

void jkGuiMultiLobby_Startup(void);
void jkGuiMultiLobby_Shutdown(void);

// The Multiplayer screen: a jkGuiMultiLobbyChoice
int jkGuiMultiLobby_ShowMain(void);

// HOST GAME: deathmatch or co-op picked (and, for co-op over a saved campaign, starting over agreed
// to later, by jkGuiMultiLobby_ConfirmNewCoop); the host settings then show that kind
// (jkGuiNetHost_lobbyMode, jkGuiNetHost_lobbyEpisode). 0 if not.
int jkGuiMultiLobby_ShowHostMode(void);
// RESUME CO-OP: a saved campaign picked: hosted as co-op on its episode, carried on. 0 if not.
int jkGuiMultiLobby_ShowResume(void);
// The host settings' OK, hosting a new co-op campaign on that episode: 1 to go on (no save there,
// or starting over agreed to)
int jkGuiMultiLobby_ConfirmNewCoop(const char* pEpisodeGob);

// JOIN GAME: 1 if a game was picked and answered, set up for the game's own joining (as the old Join
// screen left it: jkGuiMultiplayer_aEntries[0], jkGuiMultiplayer_ipText, the password typed); 0 if not
int jkGuiMultiLobby_ShowJoin(void);
// That game joined: kept among the hosts played with before
void jkGuiMultiLobby_Joined(void);

#endif // JKGUI_MULTI_LOBBY

#ifdef __cplusplus
}
#endif

#endif // _GUI_JKGUIMULTILOBBY_H

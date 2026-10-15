// Added: entire file. Finding games on the same Wi-Fi without typing an address: a host says it is
// there (Bonjour: the service "_openjkdf2._udp" with the game's details), and the Join screen lists
// the ones it hears. Bonjour is Apple's (and needs no special permission on iOS, unlike a broadcast);
// elsewhere it works where a Bonjour library is found (Linux: Avahi's), and otherwise there is
// nothing to find and an address is typed as before.
//
// Jedi Knight and Mysteries of the Sith games are told apart (each lists only its own). A host's
// address(es) go with its details, so no name needs looking up, and its player id too (the same as
// its co-op saves': kept in its settings), so the Join screen can tell it is the same host as before
// even when its address has changed.
//
// Everything here runs on a thread of its own; what was found is read with
// stdComm_ENetDiscover_GetGames at any time.

#ifndef _PLATFORM_NETWORKING_ENET_STDCOMM_ENETDISCOVER_H
#define _PLATFORM_NETWORKING_ENET_STDCOMM_ENETDISCOVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDCOMM_ENETDISCOVER_SERVICE "_openjkdf2._udp"
#define STDCOMM_ENETDISCOVER_MAX_GAMES 16

// What a game says about itself (and what is heard of one)
typedef struct stdCommENetDiscoverGame
{
    char name[64];       // (the game's name, UTF-8)
    char hostId[20];     // (the host's player id, 16 hex digits; "" if none)
    char address[64];    // (where to join it: "a.b.c.d:port")
    char game[8];        // ("jk", "mots")
    char mode[8];        // ("dm", "team", "ctf", "coop")
    char level[40];      // (the level's file, e.g. "01narshadda.jkl")
    int numPlayers;
    int maxPlayers;
    int bPassword;
} stdCommENetDiscoverGame;

// 1 if this build can find (and be found)
int stdComm_ENetDiscover_IsAvailable(void);

// Hosting: said to the network (pGame's address and host id needn't be filled: the host's own
// addresses, the port it listens on, and its id are added), again whenever what it says changes (the
// player count); and stopped
void stdComm_ENetDiscover_Advertise(const stdCommENetDiscoverGame* pGame, int port);
void stdComm_ENetDiscover_StopAdvertising(void);

// Joining: listening for games (game: this game's, "jk" or "mots"; others aren't listed), and stopped
void stdComm_ENetDiscover_StartBrowsing(const char* game);
void stdComm_ENetDiscover_StopBrowsing(void);
// The games heard now (up to max), how many
int stdComm_ENetDiscover_GetGames(stdCommENetDiscoverGame* aOut, int max);

// All of it stopped (the game ending)
void stdComm_ENetDiscover_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_ENET_STDCOMM_ENETDISCOVER_H

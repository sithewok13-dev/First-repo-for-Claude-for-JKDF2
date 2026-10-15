// Added: entire file. This port's own multiplayer messages, all carried by one
// message type the game never sends (DSS_64), each with a kind of its own:
// [kind: u8][length: u16, low byte first][that many bytes]. A game that doesn't know the type,
// or a kind, just passes it by, so older builds and other ports still play.
//
// Kinds (each the business of its own module):
//   JKNETEXT_LOOK    -- a player's saber as they chose it (jkPlayerLook)
//   JKNETEXT_COOPKIT -- a co-op player's gear, for the host's save (jkCoopSave)

#ifndef _DSS_JKNETEXT_H
#define _DSS_JKNETEXT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QOL_IMPROVEMENTS

#define DSS_JKNETEXT DSS_64
#define JKNETEXT_MAX_LEN 1800

enum
{
    JKNETEXT_LOOK = 1,
    JKNETEXT_COOPKIT = 2,
    JKNETEXT_NUM_KINDS = 16
};

// What a kind's arrival is handed: who sent it (their net id) and its bytes
typedef void (*jkNetExtHandler_t)(int idFrom, const uint8_t* pData, int len);

void jkNetExt_Startup(void);
void jkNetExt_Register(int kind, jkNetExtHandler_t pfHandler);

// Sent reliably, to one player's net id or to everyone (-1)
void jkNetExt_Send(int kind, const void* pData, int len, int idTo);

#endif // QOL_IMPROVEMENTS

#ifdef __cplusplus
}
#endif

#endif // _DSS_JKNETEXT_H

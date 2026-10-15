// Added: entire file. Encryption for multiplayer games with a password (over
// ENet, stdComm_ENet.c): what the players send each other can't be read or
// changed on the way, and only those who know the password get in. Games
// without a password stay as they were (plain).
//
// The password itself is never sent. Each side turns it into a key
// (PBKDF2-HMAC-SHA256, salted with the host's random salt, so it is slow to
// guess); each connection then gets a key of its own from that and two fresh
// random numbers (one from each side), so nothing recorded from one connection
// works in another. Every message is sealed with ChaCha20-Poly1305 (mbedtls),
// numbered, and one that was changed, replayed or sealed with another key is
// thrown away.
//
// Not protected: someone who records a join can try passwords offline (a long
// password makes that hopeless), and the session's description (its name,
// level, player count) is still readable, as finding games needs it.

#ifndef _PLATFORM_NETWORKING_ENET_STDCOMM_ENETCRYPTO_H
#define _PLATFORM_NETWORKING_ENET_STDCOMM_ENETCRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDCOMM_ENETCRYPTO_KEY_LEN 32
#define STDCOMM_ENETCRYPTO_SALT_LEN 16
#define STDCOMM_ENETCRYPTO_NONCE_LEN 16
#define STDCOMM_ENETCRYPTO_ITERATIONS 100000
// What sealing adds to a message: its number (8) and its tag (16)
#define STDCOMM_ENETCRYPTO_OVERHEAD 24
#define STDCOMM_ENETCRYPTO_CHANNELS 2

// Who seals: the host, or a player (each direction numbers its own messages)
#define STDCOMM_ENETCRYPTO_FROM_HOST 1
#define STDCOMM_ENETCRYPTO_FROM_PLAYER 2

// One connection's keys and message numbers
typedef struct stdCommENetCryptoSession
{
    uint8_t key[STDCOMM_ENETCRYPTO_KEY_LEN];
    int from;                                            // (who this side is: _FROM_HOST or _FROM_PLAYER)
    uint64_t sendNext[STDCOMM_ENETCRYPTO_CHANNELS];      // (the next number to send, per channel)
    uint64_t recvTop[STDCOMM_ENETCRYPTO_CHANNELS];       // (the highest number taken, per channel; 0 none)
    uint64_t recvSeen[STDCOMM_ENETCRYPTO_CHANNELS];      // (the 64 below it: bit n, top - n, taken)
    int bOn;
} stdCommENetCryptoSession;

// Random bytes from the system (1 if it gave them)
int stdComm_ENetCrypto_Random(void* pOut, size_t len);

// The password's key (the slow part: on a phone a tenth of a second or so); the password as
// UTF-16 units, up to 32 or its end (0)
void stdComm_ENetCrypto_PasswordKey(const uint16_t* pPassword, const uint8_t salt[STDCOMM_ENETCRYPTO_SALT_LEN],
                                    uint8_t keyOut[STDCOMM_ENETCRYPTO_KEY_LEN]);

// A connection's session from the password's key and both sides' random numbers; from: which side
// this is
void stdComm_ENetCrypto_StartSession(stdCommENetCryptoSession* pSession, const uint8_t passwordKey[STDCOMM_ENETCRYPTO_KEY_LEN],
                                     const uint8_t hostNonce[STDCOMM_ENETCRYPTO_NONCE_LEN],
                                     const uint8_t playerNonce[STDCOMM_ENETCRYPTO_NONCE_LEN], int from);
void stdComm_ENetCrypto_EndSession(stdCommENetCryptoSession* pSession);

// A message sealed to send on that channel (into pOut, len + _OVERHEAD bytes); its length, -1 if not
int stdComm_ENetCrypto_Seal(stdCommENetCryptoSession* pSession, int channel, const void* pIn, size_t len,
                            void* pOut, size_t outCap);
// A sealed message that came on that channel opened (into pOut); its length, -1 if it was changed,
// replayed, or not sealed by the other side of this session
int stdComm_ENetCrypto_Open(stdCommENetCryptoSession* pSession, int channel, const void* pIn, size_t len,
                            void* pOut, size_t outCap);

// HMAC-SHA256 and PBKDF2-HMAC-SHA256 (for the above; and their tests)
void stdComm_ENetCrypto_HmacSha256(const uint8_t* pKey, size_t keyLen, const uint8_t* pMsg, size_t msgLen, uint8_t out[32]);
void stdComm_ENetCrypto_Pbkdf2Sha256(const uint8_t* pPass, size_t passLen, const uint8_t* pSalt, size_t saltLen,
                                     uint32_t iterations, uint8_t* pOut, size_t outLen);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_NETWORKING_ENET_STDCOMM_ENETCRYPTO_H

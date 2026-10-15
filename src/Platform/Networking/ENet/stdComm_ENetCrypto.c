// Added: entire file. See stdComm_ENetCrypto.h.

#include "stdComm_ENetCrypto.h"

#include "mbedtls/chachapoly.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt")
#endif
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <stdio.h>
#endif

// ------------------------------------------------------------------ random

int stdComm_ENetCrypto_Random(void* pOut, size_t len)
{
#if defined(_WIN32)
    return BCryptGenRandom(NULL, (PUCHAR)pOut, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    arc4random_buf(pOut, len);
    return 1;
#else
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    size_t got = fread(pOut, 1, len, f);
    fclose(f);
    return got == len;
#endif
}

// ------------------------------------------------------------------ HMAC-SHA256, PBKDF2

// The key's inner and outer hashes started (HMAC's ipad and opad blocks)
static void stdComm_ENetCrypto_HmacStart(const uint8_t* pKey, size_t keyLen, mbedtls_sha256_context* pInner, mbedtls_sha256_context* pOuter)
{
    uint8_t k[64], pad[64];
    memset(k, 0, sizeof(k));
    if (keyLen > 64) mbedtls_sha256(pKey, keyLen, k, 0);
    else memcpy(k, pKey, keyLen);

    mbedtls_sha256_init(pInner);
    mbedtls_sha256_starts(pInner, 0);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    mbedtls_sha256_update(pInner, pad, 64);

    mbedtls_sha256_init(pOuter);
    mbedtls_sha256_starts(pOuter, 0);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    mbedtls_sha256_update(pOuter, pad, 64);

    mbedtls_platform_zeroize(k, sizeof(k));
    mbedtls_platform_zeroize(pad, sizeof(pad));
}

// One HMAC from the started key (its hashes left as they were): msg, then msg2 if any
static void stdComm_ENetCrypto_HmacFrom(const mbedtls_sha256_context* pInner, const mbedtls_sha256_context* pOuter,
                                        const uint8_t* pMsg, size_t msgLen, const uint8_t* pMsg2, size_t msg2Len, uint8_t out[32])
{
    mbedtls_sha256_context c;
    uint8_t inner[32];
    mbedtls_sha256_init(&c);
    mbedtls_sha256_clone(&c, pInner);
    mbedtls_sha256_update(&c, pMsg, msgLen);
    if (pMsg2 && msg2Len) mbedtls_sha256_update(&c, pMsg2, msg2Len);
    mbedtls_sha256_finish(&c, inner);
    mbedtls_sha256_clone(&c, pOuter);
    mbedtls_sha256_update(&c, inner, 32);
    mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
    mbedtls_platform_zeroize(inner, sizeof(inner));
}

void stdComm_ENetCrypto_HmacSha256(const uint8_t* pKey, size_t keyLen, const uint8_t* pMsg, size_t msgLen, uint8_t out[32])
{
    mbedtls_sha256_context inner, outer;
    stdComm_ENetCrypto_HmacStart(pKey, keyLen, &inner, &outer);
    stdComm_ENetCrypto_HmacFrom(&inner, &outer, pMsg, msgLen, NULL, 0, out);
    mbedtls_sha256_free(&inner);
    mbedtls_sha256_free(&outer);
}

void stdComm_ENetCrypto_Pbkdf2Sha256(const uint8_t* pPass, size_t passLen, const uint8_t* pSalt, size_t saltLen,
                                     uint32_t iterations, uint8_t* pOut, size_t outLen)
{
    mbedtls_sha256_context inner, outer;
    stdComm_ENetCrypto_HmacStart(pPass, passLen, &inner, &outer);
    for (uint32_t block = 1; outLen > 0; block++) {
        uint8_t idx[4] = { (uint8_t)(block >> 24), (uint8_t)(block >> 16), (uint8_t)(block >> 8), (uint8_t)block };
        uint8_t u[32], t[32];
        stdComm_ENetCrypto_HmacFrom(&inner, &outer, pSalt, saltLen, idx, 4, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iterations; i++) {
            stdComm_ENetCrypto_HmacFrom(&inner, &outer, u, 32, NULL, 0, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        size_t n = outLen < 32 ? outLen : 32;
        memcpy(pOut, t, n);
        pOut += n;
        outLen -= n;
        mbedtls_platform_zeroize(u, sizeof(u));
        mbedtls_platform_zeroize(t, sizeof(t));
    }
    mbedtls_sha256_free(&inner);
    mbedtls_sha256_free(&outer);
}

// ------------------------------------------------------------------ keys

void stdComm_ENetCrypto_PasswordKey(const uint16_t* pPassword, const uint8_t salt[STDCOMM_ENETCRYPTO_SALT_LEN],
                                    uint8_t keyOut[STDCOMM_ENETCRYPTO_KEY_LEN])
{
    // (the password as UTF-16 little-endian bytes, the same on every machine)
    uint8_t pw[64];
    size_t n = 0;
    for (int i = 0; pPassword && i < 32 && pPassword[i]; i++) {
        pw[n++] = (uint8_t)(pPassword[i] & 0xFF);
        pw[n++] = (uint8_t)(pPassword[i] >> 8);
    }
    uint8_t s[5 + STDCOMM_ENETCRYPTO_SALT_LEN];
    memcpy(s, "OJKE1", 5);
    memcpy(s + 5, salt, STDCOMM_ENETCRYPTO_SALT_LEN);
    stdComm_ENetCrypto_Pbkdf2Sha256(pw, n, s, sizeof(s), STDCOMM_ENETCRYPTO_ITERATIONS, keyOut, STDCOMM_ENETCRYPTO_KEY_LEN);
    mbedtls_platform_zeroize(pw, sizeof(pw));
}

void stdComm_ENetCrypto_StartSession(stdCommENetCryptoSession* pSession, const uint8_t passwordKey[STDCOMM_ENETCRYPTO_KEY_LEN],
                                     const uint8_t hostNonce[STDCOMM_ENETCRYPTO_NONCE_LEN],
                                     const uint8_t playerNonce[STDCOMM_ENETCRYPTO_NONCE_LEN], int from)
{
    uint8_t msg[11 + 2 * STDCOMM_ENETCRYPTO_NONCE_LEN];
    memcpy(msg, "OJK-SESSION", 11);
    memcpy(msg + 11, hostNonce, STDCOMM_ENETCRYPTO_NONCE_LEN);
    memcpy(msg + 11 + STDCOMM_ENETCRYPTO_NONCE_LEN, playerNonce, STDCOMM_ENETCRYPTO_NONCE_LEN);
    memset(pSession, 0, sizeof(*pSession));
    stdComm_ENetCrypto_HmacSha256(passwordKey, STDCOMM_ENETCRYPTO_KEY_LEN, msg, sizeof(msg), pSession->key);
    pSession->from = from;
    for (int c = 0; c < STDCOMM_ENETCRYPTO_CHANNELS; c++) pSession->sendNext[c] = 1;
    pSession->bOn = 1;
}

void stdComm_ENetCrypto_EndSession(stdCommENetCryptoSession* pSession)
{
    mbedtls_platform_zeroize(pSession, sizeof(*pSession));
}

// ------------------------------------------------------------------ sealing

// The nonce: who sealed it, the channel, the message's number
static void stdComm_ENetCrypto_Nonce(uint8_t nonce[12], int from, int channel, uint64_t num)
{
    nonce[0] = (uint8_t)from;
    nonce[1] = (uint8_t)channel;
    nonce[2] = 0;
    nonce[3] = 0;
    for (int i = 0; i < 8; i++) nonce[4 + i] = (uint8_t)(num >> (8 * i));
}

int stdComm_ENetCrypto_Seal(stdCommENetCryptoSession* pSession, int channel, const void* pIn, size_t len,
                            void* pOut, size_t outCap)
{
    if (!pSession->bOn || channel < 0 || channel >= STDCOMM_ENETCRYPTO_CHANNELS) return -1;
    if (outCap < len + STDCOMM_ENETCRYPTO_OVERHEAD) return -1;
    uint8_t* out = (uint8_t*)pOut;
    const uint64_t num = pSession->sendNext[channel]++;
    uint8_t nonce[12];
    stdComm_ENetCrypto_Nonce(nonce, pSession->from, channel, num);
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(num >> (8 * i));

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int ok = mbedtls_chachapoly_setkey(&ctx, pSession->key) == 0
          && mbedtls_chachapoly_encrypt_and_tag(&ctx, len, nonce, NULL, 0, (const unsigned char*)pIn, out + 8, out + 8 + len) == 0;
    mbedtls_chachapoly_free(&ctx);
    return ok ? (int)(len + STDCOMM_ENETCRYPTO_OVERHEAD) : -1;
}

int stdComm_ENetCrypto_Open(stdCommENetCryptoSession* pSession, int channel, const void* pIn, size_t len,
                            void* pOut, size_t outCap)
{
    if (!pSession->bOn || channel < 0 || channel >= STDCOMM_ENETCRYPTO_CHANNELS) return -1;
    if (len < STDCOMM_ENETCRYPTO_OVERHEAD) return -1;
    const size_t plainLen = len - STDCOMM_ENETCRYPTO_OVERHEAD;
    if (outCap < plainLen) return -1;
    const uint8_t* in = (const uint8_t*)pIn;
    uint64_t num = 0;
    for (int i = 0; i < 8; i++) num |= (uint64_t)in[i] << (8 * i);
    if (!num) return -1;

    // (replayed, or too old to tell)
    const uint64_t top = pSession->recvTop[channel];
    if (num <= top) {
        const uint64_t back = top - num;
        if (back >= 64 || (pSession->recvSeen[channel] & (1ULL << back))) return -1;
    }

    // (sealed by the other side: its "from")
    const int otherFrom = pSession->from == STDCOMM_ENETCRYPTO_FROM_HOST ? STDCOMM_ENETCRYPTO_FROM_PLAYER : STDCOMM_ENETCRYPTO_FROM_HOST;
    uint8_t nonce[12];
    stdComm_ENetCrypto_Nonce(nonce, otherFrom, channel, num);
    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int ok = mbedtls_chachapoly_setkey(&ctx, pSession->key) == 0
          && mbedtls_chachapoly_auth_decrypt(&ctx, plainLen, nonce, NULL, 0, in + 8 + plainLen, in + 8, (unsigned char*)pOut) == 0;
    mbedtls_chachapoly_free(&ctx);
    if (!ok) return -1;

    // (taken: marked)
    if (num > top) {
        const uint64_t shift = num - top;
        pSession->recvSeen[channel] = shift >= 64 ? 0 : pSession->recvSeen[channel] << shift;
        pSession->recvSeen[channel] |= 1;
        pSession->recvTop[channel] = num;
    }
    else {
        pSession->recvSeen[channel] |= 1ULL << (top - num);
    }
    return (int)plainLen;
}

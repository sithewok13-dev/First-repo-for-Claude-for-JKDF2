// Added: entire file. See jkCoopSave.h.

#include "Main/jkCoopSave.h"

#ifdef QOL_IMPROVEMENTS

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include "jk.h"
#include "stdPlatform.h"
#include "Main/jkCoop.h"
#include "Dss/jkNetExt.h"
#include "Dss/sithMulti.h"
#include "Win95/stdComm.h"
#include "Platform/wuRegistry.h"
#include "General/stdFileUtil.h"
#include "General/stdString.h"
#include "General/util.h"

#define JKCOOPSAVE_VERSION 1
#define JKCOOPSAVE_ID_LEN 17      // (16 hex digits and the end)
#define JKCOOPSAVE_PLAYERS 16
#define JKCOOPSAVE_WRITE_MS 1500  // (a change written this long after, others with it)

typedef struct jkCoopSavePlayer
{
    char id[JKCOOPSAVE_ID_LEN]; // ("" free)
    int netId;                  // (the host's: the game that first sent this id this session; 0 none yet)
    jkCoopKitBin aKit[SITHBIN_NUMBINS];
} jkCoopSavePlayer;

static char jkCoopSave_myId[JKCOOPSAVE_ID_LEN];
static char jkCoopSave_episode[64];
static char jkCoopSave_resumeLevel[64];
static int jkCoopSave_bHostKitPending = 0;   // (resumed: the host's own saved kit to give)
static jkCoopSavePlayer jkCoopSave_aPlayer[JKCOOPSAVE_PLAYERS]; // (the host's: everyone's)
static int jkCoopSave_bDirty = 0;
static uint32_t jkCoopSave_dirtyMs = 0;
static char jkCoopSave_level[64];       // (the level a save is of: the one being played)
static int jkCoopSave_bResumed = 0;      // (this session carried on from the save)
static int jkCoopSave_bWrittenThisSession = 0;

static int jkCoopSave_IdOk(const char* id)
{
    if (strlen(id) != JKCOOPSAVE_ID_LEN - 1) return 0;
    for (const char* p = id; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return 0;
    return 1;
}

static void jkCoopSave_MarkDirty(void)
{
    jkCoopSave_bDirty = 1;
    jkCoopSave_dirtyMs = stdPlatform_GetTimeMsec();
    if (sithWorld_g_pCurrentWorld)
        stdString_SafeStrCopy(jkCoopSave_level, sithWorld_g_pCurrentWorld->map_jkl_fname, sizeof(jkCoopSave_level));
}

// ------------------------------------------------------------------ the player id

static const char* jkCoopSave_MyId(void);
const char* jkCoopSave_PlayerId(void)
{
    return jkCoopSave_MyId();
}

static const char* jkCoopSave_MyId(void)
{
    if (jkCoopSave_myId[0])
        return jkCoopSave_myId;
    char stored[64] = "";
    wuRegistry_GetStr("coopPlayerId", stored, sizeof(stored), "");
    if (jkCoopSave_IdOk(stored)) {
        stdString_SafeStrCopy(jkCoopSave_myId, stored, JKCOOPSAVE_ID_LEN);
        return jkCoopSave_myId;
    }
    // (made once: the time, the clock, and where things happen to be in memory, mixed)
    uint32_t a = (uint32_t)time(NULL) ^ (uint32_t)stdPlatform_GetTimeMsec() * 2654435761u;
    uint32_t b = (uint32_t)(uintptr_t)&stored ^ ((uint32_t)rand() << 16) ^ (uint32_t)rand() ^ (a >> 7) * 40503u;
    snprintf(jkCoopSave_myId, sizeof(jkCoopSave_myId), "%08x%08x", a, b);
    wuRegistry_SaveStr("coopPlayerId", jkCoopSave_myId);
    return jkCoopSave_myId;
}

static jkCoopSavePlayer* jkCoopSave_Player(const char* id, int bMake)
{
    for (int i = 0; i < JKCOOPSAVE_PLAYERS; i++)
        if (jkCoopSave_aPlayer[i].id[0] && !strcmp(jkCoopSave_aPlayer[i].id, id))
            return &jkCoopSave_aPlayer[i];
    if (!bMake)
        return NULL;
    for (int i = 0; i < JKCOOPSAVE_PLAYERS; i++)
        if (!jkCoopSave_aPlayer[i].id[0]) {
            memset(&jkCoopSave_aPlayer[i], 0, sizeof(jkCoopSave_aPlayer[i]));
            stdString_SafeStrCopy(jkCoopSave_aPlayer[i].id, id, JKCOOPSAVE_ID_LEN);
            return &jkCoopSave_aPlayer[i];
        }
    return NULL;
}

// ------------------------------------------------------------------ the file

static void jkCoopSave_Path(char* pOut, int outLen, const char* suffix)
{
    char ep[64];
    int n = 0;
    for (const char* p = jkCoopSave_episode; *p && n < (int)sizeof(ep) - 1; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        ep[n++] = ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') ? c : '_';
    }
    ep[n] = 0;
    snprintf(pOut, outLen, "%s/%s.txt%s", JKCOOPSAVE_DIR, ep, suffix);
}

// The earlier campaigns kept (<episode>.txt.<date-time>): the newest JKCOOPSAVE_KEEP_ARCHIVES of this
// episode's, the older ones removed, so starting over again and again doesn't fill the folder
#define JKCOOPSAVE_KEEP_ARCHIVES 5
static void jkCoopSave_PruneArchives(void)
{
    char prefix[160];
    jkCoopSave_Path(prefix, sizeof(prefix), ".");
    const char* base = strrchr(prefix, '/');
    base = base ? base + 1 : prefix;
    const size_t baseLen = strlen(base);

    // (the newest kept while looking through them, any older one removed as it is passed)
    char aKeep[JKCOOPSAVE_KEEP_ARCHIVES][64];
    int numKeep = 0;
    stdFileSearch* pFind = stdFileUtil_NewFind(JKCOOPSAVE_DIR, 2, NULL);
    if (!pFind) return;
    stdFileSearchResult result;
    while (stdFileUtil_FindNext(pFind, &result)) {
        const char* name = result.fpath;
        const char* slash = strrchr(name, '/');
        if (slash) name = slash + 1;
        slash = strrchr(name, '\\');
        if (slash) name = slash + 1;
        // ("<episode>.txt." and a date-time: 8 digits, '-', 6 digits)
        if (strncmp(name, base, baseLen) || strlen(name) != baseLen + 15) continue;
        const char* stamp = name + baseLen;
        int bStamp = stamp[8] == '-';
        for (int i = 0; i < 15 && bStamp; i++) bStamp = i == 8 || (stamp[i] >= '0' && stamp[i] <= '9');
        if (!bStamp) continue;
        char drop[64] = "";
        if (numKeep < JKCOOPSAVE_KEEP_ARCHIVES) {
            stdString_SafeStrCopy(aKeep[numKeep++], name, 64);
        }
        else {
            // (the date-time sorts as text: the oldest kept, or this one, goes)
            int oldest = 0;
            for (int i = 1; i < numKeep; i++)
                if (strcmp(aKeep[i], aKeep[oldest]) < 0) oldest = i;
            if (strcmp(name, aKeep[oldest]) > 0) {
                stdString_SafeStrCopy(drop, aKeep[oldest], 64);
                stdString_SafeStrCopy(aKeep[oldest], name, 64);
            }
            else {
                stdString_SafeStrCopy(drop, name, 64);
            }
        }
        if (drop[0]) {
            char path[200];
            snprintf(path, sizeof(path), "%s/%s", JKCOOPSAVE_DIR, drop);
            remove(path);
        }
    }
    stdFileUtil_DisposeFind(pFind);
}

static int jkCoopSave_Write(void)
{
    if (!jkCoopSave_episode[0] || !jkCoopSave_level[0])
        return 0;
    char path[160], tmp[160], bak[160];
    jkCoopSave_Path(path, sizeof(path), "");
    jkCoopSave_Path(tmp, sizeof(tmp), ".tmp");
    jkCoopSave_Path(bak, sizeof(bak), ".bak");
    char dir[16];
    stdString_SafeStrCopy(dir, JKCOOPSAVE_DIR, sizeof(dir));
    stdFileUtil_MkDir(dir);

    FILE* f = fopen(tmp, "w");
    if (!f)
        return 0;
    int ok = fprintf(f, "OPENJKDF2_COOP %d\nepisode %s\nlevel %s\n", JKCOOPSAVE_VERSION, jkCoopSave_episode,
                     jkCoopSave_level) > 0;
    for (int i = 0; i < JKCOOPSAVE_PLAYERS && ok; i++) {
        const jkCoopSavePlayer* p = &jkCoopSave_aPlayer[i];
        if (!p->id[0]) continue;
        ok = fprintf(f, "player %s\n", p->id) > 0;
        for (int b = 0; b < SITHBIN_NUMBINS && ok; b++)
            if (p->aKit[b].amount != 0.0 || p->aKit[b].state)
                ok = fprintf(f, "bin %d %.4f %d\n", b, (double)p->aKit[b].amount, (int)p->aKit[b].state) > 0;
    }
    ok = ok && fprintf(f, "end\n") > 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return 0;
    }
    // The first write of a session keeps what was there: a campaign carried on, as .bak; another one
    // (this game started afresh), under its own dated name, never overwritten
    if (!jkCoopSave_bWrittenThisSession && util_FileExists(path)) {
        if (jkCoopSave_bResumed) {
            remove(bak);
            if (rename(path, bak) != 0) { remove(tmp); return 0; }
        }
        else {
            char old[200];
            const time_t now = time(NULL);
            const struct tm* t = localtime(&now);
            char stamp[32] = "old";
            if (t) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", t);
            jkCoopSave_Path(old, sizeof(old), "");
            snprintf(old + strlen(old), sizeof(old) - strlen(old), ".%s", stamp);
            if (rename(path, old) != 0) { remove(tmp); return 0; }
            stdPlatform_Printf("OpenJKDF2: co-op, the earlier campaign kept as %s\n", old);
            jkCoopSave_PruneArchives();
        }
    }
    // (then the new one put in its place in one step; Windows won't rename over a file)
#ifdef _WIN32
    if (util_FileExists(path) && util_FileExists(tmp)) remove(path);
#endif
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    jkCoopSave_bWrittenThisSession = 1;
    stdPlatform_Printf("OpenJKDF2: co-op saved, %s at %s\n", jkCoopSave_episode, jkCoopSave_level);
    return 1;
}

// The save (or, if that isn't there whole, its .bak) read into the players' table and the level; 0 if
// there is none
static int jkCoopSave_ReadFile(const char* path, char* pLevel, int levelLen);
static int jkCoopSave_Read(char* pLevel, int levelLen)
{
    char path[160];
    jkCoopSave_Path(path, sizeof(path), "");
    if (jkCoopSave_ReadFile(path, pLevel, levelLen))
        return 1;
    jkCoopSave_Path(path, sizeof(path), ".bak");
    return jkCoopSave_ReadFile(path, pLevel, levelLen);
}

static int jkCoopSave_ReadFile(const char* path, char* pLevel, int levelLen)
{
    char line[256];
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;
    int version = 0, bEnd = 0;
    char level[64] = "";
    jkCoopSavePlayer* pCur = NULL;
    memset(jkCoopSave_aPlayer, 0, sizeof(jkCoopSave_aPlayer));
    while (fgets(line, sizeof(line), f)) {
        char word[64] = "", arg[64] = "";
        int bin = 0, state = 0;
        float amount = 0.0f;
        if (sscanf(line, "OPENJKDF2_COOP %d", &version) == 1) continue;
        if (sscanf(line, "level %63s", level) == 1) continue;
        if (sscanf(line, "player %63s", arg) == 1) { pCur = jkCoopSave_IdOk(arg) ? jkCoopSave_Player(arg, 1) : NULL; continue; }
        if (sscanf(line, "bin %d %f %d", &bin, &amount, &state) == 3) {
            if (pCur && bin >= 0 && bin < SITHBIN_NUMBINS) { pCur->aKit[bin].amount = amount; pCur->aKit[bin].state = state; }
            continue;
        }
        if (sscanf(line, "%63s", word) == 1 && !strcmp(word, "end")) bEnd = 1;
    }
    fclose(f);
    if (version != JKCOOPSAVE_VERSION || !bEnd || !level[0]) {
        memset(jkCoopSave_aPlayer, 0, sizeof(jkCoopSave_aPlayer));
        return 0;
    }
    stdString_SafeStrCopy(pLevel, level, levelLen);
    return 1;
}

// An episode's save, looked at (nothing read into the game): 1 if there is one whole, its level and
// when it was written
int jkCoopSave_Peek(const char* pEpisodeGob, char* pLevel, int levelLen, time_t* pWhen)
{
    char keep[64];
    stdString_SafeStrCopy(keep, jkCoopSave_episode, sizeof(keep));
    stdString_SafeStrCopy(jkCoopSave_episode, pEpisodeGob ? pEpisodeGob : "", sizeof(jkCoopSave_episode));
    char path[160];
    jkCoopSave_Path(path, sizeof(path), "");
    stdString_SafeStrCopy(jkCoopSave_episode, keep, sizeof(jkCoopSave_episode));

    int ok = 0;
    char line[256], level[64] = "";
    int version = 0, bEnd = 0;
    FILE* f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char word[64] = "";
            if (sscanf(line, "OPENJKDF2_COOP %d", &version) == 1) continue;
            if (sscanf(line, "level %63s", level) == 1) continue;
            if (sscanf(line, "%63s", word) == 1 && !strcmp(word, "end")) bEnd = 1;
        }
        fclose(f);
        ok = version == JKCOOPSAVE_VERSION && bEnd && level[0];
    }
    if (!ok) return 0;
    if (pLevel) stdString_SafeStrCopy(pLevel, level, levelLen);
    if (pWhen) {
        struct stat st;
        *pWhen = stat(path, &st) == 0 ? st.st_mtime : 0;
    }
    return 1;
}

// ------------------------------------------------------------------ gear sent between the games

// ([version][flags: 1, carried nothing in][id: 16][count: u16][count x (bin: u8, amount: f32, state: u8)])
#define JKCOOPSAVE_FRESH 1

static void jkCoopSave_SendKit(const char* id, const jkCoopKitBin* aKit, int flags, int idTo)
{
    uint8_t aMsg[JKNETEXT_MAX_LEN];
    int n = 0;
    aMsg[n++] = JKCOOPSAVE_VERSION;
    aMsg[n++] = (uint8_t)flags;
    memcpy(aMsg + n, id, JKCOOPSAVE_ID_LEN - 1); n += JKCOOPSAVE_ID_LEN - 1;
    const int countAt = n; n += 2;
    int count = 0;
    for (int b = 0; b < SITHBIN_NUMBINS && b < 256 && n + 6 <= (int)sizeof(aMsg); b++) {
        if (aKit[b].amount == 0.0 && !aKit[b].state) continue;
        const float amount = (float)aKit[b].amount;
        aMsg[n++] = (uint8_t)b;
        memcpy(aMsg + n, &amount, 4); n += 4;
        aMsg[n++] = (uint8_t)aKit[b].state;
        count++;
    }
    aMsg[countAt] = (uint8_t)(count & 0xFF);
    aMsg[countAt + 1] = (uint8_t)(count >> 8);
    jkNetExt_Send(JKNETEXT_COOPKIT, aMsg, n, idTo);
}

static void jkCoopSave_KitArrived(int idFrom, const uint8_t* pData, int len)
{
    if (!jkCoop_IsActive() || len < 2 + (JKCOOPSAVE_ID_LEN - 1) + 2 || pData[0] != JKCOOPSAVE_VERSION)
        return;
    const int flags = pData[1];
    char id[JKCOOPSAVE_ID_LEN];
    memcpy(id, pData + 2, JKCOOPSAVE_ID_LEN - 1);
    id[JKCOOPSAVE_ID_LEN - 1] = 0;
    if (!jkCoopSave_IdOk(id))
        return;
    int n = 2 + JKCOOPSAVE_ID_LEN - 1;
    const int count = pData[n] | (pData[n + 1] << 8);
    n += 2;
    jkCoopKitBin aKit[SITHBIN_NUMBINS];
    memset(aKit, 0, sizeof(aKit));
    for (int k = 0; k < count && n + 6 <= len; k++, n += 6) {
        float amount;
        memcpy(&amount, pData + n + 1, 4);
        if (pData[n] < SITHBIN_NUMBINS) {
            aKit[pData[n]].amount = amount;
            aKit[pData[n]].state = pData[n + 5];
        }
    }

    if (stdComm_bIsServer) {
        // a guest's kit: kept for the save; one who came in with nothing given theirs back, if kept.
        // (Not the host's own id, nor one another game in this session sent first.)
        if (!strcmp(id, jkCoopSave_MyId()))
            return;
        jkCoopSavePlayer* p = jkCoopSave_Player(id, 1);
        if (!p || (p->netId && p->netId != idFrom))
            return;
        p->netId = idFrom;
        int bKnown = 0;
        for (int b = 0; b < SITHBIN_NUMBINS && !bKnown; b++) bKnown = p->aKit[b].amount != 0.0 || p->aKit[b].state;
        if ((flags & JKCOOPSAVE_FRESH) && bKnown) {
            stdPlatform_Printf("OpenJKDF2: co-op, %s's gear given back\n", id);
            jkCoopSave_SendKit(id, p->aKit, 0, idFrom);
            return;
        }
        memcpy(p->aKit, aKit, sizeof(aKit));
        jkCoopSave_MarkDirty();
    }
    else if (idFrom == (int)sithNet_serverNetId && !strcmp(id, jkCoopSave_MyId())) {
        // ours, from the host
        stdPlatform_Printf("OpenJKDF2: co-op, gear given back by the host\n");
        jkCoop_SetKit(aKit);
    }
}

// ------------------------------------------------------------------ the game's side

void jkCoopSave_Startup(void)
{
    jkNetExt_Register(JKNETEXT_COOPKIT, jkCoopSave_KitArrived);
}

void jkCoopSave_LevelLoading(const char* pEpisodeGob, char** ppLevel)
{
    stdString_SafeStrCopy(jkCoopSave_episode, pEpisodeGob ? pEpisodeGob : "", sizeof(jkCoopSave_episode));
    jkCoopSave_bHostKitPending = 0;
    memset(jkCoopSave_aPlayer, 0, sizeof(jkCoopSave_aPlayer));
    jkCoopSave_bDirty = 0;
    jkCoopSave_bResumed = 0;
    jkCoopSave_bWrittenThisSession = 0;
    // (a co-op game being hosted, from its start: carried on from the save)
    if (!stdComm_bIsServer || !(sithMulti_multiModeFlags & MULTIMODEFLAG_COOP) || !wuRegistry_GetBool("bCoopResume", 1))
        return;
    if (!jkCoopSave_Read(jkCoopSave_resumeLevel, sizeof(jkCoopSave_resumeLevel)))
        return;
    stdPlatform_Printf("OpenJKDF2: co-op resumed, %s at %s\n", jkCoopSave_episode, jkCoopSave_resumeLevel);
    jkCoopSave_bResumed = 1;
    if (ppLevel)
        *ppLevel = jkCoopSave_resumeLevel;
    jkCoopSave_bHostKitPending = jkCoopSave_Player(jkCoopSave_MyId(), 0) != NULL;
}

void jkCoopSave_LevelOpened(void)
{
    if (!jkCoopSave_bHostKitPending || !jkCoop_IsActive() || !stdComm_bIsServer)
        return;
    jkCoopSave_bHostKitPending = 0;
    jkCoopSavePlayer* p = jkCoopSave_Player(jkCoopSave_MyId(), 0);
    if (p)
        jkCoop_SetKit(p->aKit);
}

void jkCoopSave_KitTaken(int bCarried)
{
    jkCoopKitBin aKit[SITHBIN_NUMBINS];
    if (!jkCoop_IsActive() || !jkCoop_GetKit(aKit))
        return;
    if (stdComm_bIsServer) {
        jkCoopSavePlayer* p = jkCoopSave_Player(jkCoopSave_MyId(), 1);
        if (p)
            memcpy(p->aKit, aKit, sizeof(aKit));
        jkCoopSave_MarkDirty();
    }
    else {
        jkCoopSave_SendKit(jkCoopSave_MyId(), aKit, bCarried ? 0 : JKCOOPSAVE_FRESH, sithNet_serverNetId);
    }
}

void jkCoopSave_Flush(void)
{
    if (jkCoopSave_bDirty && stdComm_bIsServer) {
        jkCoopSave_bDirty = 0;
        jkCoopSave_Write();
    }
}

void jkCoopSave_ResumeFailed(void)
{
    // (the saved level isn't in the episode: the game's own start, and the save left alone)
    jkCoopSave_bResumed = 0;
    jkCoopSave_bHostKitPending = 0;
    memset(jkCoopSave_aPlayer, 0, sizeof(jkCoopSave_aPlayer));
}

void jkCoopSave_Tick(void)
{
    if (!jkCoopSave_bDirty || !jkCoop_IsActive() || !stdComm_bIsServer)
        return;
    if (stdPlatform_GetTimeMsec() - jkCoopSave_dirtyMs < JKCOOPSAVE_WRITE_MS)
        return;
    jkCoopSave_bDirty = 0;
    jkCoopSave_Write();
}

#endif // QOL_IMPROVEMENTS

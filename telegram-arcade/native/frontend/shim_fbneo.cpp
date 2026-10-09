// FBNeo-specific hooks for the deterministic frontend (fe.cpp).
//
// * Forces FBNeo's netgame mode before a game loads. In that mode FBNeo
//   seeds its random generator with a constant and gives the emulated
//   real-time clock (e.g. the Neo Geo calendar chip) a fixed date, so every
//   replica boots identically. It also disables hiscore.dat persistence,
//   which keeps the emulated cabinet's own score table out of netplay state.
// * Exposes driver metadata (name, parent, BIOS, players, genre, ROM CRCs)
//   used by the upload validator to identify romsets and dependencies.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

typedef unsigned int UINT32;
typedef signed int INT32;

struct BurnRomInfo {
  char *szName;
  UINT32 nLen;
  UINT32 nCrc;
  UINT32 nType;
};

struct BurnArea { void *Data; UINT32 nLen; INT32 nAddress; char *szName; };
extern "C" {
extern INT32 (*BurnAcb)(struct BurnArea *pba);
INT32 BurnAreaScan(INT32 nAction, INT32 *pnMin);
}

extern int kNetGame;
extern UINT32 nBurnDrvCount;
extern UINT32 nBurnDrvActive;

extern "C" {
char *BurnDrvGetTextA(UINT32 i);
INT32 BurnDrvGetRomInfo(struct BurnRomInfo *pri, UINT32 i);
INT32 BurnDrvGetRomName(char **pszName, UINT32 i, INT32 nAka);
INT32 BurnDrvGetHardwareCode();
INT32 BurnDrvGetFlags();
INT32 BurnDrvGetMaxPlayers();
INT32 BurnDrvGetGenreFlags();
}

#define DRV_NAME (0)
#define DRV_DATE (1)
#define DRV_FULLNAME (2)
#define DRV_MANUFACTURER (5)
#define DRV_SYSTEM (6)
#define DRV_PARENT (7)
#define DRV_BOARDROM (8)
#define DRV_SAMPLENAME (9)
#define DRV_ASCIIONLY (1 << 12)

static void json_str(std::string &out, const char *s) {
  out += '"';
  if (s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
      unsigned char c = *p;
      if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
      else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
      else if (c >= 0x80) { out += '?'; }  // keep the catalog plain ASCII
      else out += (char)c;
    }
  }
  out += '"';
}

static void append_driver(std::string &out) {
  char buf[64];
  out += "{\"name\":";
  json_str(out, BurnDrvGetTextA(DRV_NAME));
  out += ",\"fullname\":";
  json_str(out, BurnDrvGetTextA(DRV_FULLNAME));
  out += ",\"parent\":";
  json_str(out, BurnDrvGetTextA(DRV_PARENT));
  out += ",\"board\":";
  json_str(out, BurnDrvGetTextA(DRV_BOARDROM));
  out += ",\"samples\":";
  json_str(out, BurnDrvGetTextA(DRV_SAMPLENAME));
  out += ",\"system\":";
  json_str(out, BurnDrvGetTextA(DRV_SYSTEM));
  out += ",\"manufacturer\":";
  json_str(out, BurnDrvGetTextA(DRV_MANUFACTURER));
  out += ",\"date\":";
  json_str(out, BurnDrvGetTextA(DRV_DATE));
  snprintf(buf, sizeof(buf), ",\"players\":%d", BurnDrvGetMaxPlayers());
  out += buf;
  snprintf(buf, sizeof(buf), ",\"genre\":%u", (unsigned)BurnDrvGetGenreFlags());
  out += buf;
  snprintf(buf, sizeof(buf), ",\"flags\":%u", (unsigned)BurnDrvGetFlags());
  out += buf;
  snprintf(buf, sizeof(buf), ",\"hardware\":%u", (unsigned)BurnDrvGetHardwareCode());
  out += buf;
  out += ",\"roms\":[";
  int first = 1;
  for (UINT32 j = 0; j < 4096; j++) {
    struct BurnRomInfo ri;
    memset(&ri, 0, sizeof(ri));
    if (BurnDrvGetRomInfo(&ri, j)) break;
    char *name = NULL;
    if (BurnDrvGetRomName(&name, j, 0)) name = NULL;
    if (!name || !name[0]) continue;
    if (!first) out += ',';
    first = 0;
    out += "{\"n\":";
    json_str(out, name);
    snprintf(buf, sizeof(buf), ",\"s\":%u,\"c\":%u,\"t\":%u}", ri.nLen, ri.nCrc, ri.nType);
    out += buf;
  }
  out += "]}";
}

static std::string g_info;
static std::string g_catalog;

extern "C" void fe_core_pre_load(void) { kNetGame = 1; }

extern "C" const char *fe_core_driver_info(void) {
  g_info.clear();
  if (nBurnDrvActive >= nBurnDrvCount) return "{}";
  append_driver(g_info);
  return g_info.c_str();
}

extern "C" const char *fe_core_catalog(void) {
  UINT32 saved = nBurnDrvActive;
  g_catalog.clear();
  g_catalog.reserve(1 << 20);
  g_catalog += '[';
  for (UINT32 i = 0; i < nBurnDrvCount; i++) {
    nBurnDrvActive = i;
    if (i) g_catalog += ',';
    append_driver(g_catalog);
  }
  g_catalog += ']';
  nBurnDrvActive = saved;
  return g_catalog.c_str();
}

// Debug aid for desync analysis: the savestate's areas in scan order
// (name, length), matching the byte layout of retro_serialize after the
// leading nCurrentFrame variable.
static std::string g_map;
static INT32 map_acb(struct BurnArea *pba) {
  char buf[64];
  if (g_map.size() > 1) g_map += ',';
  g_map += "[";
  json_str(g_map, pba->szName ? pba->szName : "");
  snprintf(buf, sizeof(buf), ",%u]", pba->nLen);
  g_map += buf;
  return 0;
}

extern "C" const char *fe_core_state_map(int action) {
  g_map = "[";
  INT32 (*prev)(struct BurnArea *) = BurnAcb;
  BurnAcb = map_acb;
  BurnAreaScan(action, NULL);
  BurnAcb = prev;
  g_map += "]";
  return g_map.c_str();
}

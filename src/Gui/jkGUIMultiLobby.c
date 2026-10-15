// Added: entire file. See jkGUIMultiLobby.h.

#include "jkGUIMultiLobby.h"

#ifdef JKGUI_MULTI_LOBBY

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "jk.h"
#include "stdPlatform.h"
#include "SDL2_helper.h"
#include "General/stdString.h"
#include "Gui/jkGUI.h"
#include "Gui/jkGUIRend.h"
#include "Gui/jkGUIDialog.h"
#include "Gui/jkGUINetHost.h"
#include "Gui/jkGUISingleplayer.h"
#include "Gui/jkGUITitle.h"
#include "Main/jkRes.h"
#include "General/stdStrTable.h"
#include "Main/jkStrings.h"
#include "Main/Main.h"
#include "Main/jkCoop.h"
#include "Main/jkCoopSave.h"
#include "Platform/wuRegistry.h"
#include "Platform/Networking/ENet/stdComm_ENet.h"
#include "Platform/Networking/ENet/stdComm_ENetDiscover.h"
#include "Platform/Networking/stdCommTransport.h"

extern char16_t jkGuiMultiplayer_ipText[256];

#define LOBBY_NUM_NEARBY 3       // (rows of games on this Wi-Fi)
#define LOBBY_NUM_RECENT_ROWS 2  // (rows of hosts played with before)
#define LOBBY_NUM_RECENT 6       // (hosts remembered)
#define LOBBY_NUM_RESUME 5       // (rows of saved campaigns)
#define LOBBY_ROW_LEN 128

#define LOBBY_ID_NEARBY 10       // (+ the row)
#define LOBBY_ID_RECENT 20       // (+ the row)
#define LOBBY_ID_RESUME 40       // (+ the row)
#define LOBBY_ID_JOIN_TYPED 30
#define LOBBY_ID_ANSWERED 31     // (the game asked about answered, or didn't)

#define LOBBY_JOIN_ASK_MS 2500   // (how long a game picked is waited for)
#define LOBBY_RECENT_ASK_MS 1200 // (how long a host played with before is waited for)
#define LOBBY_RECENT_EVERY_MS 5000

static const char16_t* jkGuiMultiLobby_Str(const char* key)
{
    return jkStrings_GetUniStringWithFallback((char*)key);
}

static const char* jkGuiMultiLobby_ThisGame(void)
{
    return Main_bMotsCompat ? "mots" : "jk";
}

// A screen's words put in (jkStrings, at show time: a word missing shows its name, never garbage)
static void jkGuiMultiLobby_Words(jkGuiElement* aElements, const char* const* aKeys)
{
    for (int i = 0; aElements[i].type != ELEMENT_END; i++) {
        if (aKeys[i]) aElements[i].wstr = jkGuiMultiLobby_Str(aKeys[i]);
    }
}

// ------------------------------------------------------------------ the Multiplayer screen

static char16_t jkGuiMultiLobby_wResumeSub[128];

static jkGuiElement jkGuiMultiLobby_aMain[] = {
    { ELEMENT_TEXT,       0,                          6, NULL             ,            3, { 20, 20, 600, 40 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_HOST,       5, NULL            ,             3, { 0, 82, 640, 56 },    1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                          1, NULL                ,         3, { 0, 136, 640, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_JOIN,       5, NULL            ,             3, { 0, 176, 640, 56 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                          1, NULL                ,         3, { 0, 230, 640, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_RESUME,     5, NULL              ,           3, { 0, 270, 640, 56 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                          1, NULL,                         3, { 0, 324, 640, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_BACK,       2, NULL            ,             3, { 20, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_CHARACTERS, 2, NULL                        , 3, { 300, 420, 320, 56 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,                          0, NULL,                         0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static const char* const jkGuiMultiLobby_aMain_words[] = { "GUI_MULTIPLAYER", "GUIEXT_MP_HOST", "GUIEXT_MP_HOST_SUB", "GUIEXT_MP_JOIN", "GUIEXT_MP_JOIN_SUB", "GUIEXT_MP_RESUME", NULL, "GUIEXT_MP_BACK", "GUI_MULTIPLAYER_CHARACTERS", NULL };
#define LOBBY_MAIN_RESUME 5
#define LOBBY_MAIN_RESUME_SUB 6
#define LOBBY_MAIN_BACK 7

static jkGuiMenu jkGuiMultiLobby_menuMain = { jkGuiMultiLobby_aMain, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, NULL, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

// ------------------------------------------------------------------ Host Game: what kind

static jkGuiElement jkGuiMultiLobby_aHostMode[] = {
    { ELEMENT_TEXT,       0,                                6, NULL,                       3, { 20, 20, 600, 40 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_MODE_DEATHMATCH,  5, NULL                  ,     3, { 0, 110, 640, 56 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                                1, NULL                      , 3, { 0, 164, 640, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, JKGUIMULTILOBBY_MODE_COOP,        5, NULL            ,           3, { 0, 220, 640, 56 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                                1, NULL                ,       3, { 0, 274, 640, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, -1,                               2, NULL            ,           3, { 20, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,                                0, NULL,                       0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static const char* const jkGuiMultiLobby_aHostMode_words[] = { NULL, "GUIEXT_MP_DEATHMATCH", "GUIEXT_MP_DEATHMATCH_SUB", "GUIEXT_MP_COOP", "GUIEXT_MP_COOP_SUB", "GUIEXT_MP_BACK", NULL };
static jkGuiMenu jkGuiMultiLobby_menuHostMode = { jkGuiMultiLobby_aHostMode, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, NULL, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

// ------------------------------------------------------------------ Resume Co-op

static char16_t jkGuiMultiLobby_awResume[LOBBY_NUM_RESUME][LOBBY_ROW_LEN];
static char jkGuiMultiLobby_aResumeGob[LOBBY_NUM_RESUME][64];

static jkGuiElement jkGuiMultiLobby_aResume[] = {
    { ELEMENT_TEXT,       0,                    6, NULL              ,      3, { 20, 20, 600, 40 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                    1, NULL                   , 3, { 0, 76, 640, 24 },    1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RESUME + 0,  2, NULL,                    3, { 40, 110, 560, 50 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RESUME + 1,  2, NULL,                    3, { 40, 165, 560, 50 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RESUME + 2,  2, NULL,                    3, { 40, 220, 560, 50 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RESUME + 3,  2, NULL,                    3, { 40, 275, 560, 50 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RESUME + 4,  2, NULL,                    3, { 40, 330, 560, 50 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, -1,                   2, NULL            ,        3, { 20, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_LISTBOX,    0,                    0, NULL,                    0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,                    0, NULL,                    0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static const char* const jkGuiMultiLobby_aResume_words[] = { "GUIEXT_MP_RESUME", "GUIEXT_MP_RESUME_PICK", NULL, NULL, NULL, NULL, NULL, "GUIEXT_MP_BACK", NULL, NULL };
#define LOBBY_RESUME_ROW0 2
#define LOBBY_RESUME_EPISODES 8 // (a hidden list the episodes are read into)
static jkGuiMenu jkGuiMultiLobby_menuResume = { jkGuiMultiLobby_aResume, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, NULL, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

// ------------------------------------------------------------------ Join Game

static char16_t jkGuiMultiLobby_awNearby[LOBBY_NUM_NEARBY][LOBBY_ROW_LEN];
static char16_t jkGuiMultiLobby_awRecent[LOBBY_NUM_RECENT_ROWS][LOBBY_ROW_LEN];
static char16_t jkGuiMultiLobby_wNearbyStatus[LOBBY_ROW_LEN];
static char16_t jkGuiMultiLobby_wJoinStatus[LOBBY_ROW_LEN];

static jkGuiElement jkGuiMultiLobby_aJoin[] = {
    { ELEMENT_TEXT,       0,                        6, NULL, 3, { 20, 10, 600, 40 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                        1, NULL, 2, { 40, 54, 560, 22 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_NEARBY + 0,      2, NULL, 2, { 40, 78, 560, 46 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_NEARBY + 1,      2, NULL, 2, { 40, 124, 560, 46 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_NEARBY + 2,      2, NULL, 2, { 40, 170, 560, 46 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                        0, NULL, 2, { 56, 78, 544, 46 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                        1, NULL, 2, { 40, 222, 560, 22 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RECENT + 0,      2, NULL, 2, { 40, 246, 560, 46 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_RECENT + 1,      2, NULL, 2, { 40, 292, 560, 46 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                        1, NULL, 2, { 40, 350, 110, 46 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBOX,    0,                        0, NULL, 100, { 150, 358, 300, 30 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, LOBBY_ID_JOIN_TYPED,      2, NULL, 3, { 460, 348, 160, 50 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, -1,                       2, NULL, 3, { 20, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,                        0, NULL, 3, { 190, 420, 430, 56 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,                        0, NULL, 0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static const char* const jkGuiMultiLobby_aJoin_words[] = { "GUIEXT_MP_JOIN", "GUIEXT_MP_NEARBY", NULL, NULL, NULL, NULL, "GUIEXT_MP_RECENT", NULL, NULL, "GUIEXT_MP_ADDRESS", NULL, "GUIEXT_MP_JOIN_BUTTON", "GUIEXT_MP_BACK", NULL, NULL };
#define LOBBY_JOIN_NEARBY0 2
#define LOBBY_JOIN_NEARBY_STATUS 5
#define LOBBY_JOIN_RECENT_LABEL 6
#define LOBBY_JOIN_RECENT0 7
#define LOBBY_JOIN_ADDRESS 10
#define LOBBY_JOIN_TYPED 11
#define LOBBY_JOIN_BACK 12
#define LOBBY_JOIN_STATUS 13

static void jkGuiMultiLobby_JoinIdle(jkGuiMenu* pMenu);
static jkGuiMenu jkGuiMultiLobby_menuJoin = { jkGuiMultiLobby_aJoin, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, jkGuiMultiLobby_JoinIdle, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

// The password asked for
static char16_t jkGuiMultiLobby_wPasswordTitle[LOBBY_ROW_LEN];
static jkGuiElement jkGuiMultiLobby_aPassword[] = {
    { ELEMENT_TEXT,       0,  6, NULL             ,           3, { 20, 20, 600, 40 },    1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,  2, NULL,                        3, { 20, 120, 600, 30 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,  1, NULL                       , 3, { 20, 170, 600, 30 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBOX,    0,  0, NULL,                        16, { 170, 212, 300, 32 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, 1,  2, NULL                   ,     3, { 460, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, -1, 2, NULL            ,            3, { 20, 420, 160, 56 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,  0, NULL,                        3, { 20, 262, 600, 30 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,  0, NULL,                        0, { 0, 0, 0, 0 },         0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static const char* const jkGuiMultiLobby_aPassword_words[] = { "GUINET_PASSWORD", NULL, "GUIEXT_MP_PASSWORD_PROMPT", NULL, "GUIEXT_MP_JOIN_BUTTON", "GUIEXT_MP_BACK", NULL, NULL };
#define LOBBY_PASSWORD_STATUS 6
static char16_t jkGuiMultiLobby_wPasswordStatus[LOBBY_ROW_LEN];
static void jkGuiMultiLobby_PasswordIdle(jkGuiMenu* pMenu);
static jkGuiMenu jkGuiMultiLobby_menuPassword = { jkGuiMultiLobby_aPassword, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, jkGuiMultiLobby_PasswordIdle, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };

// ------------------------------------------------------------------ hosts played with before

typedef struct jkGuiMultiLobbyRecent
{
    char address[64];
    char hostId[20];
    char name[64];
} jkGuiMultiLobbyRecent;

static jkGuiMultiLobbyRecent jkGuiMultiLobby_aRecent[LOBBY_NUM_RECENT];

// An address as kept and compared: trimmed, with its port (the game's own, 27020, if none given)
static void jkGuiMultiLobby_NormAddress(const char* pIn, char* pOut, int outLen)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s", pIn ? pIn : "");
    char* s = tmp;
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    if (*s && !strchr(s, ':')) snprintf(pOut, outLen, "%s:27020", s);
    else snprintf(pOut, outLen, "%s", s);
}
static int jkGuiMultiLobby_numRecent = 0;

static void jkGuiMultiLobby_LoadRecent(void)
{
    jkGuiMultiLobby_numRecent = 0;
    for (int i = 0; i < LOBBY_NUM_RECENT; i++) {
        char key[32], val[200] = "";
        snprintf(key, sizeof(key), "mpRecentHost%d", i);
        wuRegistry_GetStr(key, val, sizeof(val), "");
        // ("address<TAB>host id<TAB>name")
        char* t1 = strchr(val, '\t');
        char* t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!val[0] || !t1 || !t2) continue;
        *t1 = 0;
        *t2 = 0;
        jkGuiMultiLobbyRecent* r = &jkGuiMultiLobby_aRecent[jkGuiMultiLobby_numRecent++];
        jkGuiMultiLobby_NormAddress(val, r->address, sizeof(r->address));
        snprintf(r->hostId, sizeof(r->hostId), "%s", t1 + 1);
        snprintf(r->name, sizeof(r->name), "%s", t2 + 1);
    }
    // (the address joined last by the old screen, kept)
    if (!jkGuiMultiLobby_numRecent) {
        char16_t wLast[256];
        char last[256];
        wuRegistry_GetWString("lastConnectedHost", wLast, 256, u"");
        stdString_WcharToChar(last, wLast, 255);
        last[255] = 0;
        if (last[0] && strcmp(last, "127.0.0.1")) {
            jkGuiMultiLobbyRecent* r = &jkGuiMultiLobby_aRecent[jkGuiMultiLobby_numRecent++];
            memset(r, 0, sizeof(*r));
            jkGuiMultiLobby_NormAddress(last, r->address, sizeof(r->address));
            snprintf(r->name, sizeof(r->name), "%s", last);
        }
    }
}

static void jkGuiMultiLobby_SaveRecent(void)
{
    for (int i = 0; i < LOBBY_NUM_RECENT; i++) {
        char key[32], val[200] = "";
        snprintf(key, sizeof(key), "mpRecentHost%d", i);
        if (i < jkGuiMultiLobby_numRecent) {
            const jkGuiMultiLobbyRecent* r = &jkGuiMultiLobby_aRecent[i];
            snprintf(val, sizeof(val), "%s\t%s\t%s", r->address, r->hostId, r->name);
        }
        wuRegistry_SaveStr(key, val);
    }
}

// A host joined: first among those played with before (the same host's older address replaced)
static void jkGuiMultiLobby_Remember(const char* address, const char* hostId, const char* name)
{
    jkGuiMultiLobbyRecent keep[LOBBY_NUM_RECENT];
    int n = 0;
    memset(&keep[0], 0, sizeof(keep[0]));
    jkGuiMultiLobby_NormAddress(address, keep[0].address, sizeof(keep[0].address));
    snprintf(keep[0].hostId, sizeof(keep[0].hostId), "%s", hostId ? hostId : "");
    snprintf(keep[0].name, sizeof(keep[0].name), "%s", (name && name[0]) ? name : address);
    // (no tabs in what is kept)
    for (char* c = keep[0].name; *c; c++) if (*c == '\t') *c = ' ';
    n = 1;
    for (int i = 0; i < jkGuiMultiLobby_numRecent && n < LOBBY_NUM_RECENT; i++) {
        const jkGuiMultiLobbyRecent* r = &jkGuiMultiLobby_aRecent[i];
        if (!strcmp(r->address, keep[0].address)) continue;
        if (keep[0].hostId[0] && !strcmp(r->hostId, keep[0].hostId)) continue;
        keep[n++] = *r;
    }
    memcpy(jkGuiMultiLobby_aRecent, keep, sizeof(keep[0]) * n);
    jkGuiMultiLobby_numRecent = n;
    jkGuiMultiLobby_SaveRecent();
}

// ------------------------------------------------------------------ asking games (a thread)

// What the asking thread found out, by address (the hosts played with before, and the games found
// nearby: one whose record is still about after it went is seen not to answer), and about the game
// picked
enum { LOBBY_ASK_UNKNOWN = 0, LOBBY_ASK_HERE = 1, LOBBY_ASK_GONE = 2, LOBBY_ASK_BADADDRESS = 3 };
#define LOBBY_NUM_ASK (LOBBY_NUM_RECENT + LOBBY_NUM_NEARBY + 2)

typedef struct jkGuiMultiLobbyAsk
{
    char address[64];
    int state;
    jkMultiEntry entry;
} jkGuiMultiLobbyAsk;

// One asking thread for each visit to the Join screen, with its own "keep going": leaving the screen
// never waits for it (an address that is a name can take a while to look up); one left behind
// finishes by itself and writes nothing more
typedef struct jkGuiMultiLobbyAsker
{
    volatile int bRun;
} jkGuiMultiLobbyAsker;

static SDL_Mutex* jkGuiMultiLobby_mutex = NULL;
static jkGuiMultiLobbyAsker* jkGuiMultiLobby_pAsker = NULL; // (the current one; under the mutex)

static jkGuiMultiLobbyAsk jkGuiMultiLobby_aAsk[LOBBY_NUM_ASK];
static int jkGuiMultiLobby_numAsk = 0;

static char jkGuiMultiLobby_joinAddress[64];
static int jkGuiMultiLobby_joinAsk = 0;     // (counts up each time a game is picked)
static int jkGuiMultiLobby_joinAnswered = 0; // (the last one answered: its count)
static int jkGuiMultiLobby_joinResult = 0;   // (stdCommTransport_ProbeAddress's, or _CheckPassword's)
static int jkGuiMultiLobby_joinKind = STDCOMM_TRANSPORT_ENET; // (the transport the game picked answered on)
static int jkGuiMultiLobby_bJoinIsPassword = 0; // (the asking: the password typed checked, not the game asked about)
static char16_t jkGuiMultiLobby_joinPassword[33];
static jkMultiEntry jkGuiMultiLobby_joinEntry;

// (under the mutex) An address to keep asking about; its entry
static jkGuiMultiLobbyAsk* jkGuiMultiLobby_AskAbout(const char* address)
{
    for (int i = 0; i < jkGuiMultiLobby_numAsk; i++)
        if (!strcmp(jkGuiMultiLobby_aAsk[i].address, address)) return &jkGuiMultiLobby_aAsk[i];
    if (jkGuiMultiLobby_numAsk >= LOBBY_NUM_ASK) return NULL;
    jkGuiMultiLobbyAsk* a = &jkGuiMultiLobby_aAsk[jkGuiMultiLobby_numAsk++];
    memset(a, 0, sizeof(*a));
    snprintf(a->address, sizeof(a->address), "%s", address);
    return a;
}

static int jkGuiMultiLobby_AskThread(void* pArg)
{
    jkGuiMultiLobbyAsker* pSelf = (jkGuiMultiLobbyAsker*)pArg;
    uint32_t lastRoundMs = 0;
    while (pSelf->bRun) {
        // (the game picked)
        SDL_LockMutex(jkGuiMultiLobby_mutex);
        const int ask = jkGuiMultiLobby_joinAsk;
        const int bPending = ask != jkGuiMultiLobby_joinAnswered;
        const int bPassword = jkGuiMultiLobby_bJoinIsPassword;
        const int passwordKind = jkGuiMultiLobby_joinKind;
        char address[64];
        char16_t password[33];
        snprintf(address, sizeof(address), "%s", jkGuiMultiLobby_joinAddress);
        memcpy(password, jkGuiMultiLobby_joinPassword, sizeof(password));
        SDL_UnlockMutex(jkGuiMultiLobby_mutex);
        if (bPending) {
            jkMultiEntry entry;
            memset(&entry, 0, sizeof(entry));
            // (a game asked about over every transport at once; its password over the one it answered on)
            int kind = passwordKind;
            const int r = bPassword ? stdCommTransport_CheckPassword(passwordKind, address, password, LOBBY_JOIN_ASK_MS + 2000, &pSelf->bRun)
                                    : stdCommTransport_ProbeAddress(address, &entry, LOBBY_JOIN_ASK_MS, &pSelf->bRun, &kind);
            memset(password, 0, sizeof(password));
            SDL_LockMutex(jkGuiMultiLobby_mutex);
            if (pSelf->bRun && jkGuiMultiLobby_joinAsk == ask) {
                if (!bPassword) {
                    jkGuiMultiLobby_joinEntry = entry;
                    jkGuiMultiLobby_joinKind = kind;
                }
                jkGuiMultiLobby_joinResult = r;
                jkGuiMultiLobby_joinAnswered = ask;
            }
            SDL_UnlockMutex(jkGuiMultiLobby_mutex);
            continue;
        }

        // (the addresses known, now and then: each asked, the game picked first if one is)
        const uint32_t now = stdPlatform_GetTimeMsec();
        if (lastRoundMs && now - lastRoundMs < LOBBY_RECENT_EVERY_MS) {
            SDL_Delay(50);
            continue;
        }
        lastRoundMs = now ? now : 1;
        for (int i = 0; pSelf->bRun; i++) {
            SDL_LockMutex(jkGuiMultiLobby_mutex);
            const int bMore = i < jkGuiMultiLobby_numAsk && jkGuiMultiLobby_joinAsk == jkGuiMultiLobby_joinAnswered;
            if (bMore) snprintf(address, sizeof(address), "%s", jkGuiMultiLobby_aAsk[i].address);
            SDL_UnlockMutex(jkGuiMultiLobby_mutex);
            if (!bMore) break;
            jkMultiEntry entry;
            memset(&entry, 0, sizeof(entry));
            const int r = stdCommTransport_ProbeAddress(address, &entry, LOBBY_RECENT_ASK_MS, &pSelf->bRun, NULL);
            SDL_LockMutex(jkGuiMultiLobby_mutex);
            if (pSelf->bRun && i < jkGuiMultiLobby_numAsk && !strcmp(jkGuiMultiLobby_aAsk[i].address, address)) {
                jkGuiMultiLobby_aAsk[i].state = r > 0 ? LOBBY_ASK_HERE : r < 0 ? LOBBY_ASK_BADADDRESS : LOBBY_ASK_GONE;
                jkGuiMultiLobby_aAsk[i].entry = entry;
            }
            SDL_UnlockMutex(jkGuiMultiLobby_mutex);
        }
    }
    free(pSelf);
    return 0;
}

static void jkGuiMultiLobby_StopAsking(void)
{
    if (!jkGuiMultiLobby_mutex) return;
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    if (jkGuiMultiLobby_pAsker) jkGuiMultiLobby_pAsker->bRun = 0; // (it ends, and frees itself, by itself)
    jkGuiMultiLobby_pAsker = NULL;
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
}

static void jkGuiMultiLobby_StartAsking(void)
{
    if (!jkGuiMultiLobby_mutex) jkGuiMultiLobby_mutex = SDL_CreateMutex();
    jkGuiMultiLobby_StopAsking();
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    jkGuiMultiLobby_numAsk = 0;
    for (int i = 0; i < jkGuiMultiLobby_numRecent; i++)
        jkGuiMultiLobby_AskAbout(jkGuiMultiLobby_aRecent[i].address);
    jkGuiMultiLobby_joinAnswered = jkGuiMultiLobby_joinAsk;
    jkGuiMultiLobbyAsker* pAsker = (jkGuiMultiLobbyAsker*)malloc(sizeof(jkGuiMultiLobbyAsker));
    if (pAsker) {
        pAsker->bRun = 1;
        SDL_Thread* pThread = SDL_CreateThread(jkGuiMultiLobby_AskThread, "jkGuiMultiLobby", pAsker);
        if (pThread) {
            SDL_DetachThread(pThread);
            jkGuiMultiLobby_pAsker = pAsker;
        }
        else {
            free(pAsker);
        }
    }
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
}

// ------------------------------------------------------------------ the Join screen's rows

// What the Join screen shows, row by row: where each row's game is
typedef struct jkGuiMultiLobbyTarget
{
    char address[64];
    char hostId[20];
    char name[64];
    int bPassword;
} jkGuiMultiLobbyTarget;

static jkGuiMultiLobbyTarget jkGuiMultiLobby_aNearbyTarget[LOBBY_NUM_NEARBY];
static jkGuiMultiLobbyTarget jkGuiMultiLobby_aRecentTarget[LOBBY_NUM_RECENT_ROWS];
static int jkGuiMultiLobby_numNearbyRows = -1;
static int jkGuiMultiLobby_numRecentRows = -1;
static uint32_t jkGuiMultiLobby_rowsMs = 0;
static int jkGuiMultiLobby_bConnecting = 0;
static uint32_t jkGuiMultiLobby_connectingMs = 0;
static jkGuiMultiLobbyTarget jkGuiMultiLobby_picked; // (the game being joined)

// A level's title as plain text (for a game asked rather than heard, whose host gives only its file),
// from this game's own level titles, loaded the first time one is wanted; "" if not known
static stdStrTable jkGuiMultiLobby_titles;
static int jkGuiMultiLobby_bTitles = 0;
static void jkGuiMultiLobby_LevelTitle(char* pOut, int outLen, const char* level)
{
    pOut[0] = 0;
    if (!level || !level[0]) return;
    if (!jkGuiMultiLobby_bTitles) {
        memset(&jkGuiMultiLobby_titles, 0, sizeof(jkGuiMultiLobby_titles));
        stdStrTable_Load(&jkGuiMultiLobby_titles, "misc\\cogStrings.uni"); // (its result means little: an early END reads as 0)
        jkGuiMultiLobby_bTitles = 1;
    }
    char16_t wTitle[64];
    if (!jkGuiTitle_LevelTitle(&jkGuiMultiLobby_titles, level, wTitle, 64)) return;
    int n = 0;
    for (int i = 0; wTitle[i] && n < outLen - 1; i++) {
        const char16_t c = wTitle[i];
        pOut[n++] = (c == 0x2018 || c == 0x2019) ? '\'' : (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    pOut[n] = 0;
}

static void jkGuiMultiLobby_ModeText(char16_t* pOut, int outLen, const char* mode, const char* episodeGob, int flags)
{
    // (heard: the host's word for it; asked: worked out from its episode and flags)
    if (!mode) mode = jkGuiNetHost_ModeName(episodeGob, flags);
    const char* key = "GUIEXT_MP_MODE_DM";
    if (!strcmp(mode, "coop")) key = "GUIEXT_MP_MODE_COOP";
    else if (!strcmp(mode, "team")) key = "GUIEXT_MP_MODE_TEAM";
    else if (!strcmp(mode, "ctf")) key = "GUIEXT_MP_MODE_CTF";
    else if (!strcmp(mode, "kfy")) key = "GUIEXT_MP_MODE_KFY";
    else if (!strcmp(mode, "saber")) key = "GUIEXT_MP_MODE_SABER";
    stdString_SafeWStrCopy(pOut, jkGuiMultiLobby_Str(key), outLen);
}

// A game's row: "name - Mode: level  n/max  (password)", the level by its title when known (without its
// "1 - " number, and shortened to fit the row)
#define LOBBY_ROW_FIT 60
static void jkGuiMultiLobby_GameRow(char16_t* pOut, int outLen, const char* name, const char* mode, const char* episodeGob, int flags, const char* level,
                                    const char* title, int numPlayers, int maxPlayers, int bPassword)
{
    char16_t wMode[32];
    char lvl[64];
    jkGuiMultiLobby_ModeText(wMode, 32, mode, episodeGob, flags);
    if (title && title[0]) {
        const char* t = title;
        const char* dash = strstr(t, " - ");
        if (dash && dash - t <= 3) {
            int bNum = 1;
            for (const char* c = t; c < dash; c++) bNum = bNum && *c >= '0' && *c <= '9';
            if (bNum) t = dash + 3;
        }
        snprintf(lvl, sizeof(lvl), "%s", t);
    }
    else {
        snprintf(lvl, sizeof(lvl), "%s", level ? level : "");
        char* dot = strrchr(lvl, '.');
        if (dot && !__strcmpi(dot, ".jkl")) *dot = 0;
    }
    const char16_t* wSuffix = (maxPlayers > 0 && numPlayers >= maxPlayers) ? jkGuiMultiLobby_Str("GUIEXT_MP_FULL")
                            : bPassword ? jkGuiMultiLobby_Str("GUIEXT_MP_HAS_PASSWORD") : NULL;
    // (the title shortened if the row would be too long)
    const int fixedLen = (int)strlen(name) + (int)_wcslen(wMode) + 14 + (wSuffix ? (int)_wcslen(wSuffix) + 4 : 0);
    const int room = LOBBY_ROW_FIT - fixedLen;
    if ((int)strlen(lvl) > room) {
        const int keep = room > 6 ? room - 3 : 3;
        if (keep < (int)strlen(lvl)) {
            lvl[keep] = 0;
            strcat(lvl, "...");
        }
    }
    jk_snwprintf(pOut, outLen, u"%S - %s: %S   %d/%d", name, wMode, lvl, numPlayers, maxPlayers);
    if (wSuffix) {
        int n = (int)_wcslen(pOut);
        jk_snwprintf(pOut + n, outLen - n, u"  (%s)", wSuffix);
    }
}

static void jkGuiMultiLobby_SetRow(jkGuiElement* pElement, jkGuiMenu* pMenu, char16_t* pText, const char16_t* pNew, int bVisible, int bRedraw)
{
    int bChanged = pElement->bIsVisible != bVisible || __wcscmp(pText, pNew);
    if (!bChanged) return;
    stdString_SafeWStrCopy(pText, pNew, LOBBY_ROW_LEN);
    pElement->wstr = pText;
    if (bRedraw) jkGuiRend_SetVisibleAndDraw(pElement, pMenu, bVisible);
    else pElement->bIsVisible = bVisible;
}

// The rows brought up to date: games found nearby, then the hosts played with before that aren't
// among them (a host found nearby is that host, wherever it was before)
static void jkGuiMultiLobby_UpdateRows(jkGuiMenu* pMenu, int bRedraw)
{
    // (the games found nearby, but not one seen not to answer: a host gone whose record is still about)
    stdCommENetDiscoverGame aFound[STDCOMM_ENETDISCOVER_MAX_GAMES];
    const int numFound = stdComm_ENetDiscover_GetGames(aFound, STDCOMM_ENETDISCOVER_MAX_GAMES);
    stdCommENetDiscoverGame aGame[LOBBY_NUM_NEARBY];
    int numGames = 0;
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    for (int i = 0; i < numFound && numGames < LOBBY_NUM_NEARBY; i++) {
        const jkGuiMultiLobbyAsk* pAsk = jkGuiMultiLobby_AskAbout(aFound[i].address);
        if (pAsk && pAsk->state == LOBBY_ASK_GONE) continue;
        aGame[numGames++] = aFound[i];
    }
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
    char16_t row[LOBBY_ROW_LEN];

    for (int i = 0; i < LOBBY_NUM_NEARBY; i++) {
        jkGuiElement* e = &jkGuiMultiLobby_aJoin[LOBBY_JOIN_NEARBY0 + i];
        if (i < numGames) {
            const stdCommENetDiscoverGame* g = &aGame[i];
            jkGuiMultiLobby_GameRow(row, LOBBY_ROW_LEN, g->name, g->mode, NULL, 0, g->level, g->title, g->numPlayers, g->maxPlayers, g->bPassword);
            jkGuiMultiLobbyTarget* t = &jkGuiMultiLobby_aNearbyTarget[i];
            snprintf(t->address, sizeof(t->address), "%s", g->address);
            snprintf(t->hostId, sizeof(t->hostId), "%s", g->hostId);
            snprintf(t->name, sizeof(t->name), "%s", g->name);
            t->bPassword = g->bPassword;
            jkGuiMultiLobby_SetRow(e, pMenu, jkGuiMultiLobby_awNearby[i], row, 1, bRedraw);
        }
        else {
            jkGuiMultiLobby_SetRow(e, pMenu, jkGuiMultiLobby_awNearby[i], u"", 0, bRedraw);
        }
    }
    // (nothing nearby: why)
    const char16_t* status = numGames ? u""
                           : jkGuiMultiLobby_Str(!stdComm_ENetDiscover_IsAvailable() ? "GUIEXT_MP_NO_DISCOVERY"
                                                 : stdComm_ENetDiscover_IsBlocked() ? "GUIEXT_MP_BLOCKED" : "GUIEXT_MP_SEARCHING");
    jkGuiMultiLobby_SetRow(&jkGuiMultiLobby_aJoin[LOBBY_JOIN_NEARBY_STATUS], pMenu, jkGuiMultiLobby_wNearbyStatus, status, numGames == 0, bRedraw);

    int n = 0;
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    for (int i = 0; i < jkGuiMultiLobby_numRecent && n < LOBBY_NUM_RECENT_ROWS; i++) {
        const jkGuiMultiLobbyRecent* r = &jkGuiMultiLobby_aRecent[i];
        int bNearby = 0;
        for (int k = 0; k < numGames && !bNearby; k++) {
            bNearby = (r->hostId[0] && !strcmp(r->hostId, aGame[k].hostId)) || !strcmp(r->address, aGame[k].address);
        }
        if (bNearby) continue;
        const jkGuiMultiLobbyAsk* pAsk = jkGuiMultiLobby_AskAbout(r->address);
        const int state = pAsk ? pAsk->state : LOBBY_ASK_UNKNOWN;
        jkMultiEntry entryNone;
        memset(&entryNone, 0, sizeof(entryNone));
        const jkMultiEntry* pEntry = pAsk ? &pAsk->entry : &entryNone;
        if (state == LOBBY_ASK_HERE) {
            char name[64];
            stdString_WcharToChar(name, pEntry->serverName, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            int maxPlayers = (int)pEntry->maxPlayers;
            if ((pEntry->sessionFlags & SESSIONFLAG_ISDEDICATED) && maxPlayers > 1) maxPlayers--;
            char title[64];
            jkGuiMultiLobby_LevelTitle(title, sizeof(title), pEntry->mapJklFname);
            jkGuiMultiLobby_GameRow(row, LOBBY_ROW_LEN, name[0] ? name : r->name, NULL, pEntry->episodeGobName, pEntry->multiModeFlags, pEntry->mapJklFname,
                                    title, pEntry->numPlayers, maxPlayers, (pEntry->sessionFlags & SESSIONFLAG_PASSWORD) != 0);
        }
        else {
            const char* key = state == LOBBY_ASK_UNKNOWN ? "GUIEXT_MP_CHECKING" : "GUIEXT_MP_NOT_ANSWERING";
            jk_snwprintf(row, LOBBY_ROW_LEN, u"%S   (%S, %s)", r->name, r->address, jkGuiMultiLobby_Str(key));
        }
        jkGuiMultiLobbyTarget* t = &jkGuiMultiLobby_aRecentTarget[n];
        snprintf(t->address, sizeof(t->address), "%s", r->address);
        snprintf(t->hostId, sizeof(t->hostId), "%s", r->hostId);
        snprintf(t->name, sizeof(t->name), "%s", r->name);
        t->bPassword = 0;
        jkGuiMultiLobby_SetRow(&jkGuiMultiLobby_aJoin[LOBBY_JOIN_RECENT0 + n], pMenu, jkGuiMultiLobby_awRecent[n], row, 1, bRedraw);
        n++;
    }
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
    for (int i = n; i < LOBBY_NUM_RECENT_ROWS; i++)
        jkGuiMultiLobby_SetRow(&jkGuiMultiLobby_aJoin[LOBBY_JOIN_RECENT0 + i], pMenu, jkGuiMultiLobby_awRecent[i], u"", 0, bRedraw);
    if (jkGuiMultiLobby_aJoin[LOBBY_JOIN_RECENT_LABEL].bIsVisible != (n > 0)) {
        if (bRedraw) jkGuiRend_SetVisibleAndDraw(&jkGuiMultiLobby_aJoin[LOBBY_JOIN_RECENT_LABEL], pMenu, n > 0);
        else jkGuiMultiLobby_aJoin[LOBBY_JOIN_RECENT_LABEL].bIsVisible = n > 0;
    }
    jkGuiMultiLobby_numNearbyRows = numGames;
    jkGuiMultiLobby_numRecentRows = n;
}

static void jkGuiMultiLobby_SetStatus(jkGuiMenu* pMenu, const char16_t* pText)
{
    stdString_SafeWStrCopy(jkGuiMultiLobby_wJoinStatus, pText, LOBBY_ROW_LEN);
    jkGuiMultiLobby_aJoin[LOBBY_JOIN_STATUS].wstr = jkGuiMultiLobby_wJoinStatus;
    jkGuiRend_UpdateAndDrawClickable(&jkGuiMultiLobby_aJoin[LOBBY_JOIN_STATUS], pMenu, 1);
}

static void jkGuiMultiLobby_JoinIdle(jkGuiMenu* pMenu)
{
    const uint32_t now = stdPlatform_GetTimeMsec();
    if (now - jkGuiMultiLobby_rowsMs > 500) {
        jkGuiMultiLobby_rowsMs = now;
        jkGuiMultiLobby_UpdateRows(pMenu, 1);
    }
    if (jkGuiMultiLobby_bConnecting) {
        SDL_LockMutex(jkGuiMultiLobby_mutex);
        const int bAnswered = jkGuiMultiLobby_joinAnswered == jkGuiMultiLobby_joinAsk;
        SDL_UnlockMutex(jkGuiMultiLobby_mutex);
        if (bAnswered) pMenu->lastClicked = LOBBY_ID_ANSWERED;
    }
}

static void jkGuiMultiLobby_PasswordIdle(jkGuiMenu* pMenu)
{
    if (!jkGuiMultiLobby_bConnecting) return;
    jkGuiRend_UpdateAndDrawClickable(&jkGuiMultiLobby_aPassword[LOBBY_PASSWORD_STATUS], pMenu, 1);
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    const int bAnswered = jkGuiMultiLobby_joinAnswered == jkGuiMultiLobby_joinAsk;
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
    if (bAnswered) pMenu->lastClicked = LOBBY_ID_ANSWERED;
}

// The game picked asked about (the asking thread), "Connecting..." shown meanwhile
static void jkGuiMultiLobby_Pick(jkGuiMenu* pMenu, const jkGuiMultiLobbyTarget* pTarget)
{
    jkGuiMultiLobby_picked = *pTarget;
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    snprintf(jkGuiMultiLobby_joinAddress, sizeof(jkGuiMultiLobby_joinAddress), "%s", pTarget->address);
    jkGuiMultiLobby_bJoinIsPassword = 0;
    jkGuiMultiLobby_joinAsk++;
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
    jkGuiMultiLobby_bConnecting = 1;
    jkGuiMultiLobby_connectingMs = stdPlatform_GetTimeMsec();
    char16_t text[LOBBY_ROW_LEN];
    jk_snwprintf(text, LOBBY_ROW_LEN, u"%s %S", jkGuiMultiLobby_Str("GUIEXT_MP_CONNECTING"), pTarget->name[0] ? pTarget->name : pTarget->address);
    jkGuiMultiLobby_SetStatus(pMenu, text);
}

// The game picked answered (or not): 1 if it can be joined (and the password, if it has one, typed)
static int jkGuiMultiLobby_Answered(jkGuiMenu* pMenu)
{
    jkGuiMultiLobby_bConnecting = 0;
    jkGuiMultiLobby_SetStatus(pMenu, u"");
    SDL_LockMutex(jkGuiMultiLobby_mutex);
    const int result = jkGuiMultiLobby_joinResult;
    const jkMultiEntry entry = jkGuiMultiLobby_joinEntry;
    const int kind = jkGuiMultiLobby_joinKind;
    SDL_UnlockMutex(jkGuiMultiLobby_mutex);

    char16_t msg[256];
    if (result < 0) {
        jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), (char16_t*)jkGuiMultiLobby_Str("GUIEXT_MP_ERR_ADDRESS"));
        return 0;
    }
    if (result == 0) {
        char16_t wAddr[64];
        stdString_CharToWchar(wAddr, jkGuiMultiLobby_picked.address, 63);
        wAddr[63] = 0;
        jk_snwprintf(msg, 256, jkGuiMultiLobby_Str("GUIEXT_MP_ERR_NOGAME"), wAddr);
        jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), msg);
        return 0;
    }
    int maxPlayers = (int)entry.maxPlayers;
    if ((entry.sessionFlags & SESSIONFLAG_ISDEDICATED) && maxPlayers > 1) maxPlayers--;
    if (maxPlayers > 0 && (int)entry.numPlayers >= maxPlayers) {
        jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), (char16_t*)jkGuiMultiLobby_Str("GUIEXT_MP_ERR_FULL"));
        return 0;
    }
    // (a classic host's game this port can't play: co-op)
    const char* whyKey = NULL;
    if (!stdCommTransport_CanJoin(kind, &entry, &whyKey)) {
        jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), (char16_t*)jkGuiMultiLobby_Str(whyKey));
        return 0;
    }
    stdCommTransport_SetJoinKind(kind);

    // (found: as the old Join screen leaves it)
    jkGuiMultiplayer_aEntries[0] = entry;
    dplay_dword_55D618 = 1;
    sithMulti_multiModeFlags = entry.multiModeFlags;
    sithNet_MultiModeFlags = entry.multiModeFlags;
    stdString_CharToWchar(jkGuiMultiplayer_ipText, jkGuiMultiLobby_picked.address, 255);
    jkGuiMultiplayer_ipText[255] = 0;
    memset(jkGuiMultiplayer_stru_556168.field_300, 0, sizeof(jkGuiMultiplayer_stru_556168.field_300));
    if (!jkGuiMultiLobby_picked.name[0])
        stdString_WcharToChar(jkGuiMultiLobby_picked.name, entry.serverName, sizeof(jkGuiMultiLobby_picked.name) - 1);

    if (entry.sessionFlags & SESSIONFLAG_PASSWORD) {
        // (the password asked for, and checked with the host before going on: a wrong one is typed
        // again here)
        jkGuiMultiLobby_aPassword[1].wstr = jkGuiMultiLobby_wPasswordTitle;
        stdString_SafeWStrCopy(jkGuiMultiLobby_wPasswordTitle, entry.serverName, LOBBY_ROW_LEN);
        if (!jkGuiMultiLobby_wPasswordTitle[0]) {
            stdString_CharToWchar(jkGuiMultiLobby_wPasswordTitle, jkGuiMultiLobby_picked.name, LOBBY_ROW_LEN - 1);
            jkGuiMultiLobby_wPasswordTitle[LOBBY_ROW_LEN - 1] = 0;
        }
        char16_t* pwTyped = (char16_t*)jkGuiMultiplayer_stru_556168.field_300;
        jkGuiMultiLobby_aPassword[3].wstr = pwTyped;
        jkGuiMultiLobby_aPassword[3].selectedTextEntry = 16;
        jkGuiMultiLobby_wPasswordStatus[0] = 0;
        jkGuiMultiLobby_aPassword[LOBBY_PASSWORD_STATUS].wstr = jkGuiMultiLobby_wPasswordStatus;
        jkGuiMultiLobby_bConnecting = 0;
        for (;;) {
            jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiMultiLobby_menuPassword, &jkGuiMultiLobby_aPassword[4]);
            jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiLobby_menuPassword, &jkGuiMultiLobby_aPassword[5]);
            const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiLobby_menuPassword);
            if (clicked == LOBBY_ID_ANSWERED) {
                jkGuiMultiLobby_bConnecting = 0;
                jkGuiMultiLobby_wPasswordStatus[0] = 0;
                SDL_LockMutex(jkGuiMultiLobby_mutex);
                const int r = jkGuiMultiLobby_joinResult;
                SDL_UnlockMutex(jkGuiMultiLobby_mutex);
                if (r > 0) {
                    // (the password joined with is the one checked, whatever was typed meanwhile)
                    memset(jkGuiMultiplayer_stru_556168.field_300, 0, sizeof(jkGuiMultiplayer_stru_556168.field_300));
                    SDL_LockMutex(jkGuiMultiLobby_mutex);
                    for (int i = 0; i < 32 && jkGuiMultiLobby_joinPassword[i]; i++) pwTyped[i] = jkGuiMultiLobby_joinPassword[i];
                    memset(jkGuiMultiLobby_joinPassword, 0, sizeof(jkGuiMultiLobby_joinPassword));
                    SDL_UnlockMutex(jkGuiMultiLobby_mutex);
                    return 1;
                }
                if (r == 0) {
                    jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), jkStrings_GetUniStringWithFallback("GUINET_WRONGPASSWORD"));
                    memset(jkGuiMultiplayer_stru_556168.field_300, 0, sizeof(jkGuiMultiplayer_stru_556168.field_300));
                    continue;
                }
                char16_t wAddr[64];
                stdString_CharToWchar(wAddr, jkGuiMultiLobby_picked.address, 63);
                wAddr[63] = 0;
                jk_snwprintf(msg, 256, jkGuiMultiLobby_Str("GUIEXT_MP_ERR_NOGAME"), wAddr);
                jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), msg);
                return 0;
            }
            if (clicked != 1) {
                jkGuiMultiLobby_bConnecting = 0; // (Back: the checking, if any, forgotten)
                return 0;
            }
            if (!pwTyped[0] || jkGuiMultiLobby_bConnecting) continue; // (one check at a time)
            SDL_LockMutex(jkGuiMultiLobby_mutex);
            snprintf(jkGuiMultiLobby_joinAddress, sizeof(jkGuiMultiLobby_joinAddress), "%s", jkGuiMultiLobby_picked.address);
            memset(jkGuiMultiLobby_joinPassword, 0, sizeof(jkGuiMultiLobby_joinPassword));
            for (int i = 0; i < 32 && pwTyped[i]; i++) jkGuiMultiLobby_joinPassword[i] = pwTyped[i];
            jkGuiMultiLobby_bJoinIsPassword = 1;
            jkGuiMultiLobby_joinAsk++;
            SDL_UnlockMutex(jkGuiMultiLobby_mutex);
            jkGuiMultiLobby_bConnecting = 1;
            stdString_SafeWStrCopy(jkGuiMultiLobby_wPasswordStatus, jkGuiMultiLobby_Str("GUIEXT_MP_CHECKING_PASSWORD"), LOBBY_ROW_LEN);
        }
    }
    return 1;
}

int jkGuiMultiLobby_ShowJoin(void)
{
    jkGuiMultiLobby_LoadRecent();
    jkGuiMultiLobby_Words(jkGuiMultiLobby_aJoin, jkGuiMultiLobby_aJoin_words);
    jkGuiMultiLobby_Words(jkGuiMultiLobby_aPassword, jkGuiMultiLobby_aPassword_words);
    stdComm_ENetDiscover_StartBrowsing(jkGuiMultiLobby_ThisGame());
    jkGuiMultiLobby_StartAsking();

    // (the address box: the last one typed)
    static char16_t wTyped[128];
    char16_t wLast[256];
    wuRegistry_GetWString("lastTypedHost", wLast, 256, u"");
    stdString_SafeWStrCopy(wTyped, wLast, 100);
    jkGuiMultiLobby_aJoin[LOBBY_JOIN_ADDRESS].wstr = wTyped;
    jkGuiMultiLobby_aJoin[LOBBY_JOIN_ADDRESS].selectedTextEntry = 100;
    jkGuiMultiLobby_aJoin[LOBBY_JOIN_STATUS].wstr = jkGuiMultiLobby_wJoinStatus;
    jkGuiMultiLobby_wJoinStatus[0] = 0;
    jkGuiMultiLobby_bConnecting = 0;
    jkGuiMultiLobby_rowsMs = stdPlatform_GetTimeMsec();
    jkGuiMultiLobby_UpdateRows(&jkGuiMultiLobby_menuJoin, 0);

    int bJoin = 0;
    for (;;) {
        jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiMultiLobby_menuJoin, &jkGuiMultiLobby_aJoin[LOBBY_JOIN_TYPED]);
        jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiLobby_menuJoin, &jkGuiMultiLobby_aJoin[LOBBY_JOIN_BACK]);
        const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiLobby_menuJoin);
        if (clicked == -1) {
            if (jkGuiMultiLobby_bConnecting) {
                // (Back while connecting: that stopped, the screen kept)
                jkGuiMultiLobby_bConnecting = 0;
                jkGuiMultiLobby_SetStatus(&jkGuiMultiLobby_menuJoin, u"");
                continue;
            }
            break;
        }
        if (clicked == LOBBY_ID_ANSWERED) {
            if (jkGuiMultiLobby_Answered(&jkGuiMultiLobby_menuJoin)) {
                bJoin = 1;
                break;
            }
            continue;
        }
        jkGuiMultiLobbyTarget target;
        memset(&target, 0, sizeof(target));
        if (clicked >= LOBBY_ID_NEARBY && clicked < LOBBY_ID_NEARBY + LOBBY_NUM_NEARBY) {
            target = jkGuiMultiLobby_aNearbyTarget[clicked - LOBBY_ID_NEARBY];
        }
        else if (clicked >= LOBBY_ID_RECENT && clicked < LOBBY_ID_RECENT + LOBBY_NUM_RECENT_ROWS) {
            target = jkGuiMultiLobby_aRecentTarget[clicked - LOBBY_ID_RECENT];
        }
        else if (clicked == LOBBY_ID_JOIN_TYPED) {
            char typed[128];
            stdString_WcharToChar(typed, wTyped, sizeof(typed) - 1);
            typed[sizeof(typed) - 1] = 0;
            // (trimmed)
            char* s = typed;
            while (*s == ' ') s++;
            char* e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
            if (!*s) {
                jkGuiDialog_ErrorDialog(jkStrings_GetUniStringWithFallback("GUINET_JOINERROR"), (char16_t*)jkGuiMultiLobby_Str("GUIEXT_MP_ERR_ADDRESS"));
                continue;
            }
            wuRegistry_SetWString("lastTypedHost", wTyped);
            jkGuiMultiLobby_NormAddress(s, target.address, sizeof(target.address));
            // (an address that is a game found nearby: that game's)
            for (int i = 0; i < jkGuiMultiLobby_numNearbyRows; i++) {
                if (!strcmp(jkGuiMultiLobby_aNearbyTarget[i].address, target.address)) target = jkGuiMultiLobby_aNearbyTarget[i];
            }
        }
        else {
            continue;
        }
        if (!target.address[0]) continue;
        jkGuiMultiLobby_Pick(&jkGuiMultiLobby_menuJoin, &target);
    }

    jkGuiMultiLobby_StopAsking();
    stdComm_ENetDiscover_StopBrowsing();
    return bJoin;
}

void jkGuiMultiLobby_Joined(void)
{
    jkGuiMultiLobby_LoadRecent();
    jkGuiMultiLobby_Remember(jkGuiMultiLobby_picked.address, jkGuiMultiLobby_picked.hostId, jkGuiMultiLobby_picked.name);
}

// ------------------------------------------------------------------ Resume Co-op

// The saved campaigns (their episodes), into the rows; how many
static int jkGuiMultiLobby_FindSaves(void)
{
    Darray episodes;
    int n = 0;
    jkGuiRend_DarrayNewStr(&episodes, 8, 1);
    jkGuiSingleplayer_EnumEpisodes(&episodes, &jkGuiMultiLobby_aResume[LOBBY_RESUME_EPISODES], 0, JK_EPISODE_SINGLEPLAYER);
    for (int i = 0; i < (int)episodes.total && n < LOBBY_NUM_RESUME; i++) {
        jkGuiStringEntry* pEntry = jkGuiRend_GetStringEntry(&episodes, i);
        if (!pEntry || !pEntry->str || !pEntry->c_str) continue;
        time_t when = 0;
        char level[64];
        if (!jkCoopSave_Peek(pEntry->c_str, level, sizeof(level), &when)) continue;
        char date[32] = "";
        const struct tm* t = when ? localtime(&when) : NULL;
        if (t) strftime(date, sizeof(date), "%d %b %Y", t);
        // (the level by its title, from the episode's own strings, as the host's list shows it)
        char16_t wLevel[64];
        stdString_CharToWchar(wLevel, level, 63);
        wLevel[63] = 0;
        jkRes_LoadGob(pEntry->c_str);
        stdStrTable titles;
        memset(&titles, 0, sizeof(titles));
        stdStrTable_Load(&titles, "misc\\cogStrings.uni"); // (its result means little: an early END reads as 0)
        const char16_t* pTitle = jkGuiTitle_quicksave_related_func1(&titles, level);
        if (pTitle && pTitle[0]) stdString_SafeWStrCopy(wLevel, pTitle, 64);
        stdStrTable_Free(&titles);
        jk_snwprintf(jkGuiMultiLobby_awResume[n], LOBBY_ROW_LEN, u"%s: %s   (%s %S)", pEntry->str, wLevel,
                     jkGuiMultiLobby_Str("GUIEXT_MP_SAVED"), date);
        snprintf(jkGuiMultiLobby_aResumeGob[n], sizeof(jkGuiMultiLobby_aResumeGob[n]), "%s", pEntry->c_str);
        n++;
    }
    jkGuiRend_DarrayFree(&episodes);
    return n;
}

int jkGuiMultiLobby_ShowResume(void)
{
    const int n = jkGuiMultiLobby_FindSaves();
    jkGuiMultiLobby_Words(jkGuiMultiLobby_aResume, jkGuiMultiLobby_aResume_words);
    for (int i = 0; i < LOBBY_NUM_RESUME; i++) {
        jkGuiElement* e = &jkGuiMultiLobby_aResume[LOBBY_RESUME_ROW0 + i];
        e->wstr = jkGuiMultiLobby_awResume[i];
        e->bIsVisible = i < n;
    }
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiLobby_menuResume, &jkGuiMultiLobby_aResume[LOBBY_RESUME_ROW0 + LOBBY_NUM_RESUME]);
    const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiLobby_menuResume);
    if (clicked < LOBBY_ID_RESUME || clicked >= LOBBY_ID_RESUME + n) return 0;
    jkGuiNetHost_lobbyMode = JKGUIMULTILOBBY_MODE_COOP;
    stdString_SafeStrCopy(jkGuiNetHost_lobbyEpisode, jkGuiMultiLobby_aResumeGob[clicked - LOBBY_ID_RESUME], sizeof(jkGuiNetHost_lobbyEpisode));
    jkGuiNetHost_bLobbyResume = 1;
    return 1;
}

int jkGuiMultiLobby_ConfirmNewCoop(const char* pEpisodeGob)
{
    if (!jkCoopSave_Peek(pEpisodeGob, NULL, 0, NULL)) return 1;
    return jkGuiDialog_YesNoDialog((char16_t*)jkGuiMultiLobby_Str("GUIEXT_MP_COOP"), (char16_t*)jkGuiMultiLobby_Str("GUIEXT_MP_COOP_RESTART"));
}

// ------------------------------------------------------------------ Host Game, the Multiplayer screen

int jkGuiMultiLobby_ShowHostMode(void)
{
    jkGuiMultiLobby_Words(jkGuiMultiLobby_aHostMode, jkGuiMultiLobby_aHostMode_words);
    jkGuiMultiLobby_aHostMode[0].wstr = jkGuiMultiLobby_Str("GUIEXT_MP_HOST");
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiLobby_menuHostMode, &jkGuiMultiLobby_aHostMode[5]);
    const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiLobby_menuHostMode);
    if (clicked != JKGUIMULTILOBBY_MODE_DEATHMATCH && clicked != JKGUIMULTILOBBY_MODE_COOP) return 0;
    jkGuiNetHost_lobbyMode = clicked;
    jkGuiNetHost_lobbyEpisode[0] = 0;
    jkGuiNetHost_bLobbyResume = 0;
    return 1;
}

int jkGuiMultiLobby_ShowMain(void)
{
    // (RESUME CO-OP: only if there is a campaign to carry on)
    int bAnySaves = 0;
    {
        Darray episodes;
        jkGuiRend_DarrayNewStr(&episodes, 8, 1);
        jkGuiSingleplayer_EnumEpisodes(&episodes, &jkGuiMultiLobby_aResume[LOBBY_RESUME_EPISODES], 0, JK_EPISODE_SINGLEPLAYER);
        for (int i = 0; i < (int)episodes.total && !bAnySaves; i++) {
            jkGuiStringEntry* pEntry = jkGuiRend_GetStringEntry(&episodes, i);
            if (pEntry && pEntry->c_str) bAnySaves = jkCoopSave_Peek(pEntry->c_str, NULL, 0, NULL);
        }
        jkGuiRend_DarrayFree(&episodes);
    }
    jkGuiMultiLobby_Words(jkGuiMultiLobby_aMain, jkGuiMultiLobby_aMain_words);
    stdString_SafeWStrCopy(jkGuiMultiLobby_wResumeSub, jkGuiMultiLobby_Str(bAnySaves ? "GUIEXT_MP_RESUME_SUB" : "GUIEXT_MP_RESUME_NONE"), 128);
    jkGuiMultiLobby_aMain[LOBBY_MAIN_RESUME_SUB].wstr = jkGuiMultiLobby_wResumeSub;
    jkGuiMultiLobby_aMain[LOBBY_MAIN_RESUME].enableHover = !bAnySaves; // (greyed: nothing to resume)

    jkGuiNetHost_lobbyMode = JKGUIMULTILOBBY_MODE_CLASSIC;
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiMultiLobby_menuMain, &jkGuiMultiLobby_aMain[LOBBY_MAIN_BACK]);
    const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiMultiLobby_menuMain);
    switch (clicked) {
        case JKGUIMULTILOBBY_HOST:
        case JKGUIMULTILOBBY_JOIN:
        case JKGUIMULTILOBBY_RESUME:
        case JKGUIMULTILOBBY_CHARACTERS:
            return clicked;
        default:
            return JKGUIMULTILOBBY_BACK;
    }
}

void jkGuiMultiLobby_Startup(void)
{
    jkGui_InitMenu(&jkGuiMultiLobby_menuMain, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
    jkGui_InitMenu(&jkGuiMultiLobby_menuHostMode, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
    jkGui_InitMenu(&jkGuiMultiLobby_menuResume, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
    jkGui_InitMenu(&jkGuiMultiLobby_menuJoin, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
    jkGui_InitMenu(&jkGuiMultiLobby_menuPassword, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
}

void jkGuiMultiLobby_Shutdown(void)
{
    jkGuiMultiLobby_StopAsking();
    if (jkGuiMultiLobby_bTitles) {
        stdStrTable_Free(&jkGuiMultiLobby_titles);
        jkGuiMultiLobby_bTitles = 0;
    }
}

#endif // JKGUI_MULTI_LOBBY

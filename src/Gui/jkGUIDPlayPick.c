// Added: entire file. Experimental (PLATFORM_DPLAY): an address that answers with several
// DirectPlay sessions -- a computer hosting several games of the original Jedi Knight answers for
// each -- shows them all, one row each (its name, level and players), to pick the one to join.
// Kept apart from the Join screen (jkGUIMultiLobby.c), which only calls jkGuiDPlayPick_Pick.

#include "types.h"

#ifdef PLATFORM_DPLAY

#include "Platform/Networking/DPlay/stdComm_DPlay.h"
#include "Gui/jkGUI.h"
#include "Gui/jkGUIRend.h"
#include "General/stdString.h"
#include "jk.h"
#ifdef JK_ENHANCED
#include "Enhanced/enhMenuRead.h"
#endif

#include <string.h>

#define DPLAYPICK_MAX 16
#define DPLAYPICK_LIST 2
#define DPLAYPICK_DETAIL 3

static int32_t jkGuiDPlayPick_aListBitmaps[2] = { 0xd, 0xe };
static int jkGuiDPlayPick_ListClicked(jkGuiElement* pElement, jkGuiMenu* pMenu, int32_t mouseX, int32_t mouseY, int bRedraw);

static char16_t jkGuiDPlayPick_wDetail[128];
static jkGuiElement jkGuiDPlayPick_aElements[] = {
    { ELEMENT_TEXT,       0,  6, NULL, 3, { 20, 20, 600, 40 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,  2, NULL, 3, { 20, 90, 600, 30 },   1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_LISTBOX,    1,  0, NULL, 0, { 60, 130, 520, 200 }, 1, 0, NULL, NULL, jkGuiDPlayPick_ListClicked, jkGuiDPlayPick_aListBitmaps, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXT,       0,  0, NULL, 3, { 20, 345, 600, 30 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, 1,  2, NULL, 3, { 460, 420, 160, 56 }, 1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_TEXTBUTTON, -1, 2, NULL, 3, { 20, 420, 160, 56 },  1, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
    { ELEMENT_END,        0,  0, NULL, 0, { 0, 0, 0, 0 },        0, 0, NULL, NULL, NULL, NULL, { 0, 0, 0, 0, 0, { 0, 0, 0, 0 } }, 0 },
};
static jkGuiMenu jkGuiDPlayPick_menu = { jkGuiDPlayPick_aElements, -1, 0xFFFF, 0xFFFF, 0xF, 0, 0, jkGui_stdBitmaps, jkGui_stdFonts, 0, NULL, "thermloop01.wav", "thrmlpu2.wav", 0, 0, 0, 0, 0, 0 };
static int jkGuiDPlayPick_bInitted = 0;

static jkMultiEntry jkGuiDPlayPick_aEntries[DPLAYPICK_MAX];
static int jkGuiDPlayPick_num = 0;

static void jkGuiDPlayPick_Detail(int idx)
{
    jkGuiDPlayPick_wDetail[0] = 0;
    if (idx >= 0 && idx < jkGuiDPlayPick_num) {
        const jkMultiEntry* e = &jkGuiDPlayPick_aEntries[idx];
        jk_snwprintf(jkGuiDPlayPick_wDetail, 128, u"%S, %S: %d of %d players%s", e->episodeGobName, e->mapJklFname,
                     (int)e->numPlayers, (int)e->maxPlayers, (e->sessionFlags & SESSIONFLAG_PASSWORD) ? u" (password)" : u"");
    }
}

static int jkGuiDPlayPick_ListClicked(jkGuiElement* pElement, jkGuiMenu* pMenu, int32_t mouseX, int32_t mouseY, int bRedraw)
{
    jkGuiRend_ClickSound(pElement, pMenu, mouseX, mouseY, bRedraw);
    jkGuiDPlayPick_Detail(jkGuiDPlayPick_aElements[DPLAYPICK_LIST].selectedTextEntry);
    jkGuiRend_UpdateAndDrawClickable(&jkGuiDPlayPick_aElements[DPLAYPICK_DETAIL], pMenu, 1);
    return 0;
}

int jkGuiDPlayPick_Pick(jkMultiEntry* pInOut)
{
    jkGuiDPlayPick_num = stdComm_DPlay_GetSessions(&pInOut->guidInstance, jkGuiDPlayPick_aEntries, DPLAYPICK_MAX);
    if (jkGuiDPlayPick_num <= 1) return 1; // (one game there: that one)

    if (!jkGuiDPlayPick_bInitted) {
        jkGui_InitMenu(&jkGuiDPlayPick_menu, jkGui_stdBitmaps[JKGUI_BM_BK_MULTI]);
#ifdef JK_ENHANCED
        enhMenuRead_Mark(&jkGuiDPlayPick_menu); // shown easier to read, as the Join screen
#endif
        jkGuiDPlayPick_bInitted = 1;
    }
    jkGuiDPlayPick_aElements[0].wstr = u"Games at this address";
    jkGuiDPlayPick_aElements[1].wstr = u"Pick the one to join:";
    jkGuiDPlayPick_aElements[DPLAYPICK_DETAIL].wstr = jkGuiDPlayPick_wDetail;
    jkGuiDPlayPick_aElements[4].wstr = u"Join";
    jkGuiDPlayPick_aElements[5].wstr = u"Back";

    // (one row each: its name, level and players)
    static char16_t aRows[DPLAYPICK_MAX][96];
    Darray rows;
    jkGuiRend_DarrayNewStr(&rows, jkGuiDPlayPick_num + 1, 1);
    for (int i = 0; i < jkGuiDPlayPick_num; i++) {
        const jkMultiEntry* e = &jkGuiDPlayPick_aEntries[i];
        jk_snwprintf(aRows[i], 96, u"%s  -  %S  -  %d/%d", e->serverName, e->mapJklFname, (int)e->numPlayers, (int)e->maxPlayers);
        jkGuiRend_DarrayReallocStr(&rows, aRows[i], i);
    }
    jkGuiRend_DarrayReallocStr(&rows, 0, 0);
    jkGuiRend_SetClickableString(&jkGuiDPlayPick_aElements[DPLAYPICK_LIST], &rows);
    jkGuiDPlayPick_aElements[DPLAYPICK_LIST].selectedTextEntry = 0;
    jkGuiDPlayPick_Detail(0);

    jkGuiRend_MenuSetReturnKeyShortcutElement(&jkGuiDPlayPick_menu, &jkGuiDPlayPick_aElements[4]);
    jkGuiRend_MenuSetEscapeKeyShortcutElement(&jkGuiDPlayPick_menu, &jkGuiDPlayPick_aElements[5]);
    const int clicked = jkGuiRend_DisplayAndReturnClicked(&jkGuiDPlayPick_menu);
    const int idx = jkGuiDPlayPick_aElements[DPLAYPICK_LIST].selectedTextEntry;
    jkGuiRend_DarrayFree(&rows);
    if (clicked != 1 || idx < 0 || idx >= jkGuiDPlayPick_num) return 0;
    *pInOut = jkGuiDPlayPick_aEntries[idx];
    stdComm_DPlay_SelectSession(&pInOut->guidInstance);
    return 1;
}

#endif // PLATFORM_DPLAY

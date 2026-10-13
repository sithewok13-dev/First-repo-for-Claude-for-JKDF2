#include "jkDev.h"

#include "General/stdHashtbl.h"
#include "General/stdBitmap.h"
#include "General/stdFont.h"
#include "General/stdString.h"
#include "General/stdFnames.h"
#include "Devices/sithConsole.h"
#include "Win95/stdDisplay.h"
#include "Win95/WinIdk.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/jkSaber.h"
#include "World/sithThing.h"
#include "World/jkPlayer.h"
#include "World/sithActor.h"
#include "World/sithWeapon.h"
#include "Gameplay/sithTime.h"
#include "Main/sithCommand.h"
#include "Main/Main.h"
#include "Main/jkMain.h"
#include "Main/jkStrings.h"
#include "Main/jkQuakeConsole.h"
#include "stdPlatform.h"
#include "wprintf.h"
#include "Dss/sithMulti.h"
#include "Dss/sithGamesave.h"
#include "Dss/jkDSS.h"
#include "../jk.h"
#ifdef TARGET_IOS
#include "Platform/iOS/iosSafeArea.h" // Added

// Added: the top message text on iOS, relative to the HUD scale
#define JKDEV_IOS_TEXT_SCALE ((flex_t)1.33)
#endif


void jkDev_DrawEntriesGPU();
void jkDev_BlitLogToScreenGPU();
void jkDev_RenderQuakeConsole();

// Added: set while sithewok runs red5, wamprat, bactame (and iamagod in MOTS), so they skip their messages
static int jkDev_bQuietCheats = 0;

#ifdef QOL_IMPROVEMENTS
// Added: sithewok's endless Force, in memory only
static int jkDev_bEndlessForce = 0;
// Added: MOTS: sithewok's powers past the tier limits stay, in memory only ("sithewok off" leaves it on)
static int jkDev_bKeepForcePowers = 0;
// Added: sithewok: falls don't hurt (invincibility lets fall damage through), and the Force catches the
// player falling into a pit (sithPlayer_Update), in memory only
static int jkDev_bNoFallHurt = 0;
// Added: sithewok: the player is flying because of it (jkDev_Custom_ToggleFlight), so "sithewok off" lands them
static int jkDev_bSithEwokFlying = 0;
// Added: sithewok: noclip is on because of it (jkDev_Custom_ToggleNoclip), so "sithewok off" ends it
static int jkDev_bSithEwokNoclip = 0;
// Added: sithewok is on, and stays on from level to level (and through loads): the player it was last
// seen on, and the frames left before it is put on again for a new one (once the level's own start-up,
// which can take weapons away, has run)
static int jkDev_bSithEwokOn = 0;
static SithThing* jkDev_pSithEwokPlayer = NULL;
static int jkDev_sithEwokWait = 0;
#define JKDEV_SITHEWOK_WAIT 20
static int jkDev_bSithEwokShown = 0; // something on screen shows it is on (jkDev_Custom_SetSithEwokShown)
// Added: sithewok keeps the weapon in hand from level to level: the one in hand (or being switched to)
// as last seen, and the one to put back in a new level, for at most so many more frames
static int jkDev_sithEwokWeapon = -1;
static int jkDev_sithEwokWeaponBack = -1;
static int jkDev_sithEwokWeaponTries = 0;
#define JKDEV_SITHEWOK_WEAPON_TRIES 600
static int jkDev_Custom_PutWeaponBack(SithThing *pPlayer);
// Added: the last weapon other than the saber the player had in hand (or was switching to), for when a
// saber only the cheat gave is taken back (jkDev_Custom_TakeCheatSaber)
static int jkDev_lastOtherWeapon = -1;
// Added: sithewok keeps the Force power selected from level to level, as it keeps the weapon: the one
// selected as last seen, and the one to select again in a new level (once its powers are given again)
static int jkDev_sithEwokPower = -1;
static int jkDev_sithEwokPowerBack = -1;
static void jkDev_Custom_TakeCheatSaber(SithThing *pPlayer);
static void jkDev_Custom_KeepForceFull();
static void jkDev_Custom_SithEwokOn(SithThing *pPlayer, int bAgain);
#endif

// MOTS altered
void jkDev_Startup()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
#if !defined(SDL2_RENDER) && defined(WIN32)
    if ( Main_bDevMode && Main_bWindowGUI)
    {
        jkDev_hDlg = jk_CreateDialogParamA(stdGdi_GetHInstance(), (LPCSTR)0x70, 0, jkDev_DialogFunc, 0);
        if ( jkDev_hDlg )
            Window_AddDialogHwnd(jkDev_hDlg);

    }
#endif

    sithConsole_Startup(JKDEV_NUM_CHEATS*2);
    sithConsole_Open(16);
    sithConsole_RegisterPrintFunctions(jkDev_DebugLog, jkDev_PrintUniString);

    jkDev_cheatHashtable = stdHashtbl_New(JKDEV_NUM_CHEATS*2);
    _memset(jkDev_aCheatCmds, 0, sizeof(stdDebugConsoleCmd) * JKDEV_NUM_CHEATS);

    sithConsole_RegisterCommand(jkDev_CmdVersion, "version", 0);
    sithConsole_RegisterCommand(jkDev_CmdTeam, "team", 0);
    sithConsole_RegisterCommand(jkDev_CmdFramerate, "framerate", 0);
    sithConsole_RegisterCommand(jkDev_CmdDispStats, "dispstats", 0);
    sithConsole_RegisterCommand(jkDev_CmdKill, "kill", 0);
    sithConsole_RegisterCommand(jkDev_CmdEndLevel, "endlevel", 0);

    jkDev_RegisterCmd(jkDev_CmdDebugFlags, "whiteflag", "Disable AI", 0);
    jkDev_RegisterCmd(jkDev_CmdFly, "eriamjh", "", 0);
    jkDev_RegisterCmd(jkDev_CmdDebugFlags2, "jediwannabe", "Invul", 5);
    jkDev_RegisterCmd(jkDev_CmdWarp, "warp", "", 0);
    jkDev_RegisterCmd(jkDev_CmdActivate, "activate", "", 0);
    jkDev_RegisterCmd(jkDev_CmdDebugFlags3, "slowmo", "Slowmo", 7);
    jkDev_RegisterCmd(jkDev_CmdJump, "hyper", "", 0);
    jkDev_RegisterCmd(jkDev_CmdEndLevel2, "thereisnotry", "", 0);
    jkDev_RegisterCmd(jkDev_CmdAllWeapons, "red5", "", 0);
    jkDev_RegisterCmd(jkDev_CmdAllItems, "wamprat", "", 0);
    jkDev_RegisterCmd(jkDev_CmdLightMaster, "imayoda", "", 0); // MOTS removed
    jkDev_RegisterCmd(jkDev_CmdDarkMaster, "sithlord", "", 0); // MOTS removed
    jkDev_RegisterCmd(jkDev_CmdUberJedi, "raccoonking", "", 0);
    jkDev_RegisterCmd(jkDev_CmdLevelUp, "deeznuts", "", 0);
    jkDev_RegisterCmd(jkDev_CmdHeal, "bactame", "", 0);
    jkDev_RegisterCmd(jkDev_CmdAllMap, "5858lvr", "", 0);
    jkDev_RegisterCmd(jkDev_CmdMana, "yodajammies", "", 0);
    jkDev_RegisterCmd(jkDev_CmdSkipToLevel, "pinotnoir", "", 0);

    // Added: MoTS cheats
    jkDev_RegisterCmd(jkDev_CmdDebugFlags, "statuesque", "Disable AI", 0);
    jkDev_RegisterCmd(jkDev_CmdFly, "freebird", "", 0);
    jkDev_RegisterCmd(jkDev_CmdDebugFlags2, "boinga", "Invul", 5);
    jkDev_RegisterCmd(jkDev_CmdWarp, "youarehere", "", 0); // Undoc'd?
    jkDev_RegisterCmd(jkDev_CmdActivate, "makeitwork", "", 0); // Undoc'd?
    jkDev_RegisterCmd(jkDev_CmdDebugFlags3, "gospeedgo", "Slowmo", 7);
    jkDev_RegisterCmd(jkDev_CmdJump, "quickzap", "", 0);
    jkDev_RegisterCmd(jkDev_CmdEndLevel2, "gameover", "", 0);
    jkDev_RegisterCmd(jkDev_CmdAllWeapons, "diediedie", "", 0);
    jkDev_RegisterCmd(jkDev_CmdAllItems, "gimmestuff", "", 0);
    jkDev_RegisterCmd(jkDev_CmdUberJedi, "iamagod", "", 0);
    jkDev_RegisterCmd(jkDev_CmdLevelUp, "trainme", "", 0);
    jkDev_RegisterCmd(jkDev_CmdHeal, "morelife", "", 0); // Undoc'd?
    jkDev_RegisterCmd(jkDev_CmdAllMap, "cartograph", "", 0);
    jkDev_RegisterCmd(jkDev_CmdMana, "trixie", "", 0);
    jkDev_RegisterCmd(jkDev_CmdSkipToLevel, "takemeto", "", 0); // Undoc'd?

#ifdef QOL_IMPROVEMENTS
    jkDev_RegisterCmd(jkDev_CmdNoclip, "noclip", "Noclip", 0);
	jkDev_RegisterCmd(jkDev_Custom_CmdJumpNextCheckpoint, "checkmate", "", 0);  // cycles to next auto-restart checkpoint
    jkDev_RegisterCmd(jkDev_Custom_CmdSithEwok, "sithewok", "", 0); // everything at once, invincible, endless Force
#endif

    jkDev_bInitted = 1;
}

void jkDev_Shutdown()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);

    if ( jkDev_cheatHashtable )
    {
        stdHashtbl_Free(jkDev_cheatHashtable);
        jkDev_cheatHashtable = 0;
    }
    sithConsole_Close();
    sithConsole_Shutdown();
    jkDev_bInitted = 0;
}

int jkDev_Open()
{
    tRasterInfo a1; // [esp+0h] [ebp-4Ch] BYREF

#ifdef QOL_IMPROVEMENTS
    jkDev_Custom_SithEwokRestored(); // Added: a level opening (or a load)
#endif

    if ( jkDev_bOpened )
        return 0;

    // Added: Prevent crashes
    if ( Main_bNoHUD )
        return 1;

    jkDev_log_55A4A4 = 0;
    jkDev_BMFontHeight = stdFont_GetHeight(jkHud_pMsgFontSft);

    _memcpy(&a1, &stdDisplay_pCurVideoMode->format, sizeof(a1));
    a1.height = 5 * jkDev_BMFontHeight;
#ifdef SDL2_RENDER
    a1.width -= (48*2); // Make sure the hud scaling doesn't cause overlap
#endif

    jkDev_vbuf = stdDisplay_VBufferNew(&a1, 1, 0, 0);
    jkDev_ColorKey = jkHud_pMsgFontSft->pBitmap->colorkey;

    if ( jkDev_vbuf )
    {
        stdDisplay_VBufferFill(jkDev_vbuf, jkDev_ColorKey, 0);
        stdDisplay_VBufferSetColorKey(jkDev_vbuf, jkDev_ColorKey);
    }

    jkDev_bOpened = 1;
    return 1;
}

void jkDev_Close()
{
    if (!jkDev_bOpened)
        return;

    if ( jkDev_vbuf )
    {
        stdDisplay_VBufferFree(jkDev_vbuf);
        jkDev_vbuf = NULL;
    }

    jkDev_bOpened = 0;
}

void jkDev_DrawLog()
{
    tVBuffer *v0; // ecx
    signed int v2; // edi
    jkDevLogEnt* v4; // esi
    int v5; // edx
    int v6; // eax
    rdCanvas *v7; // ebp
    int v8; // edx
    signed int v9; // ebx
    int v10; // edi
    jkDevLogEnt* v11; // esi
    int v12; // eax
    int v13; // edx
    int v14; // eax
    rdRect a4; // [esp+10h] [ebp-10h] BYREF

#ifdef QOL_IMPROVEMENTS
    // Added: sithewok's endless Force, after the tick and before the HUD draws the meter
    jkDev_Custom_KeepForceFull();
#endif

    // Added: Prevent crashes
    if ( Main_bNoHUD )
        return;

    jkDev_UpdateEntries();
    v0 = jkDev_vbuf;
    jkDev_DrawEntries();

    if ( v0 )
    {
        v7 = Video_pCanvas;
        v8 = jkDev_dword_55A9D0;
        a4.height = jkDev_BMFontHeight;
        v9 = 4;
        v10 = 0;
        v11 = &jkDev_aEntries[0];
        for (int i = 0; i < 5; i++)
        {
            if ( v11->field_10C && (v9 < v7->yStart || jkDev_aEntryPositions[v8 + v10].x < v7->xStart) )
            {
                v12 = v8 + v10;
                a4.y = v9;
                v13 = jkDev_aEntryPositions[v12].x;
                v14 = jkDev_aEntryPositions[v12].y;
                a4.x = v13;
                a4.width = v14;
                stdDisplay_VBufferFill(Video_pMenuBuffer, Video_fillColor, &a4);
                v7 = Video_pCanvas;
                v8 = jkDev_dword_55A9D0;
            }
            v11->bDrawEntry = v11->field_10C;
            if ( v11->field_10C > 0 )
                --v11->field_10C;
            ++v11;
            v9 += jkDev_BMFontHeight;
            v10 += 2;
        }
    }
}

// MOTS altered
void jkDev_BlitLogToScreen()
{
    int v0; // ecx
    int v1; // ebx
    int v2; // ebp
    jkDevLogEnt* v3; // edi
    int v4; // esi
    int v5; // ecx
    int v6; // eax
    rdRect v7; // [esp+0h] [ebp-10h] BYREF

    // Added: Prevent crashes
    if ( Main_bNoHUD )
        return;

#ifdef SDL2_RENDER
    jkDev_BlitLogToScreenGPU();
    return;
#endif

    if ( jkDev_vbuf )
    {
        v0 = jkDev_BMFontHeight;
        v7.x = 0;
        v7.y = 0;
        v7.height = jkDev_BMFontHeight;
        v1 = 4;
        v2 = 0;
        v3 = &jkDev_aEntries[0];
        for (int i = 0; i < 5; i++)
        {
            if ( v2 < jkDev_log_55A4A4 && (v1 + v7.height > Video_pCanvas->yStart || v3->bDrawEntry) )
            {
                v7.width = v3->drawWidth;
                v4 = (signed int)(stdDisplay_pCurVideoMode->format.width - v7.width) / 2;
                if ( v4 < 0 )
                    v4 = 0;
                stdDisplay_VBufferCopy(Video_pMenuBuffer, jkDev_vbuf, v4, v1, &v7, 1);
                v5 = v7.width;
                v6 = jkDev_dword_55A9D0 + 2 * v2;
                jkDev_aEntryPositions[v6].x = v4;
                jkDev_aEntryPositions[v6].y = v5;
                v0 = jkDev_BMFontHeight;
            }
            if ( v3->bDrawEntry > 0 )
                --v3->bDrawEntry;
            v1 += v0;
            ++v3;
            ++v2;
            v7.y += v0;
        }
        jkDev_dword_55A9D0 = (jkDev_dword_55A9D0 + 1) % 2;
    }
}

// MOTS altered
void jkDev_BlitLogToScreenGPU()
{
    int v1; // ebx
    int v2; // ebp
    jkDevLogEnt* v3; // edi
    int v4; // esi
    int v5; // ecx
    int v6; // eax
    rdRect v7; // [esp+0h] [ebp-10h] BYREF

    // Added: Prevent crashes
    if ( Main_bNoHUD )
        return;

    if (!jkDev_vbuf) return;

#ifdef TARGET_IOS
    // Added: on iOS the lines go under the touch overlay's top button row, a
    // third bigger than the rest of the HUD, wrapped between words
    {
        int textX, textY, textW;
        iosSafeArea_GetTopTextRect(Video_format.width, Video_format.height, &textX, &textY, &textW);
        flex_t textScale = jkPlayer_hudScale * JKDEV_IOS_TEXT_SCALE;
        v1 = textY;
        v3 = &jkDev_aEntries[0];
        for (int i = 0; i < 5 && i < jkDev_log_55A4A4; i++)
        {
            int h = stdFont_DrawWrappedCenteredGPU(jkHud_pMsgFontSft, textX, v1, textW, v3->text, 1, textScale, 1);
            if ( h <= 0 ) // a blank entry still takes its line, as before
                h = (int)((flex_t)stdFont_GetHeight(jkHud_pMsgFontSft) * textScale);
            v1 += h;
            if ( v3->bDrawEntry > 0 )
                --v3->bDrawEntry;
            ++v3;
        }
        jkDev_dword_55A9D0 = (jkDev_dword_55A9D0 + 1) % 2;
        return;
    }
#endif

    v7.x = 0;
    v7.y = 0;
    v7.height = (int)((flex_t)jkDev_BMFontHeight * jkPlayer_hudScale);
    v1 = 4;
    v2 = 0;
    v3 = &jkDev_aEntries[0];
    for (int i = 0; i < 5; i++)
    {
        if ( v2 < jkDev_log_55A4A4 && (v1 + v7.height > Video_pCanvas->yStart || v3->bDrawEntry) )
        {
            v7.width = v3->drawWidth;
            v4 = (signed int)(stdDisplay_pCurVideoMode->format.width - v7.width) / 2;
            if ( v4 < 0 )
                v4 = 0;
            //stdDisplay_VBufferCopy(Video_pMenuBuffer, jkDev_vbuf, v4, v1, &v7, 1);
            stdFont_DrawMultilineCenteredGPU(jkHud_pMsgFontSft, 0, v1, stdDisplay_pCurVideoMode->format.width, v3->text, 1, jkPlayer_hudScale);
            v5 = v7.width;
            v6 = jkDev_dword_55A9D0 + 2 * v2;
            jkDev_aEntryPositions[v6].x = v4;
            jkDev_aEntryPositions[v6].y = v5;
            //printf("Draw?\n %u %u\n", v4, v1);
        }
        if ( v3->bDrawEntry > 0 )
            --v3->bDrawEntry;
        //v1 += (int)((flex_t)jkDev_BMFontHeight * jkPlayer_hudScale);
        v1 += stdFont_DrawMultilineCenteredHeight(jkHud_pMsgFontSft, 0, v1, stdDisplay_pCurVideoMode->format.width, v3->text, 1, jkPlayer_hudScale);
        ++v3;
        ++v2;
        v7.y += (int)((flex_t)jkDev_BMFontHeight * jkPlayer_hudScale);
    }
    jkDev_dword_55A9D0 = (jkDev_dword_55A9D0 + 1) % 2;
}

// For DSi
void jkDev_PrintfLog()
{
    int v1; // ebx
    int v2; // ebp
    jkDevLogEnt* v3; // edi
    char tmp[256];
    
    v2 = 0;
    v3 = &jkDev_aEntries[0];
    for (int i = 0; i < 5; i++)
    {
        if ( v2 < jkDev_log_55A4A4 /*&& v3->bDrawEntry*/ )
        {
            stdString_WcharToChar(tmp, v3->text, 255);
            tmp[255] = 0;
            // Added: raw printf -- this redraw re-emits the last few entries every
            // pass, which floods the emulator debug mirror on TWL.
            printf("%s\n", tmp);
        }
        //if ( v3->bDrawEntry > 0 )
        //    --v3->bDrawEntry;
        ++v3;
        ++v2;
    }
    jkDev_dword_55A9D0 = (jkDev_dword_55A9D0 + 1) % 2;
}

// MOTS altered? inlined?
int jkDev_PrintUniString(const char16_t *str)
{
    int v1; // ecx
    int v2; // edx
    int v3; // ecx
    int v5; // edi
    int v6; // esi

    v1 = jkDev_log_55A4A4;
    if ( jkDev_log_55A4A4 >= 5 )
    {
        v2 = 0;
        v3 = 0;
        while ( jkDev_aEntries[v3].timeMsExpiration == -1 )
        {
            ++v3;
            if ( v3 >= 5 )
                goto LABEL_7;
        }
        v2 = v3;
LABEL_7:
        jkDev_aEntries[v2].timeMsExpiration = 0;
        jkDev_UpdateEntries();
        v1 = jkDev_log_55A4A4;
    }
    v5 = v1;
    v6 = v1;
    _wcsncpy(jkDev_aEntries[v1].text, str, 0x80u);
    jkDev_aEntries[v6].field_104 = 0;
    jkDev_aEntries[v6].timeMsExpiration = stdPlatform_GetTimeMsec() + 5000;
    jkDev_bScreenNeedsUpdate = 1;
    ++jkDev_log_55A4A4;
    jkDev_aEntries[v6].bDrawEntry = 2;
    jkDev_aEntries[v6].field_10C = 2;
    char tmp[256];
    char tmp2[256+2];
    stdString_WcharToChar(tmp, str, 255);
    tmp[255] = 0;
    printf("\r                                            \r");
    stdString_snprintf(tmp2, sizeof(tmp2), "%s%c", tmp, _strlen(tmp) && tmp[_strlen(tmp)-1] == '\n' ? ' ' : '\n');
    printf("%s", tmp2);
#ifdef QUAKE_CONSOLE
    jkQuakeConsole_PrintLine(tmp2);
#endif
    return v5;
}

// MOTS altered? inlined?
// Added: a message that changes as it goes (the touch overlay's level count): put in place of the
// last one it put up, if the newest line on screen still says what that said (lines move up as older
// ones go); else a new line
int jkDev_Custom_PrintInPlace(const char16_t *str)
{
    static char16_t last[0x80];
    int idx = jkDev_log_55A4A4 - 1;
    if ( last[0] && idx >= 0 && (uint32_t)jkDev_aEntries[idx].timeMsExpiration > (uint32_t)stdPlatform_GetTimeMsec()
         && !__wcscmp(jkDev_aEntries[idx].text, last) )
    {
        _wcsncpy(jkDev_aEntries[idx].text, str, 0x80u);
        jkDev_aEntries[idx].text[0x7F] = 0;
        jkDev_aEntries[idx].timeMsExpiration = stdPlatform_GetTimeMsec() + 5000;
        jkDev_aEntries[idx].bDrawEntry = 2;
        jkDev_aEntries[idx].field_10C = 2;
        jkDev_bScreenNeedsUpdate = 1;
    }
    else
    {
        jkDev_PrintUniString(str);
        idx = jkDev_log_55A4A4 - 1;
    }
    _wcsncpy(last, jkDev_aEntries[idx].text, 0x80u);
    last[0x7F] = 0;
    return 1;
}

int jkDev_DebugLog(const char *lParam)
{
    HWND v1; // eax
    char16_t a1[128]; // [esp+8h] [ebp-100h] BYREF

    stdString_CharToWchar(a1, lParam, 127);
    a1[127] = 0;
    int ret = jkDev_PrintUniString(a1);
#if defined(WIN32) && !defined(SDL2_RENDER)
    if ( jkDev_hDlg )
    {
        v1 = GetDlgItem(jkDev_hDlg, 1037);
        WinIdk_PrintConsole(v1, (LPARAM)lParam, 50);
    }
#else
    //printf("%s", lParam);
#endif
    return ret;
}

int jkDev_sub_41FB80(int a1, const char16_t *a2)
{
    int result; // eax
    int v3; // edi
    jkDevLogEnt *v4; // esi

    result = 0;
    v3 = 0;
    if ( jkDev_log_55A4A4 <= 0 )
    {
LABEL_9:
        if ( !result )
        {
            int tmp = jkDev_PrintUniString(a2);
            jkDev_aEntries[tmp].field_104 = a1;
            jkDev_aEntries[tmp].timeMsExpiration = -1;
        }
    }
    else
    {
        v4 = jkDev_aEntries;
        while ( !result )
        {
            if ( v4->field_104 == a1 )
            {
                if ( __wcscmp(v4->text, a2) )
                {
                    _wcsncpy(v4->text, a2, 0x80u);
                    v4->bDrawEntry = 2;
                    v4->field_10C = 2;
                    jkDev_bScreenNeedsUpdate = 1;
                }
                result = 1;
            }
            ++v3;
            ++v4;
            if ( v3 >= jkDev_log_55A4A4 )
                goto LABEL_9;
        }
    }
    return result;
}

int jkDev_sub_41FC40(int a1, const char *a2)
{
    char16_t a1a[128]; // [esp+0h] [ebp-100h] BYREF

    stdString_CharToWchar(a1a, a2, 127);
    a1a[127] = 0;
    return jkDev_sub_41FB80(a1, a1a);
}

void jkDev_sub_41FC90(int a1)
{
    int v1; // eax
    jkDevLogEnt* v2; // ecx

    v1 = 0;
    if ( jkDev_log_55A4A4 > 0 )
    {
        v2 = &jkDev_aEntries[0];
        do
        {
            if ( v2->field_104 == a1 )
                break;
            ++v1;
            ++v2;
        }
        while ( v1 < jkDev_log_55A4A4 );
    }
    if ( v1 < jkDev_log_55A4A4 )
        jkDev_aEntries[v1].timeMsExpiration = 0;
}

int jkDev_RegisterCmd(DebugConsoleCmd_t pfCheatFunc, const char *pCryptCheatStr, const char *pCheatFlavortext, int extra)
{
    if ( jkDev_numCheats == JKDEV_NUM_CHEATS )
        return 0;

    _strncpy(jkDev_aCheatCmds[jkDev_numCheats].cmdStr, pCheatFlavortext, 0x1Fu);
    jkDev_aCheatCmds[jkDev_numCheats].cmdStr[31] = 0;
    jkDev_aCheatCmds[jkDev_numCheats].cmdFunc = pfCheatFunc;
    jkDev_aCheatCmds[jkDev_numCheats].extra = extra;
    stdHashtbl_Add(jkDev_cheatHashtable, pCryptCheatStr, &jkDev_aCheatCmds[jkDev_numCheats]);

    ++jkDev_numCheats;
    return 1;
}

int jkDev_TryCommand(const char *cmd)
{
    char *v1; // eax
    stdDebugConsoleCmd *pFoundCmd; // esi
    char *v5; // eax
    char key[128]; // Added: increased len 32->128
    char SrcStr[128]; // [esp+24h] [ebp-80h] BYREF

    _strncpy(SrcStr, cmd, 0x80);
    SrcStr[127] = 0;
    _strtolower(SrcStr);
    v1 = _strtok(SrcStr, ", \t\n\r");
    if ( !v1 )
        return 0;
    _strncpy(key, v1, 128);
    key[127] = 0;
    jkDev_Decrypt(key);
    pFoundCmd = (stdDebugConsoleCmd *)stdHashtbl_Find(jkDev_cheatHashtable, key);
    if ( !pFoundCmd )
        return 0;
    v5 = _strtok(0, "\n\r");
    pFoundCmd->cmdFunc(pFoundCmd, v5);
    return 1;
}

char* jkDev_Decrypt(char *cheatStr)
{
    char* result = cheatStr;

    // Added: Don't bother with the weird cheat encryption
#if 0
    for (char i = *cheatStr; i; ++result )
    {
        *result = i ^ 0xC5;
        i = result[1];
    }
#endif
    return result;
}

int jkDev_CmdVersion(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    _sprintf(std_g_genBuffer, "Jedi Knight v%d.%d%c %s %s\n", jkGuiTitle_verMajor, jkGuiTitle_verMinor, jkGuiTitle_verRevision, "Sep  8 1997", "16:17:30");
    sithConsole_PrintString(std_g_genBuffer);
    return 1;
}

int jkDev_CmdFramerate(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    Main_bFrameRate = !Main_bFrameRate;
    return 1;
}

int jkDev_CmdDispStats(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    Main_bDispStats = !Main_bDispStats;
    return 1;
}

int jkDev_CmdKill(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    sithActor_DamageActor(sithPlayer_g_pLocalPlayerThing, sithPlayer_g_pLocalPlayerThing, 200.0, 1);
    return 1;
}

int jkDev_CmdEndLevel(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( sithNet_isMulti && sithNet_isServer )
        jkDSS_SendEndLevel();
    return 1;
}

int jkDev_CmdSkipToLevel(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    int argInt = 0;

    if ( sithNet_isMulti )
        return 1;
    if ( !pArgStr || !_sscanf(pArgStr, "%d", &argInt) )
        return 0;
    if ( !jkEpisode_mLoad.paEntries )
        return 0;

    jkMain_SetMap(argInt);
    return 1;
}

int jkDev_Custom_CmdJumpNextCheckpoint(stdDebugConsoleCmd* pCmd, const char* pArgStr)
{
    char tmp[128];

    if (sithNet_isMulti)
        return 1;

    jkPlayer_bLoadingSomething = 1;
    stdString_snprintf(tmp, 128, "%s%s", "_JKAUTO_", sithGamesave_AutosaveMapName()); // Added: single-slot on DC
    stdFnames_ChangeExt(tmp, "jks");
    return sithGamesave_Restore(tmp, 1, 0);
}

int jkDev_CmdDebugFlags(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_DebugMode(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdFly(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_Fly(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdDebugFlags2(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_DebugMode(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdWarp(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    flex32_t v4; // [esp+4h] [ebp-8h] BYREF
    flex32_t v5; // [esp+8h] [ebp-4h] BYREF
    flex32_t v6;

    if ( !sithNet_isMulti )
    {
        if ( pArgStr )
        {
            if ( _sscanf(pArgStr, "%f %f %f", &v5, &v4, &v6) == 3 )
                sithCommand_Warp(pCmd, pArgStr);
        }
    }
    return 1;
}

int jkDev_CmdActivate(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_Activate(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdDebugFlags3(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_DebugMode(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdJump(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        sithCommand_Jump(pCmd, pArgStr);
    return 1;
}

int jkDev_CmdEndLevel2(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
        jkMain_EndLevel(1);
    return 1;
}

#ifdef QOL_IMPROVEMENTS
// Added: the player's saber bin (Jedi Knight's or Mysteries of the Sith's)
static int jkDev_Custom_SaberBin()
{
    return Main_bMotsCompat ? SITHBIN_MOTS_LIGHTSABER : SITHBIN_LIGHTSABER;
}

// Added: the local player's inventory entry for a bin, NULL if there is none
static SithInventoryItem* jkDev_Custom_Item(SithThing *pPlayer, int bin)
{
    if ( !pPlayer || pPlayer->type != SITH_THING_PLAYER || !pPlayer->actorParams.pPlayer
         || pPlayer->actorParams.pPlayer == (SithPlayer *)-136 || !(sithInventory_g_aTypes[bin].flags & SITHINVENTORY_TYPE_REGISTERED) )
        return NULL;
    return &pPlayer->actorParams.pPlayer->aItems[bin];
}

// Added: whether the touch overlay's SABER shows: the saber had for real (not just from a cheat), or
// sithewok on (single player, in a level)
int jkDev_Custom_SaberButtonShown()
{
    if ( sithNet_isMulti || !sithWorld_g_pCurrentWorld )
        return 0;
    SithThing *pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
    SithInventoryItem *pSaber = jkDev_Custom_Item(pPlayer, jkDev_Custom_SaberBin());
    if ( !pSaber )
        return 0;
    return jkDev_bSithEwokOn || (pSaber->amount > 0.0 && !(pSaber->state & SITHINVENTORY_ITEM_CHEAT_GIVEN));
}
#endif

// MOTS altered
int jkDev_CmdAllWeapons(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
#ifdef QOL_IMPROVEMENTS
        // Added: a saber this gives (not had before, or had from a cheat) is marked as the cheat's
        SithInventoryItem *pSaber = jkDev_Custom_Item(sithPlayer_g_pLocalPlayerThing, jkDev_Custom_SaberBin());
        const int bSaberCheat = pSaber && (pSaber->amount <= 0.0 || (pSaber->state & SITHINVENTORY_ITEM_CHEAT_GIVEN));
#endif
        if (!Main_bMotsCompat) {
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_STORMTROOPER_RIFLE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_THERMAL_DETONATOR, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_TUSKEN_PROD, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_REPEATER, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_RAIL_DETONATOR, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_SEQUENCER_CHARGE, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_CONCUSSION_RIFLE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_LIGHTSABER, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_ENERGY, 500.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_POWER, 500.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_BATTERY, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_RAILCHARGES, 100.0);
        }

        if (Main_bMotsCompat) {
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_BRYARPISTOL, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_STORMTROOPER_RIFLE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_THERMAL_DETONATOR, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_CARBO_GUN, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_REPEATER, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_RAIL_DETONATOR, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_SEQUENCER_CHARGE, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_CONCUSSION_RIFLE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_EWEB, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_LIGHTSABER, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_BLASTECH, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_STORMTROOPER_SCOPE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_FLASH_BOMB, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_TUSKEN_PROD, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_RAIL_SEEKER, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_MOTS_MANUAL_SEQUENCER, 1.0);

            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_ENERGY, 500.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_POWER, 500.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_BATTERY, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_RAILCHARGES, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_CARBPELLETS, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_SEEKRAILS, 100.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_EWEB_ROUNDS, 500.0);
        }

#ifdef QOL_IMPROVEMENTS
        if ( bSaberCheat && pSaber->amount > 0.0 )
            pSaber->state |= SITHINVENTORY_ITEM_CHEAT_GIVEN;
#endif
        if ( !jkDev_bQuietCheats ) // Added: sithewok prints one line of its own
            sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_ALLWEAPONS"));
    }
    return 1;
}

// MOTS altered
int jkDev_CmdAllItems(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        if (!Main_bMotsCompat)
        {
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_BACTATANK, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_IRGOGGLES, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_FIELDLIGHT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYIMPERIAL, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRENCH, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_DATADISK, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYRED, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYBLUE, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYYELLOW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHBLUE, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHYELLOW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYGREEN, 1);

            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_BACTATANK, 9.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_IRGOGGLES, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FIELDLIGHT, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYIMPERIAL, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRENCH, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_DATADISK, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYRED, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYBLUE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYYELLOW, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHBLUE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHYELLOW, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYGREEN, 1.0);
        }
        else {
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_BACTATANK, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_IRGOGGLES, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_FIELDLIGHT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYIMPERIAL, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRENCH, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_DATADISK, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYRED, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYBLUE, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYYELLOW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHBLUE, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHYELLOW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYGREEN, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_TSKNCLOTHES, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_HVYEXPLOSIVE, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_HLCRN, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_DRARM, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_PRYBAR, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_RADIO, 1);

            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_BACTATANK, 9.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_IRGOGGLES, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FIELDLIGHT, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYIMPERIAL, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRENCH, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_DATADISK, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYRED, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYBLUE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYYELLOW, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHBLUE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_WRCHYELLOW, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_KEYGREEN, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_TSKNCLOTHES, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_HVYEXPLOSIVE, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_HLCRN, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_DRARM, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_PRYBAR, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_RADIO, 1.0);
        }
        if ( !jkDev_bQuietCheats ) // Added: sithewok prints one line of its own
            sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_ALLITEMS"));
    }
    return 1;
}

int jkDev_CmdLightMaster(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if (Main_bMotsCompat) return 1; // Added

    if ( !sithNet_isMulti )
    {
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_JEDI_RANK, 2.0);
        jkPlayer_SetRank(2);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, (flex_t)(50 * jkPlayer_GetJediRank()));
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 1.0);
        sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_LIGHTMASTER"));
    }
    return 1;
}

int jkDev_CmdDarkMaster(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if (Main_bMotsCompat) return 1; // Added

    if ( !sithNet_isMulti )
    {
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_JEDI_RANK, 2.0);
        jkPlayer_SetRank(2);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, (flex_t)(50 * jkPlayer_GetJediRank()));
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 0);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 1);
        sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 1);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 1.0);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 1.0);
        sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_DARKMASTER"));
    }
    return 1;
}

// MOTS altered
int jkDev_CmdUberJedi(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        if (!Main_bMotsCompat)
        {
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_JEDI_RANK, 2.0);
            jkPlayer_SetRank(2);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, 100.0);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 1);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 1.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 1.0);
        }
        else {
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_JEDI_RANK, 8.0);
            jkPlayer_SetRank(8);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, 400.0);

            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_FARSIGHT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROJECT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SABERTHROW, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PUSH, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_CHAINLIGHT, 1);
            sithInventory_SetInventoryAvailable(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEFENSE, 1);

            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_FARSIGHT, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROJECT, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SABERTHROW, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PUSH, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_CHAINLIGHT, 4.0);
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEFENSE, 4.0);
        }
        
        if ( !jkDev_bQuietCheats ) // Added: sithewok prints one line of its own
            sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_UBERJEDI"));
    }
    return 1;
}

// MOTS altered
int jkDev_CmdLevelUp(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        jkDev_amt = jkDev_amt - -1.0;
        if ( jkDev_amt > 4.0 )
            jkDev_amt = 1.0;
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_JEDI_RANK, jkDev_amt + jkDev_amt);
        jkPlayer_SetRank((__int64)(jkDev_amt + jkDev_amt));
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, (flex_t)(50 * jkPlayer_GetJediRank()));
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_JUMP, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SPEED, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_SEEING, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PULL, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_HEALING, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PERSUASION, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_ABSORB, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_BLINDING, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_PROTECTION, jkDev_amt);
        if (!Main_bMotsCompat)
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_THROW, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_GRIP, jkDev_amt);
        if (!Main_bMotsCompat)
            sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_LIGHTNING, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DESTRUCTION, jkDev_amt);
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_F_DEADLYSIGHT, jkDev_amt);

        if (!Main_bMotsCompat) {
            flex_t v9 = 0.0;
            for (int i = SITHBIN_F_HEALING; i <= SITHBIN_F_ABSORB; ++i )
            {
                if ( sithInventory_GetCarries(sithPlayer_g_pLocalPlayerThing, i) )
                    v9 = sithInventory_GetInventory(sithPlayer_g_pLocalPlayerThing, i) * 5.0 + v9;
            }
            for (int j = SITHBIN_F_THROW; j <= SITHBIN_F_DESTRUCTION; ++j )
            {
                if ( sithInventory_GetCarries(sithPlayer_g_pLocalPlayerThing, j) )
                    v9 = v9 - sithInventory_GetInventory(sithPlayer_g_pLocalPlayerThing, j) * 5.0;
            }
        }

        sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_LEVELUP"));
    }
    return 1;
}

int jkDev_CmdHeal(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        sithPlayer_g_pLocalPlayerThing->actorParams.health = sithPlayer_g_pLocalPlayerThing->actorParams.maxHealth;
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_SHIELDS, 200.0);
        if ( !jkDev_bQuietCheats ) // Added: sithewok prints one line of its own
            sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_HEAL"));
    }
    return 1;
}

int jkDev_CmdAllMap(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        g_mapModeFlags ^= (SITHMAPMODE_SHOWALLTHINGS | SITHMAPMODE_SHOWALLSECTORS);
        sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_ALLMAP"));
    }
    return 1;
}

int jkDev_CmdMana(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !sithNet_isMulti )
    {
        sithInventory_SetInventory(sithPlayer_g_pLocalPlayerThing, SITHBIN_FORCEMANA, 400.0);
        sithConsole_PrintWString(jkStrings_GetUniStringWithFallback("GAME_MANA"));
    }
    return 1;
}

// MOTS altered
int jkDev_CmdTeam(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if ( !pArgStr || !sithNet_isMulti || (sithNet_MultiModeFlags & MULTIMODEFLAG_TEAMS) == 0 || (sithNet_MultiModeFlags & MULTIMODEFLAG_100) == 0 )
        return 1;

    uint32_t v2 = _atol(pArgStr);
    if ( !v2 || v2 >= 5 )
        return 0;

    jkDSS_SendSetTeam(v2);
    return 1;
}

// Dialog stuff

int jkDev_UpdateEntries()
{
    int v0; // ebp
    signed int result; // eax
    jkDevLogEnt* v2; // edi
    jkDevLogEnt* v3; // esi
    int v4; // [esp+4h] [ebp-8h]
    unsigned int v5; // [esp+8h] [ebp-4h]

    v0 = 0;
    v4 = 0;
    v5 = stdPlatform_GetTimeMsec();
    result = 0;
    if ( jkDev_log_55A4A4 > 0 )
    {
        v2 = &jkDev_aEntries[0];
        v3 = &jkDev_aEntries[0];
        do
        {
            if ( v3->timeMsExpiration <= v5 )
            {
                v3->bDrawEntry = 2;
                v3->field_10C = 2;
            }
            else
            {
                if ( v4 != v0 )
                {
                    _wcsncpy(v2->text, v3->text, 0x80u);
                    result = 1;
                    v2->timeMsExpiration = v3->timeMsExpiration;
                    v2->field_104 = v3->field_104;
                    v2->bDrawEntry = 2;
                    v2->field_10C = 2;
                    v3->bDrawEntry = 2;
                    v3->field_10C = 2;
                    jkDev_bScreenNeedsUpdate = 1;
                }
                ++v2;
                ++v4;
            }
            ++v0;
            ++v3;
        }
        while ( v0 < jkDev_log_55A4A4 );
    }
    jkDev_log_55A4A4 = v4;
    return result;
}

void jkDev_DrawEntries()
{
    int v0; // ebp
    signed int v1; // edi
    jkDevLogEnt* v3; // esi
    rdRect a4; // [esp+8h] [ebp-10h] BYREF

#ifdef TARGET_TWL
    return;
#endif

    // Added: GPU rendered text
#ifdef SDL2_RENDER
    jkDev_DrawEntriesGPU();
    return;
#endif

    v0 = 0;
    if ( jkDev_vbuf )
    {
        if ( jkDev_bScreenNeedsUpdate )
        {
            v1 = 0;
            jkDev_bScreenNeedsUpdate = 0;
            if ( jkDev_log_55A4A4 > 0 )
            {
                v3 = &jkDev_aEntries[0];
                do
                {
                    if ( v3->bDrawEntry )
                    {
                        a4.height = jkDev_BMFontHeight;
                        a4.width = v3->drawWidth;
                        a4.x = 0;
                        a4.y = v1;
                        stdDisplay_VBufferFill(jkDev_vbuf, jkDev_ColorKey, &a4);
                        v3->drawWidth = stdFont_Draw1(jkDev_vbuf, jkHud_pMsgFontSft, 0, v1, jkDev_vbuf->format.width, v3->text, 0);
                    }
                    v1 += jkDev_BMFontHeight;
                    ++v0;
                    ++v3;
                }
                while ( v0 < jkDev_log_55A4A4 );
            }
        }
    }
}

void jkDev_DrawEntriesGPU()
{
    int v0; // ebp
    signed int v1; // edi
    jkDevLogEnt* v3; // esi
    rdRect a4; // [esp+8h] [ebp-10h] BYREF

    v0 = 0;
    if ( jkDev_vbuf )
    {
        //if ( jkDev_bScreenNeedsUpdate )
        {
            v1 = 0;
            jkDev_bScreenNeedsUpdate = 0;
            if ( jkDev_log_55A4A4 > 0 )
            {
                v3 = &jkDev_aEntries[v0];
                do
                {
                    //if ( v3->bDrawEntry )
                    {
                        a4.height = (int)((flex_t)jkDev_BMFontHeight * jkPlayer_hudScale);
                        a4.width = v3->drawWidth;
                        a4.x = 0;
                        a4.y = v1;
                        stdDisplay_VBufferFill(jkDev_vbuf, jkDev_ColorKey, &a4); // jkDev_vbuf->format.width
                        v3->drawWidth = stdFont_Draw1Width(jkHud_pMsgFontSft, 0, v1, Video_menuBuffer.format.width, v3->text, 0, jkPlayer_hudScale);
                    }
                    v1 += (int)((flex_t)jkDev_BMFontHeight * jkPlayer_hudScale);
                    ++v0;
                    ++v3;
                }
                while ( v0 < jkDev_log_55A4A4 );
            }
        }
    }
}

#ifdef QOL_IMPROVEMENTS
int jkDev_CmdNoclip(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    if (sithNet_isMulti ) return 1;

    SithThing *v0; // ecx
    char16_t *v3; // eax

    if (!sithWorld_g_pCurrentWorld || !sithWorld_g_pCurrentWorld->pLocalPlayer) {
        sithConsole_PrintString("No world.");
        return 0;
    }

    v0 = sithWorld_g_pCurrentWorld->pLocalPlayer;

    if ( v0->moveType == SITH_MT_PHYSICS )
    {
        if ((g_debugmodeFlags & DEBUGFLAG_NOCLIP))
        {
            v0->physicsParams.flags &= ~SITH_PF_FLY;
            v0->physicsParams.flags |= SITH_PF_USEGRAVITY;
            g_debugmodeFlags &= ~DEBUGFLAG_NOCLIP;
            sithPlayer_bNoClippingRend = 0;
            sithConsole_PrintString("Noclip OFF");
        }
        else
        {
            v0->physicsParams.flags &= ~SITH_PF_USEGRAVITY;
            v0->physicsParams.flags |= SITH_PF_FLY;
            g_debugmodeFlags |= DEBUGFLAG_NOCLIP;
            sithConsole_PrintString("Noclip ON");
        }
        
        return 1;
    }
    else
    {
        sithConsole_PrintString("Not physics thing.");
        return 0;
    }

    return 0;
}

// Added: a full Force meter, 50 per Jedi rank as kyle.cog fills it (MOTS too: force_well.cog caps it there in single player)
static flex_t jkDev_Custom_GetForceManaMax(SithThing *pPlayer)
{
    return sithInventory_GetInventory(pPlayer, SITHBIN_JEDI_RANK) * 50.0;
}

// Added: sithewok's endless Force, refills what the powers spent (local player, single player only);
// and, once a frame, sithewok kept on: invincible always (a save from before it was on is loaded
// without), and everything again for a new level's player
static void jkDev_Custom_KeepForceFull()
{
    SithThing *pPlayer;
    flex_t maxMana;

    if ( sithNet_isMulti || !sithWorld_g_pCurrentWorld )
        return;

    pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
    if ( !pPlayer || pPlayer->type != SITH_THING_PLAYER )
        return;

    if ( pPlayer->actorParams.pPlayer && !(pPlayer->flags & SITH_TF_DEAD) )
    {
        int weapon = (sithWeapon_8BD024 != -1) ? sithWeapon_8BD024 : pPlayer->actorParams.pPlayer->curWeaponID;
        if ( weapon > 0 && weapon != jkDev_Custom_SaberBin() )
            jkDev_lastOtherWeapon = weapon;
    }

    if ( jkDev_bSithEwokOn )
    {
        pPlayer->actorParams.flags |= SITH_AF_INVULNERABLE;
        if ( pPlayer != jkDev_pSithEwokPlayer )
        {
            // a new level (or a load): not flying any more; everything again in a moment
            jkDev_pSithEwokPlayer = pPlayer;
            jkDev_bSithEwokFlying = 0;
            jkDev_sithEwokWait = JKDEV_SITHEWOK_WAIT;
        }
        if ( jkDev_sithEwokWait > 0 && --jkDev_sithEwokWait == 0 )
        {
            jkDev_Custom_SithEwokOn(pPlayer, 1);
            // the power selected in the last level, selected again (now it is had)
            const int power = jkDev_sithEwokPowerBack;
            jkDev_sithEwokPowerBack = -1;
            if ( power > 0 && pPlayer->actorParams.pPlayer && power != pPlayer->actorParams.pPlayer->curPower
                 && sithInventory_GetInventory(pPlayer, power) > 0.0 && sithInventory_IsInventoryAvailable(pPlayer, power) )
                sithInventory_SelectPower(pPlayer, power);
        }
        else if ( jkDev_sithEwokWait == 0 && jkDev_sithEwokPowerBack < 0 && pPlayer->actorParams.pPlayer
                  && pPlayer->actorParams.pPlayer->curPower > 0 )
            jkDev_sithEwokPower = pPlayer->actorParams.pPlayer->curPower;

        // the weapon in hand kept for the next level; in a new one, put back (as soon as the
        // player has it: carried over, or given again with everything after the wait above)
        if ( jkDev_sithEwokWeaponBack >= 0 )
        {
            if ( jkDev_Custom_PutWeaponBack(pPlayer) || --jkDev_sithEwokWeaponTries <= 0 )
                jkDev_sithEwokWeaponBack = -1;
        }
        else if ( pPlayer->actorParams.pPlayer && !(pPlayer->flags & SITH_TF_DEAD) )
        {
            int weapon = (sithWeapon_8BD024 != -1) ? sithWeapon_8BD024 : pPlayer->actorParams.pPlayer->curWeaponID;
            if ( weapon > 0 )
                jkDev_sithEwokWeapon = weapon;
        }
    }

    if ( !jkDev_bEndlessForce )
        return;

    maxMana = jkDev_Custom_GetForceManaMax(pPlayer);
    if ( sithInventory_GetInventory(pPlayer, SITHBIN_FORCEMANA) < maxMana )
        sithInventory_SetInventory(pPlayer, SITHBIN_FORCEMANA, maxMana);
}

// Added: MOTS: after sithewok the Force screen (jkPlayer_SyncForcePowers) leaves a tier with more powers
// than the rank allows as it is, instead of taking them all back (single player)
int jkDev_Custom_KeepsForcePowers()
{
    return jkDev_bKeepForcePowers && Main_bMotsCompat && !sithNet_isMulti;
}

// Added: sithewok: falls don't hurt the player (sithCollision_FallHurt). The game's own invincibility
// (SITH_AF_INVULNERABLE) lets fall damage through on purpose (sithActor_Hit), so this is its own switch.
int jkDev_Custom_NoFallHurt()
{
    return jkDev_bNoFallHurt && !sithNet_isMulti;
}

// Added: sithewok: flight, switched on and off by a long press on the touch overlay's JUMP (iosGame).
// The engine's own flying (the "fly" cheat, SITH_PF_FLY): no gravity, forward goes the way the player
// looks, JUMP rises and DUCK sinks (sithControl), walls still in the way. Only while sithewok is on, in
// single player, and not in noclip (which flies its own way).
static SithThing* jkDev_Custom_FlightPlayer()
{
    SithThing* pPlayer;
    if ( !jkDev_bNoFallHurt || sithNet_isMulti || (g_debugmodeFlags & DEBUGFLAG_NOCLIP) || !sithWorld_g_pCurrentWorld )
        return NULL;
    pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
    if ( !pPlayer || pPlayer->type != SITH_THING_PLAYER || pPlayer->moveType != SITH_MT_PHYSICS || (pPlayer->flags & SITH_TF_DEAD) )
        return NULL;
    return pPlayer;
}

int jkDev_Custom_CanFly()
{
    return jkDev_Custom_FlightPlayer() != NULL;
}

// Whether the player is flying now (and could stop): the touch overlay's JUMP held doesn't stop it
int jkDev_Custom_IsFlying()
{
    SithThing* pPlayer = jkDev_Custom_FlightPlayer();
    return pPlayer && (pPlayer->physicsParams.flags & SITH_PF_FLY);
}

static void jkDev_Custom_SetFlying(SithThing* pPlayer, int bFly)
{
    if ( bFly )
    {
        pPlayer->physicsParams.flags &= ~SITH_PF_USEGRAVITY;
        pPlayer->physicsParams.flags |= SITH_PF_FLY;
    }
    else
    {
        pPlayer->physicsParams.flags &= ~SITH_PF_FLY;
        pPlayer->physicsParams.flags |= SITH_PF_USEGRAVITY;
    }
    jkDev_bSithEwokFlying = bFly;
}

// 1: flying now; 0: not any more; -1: can't now (sithewok off, multiplayer, noclip, dead...)
int jkDev_Custom_ToggleFlight()
{
    SithThing* pPlayer = jkDev_Custom_FlightPlayer();
    if ( !pPlayer )
        return -1;
    if ( pPlayer->physicsParams.flags & SITH_PF_FLY )
    {
        jkDev_Custom_SetFlying(pPlayer, 0);
        sithConsole_PrintString("Flying off");
        return 0;
    }
    jkDev_Custom_SetFlying(pPlayer, 1);
    sithConsole_PrintString("Flying! JUMP rises, DUCK sinks; tap JUMP twice to land");
    return 1;
}

// Added: sithewok: noclip (through walls, flying), switched on and off by a long press on the touch
// overlay's ACT (iosGame), as the "noclip" cheat does it (jkDev_CmdNoclip). Only while sithewok is on, in
// single player. Noclip turned off while sithewok's flight is on: still flying (noclip's own way down
// would end the flight).
static SithThing* jkDev_Custom_NoclipPlayer()
{
    SithThing* pPlayer;
    if ( !jkDev_bNoFallHurt || sithNet_isMulti || !sithWorld_g_pCurrentWorld )
        return NULL;
    pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
    if ( !pPlayer || pPlayer->type != SITH_THING_PLAYER || pPlayer->moveType != SITH_MT_PHYSICS || (pPlayer->flags & SITH_TF_DEAD) )
        return NULL;
    return pPlayer;
}

int jkDev_Custom_CanNoclip()
{
    return jkDev_Custom_NoclipPlayer() != NULL;
}

// 1: noclip on now; 0: off; -1: can't now (sithewok off, multiplayer, dead...)
int jkDev_Custom_ToggleNoclip()
{
    SithThing* pPlayer = jkDev_Custom_NoclipPlayer();
    if ( !pPlayer )
        return -1;
    jkDev_CmdNoclip(NULL, ""); // (prints "Noclip ON" or "Noclip OFF")
    const int bOn = (g_debugmodeFlags & DEBUGFLAG_NOCLIP) != 0;
    jkDev_bSithEwokNoclip = bOn;
    if ( !bOn && jkDev_bSithEwokFlying )
        jkDev_Custom_SetFlying(pPlayer, 1);
    return bOn;
}

// Added: sithewok: a level picked by number (the touch overlay's DUCK, held: iosGame), loaded at once.
// Levels are numbered as the episode numbers them (as "pinotnoir" takes them: Jedi Knight 1 to 21,
// Mysteries of the Sith 1 to 13); the level itself, not the cutscene before it (pinotnoir's first entry
// with that number is often a cutscene). Where a number has two (Jedi Knight's light and dark paths),
// the first in the episode. Only while sithewok is on, in single player.
int jkDev_Custom_CanPickLevel()
{
    return jkDev_bNoFallHurt && !sithNet_isMulti && jkEpisode_mLoad.paEntries && jkEpisode_mLoad.numSeq > 0
           && sithWorld_g_pCurrentWorld && sithWorld_g_pCurrentWorld->pLocalPlayer;
}

// The highest level number in the episode (0: none)
int jkDev_Custom_NumLevels()
{
    int n = 0;
    for ( int i = 0; jkEpisode_mLoad.paEntries && i < jkEpisode_mLoad.numSeq; i++ )
    {
        const jkEpisodeEntry* pEnt = &jkEpisode_mLoad.paEntries[i];
        if ( pEnt->type == 0 && pEnt->level > n ) // (0: a level, not a cutscene or the path's choice)
            n = pEnt->level;
    }
    return n;
}

// 1: that level is loading; 0: there's no such level, or it can't now
int jkDev_Custom_GoToLevel(int level)
{
    if ( !jkDev_Custom_CanPickLevel() )
        return 0;
    for ( int i = 0; i < jkEpisode_mLoad.numSeq; i++ )
    {
        jkEpisodeEntry* pEnt = &jkEpisode_mLoad.paEntries[i];
        if ( pEnt->type != 0 || pEnt->level != level )
            continue;
        // (as pinotnoir does it, jkMain_SetMap, at this entry)
        jkEpisode_mLoad.currentEpisodeEntryIdx = i;
        jkMain_cd_swap_reverify(pEnt);
        return 1;
    }
    return 0;
}

// Added: sithewok: the Force catches the player falling into a pit and puts them back on solid ground
// (sithPlayer_Update); on and off with the safe falls
int jkDev_Custom_CatchesFalls()
{
    return jkDev_bNoFallHurt && !sithNet_isMulti;
}

// Added: sithewok, everything at once, invincible, endless Force, falls don't hurt; "sithewok off" ends
// the last three
int jkDev_Custom_CmdSithEwok(stdDebugConsoleCmd *pCmd, const char *pArgStr)
{
    SithThing *pPlayer;
    char arg[8] = {0};

    if ( sithNet_isMulti )
        return 1;

    // The local player, as jediwannabe finds it
    if ( !sithWorld_g_pCurrentWorld )
        return 0;
    pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
    if ( !pPlayer || pPlayer->type != SITH_THING_PLAYER )
        return 0;

    if ( pArgStr )
        _sscanf(pArgStr, "%7s", arg);
    if ( !__strcmpi(arg, "off") )
    {
        pPlayer->actorParams.flags &= ~SITH_AF_INVULNERABLE;
        jkDev_bSithEwokOn = 0;
        jkDev_pSithEwokPlayer = NULL;
        jkDev_sithEwokWait = 0;
        jkDev_sithEwokWeapon = -1;
        jkDev_sithEwokWeaponBack = -1;
        jkDev_sithEwokPower = -1;
        jkDev_sithEwokPowerBack = -1;
        jkDev_bEndlessForce = 0;
        jkDev_bNoFallHurt = 0;
        // (noclip because of it: off; flying because of it: back down)
        if ( jkDev_bSithEwokNoclip && (g_debugmodeFlags & DEBUGFLAG_NOCLIP) )
            jkDev_CmdNoclip(NULL, "");
        jkDev_bSithEwokNoclip = 0;
        if ( jkDev_bSithEwokFlying && (pPlayer->physicsParams.flags & SITH_PF_FLY) && !(g_debugmodeFlags & DEBUGFLAG_NOCLIP) )
            jkDev_Custom_SetFlying(pPlayer, 0);
        jkDev_bSithEwokFlying = 0;
        // (a saber only it gave goes with it)
        jkDev_Custom_TakeCheatSaber(pPlayer);
        // (no message for it going off, as none for it going on)
        return 1;
    }

    jkDev_Custom_SithEwokOn(pPlayer, 0);
    return 1;
}

// Added: whether sithewok is on (the one place that says: the cheat typed and the touch overlay's CHEAT
// both go through jkDev_Custom_CmdSithEwok); never in multiplayer
int jkDev_Custom_SithEwokIsOn()
{
    return jkDev_bSithEwokOn && !sithNet_isMulti;
}

// Added: sithewok switched as if typed ("sithewok", or "sithewok off" when on), for the touch overlay's
// CHEAT. 1: on now; 0: off now; -1: can't now (multiplayer, no player)
// Added: whether something on screen shows sithewok is on (the touch overlay's CHEAT)
void jkDev_Custom_SetSithEwokShown(int bShown)
{
    jkDev_bSithEwokShown = bShown;
}

int jkDev_Custom_ToggleSithEwok()
{
    const int bWasOn = jkDev_Custom_SithEwokIsOn();
    if ( sithNet_isMulti || !jkDev_Custom_CmdSithEwok(NULL, bWasOn ? "off" : "") )
        return -1;
    return jkDev_Custom_SithEwokIsOn();
}

// Added: a level opened or a game loaded (sithGamesave_RestoreFile, which can load on the same level):
// sithewok, if on, put on again for its player, even one at the last one's address
void jkDev_Custom_SithEwokRestored()
{
    jkDev_pSithEwokPlayer = NULL;
}

// Added: a game loaded (sithGamesave_RestoreFile): as above, and no weapon put back from the last level
// (the loaded game has its own in hand)
void jkDev_Custom_GameLoaded()
{
    jkDev_Custom_SithEwokRestored();
    jkDev_sithEwokWeaponBack = -1;
    jkDev_sithEwokPowerBack = -1; // (the loaded game's own power selected)

    // A game saved with sithewok on, loaded with it off: none of it kept -- a saber only it gave taken
    // back, and not invincible (the level scripts only make the player so in end-of-level sequences,
    // when there is no saving)
    if ( !jkDev_bSithEwokOn && !sithNet_isMulti && sithWorld_g_pCurrentWorld )
    {
        SithThing *pPlayer = sithWorld_g_pCurrentWorld->pLocalPlayer;
        if ( pPlayer && pPlayer->type == SITH_THING_PLAYER )
        {
            pPlayer->actorParams.flags &= ~SITH_AF_INVULNERABLE;
            jkDev_Custom_TakeCheatSaber(pPlayer);
        }
    }
}

// Added: a saber only a cheat gave (SITHINVENTORY_ITEM_CHEAT_GIVEN; the game's own giving of it clears
// that) taken back, with sithewok going off or a game loaded without it: if it is in hand or on its way,
// the last other weapon the player had in hand instead (else the game's own best pick). One the game
// gave is never touched.
static void jkDev_Custom_TakeCheatSaber(SithThing *pPlayer)
{
    const int bin = jkDev_Custom_SaberBin();
    SithInventoryItem *pSaber = jkDev_Custom_Item(pPlayer, bin);
    if ( !pSaber || !(pSaber->state & SITHINVENTORY_ITEM_CHEAT_GIVEN) )
        return;
    const int cur = pPlayer->actorParams.pPlayer->curWeaponID;
    const int bInHand = cur == bin || sithWeapon_8BD024 == bin;
    sithInventory_SetInventory(pPlayer, bin, 0.0);
    pSaber->state &= ~SITHINVENTORY_ITEM_CHEAT_GIVEN;
    if ( !bInHand )
        return;
    if ( sithWeapon_8BD024 == bin )
        sithWeapon_8BD024 = -1; // (no switch to it any more)
    int other = jkDev_lastOtherWeapon;
    if ( other <= 0 || other == bin || sithInventory_GetInventory(pPlayer, other) <= 0.0 || !sithInventory_IsInventoryAvailable(pPlayer, other) )
        other = sithWeapon_AutoSelect(pPlayer, 1);
    if ( other > 0 && other != bin )
    {
        if ( cur == bin )
            sithWeapon_SelectWeapon(pPlayer, other, 0);
        else if ( sithWeapon_8BD024 == -1 && cur != other )
            sithWeapon_SelectWeapon(pPlayer, other, 0);
    }
}

// Added: sithewok: the weapon to put back in hand into the player's hand, if it can be now: in place of
// the one the level's start-up is about to bring out, before it is drawn, where nothing is in hand yet;
// else switched to as its number key would. 1: done (or nothing to do), 0: not yet
static int jkDev_Custom_PutWeaponBack(SithThing *pPlayer)
{
    const int bin = jkDev_sithEwokWeaponBack;
    if ( bin < 0 )
        return 1;
    if ( !pPlayer || !pPlayer->actorParams.pPlayer || (pPlayer->flags & SITH_TF_DEAD) )
        return 0;
    const int cur = pPlayer->actorParams.pPlayer->curWeaponID;
    if ( cur == bin && sithWeapon_8BD024 == -1 )
        return 1; // in hand
    if ( sithWeapon_8BD024 == bin )
        return 0; // on its way
    if ( sithInventory_GetInventory(pPlayer, bin) == 0.0 || !sithInventory_IsInventoryAvailable(pPlayer, bin) )
        return 0; // not had (yet)
    if ( cur <= 0 && sithWeapon_8BD024 != -1 )
    {
        // the start-up's pick, not out yet: ours instead (the weapon's own cog still says whether it can be)
        const int startupPick = sithWeapon_8BD024;
        sithWeapon_8BD024 = -1;
        if ( sithWeapon_SelectWeapon(pPlayer, bin, 0) )
            return 1;
        sithWeapon_8BD024 = startupPick;
        return 0;
    }
    if ( sithWeapon_8BD024 != -1 || sithTime_g_secGameTime < sithWeapon_secMountWait
         || (pPlayer->weaponParams.flags & SITH_WF_EMITAITARGETEDEVENT) )
        return 0; // (as the number keys wait)
    return sithWeapon_SelectWeapon(pPlayer, bin, 0);
}

// Added: a level opened (jkMain, single player; not a loaded game): with sithewok on, the weapon that was
// in hand is put back, right away where it can be; and the Force power selected, once given again
void jkDev_Custom_LevelOpened()
{
    jkDev_sithEwokWeaponBack = -1;
    jkDev_sithEwokPowerBack = -1;
    if ( !jkDev_bSithEwokOn || sithNet_isMulti || !sithWorld_g_pCurrentWorld )
        return;
    jkDev_sithEwokPowerBack = jkDev_sithEwokPower; // (selected again once sithewok has given its powers)
    if ( jkDev_sithEwokWeapon < 0 )
        return;
    jkDev_sithEwokWeaponBack = jkDev_sithEwokWeapon;
    jkDev_sithEwokWeaponTries = JKDEV_SITHEWOK_WEAPON_TRIES;
    jkDev_Custom_PutWeaponBack(sithWorld_g_pCurrentWorld->pLocalPlayer); // (then followed until it is in hand)
}

// Added: sithewok put on the player: every weapon, item and power, healed, invincible, endless Force,
// safe falls, the Force's catch; and kept on from then (jkDev_Custom_KeepForceFull). bAgain: put on
// again for a new level, with a shorter message
static void jkDev_Custom_SithEwokOn(SithThing *pPlayer, int bAgain)
{
    jkDev_bQuietCheats = 1;
    jkDev_CmdAllWeapons(NULL, "");
    jkDev_CmdAllItems(NULL, "");
    if ( Main_bMotsCompat )
    {
        jkDev_CmdUberJedi(NULL, ""); // iamagod: rank 8, its powers at 4 stars
        jkDev_bKeepForcePowers = 1; // all 17 are more than rank 8's tiers allow (jkPlayer_aMotsFpBins)
    }
    else
    {
        // all 14 powers (neutral, light and dark) at 4 stars, and the top rank
        sithInventory_SetInventory(pPlayer, SITHBIN_JEDI_RANK, 8.0);
        jkPlayer_SetRank(8);
        for (int i = SITHBIN_F_JUMP; i <= SITHBIN_F_DEADLYSIGHT; i++)
        {
            sithInventory_SetInventoryAvailable(pPlayer, i, 1);
            sithInventory_SetInventory(pPlayer, i, 4.0);
        }
    }
    jkDev_CmdHeal(NULL, "");
    jkDev_bQuietCheats = 0;

    sithInventory_SetInventory(pPlayer, SITHBIN_FORCEMANA, jkDev_Custom_GetForceManaMax(pPlayer));
    pPlayer->actorParams.flags |= SITH_AF_INVULNERABLE; // jediwannabe on (sithCommand_DebugMode, extra 5)
    jkDev_bEndlessForce = 1;
    jkDev_bNoFallHurt = 1;
    jkDev_bSithEwokOn = 1;
    jkDev_pSithEwokPlayer = pPlayer;
    jkDev_sithEwokWait = 0;
    // (no message for it going on; put on again for a new level, a short reminder only where the touch
    // overlay's CHEAT isn't showing, red, that it is on)
    if ( bAgain && !jkDev_bSithEwokShown )
        sithConsole_PrintString("sithewok: still on");
}
#endif

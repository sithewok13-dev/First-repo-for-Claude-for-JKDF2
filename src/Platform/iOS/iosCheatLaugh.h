// Added: entire file. The touch overlay's cheat mode easter egg: the
// Emperor's laugh, played once each time cheat mode is switched on in MENU's
// tray (iosTouch: the CHEAT switch, off to on) -- nowhere else.
//
// The recording isn't part of the app: like the game itself, it is the
// player's own file, laughing.mp3, put in the app's folder in Files (next to
// Episode and Resource, or at the top of the app's folder). Without it,
// nothing plays.
//
// Played whole and as it is, once, at the game's sound effects volume (none
// at 0), alongside the game's own sounds; while it is still playing, another
// switch-on doesn't start a second copy.

#ifndef _PLATFORM_IOS_IOSCHEATLAUGH_H
#define _PLATFORM_IOS_IOSCHEATLAUGH_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

#define IOSCHEATLAUGH_FILE "laughing.mp3"

// The laugh, if the file is there and it isn't already playing. Main thread.
// 1: playing it now; 0: not (no file, it can't be read, or already playing).
int iosCheatLaugh_Play(float volume);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSCHEATLAUGH_H

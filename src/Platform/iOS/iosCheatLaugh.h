// Added: entire file. The touch overlay's cheat mode easter egg: the
// Emperor's laugh, played once each time cheat mode is switched on in MENU's
// tray (iosTouch: the CHEAT switch, off to on) -- nowhere else.
//
// The recording, laughing.mp3, is in the app itself (packaging/ios, copied
// into the bundle by cmake_modules/target_ios_all.cmake), so both apps have it.
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

#define IOSCHEATLAUGH_NAME "laughing"
#define IOSCHEATLAUGH_EXT "mp3"

// The laugh, if the file is there and it isn't already playing. Main thread.
// 1: playing it now; 0: not (it can't be read, sound effects are off, or it is already playing).
int iosCheatLaugh_Play(float volume);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSCHEATLAUGH_H

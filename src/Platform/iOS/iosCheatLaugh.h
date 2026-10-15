// Added: entire file. The touch overlay's cheat mode easter egg: the Emperor,
// once each time cheat mode is switched on (iosTouch: the tray's CHEAT switch
// off to on, or "sithewok" typed with CHEAT hidden) -- nowhere else. One of
// three recordings, picked at random, never the one played last time: his
// laugh, "the dark side", or "unlimited power".
//
// The recordings are in the app itself (packaging/ios, copied into the bundle
// by cmake_modules/target_ios_all.cmake), so both apps have them, as the owner
// gave them.
//
// Played whole and as it is, once, at the game's sound effects volume (none
// at 0), alongside the game's own sounds; while one is still playing, another
// switch-on doesn't start a second.

#ifndef _PLATFORM_IOS_IOSCHEATLAUGH_H
#define _PLATFORM_IOS_IOSCHEATLAUGH_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

#define IOSCHEATLAUGH_EXT "mp3"
#define IOSCHEATLAUGH_NAMES { "laughing", "palpatine_dark_side", "palpatine_unlimited_power" }
#define IOSCHEATLAUGH_NUM 3

// One of them, if it can be read and none is already playing. Main thread.
// 1: playing one now; 0: not (none can be read, sound effects are off, or one is already playing).
int iosCheatLaugh_Play(float volume);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSCHEATLAUGH_H

// Added: entire file. iOS audio session handling.
//
// iOS has two separate audio clients in this port: OpenAL Soft (sound effects,
// a CoreAudio RemoteIO unit) and SDL3_mixer (music, an SDL audio device). Only
// SDL manages the shared AVAudioSession, and it reconfigures it -- deactivate,
// change category, reactivate -- when the music device opens, which is after
// OpenAL has started. OpenAL Soft never notices: its output unit stays stopped,
// so there were no sound effects while the music still played.
//
// This sets the session up once before either client opens, the same way SDL
// would, and restarts OpenAL's output whenever the session changes under it
// (SDL reconfiguring it, a phone call or Siri interruption, a route change,
// coming back to the foreground).

#ifndef _PLATFORM_IOS_IOSAUDIO_H
#define _PLATFORM_IOS_IOSAUDIO_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// After SDL_Init, before any audio device is opened.
void iosAudio_Startup(void);

// Restart OpenAL's output if the session was changed under it. Safe to call
// any time; does nothing before OpenAL has a current context.
void iosAudio_RestartSfx(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSAUDIO_H

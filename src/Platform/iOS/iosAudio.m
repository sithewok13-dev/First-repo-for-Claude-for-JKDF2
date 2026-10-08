// Added: entire file. See iosAudio.h.
// Written to work with or without ARC, like iosTouch.m.

#ifdef TARGET_IOS

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "alc.h"
#include "alext.h"

// No engine headers here: types.h typedefs BOOL as int, which collides with
// Objective-C's BOOL. Logging goes through NSLog for the same reason.
#include "Platform/iOS/iosAudio.h"

static int iosAudio_bStarted = 0;
static int iosAudio_bInRestart = 0;
static CFTimeInterval iosAudio_lastRestart = 0.0;

// Exactly what SDL3's UpdateAudioSession picks for a playback-only app with no
// SDL_HINT_AUDIO_CATEGORY set. Matching it means SDL finds the session already
// configured when the music device opens and leaves it alone. (Playback also
// plays with the ring/silent switch on, like the SDL default.)
static void iosAudio_ConfigureSession(void)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSUInteger options = AVAudioSessionCategoryOptionMixWithOthers | AVAudioSessionCategoryOptionDuckOthers;
    NSError* err = nil;

    if (![session.category isEqualToString:AVAudioSessionCategoryPlayback] || session.categoryOptions != options) {
        if (![session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:options error:&err]) {
            NSLog(@"iosAudio: setCategory failed: %@", err);
        }
    }
}

static void iosAudio_Activate(void)
{
    NSError* err = nil;
    if (![[AVAudioSession sharedInstance] setActive:YES error:&err]) {
        // Expected during a call or Siri; the interruption-ended or
        // became-active notification brings us back here.
        NSLog(@"iosAudio: setActive failed: %@", err);
    }
}

void iosAudio_RestartSfx(void)
{
    if (iosAudio_bInRestart) return;

    ALCcontext* pContext = alcGetCurrentContext();
    ALCdevice* pDevice = pContext ? alcGetContextsDevice(pContext) : NULL;
    if (!pDevice) return;

    LPALCDEVICEPAUSESOFT pPause = (LPALCDEVICEPAUSESOFT)alcGetProcAddress(pDevice, "alcDevicePauseSOFT");
    LPALCDEVICERESUMESOFT pResume = (LPALCDEVICERESUMESOFT)alcGetProcAddress(pDevice, "alcDeviceResumeSOFT");
    if (!pPause || !pResume) return;

    iosAudio_bInRestart = 1;
    iosAudio_lastRestart = CACurrentMediaTime();
    iosAudio_Activate();
    // Pause/resume is AudioOutputUnitStop + AudioOutputUnitStart on OpenAL
    // Soft's CoreAudio backend: it restarts a unit the session stopped.
    pPause(pDevice);
    pResume(pDevice);
    iosAudio_bInRestart = 0;
}

// The whole RemoteIO unit is gone after a media services reset; reopening
// rebuilds it (ALC_SOFT_reopen_device), keeping the context and all buffers.
static void iosAudio_ReopenSfx(void)
{
    ALCcontext* pContext = alcGetCurrentContext();
    ALCdevice* pDevice = pContext ? alcGetContextsDevice(pContext) : NULL;
    if (!pDevice) return;

    iosAudio_ConfigureSession();
    iosAudio_Activate();

    LPALCREOPENDEVICESOFT pReopen = (LPALCREOPENDEVICESOFT)alcGetProcAddress(pDevice, "alcReopenDeviceSOFT");
    if (!pReopen || !pReopen(pDevice, NULL, NULL)) {
        iosAudio_RestartSfx();
    }
}

void iosAudio_Startup(void)
{
    if (iosAudio_bStarted) return;
    iosAudio_bStarted = 1;

    iosAudio_ConfigureSession();
    iosAudio_Activate();

    // Delivered on the main queue, i.e. from inside SDL's event pump on the
    // game thread -- never concurrently with the engine's own AL calls.
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    NSOperationQueue* mainQueue = [NSOperationQueue mainQueue];
    AVAudioSession* session = [AVAudioSession sharedInstance];

    [center addObserverForName:AVAudioSessionInterruptionNotification object:session queue:mainQueue
                    usingBlock:^(NSNotification* note) {
        NSNumber* type = note.userInfo[AVAudioSessionInterruptionTypeKey];
        if (type && type.unsignedIntegerValue == AVAudioSessionInterruptionTypeEnded) {
            iosAudio_RestartSfx();
        }
    }];

    // Includes category changes (SDL reconfiguring the session) and output
    // changes such as plugging in or removing headphones.
    [center addObserverForName:AVAudioSessionRouteChangeNotification object:session queue:mainQueue
                    usingBlock:^(NSNotification* note) {
        // One session change can post several of these, and our own restart
        // may post one too; a restart just now already covered them.
        if (CACurrentMediaTime() - iosAudio_lastRestart < 0.3) return;
        iosAudio_RestartSfx();
    }];

    [center addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:session queue:mainQueue
                    usingBlock:^(NSNotification* note) {
        iosAudio_ReopenSfx();
    }];

    // An interruption-ended notification isn't guaranteed, so also on returning
    // to the foreground.
    [center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:mainQueue
                    usingBlock:^(NSNotification* note) {
        iosAudio_RestartSfx();
    }];
}

#endif // TARGET_IOS

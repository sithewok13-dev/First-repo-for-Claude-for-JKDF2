// Added: entire file. See iosCheatLaugh.h.

#ifdef TARGET_IOS

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include <stdlib.h>

#include "Platform/iOS/iosCheatLaugh.h"

// (each made the first time it is picked, kept for the next; -1: can't be read)
static AVAudioPlayer* iosCheatLaugh_aPlayer[IOSCHEATLAUGH_NUM];
static int iosCheatLaugh_aBad[IOSCHEATLAUGH_NUM];
static int iosCheatLaugh_last = -1;

static AVAudioPlayer* iosCheatLaugh_Player(int i)
{
    static const char* aName[IOSCHEATLAUGH_NUM] = IOSCHEATLAUGH_NAMES;
    if (iosCheatLaugh_aPlayer[i] || iosCheatLaugh_aBad[i]) return iosCheatLaugh_aPlayer[i];
    NSURL* url = [[NSBundle mainBundle] URLForResource:[NSString stringWithUTF8String:aName[i]] withExtension:@IOSCHEATLAUGH_EXT];
    AVAudioPlayer* p = url ? [[AVAudioPlayer alloc] initWithContentsOfURL:url error:NULL] : nil;
    if (!p) { iosCheatLaugh_aBad[i] = 1; return nil; }
    p.numberOfLoops = 0;
    [p prepareToPlay];
    iosCheatLaugh_aPlayer[i] = p;
    return p;
}

int iosCheatLaugh_Play(float volume)
{
    for (int i = 0; i < IOSCHEATLAUGH_NUM; i++) {
        if (iosCheatLaugh_aPlayer[i] && iosCheatLaugh_aPlayer[i].playing)
            return 0; // (no second over the first)
    }
    const float v = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (v <= 0.0f)
        return 0; // (sound effects off)
    // one at random among those that can be read, not the last one (unless it is the only one)
    int aPick[IOSCHEATLAUGH_NUM], n = 0;
    for (int pass = 0; pass < 2 && !n; pass++) {
        for (int i = 0; i < IOSCHEATLAUGH_NUM; i++) {
            if ((pass == 0 && i == iosCheatLaugh_last) || !iosCheatLaugh_Player(i)) continue;
            aPick[n++] = i;
        }
    }
    if (!n)
        return 0;
    const int i = aPick[arc4random_uniform((uint32_t)n)];
    AVAudioPlayer* p = iosCheatLaugh_aPlayer[i];
    p.volume = v;
    p.currentTime = 0;
    if (![p play])
        return 0;
    iosCheatLaugh_last = i;
    return 1;
}

#endif // TARGET_IOS

// Added: entire file. See iosCheatLaugh.h.

#ifdef TARGET_IOS

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "Platform/iOS/iosCheatLaugh.h"

// (made once, kept for the next time)
static AVAudioPlayer* iosCheatLaugh_player = nil;

int iosCheatLaugh_Play(float volume)
{
    if (iosCheatLaugh_player && iosCheatLaugh_player.playing)
        return 0; // (no second copy over the first)
    if (!iosCheatLaugh_player) {
        NSURL* url = [[NSBundle mainBundle] URLForResource:@IOSCHEATLAUGH_NAME withExtension:@IOSCHEATLAUGH_EXT];
        if (!url)
            return 0;
        iosCheatLaugh_player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:NULL];
        if (!iosCheatLaugh_player)
            return 0;
        iosCheatLaugh_player.numberOfLoops = 0;
        [iosCheatLaugh_player prepareToPlay];
    }
    iosCheatLaugh_player.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (iosCheatLaugh_player.volume <= 0.0f)
        return 0; // (sound effects off)
    iosCheatLaugh_player.currentTime = 0;
    return [iosCheatLaugh_player play] ? 1 : 0;
}

#endif // TARGET_IOS

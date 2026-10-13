// Added: entire file. See iosCheatLaugh.h.

#ifdef TARGET_IOS

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "Platform/iOS/iosCheatLaugh.h"

// (kept for the next time, while its file stays the same)
static AVAudioPlayer* iosCheatLaugh_player = nil;

// laughing.mp3 (any capitals) in a folder, nil if it isn't there
static NSString* iosCheatLaugh_FindIn(NSString* dir)
{
    if (!dir.length) return nil;
    NSArray<NSString*>* aName = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:NULL];
    for (NSString* name in aName) {
        if ([name caseInsensitiveCompare:@IOSCHEATLAUGH_FILE] == NSOrderedSame)
            return [dir stringByAppendingPathComponent:name];
    }
    return nil;
}

// The game's folder (where the game runs from: jk1 or mots, or wherever its
// files were found), then the top of the app's folder in Files
static NSString* iosCheatLaugh_Find(void)
{
    NSString* path = iosCheatLaugh_FindIn([[NSFileManager defaultManager] currentDirectoryPath]);
    if (!path)
        path = iosCheatLaugh_FindIn(NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject);
    return path;
}

int iosCheatLaugh_Play(float volume)
{
    if (iosCheatLaugh_player && iosCheatLaugh_player.playing)
        return 0; // (no second copy over the first)
    NSString* path = iosCheatLaugh_Find();
    if (!path)
        return 0;
    NSURL* url = [NSURL fileURLWithPath:path];
    if (!iosCheatLaugh_player || ![iosCheatLaugh_player.url isEqual:url]) {
        [iosCheatLaugh_player release];
        iosCheatLaugh_player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:NULL];
        if (!iosCheatLaugh_player)
            return 0;
        iosCheatLaugh_player.numberOfLoops = 0;
    }
    iosCheatLaugh_player.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (iosCheatLaugh_player.volume <= 0.0f)
        return 0; // (sound effects off)
    iosCheatLaugh_player.currentTime = 0;
    return [iosCheatLaugh_player play] ? 1 : 0;
}

#endif // TARGET_IOS

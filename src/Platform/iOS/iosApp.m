// Added: entire file. See iosApp.h.
// Written to work with or without ARC, like iosTouch.m.

#ifdef TARGET_IOS

#import <Foundation/Foundation.h>

// No engine headers here: types.h typedefs BOOL as int, which collides with
// Objective-C's BOOL.
#include "Platform/iOS/iosApp.h"

int iosApp_IsMots(void)
{
    // A bundle's Info.plist can't change while the app runs, so read it once.
    static int bIsMots = -1;

    if (bIsMots < 0) {
        @autoreleasepool {
            id game = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"OpenJKDF2Game"];
            bIsMots = ([game isKindOfClass:[NSString class]]
                       && [(NSString*)game caseInsensitiveCompare:@"mots"] == NSOrderedSame) ? 1 : 0;
        }
    }
    return bIsMots;
}

#endif // TARGET_IOS

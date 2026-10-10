// Added: entire file. Which game this app bundle runs.
//
// The Mysteries of the Sith app is the Jedi Knight app's binary in a second
// bundle (packaging/ios/make_mots_app.sh) with its own name, icon and bundle
// ID, so iOS gives it its own sandbox: its own Documents folder in the Files
// app, with the MotS data in Documents/mots. Its Info.plist also has
// OpenJKDF2Game = mots, and main() then starts the engine the way -motsCompat
// does on a desktop. The Jedi Knight app's Info.plist has no such key.

#ifndef _PLATFORM_IOS_IOSAPP_H
#define _PLATFORM_IOS_IOSAPP_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// 1 in the Mysteries of the Sith app, 0 in the Jedi Knight app.
int iosApp_IsMots(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSAPP_H

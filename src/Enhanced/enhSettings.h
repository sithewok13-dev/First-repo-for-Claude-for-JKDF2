#ifndef _ENHANCED_ENHSETTINGS_H
#define _ENHANCED_ENHSETTINGS_H

// Added: entire file. Where the enhanced gameplay's choices are kept: the
// saber style and each blade's colour, Force Jump's style, first or third
// person.
//
// The gameplay itself always holds the current ones (iosSaber_GetStyle,
// iosSaber_GetColour, iosGame_IsThirdPerson...), which shared code reads.
// They are remembered:
//   - on iOS by the touch overlay, in NSUserDefaults, as since the first
//     builds (iosTouch.m: read at its start, written when picked); nothing
//     here runs there, so no setting moves and none is lost;
//   - elsewhere as global cvars, kept in openjkdf2_cvars.json and settable
//     from the console:
//       enh_saberStyle            SINGLE or DUAL
//       enh_saberLook             REFORGED (the default) or CLASSIC: how the
//                                 blades' colours are drawn (iosSaber)
//       enh_saberColourSingle     a colour key (iosSaber: "red", "lightblue"...),
//       enh_saberColourDualRight  "game" for the game's own
//       enh_saberColourDualLeft
//       enh_forceJumpStyle        0 smooth, 1 steps, 2 classic (IOSGAME_JUMP_*;
//                                 classic by default, the desktops' own jump)
//       enh_thirdPerson           0 or 1
//     A cvar changed (the console, the file) is applied; a choice changed in
//     the game (the camera key...) is written back, to be saved with the rest.

#if defined(JK_ENHANCED) && !defined(TARGET_IOS)

#ifdef __cplusplus
extern "C" {
#endif

// sithCvar_Startup: the cvars, before the saved ones are read
void enhSettings_StartupCvars(void);

// Once a frame (enh_Tick): cvars and gameplay kept the same (see above)
void enhSettings_Sync(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED && !TARGET_IOS

#endif // _ENHANCED_ENHSETTINGS_H

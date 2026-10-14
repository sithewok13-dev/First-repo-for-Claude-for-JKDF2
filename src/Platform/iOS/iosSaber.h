// Added: entire file. The player's lightsaber as they choose it: its style
// (one blade; later two, a crossguard...) and each blade's colour, picked on
// the touch overlay's SABER wheel.
//
// Only how it looks changes, for the local player (and, in multiplayer, for
// each other player as their game sends it: jkPlayerLook): the game's own
// saber (its materials, length, damage, saves, what the game itself sends
// other players) is left as the game set it. Each blade's colour is put on
// just while it is drawn (iosSaber_BeginBlade/EndBlade, round the blade's
// draws in jkPlayer.c and jkSaber.c), so it shows wherever the blade is drawn:
// first and third person, selfies, Action Freeze.
//
// The colours: the game's own (no change), the seven the game has materials
// for (saber<colour>0.mat, the tip; saber<colour>1.mat, the side -- in Jedi
// Knight and Mysteries of the Sith alike), and more made from the game's blue
// blade: its pattern of light kept texel for texel (the white middle, the
// colour out to the edges), only the colour changed, and drawn from a
// true-colour copy of its texture (std3D_AddToTextureCache's pTrueColour).
// Made once per level, for the colours in use (2 small textures each).

#ifndef _PLATFORM_IOS_IOSSABER_H
#define _PLATFORM_IOS_IOSSABER_H

#ifdef TARGET_IOS

#ifdef __cplusplus
extern "C" {
#endif

// ---- colours. Ids are only the order of the list (the wheel's, round from
// the gap): keep a choice by its key, which stays the same.
#define IOSSABER_COLOUR_GAME 0 // the game's own: nothing changed
int iosSaber_NumColours(void);
const char* iosSaber_ColourName(int id);    // as the wheel shows it ("LIGHT BLUE"), NULL for no such id
const char* iosSaber_ColourKey(int id);     // as it is kept ("lightblue")
int iosSaber_ColourByKey(const char* key);  // -1 if none
// The colour to show it in (a swatch on the wheel): its blade's edge colour
// (the game's own: the game's colour the player's blade has now, if it is one
// of them; else grey)
void iosSaber_ColourRGB(int id, unsigned char* pR, unsigned char* pG, unsigned char* pB);

// ---- styles: how many blades, and where. Only the ones that work can be
// picked (iosSaber_StyleAvailable); each keeps its own blade colours, so
// going from one style to another and back finds them as they were.
enum { IOSSABER_STYLE_SINGLE = 0, IOSSABER_STYLE_DUAL, IOSSABER_NUM_STYLES };
#define IOSSABER_MAX_BLADES 2
int iosSaber_StyleAvailable(int style);
const char* iosSaber_StyleName(int style);
int iosSaber_StyleNumBlades(int style);
int iosSaber_GetStyle(void);
void iosSaber_SetStyle(int style);
int iosSaber_GetColour(int style, int blade);
void iosSaber_SetColour(int style, int blade, int id);

// ---- the engine's side.
// Round the drawing of one of a thing's blades (blade 0: the right hand's, 1:
// the left's): its colour put on, then the game's put back. Nothing changes
// for anyone but the local player (and, in multiplayer, the other players
// whose games sent their choice), or for the game's own colour.
struct SithThing;
struct rdThing;
void iosSaber_BeginBlade(struct SithThing* pThing, struct rdThing* pLine, int blade);
void iosSaber_EndBlade(struct rdThing* pLine);

// Whether this thing carries a player's second blade (the local player with a
// two-blade style, not in a selfie, both in hand; in multiplayer, another
// player whose game sent a two-blade style): a saber in the left hand too, drawn
// with its own hilt, and striking as the game's own two-saber mode does
// (jkSaber_IsDual: JKFLAG_DUALSABERS, which the game itself never sets)
int iosSaber_HasSecondBlade(struct SithThing* pThing);

// Second, the two-saber style's left arm (rdPuppet_BuildJointMatrices, each
// joint posed and not yet put together; each two-saber player's, up to four
// at once): the player's left arm takes the
// right arm's pose of IOSSABER_ECHO_MS before, mirrored, so the two sabers
// are held apart and swing one after the other, the second blade striking
// where it is drawn. (The game's own stance holds the hilt in both hands.)
// Only while the saber is on.
#define IOSSABER_ECHO_MS 200
void iosSaber_PoseArms(struct rdThing* pRdThing);

// The level going (sithWorld_FreeEntry): the colours made for it freed
// (they are made again, from the next level's palette, when next drawn)
void iosSaber_FreeWorld(void);

// A thing drawn (sithRender_RenderThing): a saber the player threw, in its
// colour (iosSaberThrow_ThrownColour) just while it is drawn, between these
void iosSaber_BeginThrown(struct SithThing* pThing);
void iosSaber_EndThrown(struct SithThing* pThing);

#ifdef __cplusplus
}
#endif

#endif // TARGET_IOS

#endif // _PLATFORM_IOS_IOSSABER_H

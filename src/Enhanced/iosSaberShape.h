// Added: entire file. The lightsaber blade's shape, a choice of its own (not
// tied to the colours' look, iosSaber's CLASSIC / REFORGED):
//
//   SPIKE  the game's own: a blade that narrows to a point (the template's
//          base and tip widths). Everything is drawn exactly as before.
//   BEAM   the film blade: a slim, even beam (the same width at the hilt and
//          the tip, narrowing only with distance), drawn a little longer than
//          the game's blade and closed by a rounded end of its own (not the
//          game's round tip face), with a soft outer glow in the blade's own
//          colour (its edge colour in the look; for the game's own blade, the
//          game's colour). The glow is a few extra faces round the blade with
//          a small glow texture made once per colour and level, which only
//          adds light: nothing behind it is ever darker.
//
// For the blades iosSaber draws (the local player's, and in multiplayer other
// players' whose games sent their choice), every colour (the game's own
// included: its colour never changes, only its shape), first and third
// person, two sabers, selfies, Action Freeze; and a glow along a thrown saber
// (Mysteries of the Sith). Only how it looks: the game's blade length (which
// strikes), the template's widths, collisions and damage are left as they are.
//
// Remembered by the platform by its name, as the look is (iOS: NSUserDefaults
// iosTouchSaberShape; elsewhere the cvar enh_saberShape, enhSettings).
//
// A style can have a blade shape of its own, whatever the choice above
// (iosSaber's CROSS, a form here):
//
//   CROSS  a blade with a crossguard: its edges jagged, moved about each frame
//          of the game's clock so it crackles (still while the game is), a
//          few short sparks off its sides, narrowing to a point; and two short
//          side blades (IOSSABERSHAPE_CROSS_GUARD_LEN of its length) straight
//          out from it at the hilt, either way across the hand, jagged and
//          pointed too; all with a glow in the blade's colour, stronger than
//          BEAM's. Drawn with the blade's own side texture and colour. The
//          side blades are only drawn: the game strikes with its blade alone.
//          A thrown saber's blade (a model) gets the glow and the side blades.

#ifndef _ENHANCED_IOSSABERSHAPE_H
#define _ENHANCED_IOSSABERSHAPE_H

#ifdef JK_ENHANCED

#ifdef __cplusplus
extern "C" {
#endif

enum { IOSSABERSHAPE_SPIKE = 0, IOSSABERSHAPE_BEAM, IOSSABERSHAPE_NUM };
#define IOSSABERSHAPE_DEFAULT IOSSABERSHAPE_SPIKE // (what an unset or unknown choice is: the game's own; BEAM is chosen)

int iosSaberShape_Get(void);
void iosSaberShape_Set(int shape);
void iosSaberShape_Toggle(void);
const char* iosSaberShape_Name(int shape);  // "SPIKE", "BEAM": as it is kept
int iosSaberShape_ByName(const char* name); // -1 if none

// ---- the engine's side (iosSaber, rdPolyline; no engine headers here, so the
// platforms' own code can take the choice above)
struct SithThing;
struct rdThing;
struct rdVector3;
struct rdMaterial;
// The blade's own form, its style's (iosSaber): the shape chosen above, or
// CROSS's whatever that is
enum { IOSSABERSHAPE_FORM_CHOSEN = 0, IOSSABERSHAPE_FORM_CROSS };
// Round one blade's drawing (iosSaber_BeginBlade/EndBlade): with BEAM, the
// blade made even and its glow put on (rgb: the blade's colour; pThing:
// whose); nothing with SPIKE; CROSS, its round tip face left out (it comes to
// a point) and the rest drawn as its own (iosSaberShape_DrawBlade). End puts
// the widths back.
void iosSaberShape_BeginBlade(struct SithThing* pThing, struct rdThing* pLine, const unsigned char rgb[3], int form);
void iosSaberShape_EndBlade(void);

// rdPolyline_Draw, once its ends are placed (their depths from the eye): a
// blade begun above made wider at the tip as far as it is further off, so it
// looks even on screen (not a point again in first person)
void iosSaberShape_FitTip(struct rdThing* pLine, float tipDepth, float baseDepth);

// sithRender_Draw: at its start, and once the scene is drawn (after the
// translucent surfaces): the glows kept from the passes that write depth,
// drawn now without writing it (see iosSaberShape.c)
void iosSaberShape_BeginScene(void);
void iosSaberShape_FlushGlows(void);

// rdPolyline_Draw, once the blade is laid out (camera space: the hilt's
// matrix, the blade's tip and base, and (cos, sin) across it on screen): a
// blade begun above drawn in its shape, BEAM's (its side in slices along it
// and its glow round it) or CROSS's; 1 if it was (the side not to be drawn
// again), 0 for any other
struct rdMatrix34;
int iosSaberShape_DrawBlade(struct rdThing* pLine, const struct rdMatrix34* pToCam, const struct rdVector3* pTip, const struct rdVector3* pBase,
                            float angCos, float angSin);

// A thrown saber just drawn (iosSaber_EndThrown; pBlade: its blade's material
// as drawn): with BEAM (form CHOSEN), a glow in that colour along its blade;
// with CROSS, CROSS's glow along it and its side blades at its hilt, drawn as
// a held blade's side (pSideFace, the thrower's saber's) with pSideMat (in its
// colour; either NULL: no side blades, only the glow)
struct rdFace;
void iosSaberShape_DrawThrownGlow(struct SithThing* pThing, const struct rdMaterial* pBlade, const unsigned char rgb[3], int form,
                                  const struct rdFace* pSideFace, struct rdMaterial* pSideMat);

// The level going: the glow textures freed (made again when next drawn)
void iosSaberShape_FreeWorld(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _ENHANCED_IOSSABERSHAPE_H

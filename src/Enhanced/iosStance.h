// Added: entire file. How Kyle holds two sabers at rest, in first person (the
// player's two-blade style, iosSaber): a few natural resting stances and short
// gestures, picked now and then at random (mostly the relaxed V), moved
// between on springs, each hand a little after the other and swaying on its
// own over the game's own sway. Only how the arms look in view: the swings,
// blocks and hits are the game's own (they come from the third-person model).
//
// Each frame the first-person draw (jkPlayer_DrawPov) asks for each hand's
// pose: a small turn of the arm round its hand, and a small shift, in the
// arm model's own space (the left arm's mirrored, so the same numbers make the
// mirror image). While a swing, a block, readying or putting the saber away
// plays, the pose fades out at once, and back in after.

#ifndef _PLATFORM_IOS_IOSSTANCE_H
#define _PLATFORM_IOS_IOSSTANCE_H

#ifdef JK_ENHANCED

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Once a frame, before the arms are drawn (two sabers on, first person):
// moves on by the game's clock (still while the game holds still). bBusy: a
// swing or the like plays.
void iosStance_Update(SithThing* pActor, int bBusy);

// A hand's pose (0: right, 1: left), as a matrix to put on its arm model
// (model space; pivot: where its hand is, in the same space). 0: none now.
int iosStance_GetHand(int hand, const rdVector3* pPivot, rdMatrix34* pOut);

// Whether any of the arm's animations other than its resting hold plays
// (the first-person puppet: a swing, block, readying, mount or dismount)
int iosStance_IsPovBusy(rdPuppet* pPuppet);

// The blades' slow, out-of-step shimmer: the width to draw blade (0, 1) at,
// as a fraction of its own (two sabers only; else 1)
flex_t iosStance_BladeWidth(int blade);

// Forget everything (a new level, a load)
void iosStance_Reset(void);

#ifdef __cplusplus
}
#endif

#endif // JK_ENHANCED

#endif // _PLATFORM_IOS_IOSSTANCE_H

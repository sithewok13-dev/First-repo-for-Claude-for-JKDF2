// Added: entire file. See iosTouch.h.
// Written to work with or without ARC (no weak refs; the overlay lives for
// the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "Platform/iOS/iosTouch.h"

extern SDL_Window* displayWindow;
extern int stdControl_bControlsActive;
extern int stdControl_bControllerEscapeKey;
extern int jkCutscene_isRendering;
extern int Window_lastXRel;
extern int Window_lastYRel;

// Look speed: game mouse units per point of finger travel.
#define IOSTOUCH_LOOK_SCALE_X 1.6f
#define IOSTOUCH_LOOK_SCALE_Y 1.3f
// Stick: radius in points, dead zone and "run" threshold as fractions of it.
// It appears wherever the left thumb lands in the left IOSTOUCH_STICK_ZONE of
// the screen, and only while that thumb is down.
#define IOSTOUCH_STICK_ZONE 0.42f
#define IOSTOUCH_STICK_RADIUS 60.0f
#define IOSTOUCH_STICK_DEADZONE 0.30f
#define IOSTOUCH_STICK_RUN 0.92f

#define IOSTOUCH_MAX_TOUCHES 10
#define IOSTOUCH_NUM_SCANCODES 512

enum {
    ROLE_NONE = 0,
    ROLE_STICK,
    ROLE_LOOK,
    ROLE_BUTTON,
};

// Keys are the game's default keyboard bindings (sithControl_RegisterKeyboardBindings).
typedef struct {
    const char* label;
    int scancode;      // -1 for the menu button (sends Escape)
    float radius;      // points
    int bLookWhileHeld; // dragging on this button also turns the view
    float x, y;        // centre, set in layout
} iosTouchButton;

// Layout (see layoutSubviews): the five action buttons sit in a tight arc
// around FIRE in the bottom-right corner, under the right thumb, and all of
// them pass drags through to looking -- a thumb that lands on one while aiming
// keeps aiming. Everything used occasionally is a row of small buttons along
// the top-left, so the right side of the screen above the arc is free to look.
static iosTouchButton iosTouch_aButtons[] = {
    { "FIRE",  SDL_SCANCODE_LCTRL,  42.0f, 1 },
    { "ALT",   SDL_SCANCODE_Z,      27.0f, 1 },
    { "DUCK",  SDL_SCANCODE_C,      25.0f, 1 },
    { "USE",   SDL_SCANCODE_SPACE,  25.0f, 1 },
    { "JUMP",  SDL_SCANCODE_X,      27.0f, 1 },
    { "MENU",  -1,                  20.0f, 0 },
    { "WPN",   SDL_SCANCODE_G,      20.0f, 0 },
    { "FORCE", SDL_SCANCODE_F,      20.0f, 0 },
    { "F+",    SDL_SCANCODE_E,      20.0f, 0 },
    { "ITEM",  SDL_SCANCODE_RETURN, 20.0f, 0 },
    { "INV+",  SDL_SCANCODE_R,      20.0f, 0 },
};
#define IOSTOUCH_FIRST_TOP_BUTTON 5
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))

typedef struct {
    UITouch* touch; // not retained; only compared
    int role;
    int button;
    CGPoint origin;
    CGPoint last;
} iosTouchSlot;

static iosTouchSlot iosTouch_aSlots[IOSTOUCH_MAX_TOUCHES];
static int iosTouch_aButtonHeld[IOSTOUCH_NUM_BUTTONS];
static unsigned char iosTouch_aKeyDown[IOSTOUCH_NUM_SCANCODES];
static float iosTouch_lookX = 0.0f, iosTouch_lookY = 0.0f;
static float iosTouch_stickX = 0.0f, iosTouch_stickY = 0.0f;
static int iosTouch_bStickActive = 0;

static void iosTouch_RecomputeKeys(void)
{
    memset(iosTouch_aKeyDown, 0, sizeof(iosTouch_aKeyDown));
    int bMenu = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (!iosTouch_aButtonHeld[i]) continue;
        if (iosTouch_aButtons[i].scancode < 0) bMenu = 1;
        else iosTouch_aKeyDown[iosTouch_aButtons[i].scancode] = 1;
    }
    stdControl_bControllerEscapeKey = bMenu;

    if (iosTouch_bStickActive) {
        float x = iosTouch_stickX, y = iosTouch_stickY;
        if (y < -IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_W] = 1;
        if (y >  IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_S] = 1;
        if (x < -IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_A] = 1;
        if (x >  IOSTOUCH_STICK_DEADZONE) iosTouch_aKeyDown[SDL_SCANCODE_D] = 1;
        if (sqrtf(x * x + y * y) >= IOSTOUCH_STICK_RUN) iosTouch_aKeyDown[SDL_SCANCODE_LSHIFT] = 1;
    }
}

// ---------------------------------------------------------------- overlay view

@interface IOSTouchOverlay : UIView {
    UIView* stickBase;
    UIView* stickKnob;
    UILabel* aButtonViews[IOSTOUCH_NUM_BUTTONS];
}
- (void)resetAll;
@end

@implementation IOSTouchOverlay

static UIView* IOSTouch_MakeCircle(CGFloat radius, CGFloat alpha)
{
    UIView* v = [[UIView alloc] initWithFrame:CGRectMake(0, 0, radius * 2, radius * 2)];
    v.backgroundColor = [UIColor colorWithWhite:1.0 alpha:alpha];
    v.layer.cornerRadius = radius;
    v.layer.borderWidth = 1.5;
    v.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.45].CGColor;
    v.userInteractionEnabled = NO;
    return v;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.multipleTouchEnabled = YES;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
            iosTouchButton* b = &iosTouch_aButtons[i];
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, b->radius * 2, b->radius * 2)];
            l.text = [NSString stringWithUTF8String:b->label];
            l.textAlignment = NSTextAlignmentCenter;
            l.textColor = [UIColor colorWithWhite:1.0 alpha:0.85];
            l.font = [UIFont boldSystemFontOfSize:(b->radius >= 40 ? 16 : (b->radius >= 25 ? 12 : 10))];
            l.adjustsFontSizeToFitWidth = YES;
            l.minimumScaleFactor = 0.6;
            l.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.22];
            l.layer.cornerRadius = b->radius;
            l.layer.borderWidth = 1.5;
            l.layer.borderColor = (i == 0) ? [UIColor colorWithRed:1.0 green:0.55 blue:0.45 alpha:0.6].CGColor
                                           : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
            l.clipsToBounds = YES;
            l.userInteractionEnabled = NO;
            aButtonViews[i] = l;
            [self addSubview:l];
        }

        stickBase = IOSTouch_MakeCircle(IOSTOUCH_STICK_RADIUS, 0.08);
        stickKnob = IOSTouch_MakeCircle(24, 0.30);
        stickBase.hidden = YES;
        stickKnob.hidden = YES;
        [self addSubview:stickBase];
        [self addSubview:stickKnob];
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGRect b = self.bounds;
    UIEdgeInsets in = UIEdgeInsetsZero;
    if (@available(iOS 11.0, *)) {
        in = self.safeAreaInsets;
    }
    CGFloat left = MAX(in.left, 8.0);
    CGFloat right = b.size.width - MAX(in.right, 8.0);
    CGFloat top = MAX(in.top, 8.0);
    CGFloat bottom = b.size.height - MAX(in.bottom, 8.0);

    // FIRE in the corner, the other four on an arc around it (angles measured
    // counter-clockwise from pointing right, so 90 is straight up). Spacing is
    // picked so neighbours on the arc never touch.
    CGPoint fire = CGPointMake(right - 62, bottom - 58);
    const CGFloat arc = 112.0;
    const CGFloat aArcDeg[4] = { 182.0f, 150.0f, 120.0f, 90.0f }; // ALT, DUCK, USE, JUMP
    iosTouch_aButtons[0].x = fire.x;
    iosTouch_aButtons[0].y = fire.y;
    for (int i = 0; i < 4; i++) {
        CGFloat rad = aArcDeg[i] * (CGFloat)M_PI / 180.0;
        iosTouch_aButtons[1 + i].x = fire.x + arc * cos(rad);
        iosTouch_aButtons[1 + i].y = fire.y - arc * sin(rad);
    }

    // Top-left row: MENU, a gap, then weapon / force / inventory.
    for (int i = IOSTOUCH_FIRST_TOP_BUTTON; i < IOSTOUCH_NUM_BUTTONS; i++) {
        int n = i - IOSTOUCH_FIRST_TOP_BUTTON;
        iosTouch_aButtons[i].x = left + 26 + (n ? 16 : 0) + n * 46;
        iosTouch_aButtons[i].y = top + 26;
    }

    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        aButtonViews[i].center = CGPointMake(iosTouch_aButtons[i].x, iosTouch_aButtons[i].y);
    }
}

// Closest button the touch is on (with a little forgiveness), so a touch in the
// gap between two buttons picks the nearer one rather than the first listed.
- (int)buttonAt:(CGPoint)p
{
    int best = -1;
    CGFloat bestDist = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        iosTouchButton* b = &iosTouch_aButtons[i];
        CGFloat dx = p.x - b->x, dy = p.y - b->y;
        CGFloat d = sqrt(dx * dx + dy * dy);
        if (d <= b->radius + 6.0 && (best < 0 || d - b->radius < bestDist)) {
            best = i;
            bestDist = d - b->radius;
        }
    }
    return best;
}

- (void)hideStick
{
    stickBase.hidden = YES;
    stickKnob.hidden = YES;
}

- (void)refreshButtonLooks
{
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        aButtonViews[i].backgroundColor = [UIColor colorWithWhite:(iosTouch_aButtonHeld[i] ? 1.0 : 0.0)
                                                            alpha:(iosTouch_aButtonHeld[i] ? 0.30 : 0.22)];
    }
}

- (void)updateStickVisual:(iosTouchSlot*)s
{
    stickBase.center = s->origin;
    stickKnob.center = CGPointMake(s->origin.x + iosTouch_stickX * IOSTOUCH_STICK_RADIUS,
                                   s->origin.y + iosTouch_stickY * IOSTOUCH_STICK_RADIUS);
    stickBase.hidden = NO;
    stickKnob.hidden = NO;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches) {
        int slot = -1;
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            if (iosTouch_aSlots[i].touch == nil) { slot = i; break; }
        }
        if (slot < 0) continue;

        CGPoint p = [t locationInView:self];
        iosTouchSlot* s = &iosTouch_aSlots[slot];
        s->touch = t;
        s->origin = p;
        s->last = p;
        s->button = [self buttonAt:p];

        if (s->button >= 0) {
            s->role = ROLE_BUTTON;
            iosTouch_aButtonHeld[s->button]++;
        }
        else if (p.x < self.bounds.size.width * IOSTOUCH_STICK_ZONE && !iosTouch_bStickActive) {
            s->role = ROLE_STICK;
            iosTouch_bStickActive = 1;
            iosTouch_stickX = iosTouch_stickY = 0.0f;
            [self updateStickVisual:s];
        }
        else {
            s->role = ROLE_LOOK;
        }
    }
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];

            if (s->role == ROLE_STICK) {
                float dx = (p.x - s->origin.x) / IOSTOUCH_STICK_RADIUS;
                float dy = (p.y - s->origin.y) / IOSTOUCH_STICK_RADIUS;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1.0f) { dx /= len; dy /= len; }
                iosTouch_stickX = dx;
                iosTouch_stickY = dy;
                [self updateStickVisual:s];
            }
            else if (s->role == ROLE_LOOK ||
                     (s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].bLookWhileHeld)) {
                iosTouch_lookX += (float)(p.x - s->last.x) * IOSTOUCH_LOOK_SCALE_X;
                iosTouch_lookY += (float)(p.y - s->last.y) * IOSTOUCH_LOOK_SCALE_Y;
            }
            s->last = p;
        }
    }
    iosTouch_RecomputeKeys();
}

- (void)endTouches:(NSSet<UITouch*>*)touches
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            if (s->role == ROLE_BUTTON && iosTouch_aButtonHeld[s->button] > 0) {
                iosTouch_aButtonHeld[s->button]--;
            }
            if (s->role == ROLE_STICK) {
                iosTouch_bStickActive = 0;
                iosTouch_stickX = iosTouch_stickY = 0.0f;
                [self hideStick];
            }
            memset(s, 0, sizeof(*s));
        }
    }
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches]; }

- (void)resetAll
{
    memset(iosTouch_aSlots, 0, sizeof(iosTouch_aSlots));
    memset(iosTouch_aButtonHeld, 0, sizeof(iosTouch_aButtonHeld));
    iosTouch_bStickActive = 0;
    iosTouch_stickX = iosTouch_stickY = 0.0f;
    iosTouch_lookX = iosTouch_lookY = 0.0f;
    [self hideStick];
    iosTouch_RecomputeKeys();
    stdControl_bControllerEscapeKey = 0;
    [self refreshButtonLooks];
}

@end

// ---------------------------------------------------------------- C API

static IOSTouchOverlay* iosTouch_pOverlay = nil;

static UIView* iosTouch_GetHostView(void)
{
    if (!displayWindow) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(displayWindow);
    UIWindow* w = (__bridge UIWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    if (!w) return nil;
    return w.rootViewController.view ? w.rootViewController.view : w;
}

void iosTouch_Update(void)
{
    int bWant = stdControl_bControlsActive && !jkCutscene_isRendering;

    if (!iosTouch_pOverlay) {
        if (!bWant) return;
        UIView* host = iosTouch_GetHostView();
        if (!host) return;
        iosTouch_pOverlay = [[IOSTouchOverlay alloc] initWithFrame:host.bounds];
        [host addSubview:iosTouch_pOverlay];
    }
    else {
        // If the SDL window was ever recreated, the overlay is still sitting in
        // the old, no longer shown one: move it across.
        UIView* host = iosTouch_GetHostView();
        if (host && iosTouch_pOverlay.superview != host) {
            [iosTouch_pOverlay removeFromSuperview];
            iosTouch_pOverlay.frame = host.bounds;
            [host addSubview:iosTouch_pOverlay];
            [iosTouch_pOverlay resetAll];
        }
    }

    // keep it on top of anything SDL adds later
    if (iosTouch_pOverlay.superview && iosTouch_pOverlay.superview.subviews.lastObject != iosTouch_pOverlay) {
        [iosTouch_pOverlay.superview bringSubviewToFront:iosTouch_pOverlay];
    }

    if (bWant == iosTouch_pOverlay.hidden) {
        iosTouch_pOverlay.hidden = !bWant;
        [iosTouch_pOverlay resetAll]; // touches on a hidden view never end; start clean
    }

    if (bWant) {
        int dx = (int)iosTouch_lookX;
        int dy = (int)iosTouch_lookY;
        iosTouch_lookX -= (float)dx;
        iosTouch_lookY -= (float)dy;
        Window_lastXRel += dx;
        Window_lastYRel += dy;
    }
}

int iosTouch_IsScancodeDown(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return 0;
    if (!iosTouch_pOverlay || iosTouch_pOverlay.hidden) return 0;
    return iosTouch_aKeyDown[scancode];
}

#endif // TARGET_IOS

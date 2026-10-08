// Added: entire file. See iosTouch.h.
// Written to work with or without ARC (no weak refs; the overlay lives for
// the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "Platform/iOS/iosTouch.h"
#include "Platform/iOS/iosGame.h"

extern SDL_Window* displayWindow;
extern int stdControl_bControlsActive;
extern int stdControl_bControllerEscapeKey;
extern int jkCutscene_isRendering;
extern int jkGuiRend_IsMenuActive(void);
extern int jkHud_IosGetRightGaugeRectPt(float* pX0, float* pY0, float* pX1, float* pY1);
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

// Edge-to-edge gap between the buttons around FIRE (the arc, ALT and FORCE)
#define IOSTOUCH_CLUSTER_GAP 30.0f
// FORCE: a touch that has stayed put this long starts holding the power (so
// ones like Lightning keep going); sliding sideways picks the previous / next
// power instead, one step per IOSTOUCH_FORCE_STEP points. Moving less than
// IOSTOUCH_FORCE_STILL points counts as staying put.
#define IOSTOUCH_FORCE_HOLD_DELAY 0.12
#define IOSTOUCH_FORCE_STEP 30.0f
#define IOSTOUCH_FORCE_STILL 4.0f
// QUICK LOAD has to be held this long, so a stray tap can't throw away progress
#define IOSTOUCH_QUICKLOAD_HOLD 1.0
// A one-off key press is held for this many control reads, then released for one
#define IOSTOUCH_PULSE_READS 2

#define IOSTOUCH_MAX_TOUCHES 10
#define IOSTOUCH_NUM_SCANCODES 512

enum {
    ROLE_NONE = 0,
    ROLE_STICK,
    ROLE_LOOK,
    ROLE_BUTTON,
};

enum {
    KIND_KEY = 0,  // holds its key while touched
    KIND_MENU,     // Escape (via stdControl_bControllerEscapeKey)
    KIND_FORCE,    // tap/hold uses the force power, slide sideways picks another
    KIND_TAPKEY,   // one press of its key when the touch lifts on the button
    KIND_HOLDLOAD, // hold IOSTOUCH_QUICKLOAD_HOLD seconds to quick load
};

enum {
    FORCE_IDLE = 0,
    FORCE_PENDING, // just touched: tap, hold or slide?
    FORCE_HOLD,    // holding the use key
    FORCE_SLIDE,   // picking a power; never uses one
};

// Keys are the game's default keyboard bindings (sithControl_RegisterKeyboardBindings).
typedef struct {
    const char* label;
    int kind;
    int scancode;       // -1 if the button isn't a key
    float radius;       // points
    int bLookWhileHeld; // dragging on this button also turns the view
    float x, y;         // centre, set in layout
} iosTouchButton;

// Layout (see layoutSubviews). Bottom right, under the right thumb: FIRE, an
// arc of DUCK / ACT / JUMP around it, ALT above the ammo gauge and FORCE just
// outside the arc -- all IOSTOUCH_CLUSTER_GAP apart. The firing and movement
// buttons pass drags through to looking, so a thumb that lands on one while
// aiming keeps aiming. Top left: weapon, force and inventory -- the force
// pair stays alongside FORCE for now: USE FORCE holds the key the moment it's
// touched (FORCE waits IOSTOUCH_FORCE_HOLD_DELAY to tell a hold from a slide),
// which gets the most out of powers charged by holding, like Force Jump. Top
// right: quick save, quick load (hold) and the menu. ACT is the door/switch key; NEXT ITEM only selects (the strip
// at the bottom shows which), USE ITEM uses it.
enum {
    BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_NEXTFORCE, BTN_USEFORCE, BTN_NEXTITEM, BTN_USEITEM,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_COUNT
};
static iosTouchButton iosTouch_aButtons[] = {
    [BTN_FIRE]      = { "FIRE",        KIND_KEY,      SDL_SCANCODE_LCTRL,  42.0f, 1 },
    [BTN_ALT]       = { "ALT",         KIND_KEY,      SDL_SCANCODE_Z,      28.0f, 1 },
    [BTN_DUCK]      = { "DUCK",        KIND_KEY,      SDL_SCANCODE_C,      29.0f, 1 },
    [BTN_ACT]       = { "ACT",         KIND_KEY,      SDL_SCANCODE_SPACE,  29.0f, 1 },
    [BTN_JUMP]      = { "JUMP",        KIND_KEY,      SDL_SCANCODE_X,      31.0f, 1 },
    [BTN_FORCE]     = { "FORCE",       KIND_FORCE,    SDL_SCANCODE_F,      30.0f, 0 },
    [BTN_NEXTWPN]   = { "NEXT\nWPN",   KIND_KEY,      SDL_SCANCODE_G,      22.0f, 0 },
    [BTN_NEXTFORCE] = { "NEXT\nFORCE", KIND_KEY,      SDL_SCANCODE_E,      22.0f, 0 },
    [BTN_USEFORCE]  = { "USE\nFORCE",  KIND_KEY,      SDL_SCANCODE_F,      22.0f, 0 },
    [BTN_NEXTITEM]  = { "NEXT\nITEM",  KIND_KEY,      SDL_SCANCODE_R,      22.0f, 0 },
    [BTN_USEITEM]   = { "USE\nITEM",   KIND_KEY,      SDL_SCANCODE_RETURN, 22.0f, 0 },
    [BTN_QUICKSAVE] = { "QUICK\nSAVE", KIND_TAPKEY,   SDL_SCANCODE_F9,     22.0f, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD", KIND_HOLDLOAD, -1,                  22.0f, 0 },
    [BTN_MENU]      = { "MENU",        KIND_MENU,     -1,                  22.0f, 0 },
};
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))
typedef char iosTouch_assertButtonCount[(IOSTOUCH_NUM_BUTTONS == BTN_COUNT) ? 1 : -1];

// Previous / next force power keys (INPUT_FUNC_PREVSKILL / NEXTSKILL)
#define IOSTOUCH_SCANCODE_PREVPOWER SDL_SCANCODE_Q
#define IOSTOUCH_SCANCODE_NEXTPOWER SDL_SCANCODE_E

typedef struct {
    UITouch* touch; // not retained; only compared
    int role;
    int button;
    CGPoint origin;
    CGPoint last;
    int forceState;       // FORCE_*, for a touch on FORCE
    CFTimeInterval tDown; // when the touch began
    CFTimeInterval tMove; // FORCE: when the finger last moved more than IOSTOUCH_FORCE_STILL
    CGPoint movePoint;    // FORCE: where it was then
    int bHoldSeen;        // FORCE: the game has read the held key at least once
    int bFired;           // QUICK LOAD: already loaded for this touch
} iosTouchSlot;

static iosTouchSlot iosTouch_aSlots[IOSTOUCH_MAX_TOUCHES];
static int iosTouch_aButtonHeld[IOSTOUCH_NUM_BUTTONS];
static unsigned char iosTouch_aKeyDown[IOSTOUCH_NUM_SCANCODES];
// One-off presses waiting to be read, and the one being read right now
static unsigned char iosTouch_aPulseQueue[IOSTOUCH_NUM_SCANCODES];
static unsigned char iosTouch_aPulseReads[IOSTOUCH_NUM_SCANCODES];
static unsigned char iosTouch_aPulseGap[IOSTOUCH_NUM_SCANCODES];
static float iosTouch_lookX = 0.0f, iosTouch_lookY = 0.0f;
static float iosTouch_stickX = 0.0f, iosTouch_stickY = 0.0f;
static int iosTouch_bStickActive = 0;
// The HUD gauge rectangle the current layout was made for
static int iosTouch_bLayoutGauge = 0;
static float iosTouch_aLayoutGauge[4];

static void iosTouch_QueuePress(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return;
    if (iosTouch_aPulseQueue[scancode] < 255) iosTouch_aPulseQueue[scancode]++;
}

static void iosTouch_RecomputeKeys(void)
{
    memset(iosTouch_aKeyDown, 0, sizeof(iosTouch_aKeyDown));
    int bMenu = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (!iosTouch_aButtonHeld[i]) continue;
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (b->kind == KIND_MENU) bMenu = 1;
        else if (b->kind == KIND_KEY && b->scancode >= 0) iosTouch_aKeyDown[b->scancode] = 1;
    }
    // FORCE holds its key only once a touch on it has turned into a hold
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (s->touch && s->role == ROLE_BUTTON && s->button == BTN_FORCE && s->forceState == FORCE_HOLD) {
            iosTouch_aKeyDown[iosTouch_aButtons[BTN_FORCE].scancode] = 1;
        }
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

// Distance from p to the segment (ax,ay)-(bx,by)
static CGFloat iosTouch_DistToSegment(CGPoint p, CGFloat ax, CGFloat ay, CGFloat bx, CGFloat by)
{
    CGFloat vx = bx - ax, vy = by - ay;
    CGFloat len2 = vx * vx + vy * vy;
    CGFloat t = len2 > 0 ? ((p.x - ax) * vx + (p.y - ay) * vy) / len2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    CGFloat dx = p.x - (ax + t * vx), dy = p.y - (ay + t * vy);
    return sqrt(dx * dx + dy * dy);
}

// ---------------------------------------------------------------- overlay view

@interface IOSTouchOverlay : UIView {
    UIView* stickBase;
    UIView* stickKnob;
    UILabel* aButtonViews[IOSTOUCH_NUM_BUTTONS];
    CAShapeLayer* loadRing; // QUICK LOAD's hold progress
    const char* forceLabelName;
    int bForceLabelSet;
}
- (void)resetAll;
- (void)tick;
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

static CGFloat IOSTouch_FontSize(const char* label, CGFloat radius)
{
    if (strchr(label, '\n')) return radius >= 28 ? 10 : 9; // two-line labels
    return radius >= 40 ? 16 : (radius >= 25 ? 13 : 10);
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
            l.font = [UIFont boldSystemFontOfSize:IOSTouch_FontSize(b->label, b->radius)];
            l.numberOfLines = 2;
            l.adjustsFontSizeToFitWidth = YES;
            l.minimumScaleFactor = 0.6;
            l.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.22];
            l.layer.cornerRadius = b->radius;
            l.layer.borderWidth = 1.5;
            l.layer.borderColor = (i == BTN_FIRE) ? [UIColor colorWithRed:1.0 green:0.55 blue:0.45 alpha:0.6].CGColor
                                                  : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
            l.clipsToBounds = YES;
            l.userInteractionEnabled = NO;
            aButtonViews[i] = l;
            [self addSubview:l];
        }

        // Ring that fills while QUICK LOAD is held
        CGFloat r = iosTouch_aButtons[BTN_QUICKLOAD].radius;
        loadRing = [CAShapeLayer layer];
        loadRing.frame = CGRectMake(0, 0, r * 2, r * 2);
        loadRing.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(r, r) radius:r - 2.5
                                                   startAngle:-M_PI_2 endAngle:3 * M_PI_2 clockwise:YES].CGPath;
        loadRing.fillColor = [UIColor clearColor].CGColor;
        loadRing.strokeColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor;
        loadRing.lineWidth = 3.0;
        loadRing.strokeEnd = 0.0;
        [aButtonViews[BTN_QUICKLOAD].layer addSublayer:loadRing];

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
    CGFloat W = b.size.width, H = b.size.height;
    UIEdgeInsets in = UIEdgeInsetsZero;
    if (@available(iOS 11.0, *)) {
        in = self.safeAreaInsets;
    }
    CGFloat left = MAX(in.left, 8.0);
    CGFloat right = W - MAX(in.right, 8.0);
    CGFloat top = MAX(in.top, 8.0);
    CGFloat bottom = H - MAX(in.bottom, 8.0);

    // Where the HUD's right (ammo/force) gauge is; iosTouch_Update re-runs
    // this layout whenever it moves
    float g[4] = {0, 0, 0, 0};
    iosTouch_bLayoutGauge = jkHud_IosGetRightGaugeRectPt(&g[0], &g[1], &g[2], &g[3]);
    memcpy(iosTouch_aLayoutGauge, g, sizeof(g));

    // FIRE in the corner, left of the gauge. right - 76 clears it on notched
    // iPhones; where it is wider in points (no side insets: Home-button
    // iPhones, iPads) FIRE moves left of it.
    const CGFloat FR = iosTouch_aButtons[BTN_FIRE].radius;
    CGFloat fireX = right - 76;
    if (iosTouch_bLayoutGauge && fireX + FR + 4 > g[0]) {
        fireX = g[0] - FR - 4;
    }
    CGPoint fire = CGPointMake(fireX, bottom - 58);
    iosTouch_aButtons[BTN_FIRE].x = fire.x;
    iosTouch_aButtons[BTN_FIRE].y = fire.y;

    // DUCK, ACT and JUMP on an arc around FIRE (angles counter-clockwise from
    // pointing right, so 90 is straight up), 45 degrees apart -- which at this
    // radius leaves about IOSTOUCH_CLUSTER_GAP between them
    const CGFloat arc = 115.0;
    const int aArcBtn[3] = { BTN_DUCK, BTN_ACT, BTN_JUMP };
    const CGFloat aArcDeg[3] = { 180.0f, 135.0f, 90.0f };
    for (int i = 0; i < 3; i++) {
        CGFloat rad = aArcDeg[i] * (CGFloat)M_PI / 180.0;
        iosTouch_aButtons[aArcBtn[i]].x = fire.x + arc * cos(rad);
        iosTouch_aButtons[aArcBtn[i]].y = fire.y - arc * sin(rad);
    }

    // ALT: up and to the right of FIRE, IOSTOUCH_CLUSTER_GAP from it, as low
    // as it can sit while staying above the ammo gauge, on screen, clear of
    // JUMP and clear of the camera cutout when that is on the right (the
    // landscape side insets are the same both ways, so assume it can be).
    {
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        const CGFloat AR = a->radius;
        const CGFloat D = FR + AR + IOSTOUCH_CLUSTER_GAP;
        // Cutout as a capsule along the right edge: the Dynamic Island (side
        // inset ~59-62pt) is ~126x37pt, 11pt in from the edge; a notch (side
        // inset ~44-50pt) is up to ~210x33pt at the edge.
        int bCutout = in.right >= 40.0;
        CGFloat cutX = (in.right >= 55.0) ? W - 29.5 : W - 16.5;
        CGFloat cutR = (in.right >= 55.0) ? 18.5 : 16.5;
        CGFloat cutHalf = ((in.right >= 55.0) ? 63.0 : 105.0) - cutR;
        // If nothing at that distance fits (a short screen with the cutout
        // between the gauge and JUMP, or an iPad's tall gauge), step outwards;
        // failing that, allow ALT closer to JUMP.
        CGPoint best = CGPointMake(fire.x + D * cos(M_PI * 35.0 / 180.0), fire.y - D * sin(M_PI * 35.0 / 180.0));
        int bFound = 0;
        for (int pass = 0; pass < 2 && !bFound; pass++) {
            const CGFloat jumpGap = pass ? 2.0 : 8.0;
            for (int extra = 0; extra <= 80 && !bFound; extra += 2) {
                for (int deg = 10; deg <= 85; deg++) {
                    CGFloat rad = deg * (CGFloat)M_PI / 180.0;
                    CGPoint p = CGPointMake(fire.x + (D + extra) * cos(rad), fire.y - (D + extra) * sin(rad));
                    if (p.x + AR > W - 4 || p.y - AR < top + 60) continue;                    // on screen, below the top row
                    if (iosTouch_bLayoutGauge && p.y + AR > g[1] - 4) continue;               // above the gauge
                    CGFloat dj = sqrt((p.x - j->x) * (p.x - j->x) + (p.y - j->y) * (p.y - j->y));
                    if (dj < AR + j->radius + jumpGap) continue;                               // clear of JUMP
                    if (bCutout && iosTouch_DistToSegment(p, cutX, H * 0.5 - cutHalf, cutX, H * 0.5 + cutHalf) < AR + cutR + 2)
                        continue;                                                              // clear of the cutout
                    best = p;
                    bFound = 1;
                    break;
                }
            }
        }
        // Last resort (e.g. a very large HUD scale on an iPad): never on the
        // gauge's numbers -- lift it above the gauge
        if (!bFound && iosTouch_bLayoutGauge && best.y + AR > g[1] - 4) {
            best.y = MAX(g[1] - 4 - AR, top + 60 + AR);
        }
        a->x = best.x;
        a->y = best.y;
    }

    // FORCE: just outside the arc, IOSTOUCH_CLUSTER_GAP from both DUCK and ACT
    {
        iosTouchButton* d = &iosTouch_aButtons[BTN_DUCK];
        iosTouchButton* c = &iosTouch_aButtons[BTN_ACT];
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        CGFloat dist = d->radius + f->radius + IOSTOUCH_CLUSTER_GAP; // DUCK and ACT are the same size
        CGFloat mx = (d->x + c->x) * 0.5, my = (d->y + c->y) * 0.5;
        CGFloat vx = c->x - d->x, vy = c->y - d->y;
        CGFloat L = sqrt(vx * vx + vy * vy);
        CGFloat h = (dist > L * 0.5) ? sqrt(dist * dist - L * L * 0.25) : 0;
        // the perpendicular pointing away from FIRE
        CGFloat px = vy / L, py = -vx / L;
        if ((mx - fire.x) * px + (my - fire.y) * py < 0) { px = -px; py = -py; }
        f->x = mx + px * h;
        f->y = my + py * h;
    }

    // Top left: NEXT WPN | NEXT FORCE, USE FORCE | NEXT ITEM, USE ITEM
    iosTouch_aButtons[BTN_NEXTWPN].x = left + 24;
    iosTouch_aButtons[BTN_NEXTFORCE].x = left + 84;
    iosTouch_aButtons[BTN_USEFORCE].x = left + 132;
    iosTouch_aButtons[BTN_NEXTITEM].x = left + 192;
    iosTouch_aButtons[BTN_USEITEM].x = left + 240;
    // Top right: QUICK SAVE, QUICK LOAD, MENU in the corner -- spaced well
    // apart, so a press meant for QUICK LOAD can't land on QUICK SAVE
    iosTouch_aButtons[BTN_QUICKSAVE].x = right - 156;
    iosTouch_aButtons[BTN_QUICKLOAD].x = right - 90;
    iosTouch_aButtons[BTN_MENU].x = right - 24;
    const int aTopRow[] = { BTN_NEXTWPN, BTN_NEXTFORCE, BTN_USEFORCE, BTN_NEXTITEM, BTN_USEITEM,
                            BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU };
    for (int i = 0; i < (int)(sizeof(aTopRow) / sizeof(aTopRow[0])); i++) {
        iosTouch_aButtons[aTopRow[i]].y = top + 26;
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

- (void)setLoadProgress:(CGFloat)progress
{
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    loadRing.strokeEnd = progress;
    [CATransaction commit];
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
        memset(s, 0, sizeof(*s));
        s->touch = t;
        s->origin = p;
        s->last = p;
        s->tDown = CACurrentMediaTime();
        s->tMove = s->tDown;
        s->movePoint = p;
        s->button = [self buttonAt:p];

        if (s->button >= 0) {
            s->role = ROLE_BUTTON;
            iosTouch_aButtonHeld[s->button]++;
            iosTouchButton* b = &iosTouch_aButtons[s->button];
            if (b->kind == KIND_FORCE) s->forceState = FORCE_PENDING;
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
            else if (s->role == ROLE_BUTTON && s->button == BTN_FORCE) {
                CGFloat mx = p.x - s->movePoint.x, my = p.y - s->movePoint.y;
                if (mx * mx + my * my > IOSTOUCH_FORCE_STILL * IOSTOUCH_FORCE_STILL) {
                    s->tMove = CACurrentMediaTime();
                    s->movePoint = p;
                }
                // Sliding sideways before the hold kicks in picks the previous
                // / next power, one step per IOSTOUCH_FORCE_STEP points
                if (s->forceState == FORCE_PENDING || s->forceState == FORCE_SLIDE) {
                    CGFloat dx = p.x - s->origin.x;
                    while (fabs(dx) >= IOSTOUCH_FORCE_STEP) {
                        s->forceState = FORCE_SLIDE;
                        iosTouch_QueuePress(dx > 0 ? IOSTOUCH_SCANCODE_NEXTPOWER : IOSTOUCH_SCANCODE_PREVPOWER);
                        CGFloat step = dx > 0 ? IOSTOUCH_FORCE_STEP : -IOSTOUCH_FORCE_STEP;
                        s->origin.x += step;
                        dx -= step;
                    }
                }
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

// bCancelled: iOS took the touch away (a call, a system gesture...) -- release
// whatever it held, but don't treat it as a finished tap
- (void)endTouches:(NSSet<UITouch*>*)touches cancelled:(int)bCancelled
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            if (s->role == ROLE_BUTTON) {
                iosTouchButton* b = &iosTouch_aButtons[s->button];
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                // A tap on FORCE (no slide) uses the power: also when it had
                // just turned into a hold that the game never got to read
                if (!bCancelled && s->button == BTN_FORCE &&
                    (s->forceState == FORCE_PENDING || (s->forceState == FORCE_HOLD && !s->bHoldSeen))) {
                    iosTouch_QueuePress(b->scancode);
                }
                // QUICK SAVE saves when the finger lifts on it, so a slip onto
                // it on the way to QUICK LOAD can be dragged off again
                if (!bCancelled && b->kind == KIND_TAPKEY) {
                    CGPoint p = [t locationInView:self];
                    CGFloat dx = p.x - b->x, dy = p.y - b->y;
                    if (dx * dx + dy * dy <= (b->radius + 6.0) * (b->radius + 6.0)) iosTouch_QueuePress(b->scancode);
                }
                if (s->button == BTN_QUICKLOAD) [self setLoadProgress:0.0];
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

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:0]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:1]; }

// Once per frame while shown: timed gestures and the FORCE label
- (void)tick
{
    CFTimeInterval now = CACurrentMediaTime();
    int bKeysChanged = 0;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON) continue;

        // A touch that has stayed put for IOSTOUCH_FORCE_HOLD_DELAY becomes a
        // hold (the power is held from here on, like the F key); one that keeps
        // moving stays undecided until it has slid a step
        if (s->button == BTN_FORCE && s->forceState == FORCE_PENDING &&
            now - s->tDown >= IOSTOUCH_FORCE_HOLD_DELAY && now - s->tMove >= IOSTOUCH_FORCE_HOLD_DELAY) {
            s->forceState = FORCE_HOLD;
            bKeysChanged = 1;
        }
        if (s->button == BTN_QUICKLOAD && !s->bFired) {
            CGFloat progress = (CGFloat)((now - s->tDown) / IOSTOUCH_QUICKLOAD_HOLD);
            if (progress >= 1.0) {
                s->bFired = 1;
                [self setLoadProgress:0.0];
                iosGame_QuickLoad();
            }
            else {
                [self setLoadProgress:progress];
            }
        }
    }
    if (bKeysChanged) iosTouch_RecomputeKeys();

    // FORCE shows the power a tap would use; dimmed until there is one
    const char* name = iosGame_GetForcePowerName();
    if (!bForceLabelSet || name != forceLabelName) {
        bForceLabelSet = 1;
        forceLabelName = name;
        UILabel* l = aButtonViews[BTN_FORCE];
        l.text = name ? [NSString stringWithFormat:@"FORCE\n%s", name] : @"FORCE";
        l.font = [UIFont boldSystemFontOfSize:(name ? 10 : 13)];
        l.alpha = name ? 1.0 : 0.45;
    }
}

- (void)resetAll
{
    memset(iosTouch_aSlots, 0, sizeof(iosTouch_aSlots));
    memset(iosTouch_aButtonHeld, 0, sizeof(iosTouch_aButtonHeld));
    memset(iosTouch_aPulseQueue, 0, sizeof(iosTouch_aPulseQueue));
    memset(iosTouch_aPulseReads, 0, sizeof(iosTouch_aPulseReads));
    memset(iosTouch_aPulseGap, 0, sizeof(iosTouch_aPulseGap));
    iosTouch_bStickActive = 0;
    iosTouch_stickX = iosTouch_stickY = 0.0f;
    iosTouch_lookX = iosTouch_lookY = 0.0f;
    [self hideStick];
    [self setLoadProgress:0.0];
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
    // Not over a GUI menu either -- e.g. the objectives screen at level start
    // waits for Ok while gameplay controls are already active.
    int bWant = stdControl_bControlsActive && !jkCutscene_isRendering && !jkGuiRend_IsMenuActive();

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
        // The HUD is laid out again on level start, resize and HUD scale
        // changes; FIRE and ALT follow the right gauge
        float g[4] = {0, 0, 0, 0};
        int bGauge = jkHud_IosGetRightGaugeRectPt(&g[0], &g[1], &g[2], &g[3]);
        if (bGauge != iosTouch_bLayoutGauge || memcmp(g, iosTouch_aLayoutGauge, sizeof(g)) != 0) {
            [iosTouch_pOverlay setNeedsLayout];
        }

        [iosTouch_pOverlay tick];

        int dx = (int)iosTouch_lookX;
        int dy = (int)iosTouch_lookY;
        iosTouch_lookX -= (float)dx;
        iosTouch_lookY -= (float)dy;
        Window_lastXRel += dx;
        Window_lastYRel += dy;
    }
}

// Called once per scancode each time the game reads the keyboard. Held
// buttons report down for as long as they're held; a queued one-off press is
// reported down for IOSTOUCH_PULSE_READS reads and then up for one, so the
// game sees each queued press as its own key press.
int iosTouch_IsScancodeDown(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return 0;
    if (!iosTouch_pOverlay || iosTouch_pOverlay.hidden) return 0;

    int bHeld = iosTouch_aKeyDown[scancode];
    if (bHeld && scancode == iosTouch_aButtons[BTN_FORCE].scancode) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch && s->role == ROLE_BUTTON && s->button == BTN_FORCE && s->forceState == FORCE_HOLD)
                s->bHoldSeen = 1;
        }
    }
    if (iosTouch_aPulseReads[scancode]) {
        if (--iosTouch_aPulseReads[scancode] == 0) iosTouch_aPulseGap[scancode] = 1;
        return 1;
    }
    if (iosTouch_aPulseGap[scancode]) {
        iosTouch_aPulseGap[scancode] = 0;
        return bHeld;
    }
    if (iosTouch_aPulseQueue[scancode]) {
        iosTouch_aPulseQueue[scancode]--;
        iosTouch_aPulseReads[scancode] = IOSTOUCH_PULSE_READS - 1;
        if (!iosTouch_aPulseReads[scancode]) iosTouch_aPulseGap[scancode] = 1;
        return 1;
    }
    return bHeld;
}

#endif // TARGET_IOS

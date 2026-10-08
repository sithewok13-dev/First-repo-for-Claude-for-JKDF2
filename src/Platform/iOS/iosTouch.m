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
extern int jkHud_bChatOpen;

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

// Buttons show at this opacity while untouched, so they hide less of the game;
// a touched one shows at full strength
#define IOSTOUCH_IDLE_ALPHA 0.65
// Edge-to-edge gap between the buttons around FIRE (the arc, ALT and FORCE)
#define IOSTOUCH_CLUSTER_GAP 30.0f
// Force wheel: at most this many powers (MotS has 17), on a ring at most
// IOSTOUCH_WHEEL_RADIUS points across from the centre, each slot at most
// IOSTOUCH_WHEEL_SLOT_RADIUS
#define IOSTOUCH_WHEEL_MAX 20
#define IOSTOUCH_WHEEL_RADIUS 140.0
#define IOSTOUCH_WHEEL_SLOT_RADIUS 30.0
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
    ROLE_WHEEL,   // a touch on the open force wheel
    ROLE_IGNORED, // was down when the wheel opened; ignored until it lifts
};

enum {
    KIND_KEY = 0,  // holds its key while touched
    KIND_MENU,     // Escape (via stdControl_bControllerEscapeKey)
    KIND_TAPKEY,   // one press of its key when the touch lifts on the button
    KIND_HOLDLOAD, // hold IOSTOUCH_QUICKLOAD_HOLD seconds to quick load
    KIND_WHEEL,    // opens the force wheel
    KIND_ITEM,     // uses an inventory item when the touch lifts on it; only shown while the player has it
    KIND_CHAT,     // opens (or closes) the typing line for cheats, when the touch lifts on it
};

// Keys are the game's default keyboard bindings (sithControl_RegisterKeyboardBindings).
// Inventory bins of the usable items (SITHBIN_* in types_enums.h, which can't
// be included here); KIND_ITEM buttons select one, then press use-item (Return).
#define SITHBIN_BACTATANK_IOS 40
#define SITHBIN_IRGOGGLES_IOS 41
#define SITHBIN_FIELDLIGHT_IOS 42
typedef struct {
    const char* label;
    int kind;
    int scancode;       // -1 if the button isn't a key
    float radius;       // points
    int bLookWhileHeld; // dragging on this button also turns the view
    int bin;            // KIND_ITEM: the inventory bin it uses
    float x, y;         // centre, set in layout
} iosTouchButton;

// Layout (see layoutSubviews). Bottom right, under the right thumb: FIRE, an
// arc of DUCK / ACT / JUMP around it, ALT above the ammo gauge and FORCE right
// of JUMP, above ALT -- all IOSTOUCH_CLUSTER_GAP apart. FORCE uses the selected
// power for as long as it is held (Force Jump charges, Lightning keeps going).
// These all pass drags through to looking, so a thumb that lands on one while
// aiming keeps aiming. Top left: next weapon, the FORCE WHEEL that picks the
// power, and a button for each usable item while the player has it (field
// light, IR goggles, bacta), each always in its own place. Top middle: the
// keyboard, for the typing line (cheats). Top right: quick save, quick load
// (hold) and the menu. ACT is the door/switch key.
enum {
    BTN_FIRE, BTN_ALT, BTN_DUCK, BTN_ACT, BTN_JUMP, BTN_FORCE,
    BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA,
    BTN_KEYBOARD,
    BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU,
    BTN_COUNT
};
static iosTouchButton iosTouch_aButtons[] = {
    [BTN_FIRE]      = { "FIRE",        KIND_KEY,      SDL_SCANCODE_LCTRL,  42.0f, 1 },
    [BTN_ALT]       = { "ALT",         KIND_KEY,      SDL_SCANCODE_Z,      28.0f, 1 },
    [BTN_DUCK]      = { "DUCK",        KIND_KEY,      SDL_SCANCODE_C,      29.0f, 1 },
    [BTN_ACT]       = { "ACT",         KIND_KEY,      SDL_SCANCODE_SPACE,  29.0f, 1 },
    [BTN_JUMP]      = { "JUMP",        KIND_KEY,      SDL_SCANCODE_X,      31.0f, 1 },
    [BTN_FORCE]     = { "FORCE",       KIND_KEY,      SDL_SCANCODE_F,      30.0f, 1 },
    [BTN_NEXTWPN]   = { "NEXT\nWPN",   KIND_KEY,      SDL_SCANCODE_G,      22.0f, 0 },
    [BTN_WHEEL]     = { "FORCE\nWHEEL", KIND_WHEEL,   -1,                  22.0f, 0 },
    [BTN_LIGHT]     = { "LIGHT",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_FIELDLIGHT_IOS },
    [BTN_IR]        = { "IR",          KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_IRGOGGLES_IOS },
    [BTN_BACTA]     = { "BACTA",       KIND_ITEM,     SDL_SCANCODE_RETURN, 22.0f, 0, SITHBIN_BACTATANK_IOS },
    [BTN_KEYBOARD]  = { "",            KIND_CHAT,     -1,                  22.0f, 0 },
    [BTN_QUICKSAVE] = { "QUICK\nSAVE", KIND_TAPKEY,   SDL_SCANCODE_F9,     22.0f, 0 },
    [BTN_QUICKLOAD] = { "QUICK\nLOAD", KIND_HOLDLOAD, -1,                  22.0f, 0 },
    [BTN_MENU]      = { "MENU",        KIND_MENU,     -1,                  22.0f, 0 },
};
#define IOSTOUCH_NUM_BUTTONS ((int)(sizeof(iosTouch_aButtons) / sizeof(iosTouch_aButtons[0])))
typedef char iosTouch_assertButtonCount[(IOSTOUCH_NUM_BUTTONS == BTN_COUNT) ? 1 : -1];

typedef struct {
    UITouch* touch; // not retained; only compared
    int role;
    int button;
    CGPoint origin;
    CGPoint last;
    CFTimeInterval tDown; // when the touch began
    int bFired;           // QUICK LOAD: already loaded for this touch
    int bSeen;            // KIND_KEY: the game has read the key as held at least once
    int wheelSlot;        // ROLE_WHEEL: the power slot under the finger, or -1
    int bWheelOpener;     // ROLE_WHEEL: the touch on FORCE WHEEL that opened it...
    int bLeftOpener;      // ...and it has since slid off that button
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
// The HUD gauge rectangle and cutout side the current layout was made for
static int iosTouch_bLayoutGauge = 0;
static float iosTouch_aLayoutGauge[4];
static int iosTouch_layoutCutoutRight = -1;
// The force wheel: open or not, and the powers on it (inventory bins)
static int iosTouch_bWheelOpen = 0;
static int iosTouch_aWheelBins[IOSTOUCH_WHEEL_MAX];
static int iosTouch_numWheelBins = 0;
static CGPoint iosTouch_aWheelPos[IOSTOUCH_WHEEL_MAX];
static CGFloat iosTouch_wheelSlotRadius = IOSTOUCH_WHEEL_SLOT_RADIUS;

static void iosTouch_QueuePress(int scancode)
{
    if (scancode < 0 || scancode >= IOSTOUCH_NUM_SCANCODES) return;
    if (iosTouch_aPulseQueue[scancode] < 255) iosTouch_aPulseQueue[scancode]++;
}

static void iosTouch_RecomputeKeys(void)
{
    memset(iosTouch_aKeyDown, 0, sizeof(iosTouch_aKeyDown));
    stdControl_bControllerEscapeKey = 0;
    if (iosTouch_bWheelOpen) return; // the wheel takes every touch
    int bMenu = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (!iosTouch_aButtonHeld[i]) continue;
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (b->kind == KIND_MENU) bMenu = 1;
        else if (b->kind == KIND_KEY && b->scancode >= 0) iosTouch_aKeyDown[b->scancode] = 1;
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

// Whether the camera cutout may be on the right of the screen. Landscape right
// has the bottom of the phone on the right, so the cutout (at the top) is on
// the left; landscape left puts it on the right. Not known yet: assume it can be.
static int iosTouch_CutoutMayBeRight(UIView* v)
{
    UIWindowScene* scene = v.window.windowScene;
    return !(scene && scene.interfaceOrientation == UIInterfaceOrientationLandscapeRight);
}

// ---------------------------------------------------------------- overlay view

@interface IOSTouchOverlay : UIView {
    UIView* stickBase;
    UIView* stickKnob;
    UILabel* aButtonViews[IOSTOUCH_NUM_BUTTONS];
    CAShapeLayer* loadRing; // QUICK LOAD's hold progress
    const char* forceLabelName;
    int bForceLabelSet;
    int aItemAmount[IOSTOUCH_NUM_BUTTONS]; // KIND_ITEM: what the label shows (-1: not yet set)
    int aItemActive[IOSTOUCH_NUM_BUTTONS];
    UIView* wheelView;                     // dims the game; holds the wheel
    UILabel* aWheelViews[IOSTOUCH_WHEEL_MAX];
    UILabel* wheelTitle;                   // in the middle: the power under the finger, or a hint
    CAShapeLayer* forceRing;               // around FORCE: how full the force meter is
    float forceRingFrac;                   // what it shows (-1: not yet set)
    int bForceRingFull;
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

        // Around FORCE: the force meter, a ring that empties as the meter does
        // (QUICK LOAD's ring the other way round) and glows when full
        CGFloat fr = iosTouch_aButtons[BTN_FORCE].radius;
        forceRing = [CAShapeLayer layer];
        forceRing.frame = CGRectMake(0, 0, fr * 2, fr * 2);
        forceRing.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(fr, fr) radius:fr - 2.5
                                                    startAngle:-M_PI_2 endAngle:3 * M_PI_2 clockwise:YES].CGPath;
        forceRing.fillColor = [UIColor clearColor].CGColor;
        forceRing.strokeColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor;
        forceRing.lineWidth = 3.0;
        forceRing.lineCap = kCALineCapRound;
        forceRing.strokeEnd = 0.0;
        forceRing.shadowColor = [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:1.0].CGColor;
        forceRing.shadowOffset = CGSizeZero;
        forceRing.shadowRadius = 7.0;
        forceRing.shadowOpacity = 0.0;
        forceRing.hidden = YES;
        [self.layer insertSublayer:forceRing above:aButtonViews[BTN_FORCE].layer];
        forceRingFrac = -1.0f;
        bForceRingFull = 0;

        // The keyboard button shows the keyboard symbol
        UIImageSymbolConfiguration* cfg = [UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightSemibold];
        UIImage* kb = [UIImage systemImageNamed:@"keyboard" withConfiguration:cfg];
        if (kb) {
            NSTextAttachment* att = [[NSTextAttachment alloc] init];
            att.image = [kb imageWithTintColor:[UIColor colorWithWhite:1.0 alpha:0.85] renderingMode:UIImageRenderingModeAlwaysOriginal];
            aButtonViews[BTN_KEYBOARD].attributedText = [NSAttributedString attributedStringWithAttachment:att];
        }
        else {
            aButtonViews[BTN_KEYBOARD].text = @"TYPE";
        }

        // Item buttons appear once the player has the item (see -tick)
        for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
            aItemAmount[i] = -1;
            aItemActive[i] = 0;
            if (iosTouch_aButtons[i].kind == KIND_ITEM) aButtonViews[i].hidden = YES;
        }

        stickBase = IOSTouch_MakeCircle(IOSTOUCH_STICK_RADIUS, 0.08);
        stickKnob = IOSTouch_MakeCircle(24, 0.30);
        stickBase.hidden = YES;
        stickKnob.hidden = YES;
        [self addSubview:stickBase];
        [self addSubview:stickKnob];

        // The force wheel, on top of everything, hidden until opened
        wheelView = [[UIView alloc] initWithFrame:self.bounds];
        wheelView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        wheelView.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
        wheelView.userInteractionEnabled = NO; // touches are handled here, by the overlay
        wheelView.hidden = YES;
        for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectZero];
            l.textAlignment = NSTextAlignmentCenter;
            l.textColor = [UIColor colorWithWhite:1.0 alpha:0.95];
            l.font = [UIFont boldSystemFontOfSize:10];
            l.numberOfLines = 2;
            l.adjustsFontSizeToFitWidth = YES;
            l.minimumScaleFactor = 0.6;
            l.layer.borderWidth = 2.0;
            l.clipsToBounds = YES;
            l.hidden = YES;
            aWheelViews[i] = l;
            [wheelView addSubview:l];
        }
        wheelTitle = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 150, 44)];
        wheelTitle.textAlignment = NSTextAlignmentCenter;
        wheelTitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.95];
        wheelTitle.font = [UIFont boldSystemFontOfSize:13];
        wheelTitle.numberOfLines = 2;
        wheelTitle.adjustsFontSizeToFitWidth = YES;
        wheelTitle.minimumScaleFactor = 0.7;
        [wheelView addSubview:wheelTitle];
        [self addSubview:wheelView];

        [self refreshButtonLooks];
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

    // The camera cutout, when it is on the right, as a capsule along the right
    // edge: the Dynamic Island (side inset ~59-62pt) is ~126x37pt, 11pt in
    // from the edge; a notch (side inset ~44-50pt) is up to ~210x33pt at the
    // edge. The landscape side insets are the same both ways round, so which
    // side it is on comes from the screen's orientation.
    iosTouch_layoutCutoutRight = iosTouch_CutoutMayBeRight(self);
    int bCutout = in.right >= 40.0 && iosTouch_layoutCutoutRight;
    CGFloat cutX = (in.right >= 55.0) ? W - 29.5 : W - 16.5;
    CGFloat cutR = (in.right >= 55.0) ? 18.5 : 16.5;
    CGFloat cutHalf = ((in.right >= 55.0) ? 63.0 : 105.0) - cutR;

    // ALT: up and to the right of FIRE, IOSTOUCH_CLUSTER_GAP from it, as low
    // as it can sit while staying above the ammo gauge, on screen, clear of
    // JUMP and clear of the cutout.
    {
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        const CGFloat AR = a->radius;
        const CGFloat D = FR + AR + IOSTOUCH_CLUSTER_GAP;
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

    // FORCE: right of JUMP and above ALT, out of the way of aiming -- on the
    // circle IOSTOUCH_CLUSTER_GAP out from JUMP, as far round towards pointing
    // right as it fits on screen, below the top row and clear of ALT, ACT,
    // FIRE, the gauge and the cutout.
    // Where the cutout (or, on smaller screens, ALT) takes that spot it goes
    // higher, over JUMP; failing that, the gaps shrink.
    {
        iosTouchButton* f = &iosTouch_aButtons[BTN_FORCE];
        iosTouchButton* j = &iosTouch_aButtons[BTN_JUMP];
        iosTouchButton* a = &iosTouch_aButtons[BTN_ALT];
        iosTouchButton* c = &iosTouch_aButtons[BTN_ACT];
        const CGFloat R = f->radius;
        const CGFloat aGap[3] = { IOSTOUCH_CLUSTER_GAP, 20.0, 12.0 };
        // If nothing fits (a very short screen, e.g. with Display Zoom): just
        // outside the arc, IOSTOUCH_CLUSTER_GAP from both DUCK and ACT
        iosTouchButton* d = &iosTouch_aButtons[BTN_DUCK];
        CGFloat dist = d->radius + R + IOSTOUCH_CLUSTER_GAP; // DUCK and ACT are the same size
        CGFloat mx = (d->x + c->x) * 0.5, my = (d->y + c->y) * 0.5;
        CGFloat vx = c->x - d->x, vy = c->y - d->y;
        CGFloat L = sqrt(vx * vx + vy * vy);
        CGFloat h = (dist > L * 0.5) ? sqrt(dist * dist - L * L * 0.25) : 0;
        CGFloat px = vy / L, py = -vx / L; // the perpendicular pointing away from FIRE
        if ((mx - fire.x) * px + (my - fire.y) * py < 0) { px = -px; py = -py; }
        CGPoint best = CGPointMake(mx + px * h, my + py * h);
        int bFound = 0;
        for (int pass = 0; pass < 3 && !bFound; pass++) {
            const CGFloat gap = aGap[pass];
            const CGFloat D = j->radius + R + gap;
            for (int deg = 0; deg <= 135; deg++) {
                CGFloat rad = deg * (CGFloat)M_PI / 180.0;
                CGPoint p = CGPointMake(j->x + D * cos(rad), j->y - D * sin(rad));
                if (p.x + R > W - 4 || p.y - R < top + 60) continue;                 // on screen, below the top row
                if (hypot(p.x - a->x, p.y - a->y) < R + a->radius + gap) continue;     // clear of ALT
                if (hypot(p.x - c->x, p.y - c->y) < R + c->radius + gap) continue;     // ACT
                if (hypot(p.x - fire.x, p.y - fire.y) < R + FR + gap) continue;        // FIRE
                if (iosTouch_bLayoutGauge) {                                           // the gauge
                    CGFloat gx = MIN(MAX(p.x, g[0]), g[2]), gy = MIN(MAX(p.y, g[1]), g[3]);
                    if (hypot(p.x - gx, p.y - gy) < R + 4) continue;
                }
                if (bCutout && iosTouch_DistToSegment(p, cutX, H * 0.5 - cutHalf, cutX, H * 0.5 + cutHalf) < R + cutR + 2)
                    continue;                                                          // the cutout
                best = p;
                bFound = 1;
                break;
            }
        }
        f->x = best.x;
        f->y = best.y;
    }

    // Top left: NEXT WPN | FORCE WHEEL | LIGHT, IR, BACTA -- each item keeps its
    // own place whether or not the ones before it are showing
    iosTouch_aButtons[BTN_NEXTWPN].x = left + 24;
    iosTouch_aButtons[BTN_WHEEL].x = left + 84;
    iosTouch_aButtons[BTN_LIGHT].x = left + 148;
    iosTouch_aButtons[BTN_IR].x = left + 204;
    iosTouch_aButtons[BTN_BACTA].x = left + 260;
    // Top right: QUICK SAVE, QUICK LOAD, MENU in the corner -- spaced well
    // apart, so a press meant for QUICK LOAD can't land on QUICK SAVE
    iosTouch_aButtons[BTN_QUICKSAVE].x = right - 156;
    iosTouch_aButtons[BTN_QUICKLOAD].x = right - 90;
    iosTouch_aButtons[BTN_MENU].x = right - 24;
    // The keyboard, left of QUICK SAVE: the game draws its typing line (and
    // other messages) centred at the top, so the middle stays clear
    iosTouch_aButtons[BTN_KEYBOARD].x = right - 216;
    const int aTopRow[] = { BTN_NEXTWPN, BTN_WHEEL, BTN_LIGHT, BTN_IR, BTN_BACTA, BTN_KEYBOARD,
                            BTN_QUICKSAVE, BTN_QUICKLOAD, BTN_MENU };
    for (int i = 0; i < (int)(sizeof(aTopRow) / sizeof(aTopRow[0])); i++) {
        iosTouch_aButtons[aTopRow[i]].y = top + 26;
    }

    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        aButtonViews[i].center = CGPointMake(iosTouch_aButtons[i].x, iosTouch_aButtons[i].y);
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    forceRing.position = CGPointMake(iosTouch_aButtons[BTN_FORCE].x, iosTouch_aButtons[BTN_FORCE].y);
    [CATransaction commit];
    if (iosTouch_bWheelOpen) [self layoutWheel];
}

// Closest button the touch is on (with a little forgiveness), so a touch in the
// gap between two buttons picks the nearer one rather than the first listed.
- (int)buttonAt:(CGPoint)p
{
    int best = -1;
    CGFloat bestDist = 0;
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        iosTouchButton* b = &iosTouch_aButtons[i];
        if (aButtonViews[i].hidden) continue; // an item the player doesn't have
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
        int bHeld = iosTouch_aButtonHeld[i] != 0;
        aButtonViews[i].backgroundColor = [UIColor colorWithWhite:(bHeld ? 1.0 : 0.0) alpha:(bHeld ? 0.30 : 0.22)];
        // A switched-on item (field light, IR goggles) and the open typing line
        // stand out in yellow, at full strength
        int bOn = (iosTouch_aButtons[i].kind == KIND_ITEM && aItemActive[i])
                  || (i == BTN_KEYBOARD && jkHud_bChatOpen);
        CGFloat alpha = (bHeld || bOn) ? 1.0 : IOSTOUCH_IDLE_ALPHA;
        if (i == BTN_FORCE && bForceLabelSet && !forceLabelName) alpha *= 0.45; // no power to use yet
        aButtonViews[i].alpha = alpha;
        if (i != BTN_FIRE) {
            aButtonViews[i].layer.borderColor = bOn ? [UIColor colorWithRed:1.0 green:0.85 blue:0.3 alpha:0.95].CGColor
                                                    : [UIColor colorWithWhite:1.0 alpha:0.4].CGColor;
        }
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    forceRing.opacity = bForceRingFull ? 1.0 : (iosTouch_aButtonHeld[BTN_FORCE] ? 1.0 : IOSTOUCH_IDLE_ALPHA);
    [CATransaction commit];
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

// ------------------------------------------------------------ force wheel

// Places the wheel's slots: a ring around the middle of the safe area, first
// power at the top, going clockwise, as big as fits (two slot widths per power
// along the ring, at most IOSTOUCH_WHEEL_SLOT_RADIUS).
- (void)layoutWheel
{
    CGRect b = self.bounds;
    UIEdgeInsets in = self.safeAreaInsets;
    CGFloat left = MAX(in.left, 8.0), right = b.size.width - MAX(in.right, 8.0);
    CGPoint c = CGPointMake((left + right) * 0.5, b.size.height * 0.5);
    int n = iosTouch_numWheelBins;
    CGFloat R = MIN(IOSTOUCH_WHEEL_RADIUS, b.size.height * 0.5 - IOSTOUCH_WHEEL_SLOT_RADIUS - 10.0);
    CGFloat r = IOSTOUCH_WHEEL_SLOT_RADIUS;
    if (n > 0) r = MIN(r, M_PI * R / n - 4.0);
    r = MAX(r, 18.0);
    iosTouch_wheelSlotRadius = r;
    for (int i = 0; i < IOSTOUCH_WHEEL_MAX; i++) {
        UILabel* l = aWheelViews[i];
        if (i >= n) { l.hidden = YES; continue; }
        CGFloat a = -M_PI_2 + 2.0 * M_PI * i / n;
        iosTouch_aWheelPos[i] = CGPointMake(c.x + R * cos(a), c.y + R * sin(a));
        l.bounds = CGRectMake(0, 0, r * 2, r * 2);
        l.center = iosTouch_aWheelPos[i];
        l.layer.cornerRadius = r;
        const char* name = iosGame_GetPowerName(iosTouch_aWheelBins[i]);
        l.text = name ? [NSString stringWithUTF8String:name] : @"?";
        l.hidden = NO;
    }
    wheelTitle.center = c;
}

// The wheel slot under p (with a little forgiveness), or -1
- (int)wheelSlotAt:(CGPoint)p
{
    int best = -1;
    CGFloat bestDist = 0;
    for (int i = 0; i < iosTouch_numWheelBins; i++) {
        CGFloat d = hypot(p.x - iosTouch_aWheelPos[i].x, p.y - iosTouch_aWheelPos[i].y);
        if (d <= iosTouch_wheelSlotRadius + 8.0 && (best < 0 || d < bestDist)) {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

// The selected power in blue, the one under a finger filled in; the middle
// names the one under the finger, or says what to do
- (void)refreshWheelLooks
{
    int hot = -1;
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        if (iosTouch_aSlots[i].touch && iosTouch_aSlots[i].role == ROLE_WHEEL && iosTouch_aSlots[i].wheelSlot >= 0)
            hot = iosTouch_aSlots[i].wheelSlot;
    }
    int cur = iosGame_GetCurPower();
    for (int i = 0; i < iosTouch_numWheelBins; i++) {
        UILabel* l = aWheelViews[i];
        int bCur = iosTouch_aWheelBins[i] == cur;
        l.backgroundColor = (i == hot) ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.55]
                                       : [UIColor colorWithWhite:0.0 alpha:0.55];
        l.layer.borderColor = bCur ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:1.0].CGColor
                                   : [UIColor colorWithWhite:1.0 alpha:0.5].CGColor;
    }
    if (iosTouch_numWheelBins == 0) {
        wheelTitle.text = @"No force\npowers yet";
    }
    else if (hot >= 0) {
        const char* name = iosGame_GetPowerName(iosTouch_aWheelBins[hot]);
        wheelTitle.text = name ? [NSString stringWithUTF8String:name] : @"";
    }
    else {
        wheelTitle.text = @"Pick a power";
    }
}

// Opens the wheel with the powers the player has. Every other touch lets go of
// what it was holding and is ignored until it lifts, and the game holds still
// (iosGame_SetHold) until the wheel closes.
- (void)openWheel
{
    iosTouch_numWheelBins = iosGame_GetForcePowers(iosTouch_aWheelBins, IOSTOUCH_WHEEL_MAX);
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch) continue;
        if (s->role == ROLE_BUTTON && iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
        if (s->role == ROLE_BUTTON && s->button == BTN_QUICKLOAD) [self setLoadProgress:0.0];
        if (s->role == ROLE_STICK) {
            iosTouch_bStickActive = 0;
            iosTouch_stickX = iosTouch_stickY = 0.0f;
            [self hideStick];
        }
        s->role = ROLE_IGNORED;
    }
    iosTouch_bWheelOpen = 1;
    iosGame_SetHold(1);
    [self layoutWheel];
    wheelView.hidden = NO;
    [self bringSubviewToFront:wheelView];
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    [self refreshWheelLooks];
}

// Closes the wheel, selecting the power in slot (if it is one)
- (void)closeWheelSelecting:(int)slot
{
    if (slot >= 0 && slot < iosTouch_numWheelBins) iosGame_SelectPower(iosTouch_aWheelBins[slot]);
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        // fingers still on the wheel are ignored until they lift
        if (iosTouch_aSlots[i].touch && iosTouch_aSlots[i].role == ROLE_WHEEL) iosTouch_aSlots[i].role = ROLE_IGNORED;
    }
    iosTouch_bWheelOpen = 0;
    iosGame_SetHold(0);
    wheelView.hidden = YES;
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
}

// ------------------------------------------------------------ touches

- (int)isPoint:(CGPoint)p onButton:(int)button
{
    iosTouchButton* b = &iosTouch_aButtons[button];
    return hypot(p.x - b->x, p.y - b->y) <= b->radius + 6.0;
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
        s->button = -1;
        s->wheelSlot = -1;

        if (iosTouch_bWheelOpen) {
            s->role = ROLE_WHEEL;
            s->wheelSlot = [self wheelSlotAt:p];
            continue;
        }

        s->button = [self buttonAt:p];
        if (s->button >= 0 && iosTouch_aButtons[s->button].kind == KIND_WHEEL) {
            // Opens at once; lifting on a power picks it, lifting where it
            // started leaves the wheel open for a tap. The typing line closes
            // first: its keyboard would cover the lower half of the wheel.
            if (jkHud_bChatOpen) iosGame_ToggleChat();
            [self openWheel];
            s->role = ROLE_WHEEL;
            s->bWheelOpener = 1;
            break; // any other new touch would be ignored anyway
        }
        else if (s->button >= 0) {
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
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];

            if (s->role == ROLE_WHEEL) {
                s->wheelSlot = [self wheelSlotAt:p];
                if (s->bWheelOpener && ![self isPoint:p onButton:BTN_WHEEL]) s->bLeftOpener = 1;
            }
            else if (s->role == ROLE_STICK) {
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
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

// bCancelled: iOS took the touch away (a call, a system gesture...) -- release
// whatever it held, but don't treat it as a finished tap
- (void)endTouches:(NSSet<UITouch*>*)touches cancelled:(int)bCancelled
{
    for (UITouch* t in touches) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch != t) continue;
            CGPoint p = [t locationInView:self];
            if (s->role == ROLE_WHEEL && iosTouch_bWheelOpen && !bCancelled) {
                int slot = [self wheelSlotAt:p];
                if (slot >= 0) {
                    [self closeWheelSelecting:slot];          // picked a power
                }
                else if (s->bWheelOpener && !s->bLeftOpener) {
                    // a tap on FORCE WHEEL: stays open to tap a power
                }
                else {
                    [self closeWheelSelecting:-1];            // let go elsewhere: no change
                }
            }
            else if (s->role == ROLE_BUTTON) {
                iosTouchButton* b = &iosTouch_aButtons[s->button];
                if (iosTouch_aButtonHeld[s->button] > 0) iosTouch_aButtonHeld[s->button]--;
                // A tap so quick that it began and ended between two reads of
                // the controls (a slow frame) still counts as one press
                if (!bCancelled && !jkHud_bChatOpen && b->kind == KIND_KEY && b->scancode >= 0 && !s->bSeen) {
                    iosTouch_QueuePress(b->scancode);
                }
                // These act when the finger lifts on the button, so a slip onto
                // one can be dragged off again. Not while typing: the game isn't
                // reading the controls then, so they would only go off later.
                if (!bCancelled && !jkHud_bChatOpen && [self isPoint:p onButton:s->button]) {
                    if (b->kind == KIND_TAPKEY) {
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (b->kind == KIND_ITEM && !aButtonViews[s->button].hidden
                             // one item at a time: the use key acts on whichever item is selected when it is read
                             && !iosTouch_aPulseQueue[b->scancode] && !iosTouch_aPulseReads[b->scancode]
                             && !iosTouch_aPulseGap[b->scancode]) {
                        iosGame_SelectItem(b->bin);
                        iosTouch_QueuePress(b->scancode);
                    }
                    else if (b->kind == KIND_CHAT) {
                        iosGame_ToggleChat();
                    }
                }
                if (s->button == BTN_QUICKLOAD) [self setLoadProgress:0.0];
            }
            else if (s->role == ROLE_STICK) {
                iosTouch_bStickActive = 0;
                iosTouch_stickX = iosTouch_stickY = 0.0f;
                [self hideStick];
            }
            memset(s, 0, sizeof(*s));
        }
    }
    iosTouch_RecomputeKeys();
    [self refreshButtonLooks];
    if (iosTouch_bWheelOpen) [self refreshWheelLooks];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:0]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self endTouches:touches cancelled:1]; }

// A full force meter glows, gently pulsing
- (void)setForceRingGlow:(int)bGlow
{
    [forceRing removeAnimationForKey:@"glow"];
    if (bGlow) {
        forceRing.shadowOpacity = 0.9;
        CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"shadowOpacity"];
        a.fromValue = @(0.35);
        a.toValue = @(1.0);
        a.duration = 0.9;
        a.autoreverses = YES;
        a.repeatCount = HUGE_VALF;
        a.removedOnCompletion = NO; // keep it when the app comes back from the background
        a.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [forceRing addAnimation:a forKey:@"glow"];
    }
    else {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        forceRing.shadowOpacity = 0.0;
        [CATransaction commit];
    }
}

// Once per frame while shown: QUICK LOAD's hold, the FORCE label and its force
// meter ring, which item buttons show and what they say
- (void)tick
{
    CFTimeInterval now = CACurrentMediaTime();
    for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
        iosTouchSlot* s = &iosTouch_aSlots[i];
        if (!s->touch || s->role != ROLE_BUTTON) continue;
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

    int bLooksChanged = 0;

    // FORCE shows the power it uses; dimmed until there is one
    const char* name = iosGame_GetForcePowerName();
    if (!bForceLabelSet || name != forceLabelName) {
        bForceLabelSet = 1;
        forceLabelName = name;
        UILabel* l = aButtonViews[BTN_FORCE];
        l.text = name ? [NSString stringWithFormat:@"FORCE\n%s", name] : @"FORCE";
        l.font = [UIFont boldSystemFontOfSize:(name ? 10 : 13)];
        bLooksChanged = 1;
    }

    // An item button shows while the player has some of it; bacta says how many
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
        if (iosTouch_aButtons[i].kind != KIND_ITEM) continue;
        int amount = 0, bActive = 0;
        int bHave = iosGame_GetItem(iosTouch_aButtons[i].bin, &amount, &bActive);
        if (!bHave) { amount = 0; bActive = 0; }
        if ((int)aButtonViews[i].hidden == bHave) {
            aButtonViews[i].hidden = !bHave;
            bLooksChanged = 1;
        }
        if (amount != aItemAmount[i]) {
            aItemAmount[i] = amount;
            if (i == BTN_BACTA) {
                aButtonViews[i].text = amount > 1 ? [NSString stringWithFormat:@"BACTA\n%d", amount] : @"BACTA";
                aButtonViews[i].font = [UIFont boldSystemFontOfSize:(amount > 1 ? 9 : 10)];
            }
        }
        if (bActive != aItemActive[i]) {
            aItemActive[i] = bActive;
            bLooksChanged = 1;
        }
    }

    // The keyboard lights up while the typing line is open. Presses queued
    // while it opens or closes are dropped: the game isn't reading the
    // controls while it is open, so they would go off much later.
    static int bChatWasOpen = 0;
    if ((jkHud_bChatOpen != 0) != bChatWasOpen) {
        bChatWasOpen = jkHud_bChatOpen != 0;
        memset(iosTouch_aPulseQueue, 0, sizeof(iosTouch_aPulseQueue));
        memset(iosTouch_aPulseReads, 0, sizeof(iosTouch_aPulseReads));
        memset(iosTouch_aPulseGap, 0, sizeof(iosTouch_aPulseGap));
        bLooksChanged = 1;
    }

    // The force meter around FORCE
    int bFull = 0;
    float frac = iosGame_GetForceMana(&bFull);
    if (fabsf(frac - forceRingFrac) >= 0.004f || bFull != bForceRingFull) {
        forceRingFrac = frac;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        forceRing.hidden = frac < 0.0f;
        forceRing.strokeEnd = frac < 0.0f ? 0.0 : frac;
        [CATransaction commit];
        if (bFull != bForceRingFull) {
            bForceRingFull = bFull;
            [self setForceRingGlow:bFull];
            bLooksChanged = 1;
        }
    }

    if (bLooksChanged) [self refreshButtonLooks];
}

- (void)resetAll
{
    if (iosTouch_bWheelOpen) {
        iosTouch_bWheelOpen = 0;
        iosGame_SetHold(0);
        wheelView.hidden = YES;
    }
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
        // changes; FIRE and ALT follow the right gauge. Turning the phone the
        // other way up changes neither the size nor the insets, but moves the
        // cutout to the other side.
        float g[4] = {0, 0, 0, 0};
        int bGauge = jkHud_IosGetRightGaugeRectPt(&g[0], &g[1], &g[2], &g[3]);
        if (bGauge != iosTouch_bLayoutGauge || memcmp(g, iosTouch_aLayoutGauge, sizeof(g)) != 0
            || iosTouch_CutoutMayBeRight(iosTouch_pOverlay) != iosTouch_layoutCutoutRight) {
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
    if (bHeld) {
        for (int i = 0; i < IOSTOUCH_MAX_TOUCHES; i++) {
            iosTouchSlot* s = &iosTouch_aSlots[i];
            if (s->touch && s->role == ROLE_BUTTON && iosTouch_aButtons[s->button].scancode == scancode)
                s->bSeen = 1;
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

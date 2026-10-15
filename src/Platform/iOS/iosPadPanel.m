// Added: entire file. See iosPadPanel.h.
// Written to work with or without ARC (as iosTouch.m: what it makes lives
// for the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <string.h>

#include "Platform/iOS/iosPadPanel.h"
#include "Platform/iOS/iosPad.h"

UIView* iosTouch_OverlayView(void);

// The options panel: a card in the middle of the screen over a dimmed game,
// a title, a row for each option (its name on the left, its value on the
// right; the one the D-pad is on edged in gold) and a line of what the
// buttons do. Each row IOSPADPANEL_ROW_H tall at most (less on a short
// screen), the card IOSPADPANEL_W wide at most.
#define IOSPADPANEL_W 440.0
#define IOSPADPANEL_ROW_H 30.0
#define IOSPADPANEL_TITLE_H 34.0
#define IOSPADPANEL_HINT_H 28.0
#define IOSPADPANEL_PAD 10.0
// The quick save / quick load ring: this wide, its top this far under the
// top of the screen (the safe area's), under the touch controls' top row
#define IOSPADPANEL_RING_R 26.0
#define IOSPADPANEL_RING_TOP 64.0
// The prompts along the bottom: this tall, this far over the bottom
#define IOSPADPANEL_PROMPT_H 28.0
#define IOSPADPANEL_PROMPT_GAP 6.0

static UIColor* iosPadPanel_Gold(CGFloat a)
{
    return [UIColor colorWithRed:1.0 green:0.84 blue:0.47 alpha:a];
}

@interface IOSPadPanelView : UIView {
@public
    UIView* card;
    UILabel* title;
    UILabel* aLabel[IOSPAD_NUM_ROWS];
    UILabel* aValue[IOSPAD_NUM_ROWS];
    UIView* aRowBack[IOSPAD_NUM_ROWS];
    UILabel* hint;
    CGFloat rowH;
}
- (void)refresh;
@end

@implementation IOSPadPanelView

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return self;
    self.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
    self.userInteractionEnabled = YES;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight; // (the phone turned round while it is up)
    card = [[UIView alloc] initWithFrame:CGRectZero];
    card.backgroundColor = [UIColor colorWithWhite:0.06 alpha:0.92];
    card.layer.cornerRadius = 16.0;
    card.layer.borderWidth = 1.5;
    card.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.35].CGColor;
    card.userInteractionEnabled = NO;
    [self addSubview:card];
    title = [[UILabel alloc] initWithFrame:CGRectZero];
    title.textColor = iosPadPanel_Gold(1.0);
    title.font = [UIFont boldSystemFontOfSize:15.0];
    title.textAlignment = NSTextAlignmentCenter;
    title.adjustsFontSizeToFitWidth = YES;
    title.minimumScaleFactor = 0.6;
    [card addSubview:title];
    for (int i = 0; i < IOSPAD_NUM_ROWS; i++) {
        aRowBack[i] = [[UIView alloc] initWithFrame:CGRectZero];
        aRowBack[i].layer.cornerRadius = 8.0;
        [card addSubview:aRowBack[i]];
        aLabel[i] = [[UILabel alloc] initWithFrame:CGRectZero];
        aLabel[i].textColor = [UIColor whiteColor];
        aLabel[i].font = [UIFont boldSystemFontOfSize:14.0];
        [card addSubview:aLabel[i]];
        aValue[i] = [[UILabel alloc] initWithFrame:CGRectZero];
        aValue[i].textColor = [UIColor colorWithWhite:0.85 alpha:1.0];
        aValue[i].font = [UIFont boldSystemFontOfSize:14.0];
        aValue[i].textAlignment = NSTextAlignmentRight;
        aValue[i].adjustsFontSizeToFitWidth = YES;
        aValue[i].minimumScaleFactor = 0.6;
        [card addSubview:aValue[i]];
    }
    hint = [[UILabel alloc] initWithFrame:CGRectZero];
    hint.textColor = [UIColor colorWithWhite:0.75 alpha:1.0];
    hint.font = [UIFont systemFontOfSize:12.0];
    hint.textAlignment = NSTextAlignmentCenter;
    hint.adjustsFontSizeToFitWidth = YES;
    hint.minimumScaleFactor = 0.6;
    [card addSubview:hint];
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    const CGRect b = self.bounds;
    const UIEdgeInsets in = self.superview ? self.superview.safeAreaInsets : UIEdgeInsetsZero;
    const CGFloat left = MAX(in.left, 8.0), right = b.size.width - MAX(in.right, 8.0);
    const CGFloat top = MAX(in.top, 8.0), bottom = b.size.height - MAX(in.bottom, 8.0);
    const CGFloat w = MIN((CGFloat)IOSPADPANEL_W, right - left);
    const CGFloat chrome = IOSPADPANEL_TITLE_H + IOSPADPANEL_HINT_H + 2.0 * IOSPADPANEL_PAD;
    rowH = MIN((CGFloat)IOSPADPANEL_ROW_H, floor((bottom - top - chrome) / IOSPAD_NUM_ROWS));
    const CGFloat h = chrome + rowH * IOSPAD_NUM_ROWS;
    card.frame = CGRectMake(round((left + right - w) * 0.5), round((top + bottom - h) * 0.5), w, h);
    title.frame = CGRectMake(IOSPADPANEL_PAD, IOSPADPANEL_PAD, w - 2.0 * IOSPADPANEL_PAD, IOSPADPANEL_TITLE_H - 6.0);
    for (int i = 0; i < IOSPAD_NUM_ROWS; i++) {
        const CGFloat y = IOSPADPANEL_PAD + IOSPADPANEL_TITLE_H + rowH * i;
        aRowBack[i].frame = CGRectMake(IOSPADPANEL_PAD, y + 1.0, w - 2.0 * IOSPADPANEL_PAD, rowH - 2.0);
        aLabel[i].frame = CGRectMake(IOSPADPANEL_PAD + 10.0, y, w * 0.45, rowH);
        aValue[i].frame = CGRectMake(w * 0.45, y, w * 0.55 - IOSPADPANEL_PAD - 10.0, rowH);
    }
    hint.frame = CGRectMake(IOSPADPANEL_PAD, h - IOSPADPANEL_PAD - IOSPADPANEL_HINT_H, w - 2.0 * IOSPADPANEL_PAD, IOSPADPANEL_HINT_H);
}

- (void)refresh
{
    title.text = [NSString stringWithUTF8String:iosPad_PanelTitle()];
    const int focus = iosPad_PanelFocus();
    for (int i = 0; i < IOSPAD_NUM_ROWS; i++) {
        char l[64], v[64];
        iosPad_PanelRow(i, l, sizeof(l), v, sizeof(v));
        aLabel[i].text = [NSString stringWithUTF8String:l];
        aValue[i].text = [NSString stringWithUTF8String:v];
        const int bFocus = i == focus;
        aRowBack[i].backgroundColor = bFocus ? [UIColor colorWithWhite:1.0 alpha:0.12] : [UIColor clearColor];
        aRowBack[i].layer.borderWidth = bFocus ? 2.0 : 0.0;
        aRowBack[i].layer.borderColor = iosPadPanel_Gold(0.95).CGColor;
        aValue[i].textColor = bFocus ? iosPadPanel_Gold(1.0) : [UIColor colorWithWhite:0.85 alpha:1.0];
    }
    hint.text = [NSString stringWithUTF8String:iosPad_PanelHint()];
}

// A finger on a row changes it (as A does); off the card, the panel closes
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    UITouch* t = [touches anyObject];
    if (!t) return;
    CGPoint p = [t locationInView:card];
    int row = -1;
    if (CGRectContainsPoint(card.bounds, p)) {
        const CGFloat y = p.y - IOSPADPANEL_PAD - IOSPADPANEL_TITLE_H;
        row = (y >= 0.0 && rowH > 0.0) ? (int)(y / rowH) : -2;
        if (row >= IOSPAD_NUM_ROWS) row = -2;
    }
    if (row == -2) return; // (the title or the hint: nothing)
    iosPad_PanelTap(row);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {}

@end

static IOSPadPanelView* iosPadPanel_pPanel = nil;
static UIView* iosPadPanel_pRing = nil;
static CAShapeLayer* iosPadPanel_pRingLayer = nil;
static UILabel* iosPadPanel_pRingLabel = nil;
static UILabel* iosPadPanel_pPrompt = nil;

// Where these go: over the touch overlay, in the view it is in
static UIView* iosPadPanel_Host(void)
{
    UIView* o = iosTouch_OverlayView();
    return o ? o.superview : nil;
}

static void iosPadPanel_OnTop(UIView* v, UIView* host)
{
    if (v.superview != host) {
        [v removeFromSuperview];
        [host addSubview:v];
    }
    if (host.subviews.lastObject != v) [host bringSubviewToFront:v];
}

int iosPadPanel_IsOwnView(const void* pView)
{
    return pView && (pView == (__bridge const void*)iosPadPanel_pPanel || pView == (__bridge const void*)iosPadPanel_pRing
                     || pView == (__bridge const void*)iosPadPanel_pPrompt);
}

void iosPadPanel_KeepOnTop(void)
{
    UIView* aViews[3] = { iosPadPanel_pRing, iosPadPanel_pPrompt, iosPadPanel_pPanel };
    for (int i = 0; i < 3; i++) {
        UIView* v = aViews[i];
        if (v && !v.hidden && v.superview) [v.superview bringSubviewToFront:v];
    }
}

void iosPadPanel_Show(int bShow)
{
    @autoreleasepool {
        UIView* host = iosPadPanel_Host();
        if (!bShow || !host) {
            iosPadPanel_pPanel.hidden = YES;
            return;
        }
        if (!iosPadPanel_pPanel) iosPadPanel_pPanel = [[IOSPadPanelView alloc] initWithFrame:host.bounds];
        iosPadPanel_pPanel.frame = host.bounds;
        iosPadPanel_OnTop(iosPadPanel_pPanel, host);
        iosPadPanel_pPanel.hidden = NO;
        [iosPadPanel_pPanel setNeedsLayout];
        [iosPadPanel_pPanel layoutIfNeeded];
        [iosPadPanel_pPanel refresh];
    }
}

void iosPadPanel_Refresh(void)
{
    @autoreleasepool {
        if (iosPadPanel_pPanel && !iosPadPanel_pPanel.hidden) [iosPadPanel_pPanel refresh];
    }
}

void iosPadPanel_HoldRing(int kind, float progress)
{
    @autoreleasepool {
        UIView* host = iosPadPanel_Host();
        if (kind == IOSPAD_RING_NONE || !host) {
            iosPadPanel_pRing.hidden = YES;
            return;
        }
        const CGFloat r = IOSPADPANEL_RING_R;
        if (!iosPadPanel_pRing) {
            iosPadPanel_pRing = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 2.0 * r + 120.0, 2.0 * r + 22.0)];
            iosPadPanel_pRing.userInteractionEnabled = NO;
            CAShapeLayer* back = [[CAShapeLayer alloc] init];
            back.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(r + 60.0, r) radius:r startAngle:0.0 endAngle:2.0 * M_PI clockwise:YES].CGPath;
            back.fillColor = [UIColor colorWithWhite:0.0 alpha:0.5].CGColor;
            back.strokeColor = [UIColor colorWithWhite:1.0 alpha:0.35].CGColor;
            back.lineWidth = 3.0;
            [iosPadPanel_pRing.layer addSublayer:back];
            iosPadPanel_pRingLayer = [[CAShapeLayer alloc] init];
            iosPadPanel_pRingLayer.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(r + 60.0, r) radius:r startAngle:-M_PI_2 endAngle:1.5 * M_PI clockwise:YES].CGPath;
            iosPadPanel_pRingLayer.fillColor = [UIColor clearColor].CGColor;
            iosPadPanel_pRingLayer.strokeColor = iosPadPanel_Gold(1.0).CGColor;
            iosPadPanel_pRingLayer.lineWidth = 4.0;
            iosPadPanel_pRingLayer.lineCap = kCALineCapRound;
            [iosPadPanel_pRing.layer addSublayer:iosPadPanel_pRingLayer];
            iosPadPanel_pRingLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, r - 14.0, 2.0 * r + 120.0, 28.0)];
            iosPadPanel_pRingLabel.textColor = [UIColor whiteColor];
            iosPadPanel_pRingLabel.font = [UIFont boldSystemFontOfSize:10.0];
            iosPadPanel_pRingLabel.numberOfLines = 2;
            iosPadPanel_pRingLabel.textAlignment = NSTextAlignmentCenter;
            [iosPadPanel_pRing addSubview:iosPadPanel_pRingLabel];
        }
        const UIEdgeInsets in = host.safeAreaInsets;
        iosPadPanel_pRing.center = CGPointMake(host.bounds.size.width * 0.5, MAX(in.top, 8.0) + IOSPADPANEL_RING_TOP + r);
        iosPadPanel_pRingLabel.text = kind == IOSPAD_RING_LOAD ? @"QUICK\nLOAD" : @"QUICK\nSAVE";
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        iosPadPanel_pRingLayer.strokeEnd = MAX(0.0f, MIN(1.0f, progress));
        [CATransaction commit];
        iosPadPanel_OnTop(iosPadPanel_pRing, host);
        iosPadPanel_pRing.hidden = NO;
    }
}

void iosPadPanel_SetPrompt(const char* pText)
{
    @autoreleasepool {
        UIView* host = iosPadPanel_Host();
        if (!pText || !host) {
            iosPadPanel_pPrompt.hidden = YES;
            return;
        }
        if (!iosPadPanel_pPrompt) {
            iosPadPanel_pPrompt = [[UILabel alloc] initWithFrame:CGRectZero];
            iosPadPanel_pPrompt.userInteractionEnabled = NO;
            iosPadPanel_pPrompt.textColor = [UIColor whiteColor];
            iosPadPanel_pPrompt.font = [UIFont boldSystemFontOfSize:13.0];
            iosPadPanel_pPrompt.textAlignment = NSTextAlignmentCenter;
            iosPadPanel_pPrompt.adjustsFontSizeToFitWidth = YES;
            iosPadPanel_pPrompt.minimumScaleFactor = 0.6;
            iosPadPanel_pPrompt.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.55];
            iosPadPanel_pPrompt.layer.cornerRadius = IOSPADPANEL_PROMPT_H * 0.5;
            iosPadPanel_pPrompt.layer.masksToBounds = YES;
        }
        const CGRect b = host.bounds;
        const UIEdgeInsets in = host.safeAreaInsets;
        const CGFloat left = MAX(in.left, 8.0), right = b.size.width - MAX(in.right, 8.0);
        iosPadPanel_pPrompt.text = [NSString stringWithUTF8String:pText];
        const CGFloat w = MIN(right - left, [iosPadPanel_pPrompt sizeThatFits:CGSizeMake(1000.0, IOSPADPANEL_PROMPT_H)].width + 28.0);
        iosPadPanel_pPrompt.frame = CGRectMake(round((left + right - w) * 0.5),
                                               b.size.height - MAX(in.bottom, 8.0) - IOSPADPANEL_PROMPT_GAP - IOSPADPANEL_PROMPT_H, w, IOSPADPANEL_PROMPT_H);
        iosPadPanel_OnTop(iosPadPanel_pPrompt, host);
        iosPadPanel_pPrompt.hidden = NO;
    }
}

int iosPadPanel_LoadInt(const char* pKey, int valDefault)
{
    @autoreleasepool {
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        NSString* k = [NSString stringWithUTF8String:pKey];
        if (![d objectForKey:k]) return valDefault;
        return (int)[d integerForKey:k];
    }
}

void iosPadPanel_SaveInt(const char* pKey, int val)
{
    @autoreleasepool {
        [[NSUserDefaults standardUserDefaults] setInteger:val forKey:[NSString stringWithUTF8String:pKey]];
    }
}

#endif // TARGET_IOS

// Added: entire file. The keyboard and mouse readout (see iosKbMouse.h): with
// the FPS readout on and a keyboard or mouse connected, two lines near the
// bottom of the screen, in play and in the menus, saying what the app sees:
//   MOUSE / KEYS: connected (GCMouse, GCKeyboard)
//   REL: the game has asked SDL for relative mouse movement (play)
//   LOCK: the game wants the pointer locked (the view controller's
//         prefersPointerLocked) / iOS has locked it (the window scene's
//         pointerLockState; n/a where iOS has none)
// and iosKbMouse's counts: relative movement events (and how far), pointer
// (absolute) movement, clicks, wheel, keys, and how the last touch on the
// touch controls came (a pointer click or a finger).
// Written to work with or without ARC (as iosTouch.m: what it makes lives
// for the whole process).

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include <SDL3/SDL.h>
#include <string.h>

#include "Platform/iOS/iosKbMouse.h"
#include "Platform/iOS/iosTouch.h"

extern SDL_Window* displayWindow;
UIView* iosTouch_OverlayView(void);

// Above the pad's prompt line (iosPadPanel: 28 pt tall, 6 pt over the bottom)
#define IOSKBMOUSEVIEW_BOTTOM 40.0
#define IOSKBMOUSEVIEW_H 30.0
#define IOSKBMOUSEVIEW_MAX_W 560.0
// The text is made again this often at most (seconds)
#define IOSKBMOUSEVIEW_EVERY 0.25

static UILabel* iosKbMouseView_pLabel = nil;
static CFTimeInterval iosKbMouseView_tLast = 0;

int iosKbMouseView_HasMouse(void)
{
    return [GCMouse mice].count > 0;
}

int iosKbMouseView_HasKeyboard(void)
{
    return [GCKeyboard coalescedKeyboard] != nil;
}

static UIWindow* iosKbMouseView_Window(void)
{
    if (!displayWindow) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(displayWindow);
    return (__bridge UIWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
}

void iosKbMouseView_Tick(void)
{
    @autoreleasepool {
        const int bFps = iosTouch_IsFpsShown();
        const int bMouse = bFps && iosKbMouseView_HasMouse(), bKeys = bFps && iosKbMouseView_HasKeyboard();
        // (a pointer click with no GCMouse behind it -- an iPhone's AssistiveTouch? -- shows it too)
        const int bClicks = bFps && iosKbMouse_SawPointerClick();
        if (!bMouse && !bKeys && !bClicks) {
            if (iosKbMouseView_pLabel && !iosKbMouseView_pLabel.hidden) iosKbMouseView_pLabel.hidden = YES;
            return;
        }
        UIWindow* w = iosKbMouseView_Window();
        UIView* host = w.rootViewController.view ? w.rootViewController.view : w;
        if (!host) return;
        if (!iosKbMouseView_pLabel) {
            UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, IOSKBMOUSEVIEW_MAX_W, IOSKBMOUSEVIEW_H)];
            l.textAlignment = NSTextAlignmentCenter;
            l.numberOfLines = 2;
            l.adjustsFontSizeToFitWidth = YES;
            l.minimumScaleFactor = 0.6;
            l.textColor = [UIColor colorWithWhite:1.0 alpha:0.9];
            l.font = [UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightSemibold];
            l.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
            l.layer.cornerRadius = 6.0;
            l.clipsToBounds = YES;
            l.userInteractionEnabled = NO; // (never in the way of a touch or a click)
            iosKbMouseView_pLabel = l;
        }
        UILabel* l = iosKbMouseView_pLabel;
        if (l.superview != host) {
            // (under the touch controls, which keep themselves on top: it
            // shows through them, and wholly while they are hidden)
            [l removeFromSuperview];
            UIView* ov = iosTouch_OverlayView();
            if (ov && ov.superview == host) [host insertSubview:l belowSubview:ov];
            else [host addSubview:l];
        }
        l.hidden = NO;

        const CFTimeInterval now = CACurrentMediaTime();
        if (now - iosKbMouseView_tLast >= IOSKBMOUSEVIEW_EVERY || !l.text) {
            iosKbMouseView_tLast = now;
            UIViewController* vc = w.rootViewController;
            const char* pWant = vc ? (vc.prefersPointerLocked ? "yes" : "no") : "-";
            UIPointerLockState* pLock = w.windowScene.pointerLockState;
            const char* pLocked = pLock ? (pLock.locked ? "yes" : "no") : "n/a";
            const int bRel = displayWindow && SDL_GetWindowRelativeMouseMode(displayWindow);
            char counts[192];
            iosKbMouse_DiagText(counts, sizeof(counts));
            l.text = [NSString stringWithFormat:@"MOUSE %s  KEYS %s  RELMODE %s  LOCK wanted %s, locked %s%s\n%s",
                      bMouse ? "yes" : "no", bKeys ? "yes" : "no", bRel ? "on" : "off", pWant, pLocked,
                      (!bMouse && bClicks) ? "  CLICKS WITHOUT A MOUSE" : "", counts];
        }

        const CGRect B = host.bounds;
        const UIEdgeInsets in = host.safeAreaInsets;
        const CGFloat wid = MIN(IOSKBMOUSEVIEW_MAX_W, B.size.width - in.left - in.right - 16.0);
        l.frame = CGRectMake(floor((B.size.width - wid) * 0.5), B.size.height - in.bottom - IOSKBMOUSEVIEW_BOTTOM - IOSKBMOUSEVIEW_H, wid, IOSKBMOUSEVIEW_H);
    }
}

#endif // TARGET_IOS

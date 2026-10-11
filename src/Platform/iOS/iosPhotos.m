// The CAMERA button's photos: adds the touch overlay to the HUD shot, then
// saves both to Photos (add-only access), or to the app's Files folder
// (Screenshots) if that access is off or saving fails. Built with ARC
// (config_platform_deps.cmake), unlike the rest of the iOS code.

#ifdef TARGET_IOS

#import <UIKit/UIKit.h>
#import <Photos/Photos.h>
#import <ImageIO/ImageIO.h>

#include "Platform/iOS/iosCapture.h"
#include "Platform/iOS/iosPhotos.h"
#include "Platform/iOS/iosGame.h"

// iosTouch.m
UIView* iosTouch_OverlayView(void);
void iosTouch_CameraFeedback(int bBurst);

static void iosPhotos_FreeBytes(void* info, const void* data, size_t size)
{
    free((void*)data);
}

// A CGImage over w x h RGBA bytes (top row first, opaque); it frees them
static CGImageRef iosPhotos_MakeImage(unsigned char* p, int w, int h)
{
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, p, (size_t)w * h * 4, iosPhotos_FreeBytes);
    if (!prov) {
        free(p);
        return NULL;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs,
                                   kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast,
                                   prov, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(prov);
    return img;
}

static NSData* iosPhotos_Png(CGImageRef img)
{
    NSMutableData* data = [NSMutableData data];
    CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)@"public.png", 1, NULL);
    if (!dst) return nil;
    CGImageDestinationAddImage(dst, img, NULL);
    BOOL bOk = CGImageDestinationFinalize(dst);
    CFRelease(dst);
    return bOk ? data : nil;
}

// What the photos are called: "OpenJKDF2 2026-10-11 14.05.09 clean.png"
static NSString* iosPhotos_BaseName(NSDate* when)
{
    NSDateFormatter* f = [[NSDateFormatter alloc] init];
    f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    f.dateFormat = @"yyyy-MM-dd HH.mm.ss";
    NSString* app = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleDisplayName"];
    if (![app isKindOfClass:[NSString class]] || !app.length) app = @"OpenJKDF2";
    NSString* base = [NSString stringWithFormat:@"%@ %@", app, [f stringFromDate:when]];
    // A second capture in the same second gets "-2", "-3"... so no file
    // overwrites another (main thread only)
    static NSString* lastBase = nil;
    static int sameSecond = 0;
    if (lastBase && [lastBase isEqualToString:base]) {
        sameSecond++;
    }
    else {
        lastBase = base;
        sameSecond = 1;
    }
    return sameSecond > 1 ? [NSString stringWithFormat:@"%@-%d", base, sameSecond] : base;
}

// One line on the game's message line once nothing more is being saved (and
// no burst is still going), counting everything since the last line: so a
// burst says "10 photos saved" once, not ten times. Main thread only.
static int iosPhotos_numPending = 0;
static int iosPhotos_bBatch = 0;
static int iosPhotos_numToPhotos = 0, iosPhotos_numToFiles = 0, iosPhotos_numFailed = 0;
static int iosPhotos_bPhotosOff = 0;
static int iosPhotos_burstShot = 0; // numbers a burst's photos, so they sort in order

static void iosPhotos_Report(void)
{
    char msg[128];
    int n = 0;
    if (iosPhotos_numPending > 0 || iosPhotos_bBatch) return;
    msg[0] = 0;
    if (iosPhotos_numToPhotos)
        n += snprintf(msg + n, sizeof(msg) - n, iosPhotos_numToPhotos == 1 ? "1 photo saved" : "%d photos saved", iosPhotos_numToPhotos);
    if (iosPhotos_numToFiles && n < (int)sizeof(msg))
        n += snprintf(msg + n, sizeof(msg) - n, "%s%d saved to Files (%s)", n ? ", " : "", iosPhotos_numToFiles,
                      iosPhotos_bPhotosOff ? "Photos access is off" : "Photos didn't take them");
    if (iosPhotos_numFailed && n < (int)sizeof(msg))
        n += snprintf(msg + n, sizeof(msg) - n, "%s%d couldn't be saved", n ? ", " : "", iosPhotos_numFailed);
    if (!msg[0]) return;
    iosGame_ShowMessage(msg);
    iosPhotos_numToPhotos = iosPhotos_numToFiles = iosPhotos_numFailed = 0;
    iosPhotos_bPhotosOff = 0;
}

// One capture's photos are saved (toPhotos), or in Files (toFiles), or not
static void iosPhotos_Finish(int toPhotos, int toFiles, int failed, int bPhotosOff)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        iosPhotos_numToPhotos += toPhotos;
        iosPhotos_numToFiles += toFiles;
        iosPhotos_numFailed += failed;
        if (bPhotosOff) iosPhotos_bPhotosOff = 1;
        if (iosPhotos_numPending > 0) iosPhotos_numPending--;
        iosCapture_Done();
        iosPhotos_Report();
    });
}

void iosPhotos_SetBatch(int bOn)
{
    iosPhotos_bBatch = bOn != 0;
    if (bOn) iosPhotos_burstShot = 0;
    iosPhotos_Report();
}

// The PNGs into Documents/Screenshots (shown in the Files app)
static int iosPhotos_SaveToFiles(NSArray<NSData*>* aData, NSArray<NSString*>* aName)
{
    NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    if (!docs) return 0;
    NSString* dir = [docs stringByAppendingPathComponent:@"Screenshots"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    int n = 0;
    for (NSUInteger i = 0; i < aData.count; i++) {
        if ([aData[i] writeToFile:[dir stringByAppendingPathComponent:aName[i]] atomically:YES]) n++;
    }
    return n;
}

// (numBad: photos of this capture already lost, counted with the rest)
static void iosPhotos_Save(NSArray<NSData*>* aData, NSArray<NSString*>* aName, NSDate* when, int numBad)
{
    int n = (int)aData.count;
    PHAuthorizationStatus st = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
    if (st != PHAuthorizationStatusAuthorized && st != PHAuthorizationStatusLimited) {
        int k = iosPhotos_SaveToFiles(aData, aName);
        iosPhotos_Finish(0, k, n - k + numBad, 1);
        return;
    }
    [[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{
        for (NSUInteger i = 0; i < aData.count; i++) {
            PHAssetCreationRequest* req = [PHAssetCreationRequest creationRequestForAsset];
            PHAssetResourceCreationOptions* opt = [[PHAssetResourceCreationOptions alloc] init];
            opt.originalFilename = aName[i];
            [req addResourceWithType:PHAssetResourceTypePhoto data:aData[i] options:opt];
            req.creationDate = when;
        }
    } completionHandler:^(BOOL bOk, NSError* err) {
        if (bOk) {
            iosPhotos_Finish(n, 0, numBad, 0);
        }
        else {
            int k = iosPhotos_SaveToFiles(aData, aName);
            iosPhotos_Finish(0, k, n - k + numBad, 0);
        }
    }];
}

void iosPhotos_SaveCapture(unsigned char* pClean, unsigned char* pHud, int w, int h, int kinds, int bBurst)
{
    @autoreleasepool {
        iosPhotos_numPending++;
        CGImageRef clean = pClean ? iosPhotos_MakeImage(pClean, w, h) : NULL;
        CGImageRef hud = pHud ? iosPhotos_MakeImage(pHud, w, h) : NULL;
        CGImageRef hudBtns = NULL;
        if (hud && (kinds & IOSCAPTURE_HUDBTNS)) {
            // The touch overlay on top of the game's frame, as the player sees
            // it, in this same frame. (UIKit views aren't in the GL frame.)
            UIView* overlay = iosTouch_OverlayView();
            UIGraphicsImageRendererFormat* fmt = [UIGraphicsImageRendererFormat preferredFormat];
            fmt.scale = 1.0;
            fmt.opaque = YES;
            fmt.preferredRange = UIGraphicsImageRendererFormatRangeStandard; // 8-bit sRGB, as the others
            UIGraphicsImageRenderer* r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(w, h) format:fmt];
            UIImage* out = [r imageWithActions:^(UIGraphicsImageRendererContext* ctx) {
                CGContextRef c = ctx.CGContext;
                [[UIImage imageWithCGImage:hud] drawInRect:CGRectMake(0, 0, w, h)];
                if (overlay && !overlay.hidden && overlay.bounds.size.width > 0 && overlay.bounds.size.height > 0) {
                    CGContextSaveGState(c);
                    CGContextScaleCTM(c, w / overlay.bounds.size.width, h / overlay.bounds.size.height);
                    [overlay.layer renderInContext:c];
                    CGContextRestoreGState(c);
                }
            }];
            hudBtns = CGImageRetain(out.CGImage);
        }

        // What to save, in this order: clean, HUD, HUD with the buttons
        CGImageRef aImg[3] = { NULL, NULL, NULL };
        int n = 0, numMissing = 0;
        if (kinds & IOSCAPTURE_CLEAN) { if (clean) aImg[0] = CGImageRetain(clean); else numMissing++; }
        if (kinds & IOSCAPTURE_HUD) { if (hud) aImg[1] = CGImageRetain(hud); else numMissing++; }
        if (kinds & IOSCAPTURE_HUDBTNS) { if (hudBtns) aImg[2] = CGImageRetain(hudBtns); else numMissing++; }
        CGImageRelease(clean);
        CGImageRelease(hud);
        CGImageRelease(hudBtns);
        for (int i = 0; i < 3; i++) n += aImg[i] != NULL;
        if (!n) {
            iosPhotos_Finish(0, 0, numMissing ? numMissing : 1, 0);
            return;
        }

        // The flash, the shutter and a tap, now the frame is taken, so none
        // of them is in it (lighter for each shot of a burst)
        iosTouch_CameraFeedback(bBurst);

        NSDate* when = [NSDate date];
        NSString* base = iosPhotos_BaseName(when);
        if (bBurst)
            base = [base stringByAppendingFormat:@" burst %02d", ++iosPhotos_burstShot];
        // (Blocks can't take C arrays)
        CGImageRef img0 = aImg[0], img1 = aImg[1], img2 = aImg[2];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSMutableArray<NSData*>* aData = [NSMutableArray array];
            NSMutableArray<NSString*>* aName = [NSMutableArray array];
            int numBad = numMissing;
            CGImageRef aJob[3] = { img0, img1, img2 };
            NSString* aJobSuffix[3] = { @"clean", @"hud", @"hud+buttons" };
            for (int i = 0; i < 3; i++) {
                if (!aJob[i]) continue;
                @autoreleasepool {
                    NSData* png = iosPhotos_Png(aJob[i]);
                    if (png) {
                        [aData addObject:png];
                        [aName addObject:bBurst ? [base stringByAppendingString:@".png"]
                                                : [NSString stringWithFormat:@"%@ %@.png", base, aJobSuffix[i]]];
                    }
                    else {
                        numBad++;
                    }
                }
                CGImageRelease(aJob[i]);
            }
            if (!aData.count) {
                iosPhotos_Finish(0, 0, numBad, 0);
                return;
            }
            iosPhotos_Save(aData, aName, when, numBad);
        });
    }
}

int iosPhotos_AskAccess(void (*done)(void))
{
    if ([PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly] != PHAuthorizationStatusNotDetermined)
        return 0;
    [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly handler:^(PHAuthorizationStatus st) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (done) done();
        });
    }];
    return 1;
}

#endif // TARGET_IOS

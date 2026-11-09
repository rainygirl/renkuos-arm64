// Let QEMU's cocoa display cover the whole screen in full screen, including
// the strip either side of a MacBook's camera notch, while staying a normal
// macOS full-screen window: its own Space, with Cmd-Tab, Mission Control and
// three-finger swipes reaching other apps.
//
// QEMU 10's ui/cocoa.m uses macOS native full screen
// (-[NSWindow toggleFullScreen:]), and macOS keeps a native full-screen
// window below the camera housing: 1470x923 of a 1470x956 pt screen, whatever
// the window asks for (-setFrame: after entering, -constrainFrameRect:toScreen:
// and a full-size content view were all tried and ignored). A borderless
// window over the menu bar does cover the housing, but is not full screen:
// no Space of its own, and other apps cannot be brought in front of it.
//
// What does both: a window with NSWindowCollectionBehaviorFullScreenAuxiliary
// lives in the full-screen window's Space and may cover the whole screen. So
// once QEMU's window has entered full screen, its view (QemuCocoaView) moves
// into such a borderless window, made the size of the screen; before full
// screen ends it moves back. The full-screen window itself stays underneath
// and keeps the Space. Everything QEMU learnt from its window delegate is
// forwarded: a resize updates the guest's bounds and size, losing key focus
// releases keys and the mouse, and Cmd-F still leaves full screen.
//
// What the guest is offered: QEMU_NOTCH_SIZE=<w>x<h> (run-qemu-renku-arm64.sh
// sets it to the panel's own resolution, 2560x1664 on a 13" MacBook
// Air) is the resolution full screen proposes instead of the screen's size in
// points, together with the display's real physical size in the EDID. RenkuOS
// does the Retina part itself: R Retina's rretina_resize takes the proposed
// size at host_scale 1 (UI scale Automatic), and app_server picks the UI
// scale from the EDID's pixel density.
//
// app_server picks that scale once, when it starts, and the guest reaches it
// in a few seconds - sooner than a QEMU started from a background shell gets
// into full screen, if it does at all (macOS ignores the full-screen request
// of an app that is not active). That boot saw the first, 640x416 pt window:
// a 13x8 cm display, 516 dpi, the largest UI scale. With
// QEMU_NOTCH_QMP=<qmp socket> (QEMU started with -S) the guest is held until
// full screen has proposed the panel's size, then started with QMP "cont" --
// or after 30 s regardless; and an app that was started in the background
// enters full screen when it is first brought forward.
//
// Loaded with DYLD_INSERT_LIBRARIES.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

@interface QNOverlay : NSWindow
@property(weak) NSWindow* fullScreenWindow;
@end
@implementation QNOverlay
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
// Cmd-F calls [[cocoaView window] toggleFullScreen:].
- (void)toggleFullScreen:(id)sender { [self.fullScreenWindow toggleFullScreen:sender]; }
@end

static IMP gSetContentSize, gSetContentAspectRatio, gUpdateUIInfoLocked, gViewFrame,
    gScreenFrame;
static NSSize gSize;
static __thread int gInUIInfo;
static QNOverlay* gOverlay;
static NSView* gView;
static BOOL gStarted;

// Starts a guest QEMU was told to hold (-S): QMP capabilities, then "cont".
static void StartGuest(void) {
    if (gStarted)
        return;
    gStarted = YES;
    const char* path = getenv("QEMU_NOTCH_QMP");
    if (path == NULL || path[0] == '\0')
        return;
    NSString* socketPath = [NSString stringWithUTF8String:path];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return;
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        strlcpy(address.sun_path, socketPath.UTF8String, sizeof(address.sun_path));
        if (connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0) {
            const char* commands = "{\"execute\":\"qmp_capabilities\"}\n"
                "{\"execute\":\"cont\"}\n";
            write(fd, commands, strlen(commands));
            char reply[512];
            // greeting, capabilities and cont each answer one line
            for (int lines = 0; lines < 3;) {
                ssize_t n = read(fd, reply, sizeof(reply));
                if (n <= 0)
                    break;
                for (ssize_t i = 0; i < n; i++)
                    lines += reply[i] == '\n';
            }
        } else {
            fprintf(stderr, "qemu-notch: could not start the guest over %s\n",
                socketPath.UTF8String);
        }
        close(fd);
    });
}

static BOOL IsOverlay(NSWindow* window) {
    return window != nil && window == gOverlay;
}

static NSRect ScreenFrame(NSWindow* window) {
    NSScreen* screen = window.screen ?: [NSScreen mainScreen];
    return screen.frame;
}

// QEMU's resizeWindow runs on every guest mode change and sizes the window to
// the guest; the overlay stays the size of the screen and the view scales.
static void SetContentSize(NSWindow* self, SEL _cmd, NSSize size) {
    if (IsOverlay(self)) {
        [self setFrame:ScreenFrame(self) display:YES];
        return;
    }
    ((void (*)(id, SEL, NSSize))gSetContentSize)(self, _cmd, size);
}

static void SetContentAspectRatio(NSWindow* self, SEL _cmd, NSSize ratio) {
    if (IsOverlay(self))
        ratio = NSZeroSize;
    ((void (*)(id, SEL, NSSize))gSetContentAspectRatio)(self, _cmd, ratio);
}

static void UpdateUIInfoLocked(NSView* self, SEL _cmd) {
    gInUIInfo++;
    ((void (*)(id, SEL))gUpdateUIInfoLocked)(self, _cmd);
    gInUIInfo--;
}

// Only while updateUIInfoLocked runs, and only in the overlay: drawing, layout
// and input see the real frames. The view (filling the screen) reads as the
// proposed size; the screen reads the same, so QEMU's
// width_mm = frame / screen * physical stays the panel's physical size.
static BOOL Proposing(NSWindow* window) {
    return gInUIInfo > 0 && gSize.width > 0 && IsOverlay(window);
}

static NSRect ViewFrame(NSView* self, SEL _cmd) {
    NSRect frame = ((NSRect (*)(id, SEL))gViewFrame)(self, _cmd);
    if (Proposing(self.window))
        frame.size = gSize;
    return frame;
}

static NSRect ScreenFrameOverride(NSScreen* self, SEL _cmd) {
    NSRect frame = ((NSRect (*)(id, SEL))gScreenFrame)(self, _cmd);
    if (gOverlay != nil && Proposing(gOverlay) && self == gOverlay.screen)
        frame.size = gSize;
    return frame;
}

static void Refresh(NSView* view) {
    if ([view respondsToSelector:@selector(updateBounds)])
        [view performSelector:@selector(updateBounds)];
    if ([view respondsToSelector:@selector(updateUIInfo)])
        [view performSelector:@selector(updateUIInfo)];
}

static void EnterOverlay(NSWindow* full) {
    NSView* view = full.contentView;
    if (gOverlay != nil || view == nil || ![view isKindOfClass:objc_getClass("QemuCocoaView")])
        return;
    NSRect frame = ScreenFrame(full);
    QNOverlay* overlay = [[QNOverlay alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    overlay.releasedWhenClosed = NO;
    overlay.fullScreenWindow = full;
    overlay.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
    overlay.level = NSMainMenuWindowLevel + 1;
    overlay.backgroundColor = NSColor.blackColor;
    overlay.hasShadow = NO;
    overlay.acceptsMouseMovedEvents = YES;
    gOverlay = overlay;
    gView = view;

    NSView* placeholder = [[NSView alloc] initWithFrame:view.frame];
    full.contentView = placeholder;
    overlay.contentView = view;
    [overlay setFrame:frame display:YES];
    [overlay makeKeyAndOrderFront:nil];
    [overlay makeFirstResponder:view];
    Refresh(view);
    // the guest's first look at the display is the panel's size
    StartGuest();
}

static void LeaveOverlay(NSWindow* full) {
    if (gOverlay == nil)
        return;
    NSView* view = gView;
    QNOverlay* overlay = gOverlay;
    gOverlay = nil;
    gView = nil;
    overlay.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    [overlay orderOut:nil];
    full.contentView = view;
    [full makeKeyAndOrderFront:nil];
    [full makeFirstResponder:view];
    Refresh(view);
}

__attribute__((constructor)) static void Init(void) {
    Class window = [NSWindow class];
    gSetContentSize = method_setImplementation(
        class_getInstanceMethod(window, @selector(setContentSize:)),
        (IMP)SetContentSize);
    gSetContentAspectRatio = method_setImplementation(
        class_getInstanceMethod(window, @selector(setContentAspectRatio:)),
        (IMP)SetContentAspectRatio);

    const char* panel = getenv("QEMU_NOTCH_SIZE");
    int w = 0, h = 0;
    if (panel != NULL && sscanf(panel, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
        gSize = NSMakeSize(w, h);

    Class view = objc_getClass("QemuCocoaView");
    Method update = view ? class_getInstanceMethod(view, @selector(updateUIInfoLocked)) : NULL;
    if (update != NULL) {
        gUpdateUIInfoLocked = method_setImplementation(update, (IMP)UpdateUIInfoLocked);
        // An override on QemuCocoaView alone, not on every NSView.
        Method frame = class_getInstanceMethod([NSView class], @selector(frame));
        gViewFrame = method_getImplementation(frame);
        class_addMethod(view, @selector(frame), (IMP)ViewFrame, method_getTypeEncoding(frame));
        gScreenFrame = method_setImplementation(
            class_getInstanceMethod([NSScreen class], @selector(frame)),
            (IMP)ScreenFrameOverride);
    } else {
        fprintf(stderr, "qemu-notch: no -[QemuCocoaView updateUIInfoLocked]\n");
    }

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    if (getenv("QEMU_NOTCH_QMP") != NULL) {
        // full-screen=on is ignored while the app is not active, and an app
        // started in the background cannot activate itself; enter full screen
        // when it is first brought forward. The guest waits for that, 30 s at
        // most.
        [center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil
                             queue:nil
                        usingBlock:^(NSNotification* note) {
                            if (gOverlay != nil || gStarted)
                                return;
                            for (NSWindow* window in NSApp.windows) {
                                if ([window.contentView isKindOfClass:objc_getClass("QemuCocoaView")]
                                    && (window.styleMask & NSWindowStyleMaskFullScreen) == 0) {
                                    [window toggleFullScreen:nil];
                                    break;
                                }
                            }
                        }];
        [center addObserverForName:NSApplicationDidFinishLaunchingNotification object:nil
                             queue:nil
                        usingBlock:^(NSNotification* note) {
                            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC),
                                dispatch_get_main_queue(), ^{ StartGuest(); });
                        }];
    }
    [center addObserverForName:NSWindowDidEnterFullScreenNotification object:nil queue:nil
                    usingBlock:^(NSNotification* note) { EnterOverlay(note.object); }];
    [center addObserverForName:NSWindowWillExitFullScreenNotification object:nil queue:nil
                    usingBlock:^(NSNotification* note) { LeaveOverlay(note.object); }];
    [center addObserverForName:NSWindowDidResizeNotification object:nil queue:nil
                    usingBlock:^(NSNotification* note) {
                        if (IsOverlay(note.object)) Refresh(gView);
                    }];
    // QEMU releases held keys and the mouse grab when its window loses focus.
    [center addObserverForName:NSWindowDidResignKeyNotification object:nil queue:nil
                    usingBlock:^(NSNotification* note) {
                        if (!IsOverlay(note.object)) return;
                        id<NSWindowDelegate> delegate = gOverlay.fullScreenWindow.delegate;
                        if ([delegate respondsToSelector:@selector(windowDidResignKey:)])
                            [delegate windowDidResignKey:note];
                    }];
    // Coming back to the Space activates the full-screen window; hand focus on.
    [center addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:nil
                    usingBlock:^(NSNotification* note) {
                        if (gOverlay != nil && note.object == gOverlay.fullScreenWindow)
                            [gOverlay makeKeyAndOrderFront:nil];
                    }];
    if (gSize.width > 0)
        fprintf(stderr, "qemu-notch: full screen proposes %dx%d pixels\n", w, h);
}

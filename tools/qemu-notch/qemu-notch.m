// Let QEMU's cocoa display cover the whole screen in full screen, including
// the strip either side of a MacBook's camera notch.
//
// QEMU 10's ui/cocoa.m uses macOS native full screen
// (-[NSWindow toggleFullScreen:]), and macOS places a native full-screen
// window below the camera housing whatever the app asks for: measured with
// CGWindowListCopyWindowInfo on a 1470x956 pt screen, QEMU's window sat at
// y=33 with a height of 923, also when QEMU was made to ask for the whole
// frame. So this replaces native full screen in the QEMU process with the
// older kind: the same window, made borderless, raised above the menu bar,
// with the menu bar and Dock hidden, and given the screen's whole frame.
// QEMU's own windowDidResize: then tells the guest the new size, and a guest
// that follows it (RenkuOS's virtio_gpu does) fills the screen 1:1.
//
// Loaded with DYLD_INSERT_LIBRARIES. Cmd-F (QEMU's "Enter Fullscreen") and
// -display cocoa,full-screen=on both go through toggleFullScreen:, so both
// get this; pressing Cmd-F again restores the window.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

static char kSavedKey;

@interface QNSavedState : NSObject
@property NSRect frame;
@property NSWindowStyleMask style;
@property NSWindowLevel level;
@property NSApplicationPresentationOptions options;
@end
@implementation QNSavedState
@end

static void Enter(NSWindow* window) {
    QNSavedState* saved = [QNSavedState new];
    saved.frame = window.frame;
    saved.style = window.styleMask;
    saved.level = window.level;
    saved.options = NSApp.presentationOptions;
    objc_setAssociatedObject(window, &kSavedKey, saved, OBJC_ASSOCIATION_RETAIN);

    NSScreen* screen = window.screen ?: [NSScreen mainScreen];
    NSApp.presentationOptions = NSApplicationPresentationHideDock |
                                NSApplicationPresentationHideMenuBar;
    // Resizable stays set: QEMU's resizeWindow keeps the window's size only
    // for a resizable window, and sets it to the guest's pixel size otherwise.
    window.styleMask = NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable;
    window.level = NSMainMenuWindowLevel + 1;
    window.hasShadow = NO;
    [window setContentAspectRatio:NSZeroSize];
    [window setFrame:screen.frame display:YES];
    [window makeKeyAndOrderFront:nil];
}

static void Leave(NSWindow* window, QNSavedState* saved) {
    objc_setAssociatedObject(window, &kSavedKey, nil, OBJC_ASSOCIATION_RETAIN);
    window.styleMask = saved.style;
    window.level = saved.level;
    window.hasShadow = YES;
    NSApp.presentationOptions = saved.options;
    [window setFrame:saved.frame display:YES];
}

static void ToggleFullScreen(NSWindow* self, SEL _cmd, id sender) {
    QNSavedState* saved = objc_getAssociatedObject(self, &kSavedKey);
    if (saved != nil)
        Leave(self, saved);
    else
        Enter(self);
}

// A borderless window cannot become key by default, and QEMU needs key
// events.
static BOOL CanBecomeKey(NSWindow* self, SEL _cmd) {
    return YES;
}

__attribute__((constructor)) static void Init(void) {
    method_setImplementation(
        class_getInstanceMethod([NSWindow class], @selector(toggleFullScreen:)),
        (IMP)ToggleFullScreen);
    method_setImplementation(
        class_getInstanceMethod([NSWindow class], @selector(canBecomeKeyWindow)),
        (IMP)CanBecomeKey);
}

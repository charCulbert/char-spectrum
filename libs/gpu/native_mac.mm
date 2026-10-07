#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "native.h"
#include <optional>

// An NSView backed by a CAMetalLayer, added to the host's view. It forwards the
// mouse and keys and runs a 60 Hz timer on the main run loop to draw frames.
@interface GpuView : NSView
@property(nonatomic) gpu::platform::Callbacks *callbacks;
@property(nonatomic) BOOL locked;
@end

@implementation GpuView
{
    NSPoint _position; // pixels; keeps moving while the pointer is locked
}
- (BOOL)isFlipped { return YES; } // top-left origin, like the shader
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    self.layer.contentsScale = self.window ? self.window.backingScaleFactor : 1.0;
}
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow]; // hosts may add us to a window after creating us
    if (self.window) self.layer.contentsScale = self.window.backingScaleFactor;
}

static unsigned modifiers(NSEvent *event)
{
    const NSEventModifierFlags flags = event.modifierFlags;
    return (flags & NSEventModifierFlagShift ? gpu::View::shift : 0u) | (flags & NSEventModifierFlagControl ? gpu::View::control : 0u)
         | (flags & NSEventModifierFlagOption ? gpu::View::alt : 0u) | (flags & NSEventModifierFlagCommand ? gpu::View::command : 0u);
}

- (void)send:(gpu::View::Pointer)type event:(NSEvent *)event
{
    const CGFloat scale = self.layer.contentsScale;
    if (_locked && type == gpu::View::Pointer::move)
    {
        // The cursor is frozen: move by the mouse's own deltas (points, y down).
        _position.x += event.deltaX * scale;
        _position.y += event.deltaY * scale;
    }
    else if (!_locked)
    {
        const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
        _position = NSMakePoint(p.x * scale, p.y * scale);
    }
    if (_callbacks) _callbacks->pointer({type, float(_position.x), float(_position.y), modifiers(event)});
}
- (void)mouseDown:(NSEvent *)event
{
    [self.window makeFirstResponder:self]; // for keys
    [self send:gpu::View::Pointer::down event:event];
}
- (void)scrollWheel:(NSEvent *)event
{
    if (!_callbacks) return;
    // Trackpads report points, wheels lines (notches). Follow the fingers,
    // not "natural scrolling": a swipe away from the user turns things up.
    double notches = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10 : event.scrollingDeltaY;
    if (event.isDirectionInvertedFromDevice) notches = -notches;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat scale = self.layer.contentsScale;
    _callbacks->pointer({gpu::View::Pointer::wheel, float(p.x * scale), float(p.y * scale), modifiers(event), float(notches)});
}
- (void)keyDown:(NSEvent *)event
{
    using Key = gpu::View::Key;
    std::optional<Key> key;
    switch (event.keyCode) // kVK_* from Carbon's Events.h
    {
    case 123: key = Key::left; break;
    case 124: key = Key::right; break;
    case 125: key = Key::down; break;
    case 126: key = Key::up; break;
    case 116: key = Key::pageUp; break;
    case 121: key = Key::pageDown; break;
    case 115: key = Key::home; break;
    case 119: key = Key::end; break;
    case 48: key = Key::tab; break;
    case 36: case 76: key = Key::enter; break;
    case 53: key = Key::escape; break;
    case 51: key = Key::backspace; break;
    case 117: key = Key::deleteForward; break;
    }
    if (_callbacks && key && _callbacks->key(*key, modifiers(event))) return;
    // Typed characters, but not shortcuts or function keys (U+F700 and up).
    NSString *characters = event.characters;
    const bool typed = !key && characters.length > 0 && [characters characterAtIndex:0] >= 0x20
                    && [characters characterAtIndex:0] < 0xF700 && [characters characterAtIndex:0] != 0x7F
                    && !(event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl));
    if (_callbacks && typed && _callbacks->text(characters.UTF8String)) return;
    [super keyDown:event]; // up the responder chain, to the host
}
- (void)mouseDragged:(NSEvent *)event { [self send:gpu::View::Pointer::move event:event]; }
- (void)mouseUp:(NSEvent *)event { [self send:gpu::View::Pointer::up event:event]; }
@end

namespace gpu::platform {

const char *const windowApi = CLAP_WINDOW_API_COCOA;
const bool needsTimer = false;

struct Window
{
    GpuView *view;
    NSTimer *timer;
    Callbacks callbacks;
};

Window *create(const clap_window_t *parent, uint32_t width, uint32_t height, Callbacks callbacks)
{
    NSView *host = (__bridge NSView *)parent->cocoa;
    if (!host) return nullptr;
    auto *window = new Window{nil, nil, std::move(callbacks)};
    window->view = [[GpuView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    window->view.wantsLayer = YES;
    window->view.callbacks = &window->callbacks;
    window->view.layer.contentsScale = host.window ? host.window.backingScaleFactor : 1.0;
    [host addSubview:window->view];
    window->timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 repeats:YES block:^(NSTimer *) {
        window->callbacks.frame();
    }];
    // Common modes: keep drawing while the host is in a modal loop or a drag.
    [[NSRunLoop mainRunLoop] addTimer:window->timer forMode:NSRunLoopCommonModes];
    return window;
}

wgpu::Surface createSurface(const wgpu::Instance &instance, Window *window)
{
    wgpu::SurfaceSourceMetalLayer source;
    source.layer = (__bridge void *)window->view.layer;
    wgpu::SurfaceDescriptor descriptor;
    descriptor.nextInChain = &source;
    return instance.CreateSurface(&descriptor);
}

void lockPointer(Window *window, bool locked)
{
    if (window->view.locked == locked) return;
    window->view.locked = locked;
    // Detach the cursor from the mouse, so it stays where the drag began.
    CGAssociateMouseAndMouseCursorPosition(!locked);
    if (locked) [NSCursor hide];
    else [NSCursor unhide];
}

void destroy(Window *window)
{
    lockPointer(window, false);
    [window->timer invalidate];
    window->view.callbacks = nullptr;
    [window->view removeFromSuperview];
    delete window;
}

void setSize(Window *window, uint32_t width, uint32_t height)
{
    [window->view setFrameSize:NSMakeSize(width, height)];
}

void pixelSize(Window *window, uint32_t &width, uint32_t &height)
{
    const CGFloat scale = window->view.layer.contentsScale;
    width = uint32_t(window->view.bounds.size.width * scale);
    height = uint32_t(window->view.bounds.size.height * scale);
}

float scale(Window *window) { return float(window->view.layer.contentsScale); }

void setVisible(Window *window, bool visible) { [window->view setHidden:!visible]; }

void pumpEvents(Window *) {}

}

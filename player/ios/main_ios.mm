// WZX Player for iOS (iPhone and iPad): a full-screen CAMetalLayer, a Vulkan device through Diligent on MoltenVK
// (Vulkan translated to Metal), the Lit renderer. No ray tracing on iOS (docs/RENDU_PARITE.md).
// Touch: one finger = orbit, pinch = zoom, double tap = default view.
#include "Player.h"

#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <string>

using namespace Diligent;

@interface WZXView : UIView
@end

@implementation WZXView
+ (Class)layerClass {
    return [CAMetalLayer class];
}
@end

@interface WZXViewController : UIViewController
@end

@implementation WZXViewController {
    player::Player _player;
    RefCntAutoPtr<IRenderDevice> _device;
    RefCntAutoPtr<IDeviceContext> _context;
    RefCntAutoPtr<ISwapChain> _swapChain;
    CADisplayLink* _displayLink;
    CGPoint _lastPan;
    CGFloat _lastPinch;
    BOOL _ready;
}

- (void)loadView {
    WZXView* view = [[WZXView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    view.multipleTouchEnabled = YES;
    self.view = view;
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    layer.contentsScale = self.view.contentScaleFactor;

    IEngineFactoryVk* factory = GetEngineFactoryVk();
    EngineVkCreateInfo ci;
    ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
    factory->CreateDeviceAndContextsVk(ci, &_device, &_context);
    if (!_device) {
        [self showError:@"No Vulkan (MoltenVK) device."];
        return;
    }
    SwapChainDesc sc;
    sc.ColorBufferFormat = TEX_FORMAT_BGRA8_UNORM;   // the post pass writes sRGB values itself
    sc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;       // the Lit renderer has its own MSAA depth
    IOSNativeWindow window{(__bridge void*)layer};
    factory->CreateSwapChainVk(_device, _context, sc, window, &_swapChain);
    if (!_swapChain) {
        [self showError:@"Cannot create the swap chain."];
        return;
    }

    NSString* shaders = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"shaders"];
    player::Options opt;
    std::string err;
    if (!_player.init(_device, _context, factory, _swapChain->GetDesc().ColorBufferFormat, shaders.UTF8String, opt, &err)) {
        [self showError:[NSString stringWithUTF8String:err.c_str()]];
        return;
    }
    _ready = YES;

    [self.view addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(pan:)]];
    [self.view addGestureRecognizer:[[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinch:)]];
    UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(doubleTap:)];
    tap.numberOfTapsRequired = 2;
    [self.view addGestureRecognizer:tap];

    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame)];
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
}

- (void)showError:(NSString*)text {
    UILabel* label = [[UILabel alloc] initWithFrame:CGRectInset(self.view.bounds, 40, 40)];
    label.text = text;
    label.numberOfLines = 0;
    label.textColor = [UIColor whiteColor];
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:label];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (!_swapChain) return;
    CGSize s = self.view.bounds.size;
    CGFloat k = self.view.contentScaleFactor;
    Uint32 w = (Uint32)(s.width * k), h = (Uint32)(s.height * k);
    if (w > 0 && h > 0) _swapChain->Resize(w, h);
}

- (void)drawFrame {
    if (!_ready) return;
    const SwapChainDesc& d = _swapChain->GetDesc();
    _player.frame(_swapChain->GetCurrentBackBufferRTV(), d.Width, d.Height);
    _swapChain->Present(1);
}

- (void)pan:(UIPanGestureRecognizer*)g {
    CGPoint p = [g translationInView:self.view];
    if (g.state == UIGestureRecognizerStateBegan) _lastPan = CGPointZero;
    CGFloat k = self.view.contentScaleFactor;
    _player.orbit((float)((p.x - _lastPan.x) * k), (float)((p.y - _lastPan.y) * k));
    _lastPan = p;
}

- (void)pinch:(UIPinchGestureRecognizer*)g {
    if (g.state == UIGestureRecognizerStateBegan) _lastPinch = 1.0;
    if (g.scale > 0) _player.zoom((float)(_lastPinch / g.scale));
    _lastPinch = g.scale;
}

- (void)doubleTap:(UITapGestureRecognizer*)g {
    _player.resetView();
}

@end

@interface WZXAppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation WZXAppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)options {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[WZXViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([WZXAppDelegate class]));
    }
}

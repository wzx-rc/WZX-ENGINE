// WZX Player for iOS (iPhone and iPad): a full-screen CAMetalLayer, a Vulkan device through Diligent on MoltenVK
// (Vulkan translated to Metal), the game, its HUD and the Lit renderer. No ray tracing on iOS (docs/RENDU_PARITE.md).
// Touch: one finger = the mouse of the game (tap = click, drag = drag a house, HUD buttons),
// two fingers = camera (drag to move, pinch to zoom, twist to turn).
#include "LiveHost.h"
#include "Player.h"
#include "app/Json.h"
#include "imgui.h"

#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <sys/utsname.h>

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
    std::unique_ptr<player::Player> _player;   // recreated when WZX Live brings a new game
    player::Options _opt;
    IEngineFactoryVk* _factory;
    std::string _shaders, _bundleDir;
    // WZX Live (the "live" option of the build profile): the editor sends the game it edits over Wi-Fi
    player::LiveHost _live;
    player::LiveConfig _liveCfg;
    BOOL _liveOn;
    float _safe[4];
    RefCntAutoPtr<IRenderDevice> _device;
    RefCntAutoPtr<IDeviceContext> _context;
    RefCntAutoPtr<ISwapChain> _swapChain;
    CADisplayLink* _displayLink;
    CFTimeInterval _lastTime;
    BOOL _ready;
    // touches in pixels, by UITouch
    std::map<void*, CGPoint> _touches;
    // one finger: held back a moment so that a second finger turns it into a camera gesture, not a click
    BOOL _pending, _mouseDown, _twoFingers;
    CGPoint _pendingPos;
    CFTimeInterval _pendingTime;
    // two fingers: previous distance and angle
    BOOL _twoStarted;
    float _prevDist, _prevAngle;
}

- (void)loadView {
    WZXView* view = [[WZXView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    view.multipleTouchEnabled = YES;
    self.view = view;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;   // the game is played in landscape (iPhone and iPad)
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeAll;   // the first swipe from an edge goes to the game
}

- (void)viewDidLoad {
    [super viewDidLoad];
    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    layer.contentsScale = self.view.contentScaleFactor;

    IEngineFactoryVk* factory = GetEngineFactoryVk();
    EngineVkCreateInfo ci;
    ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
    ci.DynamicHeapSize = 32 << 20;   // the DDGI probe captures map the draw constants thousands of times per frame
    ci.DynamicHeapPageSize = 2 << 20;
    factory->CreateDeviceAndContextsVk(ci, &_device, &_context);
    if (!_device) {
        [self showError:@"No Vulkan (MoltenVK) device."];
        return;
    }
    SwapChainDesc sc;
    sc.ColorBufferFormat = TEX_FORMAT_BGRA8_UNORM;   // the post pass writes sRGB values itself
    sc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;       // the Lit renderer has its own MSAA depth
    sc.Usage = SWAP_CHAIN_USAGE_RENDER_TARGET | SWAP_CHAIN_USAGE_COPY_SOURCE;   // WZX Live screenshots read it back
    IOSNativeWindow window{(__bridge void*)layer};
    factory->CreateSwapChainVk(_device, _context, sc, window, &_swapChain);
    if (!_swapChain) {
        [self showError:@"Cannot create the swap chain."];
        return;
    }

    NSString* res = [[NSBundle mainBundle] resourcePath];
    _factory = factory;
    _shaders = [res stringByAppendingPathComponent:@"shaders"].UTF8String;
    _bundleDir = res.UTF8String;
    player::Options opt;
    opt.dataDir = _bundleDir;    // game/ and assets/ are copied into the app
    // the game exported by the editor (game.json, scene.wzx...) when the app has one, else the built-in Studio
    NSString* gameJson = [res stringByAppendingPathComponent:@"game.json"];
    const bool bundled = [[NSFileManager defaultManager] fileExistsAtPath:gameJson];
    opt.scene = bundled ? "game" : "studio";
    opt.uiScale = (float)self.view.contentScaleFactor;
    opt.touch = true;
    // the HUD layout: iPad (Tablet) or iPhone (Phone)
    const bool pad = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
    opt.device = pad ? 1 : 2;
    _opt = opt;
    std::string err;

    // WZX Live: "live": true in game.json (the build profile's option): wait for the editor on the waiting screen
    if (bundled) {
        NSData* data = [NSData dataWithContentsOfFile:gameJson];
        app::Json cfg;
        if (data) app::Json::parse(std::string((const char*)data.bytes, data.length), cfg);
        _liveOn = cfg["live"].type == app::Json::Bool && cfg["live"].b;
    }
    if (_liveOn) {
        NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        struct utsname u;
        uname(&u);
        _liveCfg.liveDir = [docs stringByAppendingPathComponent:@"live"].UTF8String;
        _liveCfg.stateFile = [docs stringByAppendingPathComponent:@"wzx_live_device.json"].UTF8String;
        _liveCfg.deviceType = pad ? "Tablet" : "Phone";
        _liveCfg.model = u.machine;
        _liveCfg.name = [UIDevice currentDevice].name.UTF8String;
        _liveCfg.built = __DATE__ " " __TIME__;
        _liveCfg.hasBundledGame = true;
        if (!_live.start(_liveCfg, &err)) {
            [self showError:[NSString stringWithUTF8String:err.c_str()]];
            return;
        }
        [UIApplication sharedApplication].idleTimerDisabled = YES;   // the waiting screen stays on
        // back from the background: the listening socket may have been closed by iOS, it is opened again
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(willEnterForeground:)
                                                     name:UIApplicationWillEnterForegroundNotification object:nil];
        opt.scene = "empty";
    }
    if (![self startGame:opt.scene dataDir:opt.dataDir error:&err]) {
        [self showError:[NSString stringWithUTF8String:err.c_str()]];
        return;
    }

    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame:)];
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
}

- (void)willEnterForeground:(NSNotification*)note {
    std::string err;
    if (_liveOn && !_live.connected()) _live.start(_liveCfg, &err);
}

// (Re)starts the player: "game" of a data folder, or "empty" (the WZX Live waiting screen). The GPU is drained
// first: the old game's resources go away with its Player.
- (BOOL)startGame:(const std::string&)scene dataDir:(const std::string&)dataDir error:(std::string*)err {
    _ready = NO;
    if (_player) {
        _context->Flush();
        _context->WaitForIdle();
        _player->shutdown();
        _player.reset();
    }
    player::Options o = _opt;
    o.scene = scene;
    o.dataDir = dataDir;
    if (scene == "empty") o.game = false;
    auto p = std::make_unique<player::Player>();
    if (_liveOn) {
        player::LiveHost* live = &_live;
        const float k = o.uiScale;
        float* safe = _safe;
        p->overlay = [live, k, safe] { live->drawOverlay(k, safe[0], safe[1]); };
    }
    if (!p->init(_device, _context, _factory, _swapChain->GetDesc().ColorBufferFormat, _shaders, o, err)) {
        p->shutdown();
        return NO;
    }
    _player = std::move(p);
    ImGui::GetIO().AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    _live.setPlaying(scene != "empty");
    _touches.clear();
    _pending = _mouseDown = _twoFingers = _twoStarted = NO;
    _lastTime = 0;
    _ready = YES;
    [self updateSafeArea];
    return YES;
}

static std::string gameTitle(const std::string& dir) {
    NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:(dir + "/game.json").c_str()]];
    app::Json j;
    if (data) app::Json::parse(std::string((const char*)data.bytes, data.length), j);
    return j["title"].str("WZX Game");
}

// WZX Live, once per frame after Present: the log of the graph, a game received, the buttons of the waiting screen.
- (void)liveTick {
    _live.pumpGameLog(_player ? _player->game() : nullptr);
    std::string err;
    const std::string dir = _liveCfg.liveDir;
    if (_live.takeReload()) {
        const BOOL ok = [self startGame:"game" dataDir:dir error:&err];
        _live.reloaded(ok, gameTitle(dir), ok ? _player->warnings() : std::vector<std::string>(), err);
        if (!ok) [self startGame:"empty" dataDir:dir error:nullptr];
    }
    switch (_live.takeAction()) {
    case player::LiveHost::PlayLast:
        if (![self startGame:"game" dataDir:dir error:&err]) { _live.reloaded(false, "", {}, err); [self startGame:"empty" dataDir:dir error:nullptr]; }
        break;
    case player::LiveHost::PlayBundled:
        if (![self startGame:"game" dataDir:_bundleDir error:&err]) { _live.reloaded(false, "", {}, err); [self startGame:"empty" dataDir:dir error:nullptr]; }
        break;
    case player::LiveHost::ShowWaiting: [self startGame:"empty" dataDir:dir error:nullptr]; break;
    default: break;
    }
}

- (void)showError:(NSString*)text {
    UILabel* label = [[UILabel alloc] initWithFrame:CGRectInset(self.view.bounds, 40, 40)];
    label.text = text;
    label.numberOfLines = 0;
    label.textColor = [UIColor whiteColor];
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:label];
}

- (void)updateSafeArea {
    if (!_ready) return;
    UIEdgeInsets s = self.view.safeAreaInsets;
    CGFloat k = self.view.contentScaleFactor;
    _safe[0] = (float)(s.left * k);
    _safe[1] = (float)(s.top * k);
    _safe[2] = (float)(s.right * k);
    _safe[3] = (float)(s.bottom * k);
    if (_player) _player->setSafeArea(_safe[0], _safe[1], _safe[2], _safe[3]);
}

- (void)viewSafeAreaInsetsDidChange {
    [super viewSafeAreaInsetsDidChange];
    [self updateSafeArea];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self updateSafeArea];
    if (!_swapChain) return;
    CGSize s = self.view.bounds.size;
    CGFloat k = self.view.contentScaleFactor;
    Uint32 w = (Uint32)(s.width * k), h = (Uint32)(s.height * k);
    if (w > 0 && h > 0) _swapChain->Resize(w, h);
}

// --------------------------------------------------------------------------- touch

- (CGPoint)pixels:(UITouch*)t {
    CGPoint p = [t locationInView:self.view];
    CGFloat k = self.view.contentScaleFactor;
    return CGPointMake(p.x * k, p.y * k);
}

- (void)mouseAt:(CGPoint)p {
    ImGui::GetIO().AddMousePosEvent((float)p.x, (float)p.y);
}

- (void)mouseUp {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, false);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // a finger does not hover once lifted
    _mouseDown = NO;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!_ready) return;
    for (UITouch* t in touches) _touches[(__bridge void*)t] = [self pixels:t];
    if (_touches.size() >= 2) {
        // a second finger: the camera takes the gesture (a held-back press is dropped, a drag is ended)
        _pending = NO;
        if (_mouseDown) [self mouseUp];
        _twoFingers = YES;
        _twoStarted = NO;
    } else if (!_twoFingers && !_mouseDown) {
        _pending = YES;
        _pendingPos = _touches.begin()->second;
        _pendingTime = CACurrentMediaTime();
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!_ready) return;
    for (UITouch* t in touches) _touches[(__bridge void*)t] = [self pixels:t];
    if (_mouseDown && _touches.size() == 1) [self mouseAt:_touches.begin()->second];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!_ready) return;
    for (UITouch* t in touches) _touches.erase((__bridge void*)t);
    if (_pending) {   // a tap: press and release at once (ImGui spreads them over the next frames)
        [self mouseAt:_pendingPos];
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        _pending = NO;
        [self mouseUp];
    } else if (_mouseDown) {
        [self mouseUp];
    }
    if (_touches.empty()) _twoFingers = NO;
    if (_touches.size() < 2) _twoStarted = NO;
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if (!_ready) return;
    for (UITouch* t in touches) _touches.erase((__bridge void*)t);
    _pending = NO;
    if (_mouseDown) [self mouseUp];
    if (_touches.empty()) _twoFingers = NO;
}

// --------------------------------------------------------------------------- frame

- (void)drawFrame:(CADisplayLink*)link {
    if (!_ready) return;
    CFTimeInterval now = link.timestamp;
    float dt = _lastTime > 0 ? (float)(now - _lastTime) : 1.0f / 60.0f;
    _lastTime = now;

    // a finger held still or moving: the press goes to the game
    if (_pending && _touches.size() == 1) {
        CGPoint p = _touches.begin()->second;
        float dx = (float)(p.x - _pendingPos.x), dy = (float)(p.y - _pendingPos.y);
        float slop = 10.0f * (float)self.view.contentScaleFactor;
        if (CACurrentMediaTime() - _pendingTime > 0.12 || dx * dx + dy * dy > slop * slop) {
            [self mouseAt:_pendingPos];
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            [self mouseAt:p];
            _pending = NO;
            _mouseDown = YES;
        }
    }

    player::CameraControls cam;
    if (_twoFingers && _touches.size() >= 2) {
        auto it = _touches.begin();
        CGPoint a = it->second, b = (++it)->second;
        float dist = std::hypot((float)(b.x - a.x), (float)(b.y - a.y));
        float angle = std::atan2((float)(b.y - a.y), (float)(b.x - a.x));
        cam.twoFingers = true;
        cam.fingersX = (float)(a.x + b.x) * 0.5f;
        cam.fingersY = (float)(a.y + b.y) * 0.5f;
        if (_twoStarted && _prevDist > 1.0f && dist > 1.0f) {
            cam.pinch = dist / _prevDist;
            float d = angle - _prevAngle;
            if (d > (float)M_PI) d -= 2.0f * (float)M_PI;
            if (d < -(float)M_PI) d += 2.0f * (float)M_PI;
            cam.twist = d;
        }
        _prevDist = dist;
        _prevAngle = angle;
        _twoStarted = YES;
    }

    const SwapChainDesc& d = _swapChain->GetDesc();
    _player->frame(_swapChain->GetCurrentBackBufferRTV(), d.Width, d.Height, std::min(dt, 0.1f), cam);
    if (_liveOn && _live.wantsShot()) _live.captureShot(_device, _context, _swapChain->GetCurrentBackBufferRTV()->GetTexture());
    _swapChain->Present(1);
    if (_liveOn) [self liveTick];
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

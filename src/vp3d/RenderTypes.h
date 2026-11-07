// vp3d - render settings and view types shared by every renderer (DX12 vp3d::Renderer, Diligent lit::LitRenderer
// backends of BrickIt): no graphics API in here, so portable code (iPad) can fill them.
#pragma once
#include "LitPrep.h"
#include "Scene.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vp3d {

// Look of the editor grid (EditorGrid.hlsl), set by the Grid Options window of the editor.
struct EditorGridStyle {
    enum Flags : uint32_t { Lines = 1, Subdivisions = 2, Axes = 4, Arrows = 8, Fade = 16 };
    uint32_t flags = Lines | Subdivisions | Axes | Arrows | Fade;
    int subdivisions = 1;             // lines between two grid lines + 1 (1 = none)
    float extent = 0.0f;              // length and width of the grid (0 = infinite)
    float widthGrid = 1.0f, widthSub = 1.0f, widthAxis = 1.4f;   // screen pixels
    float opacity = 1.0f;
    float4 colorGrid{0.58f, 0.63f, 0.72f, 0.40f};   // rgb, a = opacity of the lines
    float4 colorSub{0.58f, 0.63f, 0.72f, 0.20f};
    float4 colorX{0.93f, 0.36f, 0.36f, 0.90f}, colorY{0.42f, 0.86f, 0.50f, 0.95f}, colorZ{0.36f, 0.56f, 0.98f, 0.90f};
};

struct RenderSettings {
    int maxBounces = 6;
    int samplesPerFrame = 1;
    int maxSamples = 4096;
    float exposure = 1.0f;
    bool denoise = true;
    float denoiseStrength = 1.0f;
    int denoisePasses = 3;          // a-trous passes (steps 1, 2, 4, ...): more = smoother, blurrier
    float denoiseEdgeLuma = 0.5f;   // luminance edge sensitivity: lower = the filter stops at shadow edges
    bool bevel = true;
    bool rasterBevel = false;       // the bevel in the raster view too (inline ray queries: builds the TLAS in raster)
    // Raster view closer to the path tracer (BrickIt HQ; builds the TLAS): every area light shadowed by rays, the
    // environment occluded by rays over rasterAO (scene units, 0 = off), reflections traced (reflSamples rays) without
    // probes. Off by default (Viewport3D unchanged).
    bool rasterRT = false;
    float rasterAO = 0.0f;
    float bevelScale = 1.0f;
    int bevelSamples = 4;
    float fireflyClamp = 25.0f;
    float filterGlossy = 0.0f;     // minimum roughness of glossy surfaces seen after a rough bounce (0 = exact; >0 removes caustic fireflies)
    float indirectClamp = 0.0f;    // caps each light contribution after a rough bounce (0 = off; temporal: fewer sparkles)
    float sssScale = 1.0f;
    float lightScale = 1.0f;
    float vignette = 0.12f;
    bool raster = false;
    int msaaSamples = 4;            // viewport anti-aliasing of the raster passes: MSAA sample count (1 off, 4, 8, 16)
    // Editor overlays of the raster viewport: ground grid (smallest cell in scene units, 0 = off, see EditorGrid.hlsl),
    // length of its Y axis, and the selection glow (style 1 = blue glow, 0 = yellow outline; age in seconds animates it).
    float editorGrid = 0.0f;
    float editorGridAxis = 10.0f;
    EditorGridStyle grid;
    int selectionStyle = 1;
    float selectionAge = 10.0f;
    float2 selectionCenter{0, 0};   // screen centre (viewport pixels) and radius of the selection: the glow light runs around it
    float selectionRadius = 100.0f;
    float selectionFill = 0.15f;
    float sharpen = 0.0f;
    float dof = 0.0f;     // raster views: depth of field (0 = off, 1 = a blur of 24 px at twice the focus distance)
    float bloom = 0.0f;   // raster views: the glow of the bright parts (0 = off)
    bool stillAA = false; // raster view of the probes (HQ): a still image accumulates jittered frames (supersampling), MSAA while it moves           // tone mapper: contrast adaptive sharpening (an image rendered below the window size)    // selection style 2 (BrickIt): the white veil over the selected objects (post, after the lighting)
    float pixelScale = 1.0f;        // render pixels per screen pixel (viewport supersampling): lines and the glow keep their size
    // raster: screen-space ambient occlusion (radius in scene units), multiplied into the image before the tone mapping
    bool ssao = false;
    float ssaoRadius = 0.6f;
    int tonemapper = 0;           // 0 ACES (Hill fit), 1 AgX, 2 AgX Punchy, 3 Standard (clip)          // light raster viewport instead of path tracing
    // Stable history (interactive, GI): the history is dropped when an object moves, edges are validated on the
    // pixel centre, the denoiser strength is bounded. Off = the previous behaviour (marks behind moved objects).
    bool historyFixes = true;
    bool rasterEnvSun = true;      // raster: the sun of the HDRI is a shadowed directional light (else all the image is ambient)
    // Drop the history when an object moves (and light changes). Off = keep it: less noise, but marks behind moved objects.
    bool resetOnMove = true;
    float resetDelay = 2.0f;   // seconds: the history fades to new samples over this time after a move
    // Real-time GI test: DDGI probe volume instead of path tracing (when raster is false).
    bool ddgi = false;
    int ddgiProbeBudget = 2048;   // target probe count over the scene bounds
    float ddgiHysteresis = 0.97f;
    // progressive probes: the hysteresis grows from 0.85 to ddgiHysteresis while nothing changes (a running average:
    // converged and still), back to 0.85 when the lights, the environment or an object change; no fast path per texel
    bool ddgiProgressive = false;
    // progressive probes held as they are (a brick carried in the air): no probe update, no restart; the objects that
    // moved meanwhile restart the probes around them once it is released (their boxes before the hold and after)
    bool ddgiHold = false;
    // progressive probes: updates of a probe after which it is converged and left alone until a change around it (or of
    // the lights, the environment, the settings) restarts it; 0 = every probe always updated
    int ddgiAgeCap = 0;
    // subsurface of the raster view (HQ): scale of the material subsurface, the light wrapped past the terminator (0 = off)
    float rasterSSS = 0.0f;
    // raster view of the probes: their light read at 5 points over the surface, this radius in probe spacings apart (0 = one read)
    float ddgiSmooth = 0.0f;
    bool ddgiSoftWeights = false;   // softer visibility weights of the probes (the grid cells less visible on flat walls)
    // GI mode (ray traced probes and visibility): while the camera or an object moves, and giSettleFrames after, the
    // raster view of the probes (no per-pixel noise), then a fade of giFadeFrames to the ray traced image
    bool giRasterWhileMoving = false;
    int giSettleFrames = 8;
    int giFadeFrames = 24;
    float ddgiViewBias = -1.0f;   // >= 0: the view bias of the probe lookup (spacings) instead of the default 0.24
    float ddgiEnergy = 1.0f;   // scale of the probe light where it is read (not in the bounces): no probe restart
    int temporalCap = 1024;        // history length in frames (exponential average window): 256 converges, 64 stays noisier
    int ddgiView = 0;             // 0 full, 1 indirect only, 2 direct only
    bool ddgiShowProbes = false;  // draw the probes as small spheres shaded with their irradiance
    // DDGI rays from a raster capture of each probe (cube faces lit by the raster pass) instead of ray tracing.
    bool ddgiRaster = false;
    int ddgiCaptureProbes = 32;   // probes captured per frame (the whole grid is refreshed in total / this frames)
    // Diagnostic: the viewport is lit by the raster pass reading the probes (as ddgiRaster) while the probes themselves
    // keep their ray traced rays. Separates the capture error from the visibility error.
    bool ddgiRasterView = false;
    // Lit mode: the main pass reads baked probes (bakeSignature / startBake / setBakedProbes) instead of its ambient.
    bool bakedProbes = false;
    // Raster modes (Lit, Metal iOS): 0 shaded, 1 wireframe only, 2 wireframe over the shaded view (override).
    int wireframe = 0;
    // Raster: object id pass (normal + id per pixel, used by the editor's id outline). Off: the target is only cleared
    // (the selection coverage still gives the outline), one geometry pass less.
    bool idPass = true;
    // Path tracing: 1 = every pixel each frame; 4 = one pixel of each 2 x 2 block per frame, in turn (a quarter of the
    // rays per frame, same convergence over time; the first frame after a reset traces every pixel)
    int traceStride = 1;
    // How the probes of the GI modes are computed: 0 DDGI (random rays, hysteresis), 1 Radiance Cascades (the
    // cascades merged every frame, no temporal accumulation). Radiance Cascades need ray tracing (not ddgiRaster).
    int giMethod = 0;
    int rcProbeBudget = 8192;     // probes of cascade 0 (the DDGI grid of this mode)
    int rcCascades = 4;           // cascade count (the top one traces to infinity)
    int rcDirRes = 8;             // directions per side of cascade 0 (8 = 64 directions, x4 per cascade), 2..8
    float rcInterval = 3.0f;      // ray length of cascade 0, in probe spacings (x4 per cascade): covers the
                                  // probe visibility range (DDGI maxDistance 2.6 spacings), so the distances are exact
    bool rcBounces = true;        // further bounces: the hits read the previous frame's probes (else one bounce)
    int rcLightSamples = 2;       // light samples per hit (fixed per texel: stable, no flicker)
    float rcHysteresis = 0.0f;    // 0 = no temporal accumulation; > 0 blends the frames (the light samples then move)
    bool rcBilinearFix = false;   // merge with one traced segment per parent probe (8x the merge rays; not better here)
    // Screen-space cascades (needs giMethod = 1 and the raster view): probes on the visible surfaces, one per
    // rcScreenSpacing x rcScreenSpacing pixels in cascade 0; the world cascades stay the cache of the further bounces.
    bool rcScreen = false;
    int rcScreenSpacing = 8;      // pixels between the cascade-0 probes (x2 per cascade)
    int rcScreenDirRes = 8;       // directions per side of cascade 0 (x2 per cascade)
    float rcScreenInterval = 0.25f;   // ray length of cascade 0, in world probe spacings (x4 per cascade)
    // Split Radiance Cascades (needs giMethod = 1 and the raster view; replaces rcScreen): sparse world probes around
    // the visible surfaces (hash tables, LODs from the camera distance), one ray per pixel split into the cascades by
    // its hit distance, nothing accumulated over frames. The world cascades stay the cache of the further bounces.
    bool rcSplit = false;
    int rcSplitProbePx = 12;      // size of a cascade-0 probe cell on screen, in pixels (LODs keep it so with distance)
    float rcSplitInterval = 2.0f; // end of the cascade-0 interval, in cascade-0 spacings (x4 per cascade)
    int rcSplitCascades = 4;      // 1..6, the top one to infinity
    bool rcSplitAnimate = false;  // jitter the ray set every frame (moving noise; off = a still image stays still)
    // Accumulation while nothing moves: up to rcSplitHistory frames per cascade-0 probe (world space, so the camera can
    // move), dropped around an object that moves and everywhere when a light or a material changes. 0 or 1 = off.
    int rcSplitHistory = 16;
    bool rcSplitFilter = true;    // each cascade-0 probe averaged with its 6 neighbour cells (less noise, a cell of blur)
    int rcSplitDebug = 0;         // inspection view: 0 off, SRC_DEBUG_* (cells, probes, hit cascade, rays, LOD, history)
    int rcSplitDebugCascade = 0;  // cascade shown by the cells view
    // Raster modes: the sun shadow traced per pixel against the scene (needs the acceleration structure of the frame,
    // i.e. a GI mode) instead of the shadow map. sunAngle: angular diameter of the sun (degrees), its penumbra.
    bool rtSunShadows = true;
    float sunAngle = 0.53f;
    int rtShadowSamples = 8;
    // Raster modes with GI: reflections traced per pixel on metals and glossy surfaces (roughness under reflMaxRough):
    // one mirror ray, or reflSamples GGX rays filtered spatially, instead of the environment reflection.
    bool rtReflections = true;
    int reflSamples = 4;
    float reflMaxRough = 0.5f;
    // GI raster modes (Metal iOS, Split RC...; Lit keeps 4x): 0 MSAA 4x, 1 MSAA 8x, 2 AAA = MSAA 8x resolved in
    // tonemapped space (bright / dark edges stay smooth instead of stair-stepping on the bright side)
    int aaQuality = 0;
    // Ray traced shadows (sun and lamp) of the raster GI modes: 0 low ... 2 medium (8 sun / 16 lamp rays, the former
    // default) ... 4 AAA (32 / 64 rays, wider filter)
    int shadowQuality = 2;
    // Split RC: GI bounds (an app with a huge ground: BrickIt), probes only in the box grown by giBoundsFade, the GI
    // fading out across it. giBoundsFade 0 = off (the whole visible scene).
    float3 giBoundsMin{0, 0, 0}, giBoundsMax{0, 0, 0};
    float giBoundsFade = 0.0f;
};

// What the Radiance Cascades of the last frame looked like (viewer statistics).
struct RCStats {
    int cascades = 0;
    uint32_t probes[RC_MAX_CASCADES] = {}, dirRes[RC_MAX_CASCADES] = {};
    float tStart[RC_MAX_CASCADES] = {}, tEnd[RC_MAX_CASCADES] = {};
    uint64_t rays = 0;            // texels traced per frame (all cascades)
    int screenCascades = 0;       // screen-space cascades (rcScreen)
    uint64_t screenRays = 0;
    int splitCascades = 0;        // Split RC: cascades, probe capacity and directions per probe of each, rays (pixels)
    uint32_t splitCapacity[RC_MAX_CASCADES] = {}, splitDirs[RC_MAX_CASCADES] = {};
    uint64_t splitRays = 0;
    float splitLodDist = 0, splitSpacing = 0;   // where LOD 1 starts, cascade-0 spacing at LOD 0
    uint32_t splitProbes[RC_MAX_CASCADES] = {};   // probes allocated per cascade (read back from the previous frame)
};

struct ViewRect { int x = 0, y = 0, w = 1, h = 1; };

} // namespace vp3d

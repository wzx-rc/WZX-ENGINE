// Split Radiance Cascades (Freeman, Sannikov, Pitt-Francis 2026): resources and helpers of the SRC*.hlsl passes.
// The probes of each cascade sit at the centres of a world grid ((k + 0.5) * spacing) and exist only where a visible
// surface is near: a hash table per cascade maps the cell (and its level of detail) to a probe record. One ray per
// pixel, from the visible surface, is split into the cascades by its hit distance (ray splitting); the cascades are
// then merged from the top down and cascade 0 is integrated per probe into a small irradiance map read per pixel.
// The world cascades (RCTrace.hlsl, no temporal either) stay the cache of the further bounces: the hits read them.
#include "RCCommon.hlsli"

ConstantBuffer<SplitRCConstants> S : register(b4);
RWByteAddressBuffer gS : register(u16);          // all the data, word offsets in S
RWTexture2D<float4> gScreenGI : register(u17);   // per pixel: irradiance / pi (rgb), confidence (a)
RWByteAddressBuffer gPrev : register(u22);       // previous frame: cascade-0 slots, irradiance, history (see S.prev*)

#define SRC_NONE 0xFFFFFFFFu

uint SLoad(uint w) { return gS.Load(w * 4); }

// Weight of the GI bounds at P: 1 inside the box, down to 0 at the fade distance outside it (always 1 when off).
float GIBoundsWeight(float3 P) {
    if (S.giMax.w <= 0) return 1.0;
    float3 d = max(max(S.giMin.xyz - P, P - S.giMax.xyz), 0.0);
    return 1.0 - smoothstep(0.0, max(S.giMin.w, 1e-3), length(d));
}
void SStore(uint w, uint v) { gS.Store(w * 4, v); }
uint SAdd(uint w, uint v) { uint o; gS.InterlockedAdd(w * 4, v, o); return o; }

float SpacingAt(uint ci, uint lod) { return S.c[ci].spacing * float(1u << lod); }

// Cell of P in cascade ci at a LOD (probe centre (k + 0.5) * spacing).
int3 CellOf(float3 P, uint ci, uint lod) { return int3(floor(P / SpacingAt(ci, lod))); }

// Key of a cell: valid bit, LOD (4 bits), cell relative to the camera's cell (9 bits per axis). SRC_NONE when the
// cell is too far from the camera to be keyed (the LODs keep the visible cells within +-256 of it).
uint CellKeyFrom(float3 cam, int3 k, uint ci, uint lod) {
    int3 r = k - CellOf(cam, ci, lod) + 256;
    if (any(r < 0) || any(r > 511) || lod > 15) return SRC_NONE;
    return 0x80000000u | (lod << 27) | uint(r.x) | (uint(r.y) << 9) | (uint(r.z) << 18);
}

uint CellKey(int3 k, uint ci, uint lod) { return CellKeyFrom(F.camPos, k, ci, lod); }

void KeyCell(uint key, uint ci, out int3 k, out uint lod) {
    lod = (key >> 27) & 15u;
    int3 r = int3(key & 511u, (key >> 9) & 511u, (key >> 18) & 511u) - 256;
    k = r + CellOf(F.camPos, ci, lod);
}

void Insert(uint ci, uint key) {
    if (key == SRC_NONE) return;
    SRCCascade c = S.c[ci];
    uint slot = Hash(key) & (c.cap - 1);
    for (uint n = 0; n < 64; ++n) {
        uint o;
        gS.InterlockedCompareExchange((c.keys + slot) * 4, 0u, key, o);
        if (o == 0u || o == key) return;
        slot = (slot + 1) & (c.cap - 1);
    }
}

// Probe index of a key in cascade ci, SRC_NONE if there is none (after SRCAlloc).
uint Find(uint ci, uint key) {
    if (key == SRC_NONE) return SRC_NONE;
    SRCCascade c = S.c[ci];
    uint slot = Hash(key) & (c.cap - 1);
    for (uint n = 0; n < 64; ++n) {
        uint k = SLoad(c.keys + slot);
        if (k == key) return SLoad(c.vals + slot);
        if (k == 0u) return SRC_NONE;
        slot = (slot + 1) & (c.cap - 1);
    }
    return SRC_NONE;
}

uint FindCell(uint ci, int3 k, uint lod) { return Find(ci, CellKey(k, ci, lod)); }

uint ProbeCount(uint ci) { return min(SLoad(S.counters + ci), S.c[ci].maxProbes); }
uint ProbeWord(uint ci, uint p, uint field) { return S.c[ci].probes + p * 8 + field; }   // 0..2 cell, 3 lod, 4 rays,
                                                                                         // 5 offset, 6 cursor, 7 parent
// Equal-area (Lambert cylindrical) mapping of the directions: u = azimuth / 2pi, v = (z + 1) / 2.
float2 EncodeDir(float3 d) { return float2(frac(atan2(d.y, d.x) / (2.0 * PI) + 1.0), saturate(d.z * 0.5 + 0.5)); }

float3 DecodeDir(float2 uv) {
    float phi = 2.0 * PI * uv.x, z = uv.y * 2.0 - 1.0, r = sqrt(saturate(1.0 - z * z));
    return float3(r * cos(phi), r * sin(phi), z);
}

uint2 DirBucket(float3 d, uint ci) {
    float2 uv = EncodeDir(d);
    return min(uint2(uv * float2(S.c[ci].dirW, S.c[ci].dirH)), uint2(S.c[ci].dirW, S.c[ci].dirH) - 1);
}

uint EntryWord(uint ci, uint p, uint2 b) { return S.c[ci].entries + (p * S.c[ci].dirW * S.c[ci].dirH + b.x + b.y * S.c[ci].dirW) * 4; }

// Pixel record: P (3 floats), LOD coordinate (float, < 0 = no surface), octahedral normal (2 x 16 bits), cascade-0
// probe of the pixel's LOD, index of the pixel's ray among the probe's rays, unused.
uint PixelWord(uint2 px) { return S.pixels + (px.x + px.y * F.resolution.x) * 8; }

uint PackNormal(float3 n) {
    uint2 q = uint2(saturate(OctEncode(n) * 0.5 + 0.5) * 65535.0 + 0.5);
    return q.x | (q.y << 16);
}

float3 UnpackNormal(uint v) { return OctDecode(float2(v & 0xFFFFu, v >> 16) / 65535.0 * 2.0 - 1.0); }

// LOD coordinate of a point: log2 of its Chebyshev distance to the camera in units of lodDist (0 below it).
float LodCoord(float3 P) {
    float3 a = abs(P - F.camPos);
    return min(log2(max(max(a.x, max(a.y, a.z)) / S.lodDist, 1.0)), 14.99);   // 4 bits of LOD in the keys
}

// The pixel's LODs: its own, and the next one with a blend weight when it sits in the overlap at the far end.
void PixelLods(float lodF, out uint lod0, out uint lod1, out float w1) {
    lod0 = (uint)floor(lodF);
    float f = frac(lodF), o = S.lodOverlap;
    lod1 = lod0 + 1;
    w1 = o > 0 ? saturate((f - (1.0 - o)) / o) : 0.0;
}

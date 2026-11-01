// vp3d - HDR environment images (HDRI): loading, conversion to a linear lat-long image, built-in test sky.
#pragma once
#include "Math.h"
#include <memory>
#include <string>
#include <vector>

namespace vp3d {

// Equirectangular (lat-long) environment in linear RGB. Row 0 is the zenith, column 0 is behind the default view
// (-Z is the centre of the image), see the direction mapping in shaders/PathTrace.hlsl (EnvDirToUV).
struct EnvImage {
    std::string name;            // file name (or "Procedural Sky")
    std::string path;            // source file, UTF-8 (empty for the built-in sky)
    uint32_t width = 0, height = 0;
    std::vector<float> rgb;      // width * height * 3
    uint32_t sourceWidth = 0, sourceHeight = 0;   // size in the file, before the downsizing

    float3 texel(uint32_t x, uint32_t y) const {
        const float* p = &rgb[((size_t)y * width + x) * 3];
        return float3(p[0], p[1], p[2]);
    }
};

using EnvImagePtr = std::shared_ptr<const EnvImage>;

// Loads an environment image and converts it to a linear lat-long EnvImage:
//   .hdr / .pic  Radiance RGBE (flat or RLE)
//   .exr         OpenEXR scanline images, uncompressed, RLE, ZIPS or ZIP, half or float RGB(A)
//   .jpg .png .bmp .tif ...  LDR images (WIC), sRGB decoded to linear (no real dynamic range)
// Images that are not 2:1 are taken as lat-long anyway. Images wider than maxWidth are box-filtered down.
// The caller must have initialised COM for the LDR formats.
bool loadEnvImage(const std::wstring& path, EnvImage& out, std::string* error = nullptr, uint32_t maxWidth = 4096);

// Writes a Radiance .hdr file (RLE), e.g. to keep a converted copy.
bool saveHdr(const std::wstring& path, const EnvImage& img);

// Built-in test environment: sky gradient, a warm sun disc and a darker ground, no file needed.
void makeProceduralSky(EnvImage& out, uint32_t width = 2048);

// Decompresses a zlib stream (deflate with the 2-byte header), e.g. the BrickIt Technic packs.
// expected: size hint for the output. The Adler-32 checksum is not checked.
bool inflateZlibStream(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected = 0);

// File extensions accepted by loadEnvImage (lower case, with the dot).
bool isEnvImageFile(const std::wstring& path);

} // namespace vp3d

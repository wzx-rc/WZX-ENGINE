// imgcmp: compares a test image with a reference (same size PNGs).
// Prints RMSE / mean absolute error on sRGB values (0-255), mean luminance of both images,
// and optionally writes a comparison sheet: reference | test | |difference| x4.
// Usage: imgcmp ref.png test.png [sheet.png]
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

static bool load(IWICImagingFactory* f, const wchar_t* path, std::vector<uint8_t>& px, UINT& w, UINT& h) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> fr;
    dec->GetFrame(0, &fr);
    ComPtr<IWICFormatConverter> cv;
    f->CreateFormatConverter(&cv);
    cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    cv->GetSize(&w, &h);
    px.resize((size_t)w * h * 4);
    return SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()));
}

static bool save(IWICImagingFactory* f, const wchar_t* path, std::vector<uint8_t>& px, UINT w, UINT h) {
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path, GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    fr->Initialize(props.Get());
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    if (fmt != GUID_WICPixelFormat32bppRGBA)
        for (size_t i = 0; i + 3 < px.size(); i += 4) std::swap(px[i], px[i + 2]);
    fr->WritePixels(h, w * 4, (UINT)px.size(), px.data());
    fr->Commit();
    enc->Commit();
    return true;
}

static float lin(uint8_t c) {
    float v = c / 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { printf("usage: imgcmp ref.png test.png [sheet.png]\n"); return 1; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    std::vector<uint8_t> a, b;
    UINT w, h, w2, h2;
    if (!load(f.Get(), argv[1], a, w, h) || !load(f.Get(), argv[2], b, w2, h2)) { printf("load failed\n"); return 1; }
    if (w != w2 || h != h2) { printf("size mismatch %ux%u vs %ux%u\n", w, h, w2, h2); return 1; }
    double se = 0, ae = 0, la = 0, lb = 0;
    size_t n = (size_t)w * h;
    std::vector<uint8_t> sheet((size_t)w * 3 * h * 4, 255);
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            double d = (double)a[i * 4 + c] - b[i * 4 + c];
            se += d * d;
            ae += std::fabs(d);
        }
        la += 0.2126 * lin(a[i * 4]) + 0.7152 * lin(a[i * 4 + 1]) + 0.0722 * lin(a[i * 4 + 2]);
        lb += 0.2126 * lin(b[i * 4]) + 0.7152 * lin(b[i * 4 + 1]) + 0.0722 * lin(b[i * 4 + 2]);
        size_t x = i % w, y = i / w;
        for (int c = 0; c < 3; ++c) {
            sheet[(y * w * 3 + x) * 4 + c] = a[i * 4 + c];
            sheet[(y * w * 3 + w + x) * 4 + c] = b[i * 4 + c];
            int d = std::abs((int)a[i * 4 + c] - (int)b[i * 4 + c]) * 4;
            sheet[(y * w * 3 + 2 * w + x) * 4 + c] = (uint8_t)std::min(d, 255);
        }
    }
    printf("rmse=%.3f mae=%.3f (0-255 sRGB)  mean_lum_ref=%.4f mean_lum_test=%.4f ratio=%.3f\n",
           std::sqrt(se / (n * 3)), ae / (n * 3), la / n, lb / n, lb / la);
    if (argc > 3 && !save(f.Get(), argv[3], sheet, w * 3, h)) { printf("save failed\n"); return 1; }
    return 0;
}

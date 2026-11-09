#include "Oidn.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <type_traits>

namespace vp3d {

namespace {

// The few entry points of the OIDN 2.x C API used here (OpenImageDenoise/oidn.h, Apache 2.0), resolved from the DLL.
typedef void* OIDNDevice;
typedef void* OIDNFilter;
typedef void* OIDNBuffer;
enum { OIDN_DEVICE_TYPE_DEFAULT = 0, OIDN_DEVICE_TYPE_CPU = 1, OIDN_DEVICE_TYPE_SYCL = 2, OIDN_DEVICE_TYPE_CUDA = 3,
       OIDN_DEVICE_TYPE_HIP = 4, OIDN_DEVICE_TYPE_METAL = 5 };
enum { OIDN_FORMAT_FLOAT3 = 3 };
enum { OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32 = 1 << 2, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE = 1 << 9,
       OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED = 1 << 30 };
enum { OIDN_QUALITY_HIGH = 6 };

struct Api {
    int (*GetNumPhysicalDevices)();
    bool (*GetPhysicalDeviceBool)(int, const char*);
    int (*GetPhysicalDeviceInt)(int, const char*);
    const char* (*GetPhysicalDeviceString)(int, const char*);
    const void* (*GetPhysicalDeviceData)(int, const char*, size_t*);
    OIDNDevice (*NewDevice)(int);
    OIDNDevice (*NewDeviceByID)(int);
    void (*ReleaseDevice)(OIDNDevice);
    int (*GetDeviceInt)(OIDNDevice, const char*);
    int (*GetDeviceError)(OIDNDevice, const char**);
    void (*CommitDevice)(OIDNDevice);
    void (*SyncDevice)(OIDNDevice);
    OIDNBuffer (*NewBuffer)(OIDNDevice, size_t);
    OIDNBuffer (*NewSharedBufferFromWin32Handle)(OIDNDevice, unsigned, void*, const void*, size_t);
    void (*ReadBuffer)(OIDNBuffer, size_t, size_t, void*);
    void (*WriteBuffer)(OIDNBuffer, size_t, size_t, const void*);
    void (*ReleaseBuffer)(OIDNBuffer);
    OIDNFilter (*NewFilter)(OIDNDevice, const char*);
    void (*ReleaseFilter)(OIDNFilter);
    void (*SetFilterImage)(OIDNFilter, const char*, OIDNBuffer, int, size_t, size_t, size_t, size_t, size_t);
    void (*SetFilterBool)(OIDNFilter, const char*, bool);
    void (*SetFilterInt)(OIDNFilter, const char*, int);
    void (*CommitFilter)(OIDNFilter);
    void (*ExecuteFilter)(OIDNFilter);
};
Api g;

std::wstring exeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/"));
}

const char* typeName(int t) {
    switch (t) {
    case OIDN_DEVICE_TYPE_CPU: return "CPU";
    case OIDN_DEVICE_TYPE_SYCL: return "SYCL";
    case OIDN_DEVICE_TYPE_CUDA: return "CUDA";
    case OIDN_DEVICE_TYPE_HIP: return "HIP";
    case OIDN_DEVICE_TYPE_METAL: return "Metal";
    default: return "GPU";
    }
}

D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE t) {
    D3D12_HEAP_PROPERTIES h = {};
    h.Type = t;
    return h;
}

D3D12_RESOURCE_DESC bufferDesc(uint64_t size, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

} // namespace

bool Oidn::dllPresent() {
    return GetFileAttributesW((exeDir() + L"\\OpenImageDenoise.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
}

Oidn::~Oidn() {
    release();
    if (m_device) g.ReleaseDevice(m_device);
    m_device = nullptr;
}

bool Oidn::check(const char* what) {
    const char* msg = nullptr;
    const int err = g.GetDeviceError(m_device, &msg);
    if (err == 0) return true;
    m_status = std::string(what) + ": " + (msg ? msg : "error");
    return false;
}

bool Oidn::ensureDevice(ID3D12Device* device) {
    if (m_device) return true;
    if (m_failed) return false;
    m_failed = true;   // until the device is up
    if (!dllPresent()) { m_status = "OpenImageDenoise.dll missing next to the editor"; return false; }
    HMODULE lib = LoadLibraryExW((exeDir() + L"\\OpenImageDenoise.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) { m_status = "OpenImageDenoise.dll could not be loaded"; return false; }
    m_lib = lib;
    bool all = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(lib, name));
        if (!fn) all = false;
    };
    get(g.GetNumPhysicalDevices, "oidnGetNumPhysicalDevices");
    get(g.GetPhysicalDeviceBool, "oidnGetPhysicalDeviceBool");
    get(g.GetPhysicalDeviceInt, "oidnGetPhysicalDeviceInt");
    get(g.GetPhysicalDeviceString, "oidnGetPhysicalDeviceString");
    get(g.GetPhysicalDeviceData, "oidnGetPhysicalDeviceData");
    get(g.NewDevice, "oidnNewDevice");
    get(g.NewDeviceByID, "oidnNewDeviceByID");
    get(g.ReleaseDevice, "oidnReleaseDevice");
    get(g.GetDeviceInt, "oidnGetDeviceInt");
    get(g.GetDeviceError, "oidnGetDeviceError");
    get(g.CommitDevice, "oidnCommitDevice");
    get(g.SyncDevice, "oidnSyncDevice");
    get(g.NewBuffer, "oidnNewBuffer");
    get(g.NewSharedBufferFromWin32Handle, "oidnNewSharedBufferFromWin32Handle");
    get(g.ReadBuffer, "oidnReadBuffer");
    get(g.WriteBuffer, "oidnWriteBuffer");
    get(g.ReleaseBuffer, "oidnReleaseBuffer");
    get(g.NewFilter, "oidnNewFilter");
    get(g.ReleaseFilter, "oidnReleaseFilter");
    get(g.SetFilterImage, "oidnSetFilterImage");
    get(g.SetFilterBool, "oidnSetFilterBool");
    get(g.SetFilterInt, "oidnSetFilterInt");
    get(g.CommitFilter, "oidnCommitFilter");
    get(g.ExecuteFilter, "oidnExecuteFilter");
    if (!all) { m_status = "OpenImageDenoise.dll is not OIDN 2.x"; return false; }

    // The physical device of the renderer's adapter (same LUID): OIDN works on the same GPU and can import its memory.
    const LUID luid = device->GetAdapterLuid();
    int pick = -1;
    const int n = g.GetNumPhysicalDevices();
    for (int i = 0; i < n && pick < 0; ++i) {
        if (!g.GetPhysicalDeviceBool(i, "luidSupported")) continue;
        size_t size = 0;
        const void* data = g.GetPhysicalDeviceData(i, "luid", &size);
        if (data && size >= sizeof(LUID) && memcmp(data, &luid, sizeof(LUID)) == 0) pick = i;
    }
    m_device = pick >= 0 ? g.NewDeviceByID(pick) : g.NewDevice(OIDN_DEVICE_TYPE_DEFAULT);
    if (!m_device) {
        const char* msg = nullptr;
        g.GetDeviceError(nullptr, &msg);
        m_status = std::string("no OIDN device: ") + (msg ? msg : "unsupported hardware");
        return false;
    }
    g.CommitDevice(m_device);
    if (!check("device")) { g.ReleaseDevice(m_device); m_device = nullptr; return false; }
    const int type = g.GetDeviceInt(m_device, "type");
    std::string name;
    if (pick >= 0) name = g.GetPhysicalDeviceString(pick, "name") ? g.GetPhysicalDeviceString(pick, "name") : "";
    else
        for (int i = 0; i < n; ++i)   // OIDN's own choice: its first physical device of that type
            if (g.GetPhysicalDeviceInt(i, "type") == type) { const char* s = g.GetPhysicalDeviceString(i, "name"); name = s ? s : ""; break; }
    m_deviceName = std::string(typeName(type)) + (name.empty() ? "" : " Â· " + name);
    const unsigned ext = (unsigned)g.GetDeviceInt(m_device, "externalMemoryTypes");
    m_importable = pick >= 0 && type != OIDN_DEVICE_TYPE_CPU &&
                   (ext & (OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE | OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32)) != 0;
    m_importType = (ext & OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE) ? (unsigned)OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE
                                                                          : (unsigned)OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32;
    m_failed = false;
    m_status = "ready";
    return true;
}

void Oidn::release() {
    if (m_filter) g.ReleaseFilter(m_filter);
    if (m_inBuf) g.ReleaseBuffer(m_inBuf);
    if (m_outBuf) g.ReleaseBuffer(m_outBuf);
    m_filter = m_inBuf = m_outBuf = nullptr;
    if (m_inHandle) CloseHandle(m_inHandle);
    if (m_outHandle) CloseHandle(m_outHandle);
    m_inHandle = m_outHandle = nullptr;
    m_in.Reset(); m_out.Reset(); m_readback.Reset(); m_upload.Reset();
    m_w = m_h = 0;
}

bool Oidn::resize(ID3D12Device* device, uint32_t w, uint32_t h) {
    if (!m_device) return false;
    if (w == m_w && h == m_h && m_filter) return true;
    release();
    const uint64_t plane = (uint64_t)w * h * 12;   // float3 per pixel
    m_inBytes = plane * 3;
    m_outBytes = plane;

    // Shared D3D12 buffers imported by OIDN (same GPU); on any failure, the staged way.
    auto create = [&](uint64_t size, D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags, D3D12_RESOURCE_FLAGS rf,
                      D3D12_RESOURCE_STATES state, Microsoft::WRL::ComPtr<ID3D12Resource>& res) -> bool {
        auto hp = heap(type);
        auto d = bufferDesc(size, rf);
        return SUCCEEDED(device->CreateCommittedResource(&hp, flags, &d, state, nullptr, IID_PPV_ARGS(&res)));
    };
    m_shared = false;
    if (m_importable) {
        bool ok = create(m_inBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_in) &&
                  create(m_outBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_out);
        ok = ok && SUCCEEDED(device->CreateSharedHandle(m_in.Get(), nullptr, GENERIC_ALL, nullptr, &m_inHandle)) &&
             SUCCEEDED(device->CreateSharedHandle(m_out.Get(), nullptr, GENERIC_ALL, nullptr, &m_outHandle));
        if (ok) {
            // a committed resource is a dedicated allocation (CUDA imports D3D12 resources as dedicated)
            const unsigned types[2] = {m_importType | OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED, m_importType};
            for (unsigned t : types) {
                m_inBuf = g.NewSharedBufferFromWin32Handle(m_device, t, m_inHandle, nullptr, (size_t)m_inBytes);
                if (m_inBuf && check("import")) {
                    m_outBuf = g.NewSharedBufferFromWin32Handle(m_device, t, m_outHandle, nullptr, (size_t)m_outBytes);
                    if (m_outBuf && check("import")) break;
                }
                if (m_inBuf) g.ReleaseBuffer(m_inBuf);
                if (m_outBuf) g.ReleaseBuffer(m_outBuf);
                m_inBuf = m_outBuf = nullptr;
            }
            ok = m_inBuf && m_outBuf;
        }
        if (ok) m_shared = true;
        else {
            release();
            m_importable = false;   // not again
        }
    }
    if (!m_shared) {
        if (!create(m_inBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_in) ||
            !create(m_outBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_out) ||
            !create(m_inBytes, D3D12_HEAP_TYPE_READBACK, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_COPY_DEST, m_readback) ||
            !create(m_outBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_GENERIC_READ, m_upload)) {
            m_status = "out of GPU memory for the OIDN buffers";
            release();
            return false;
        }
        m_inBuf = g.NewBuffer(m_device, (size_t)m_inBytes);
        m_outBuf = g.NewBuffer(m_device, (size_t)m_outBytes);
        if (!m_inBuf || !m_outBuf || !check("buffers")) { release(); return false; }
    }

    m_filter = g.NewFilter(m_device, "RT");
    if (!m_filter || !check("filter")) { release(); return false; }
    g.SetFilterImage(m_filter, "color", m_inBuf, OIDN_FORMAT_FLOAT3, w, h, 0, 12, (size_t)w * 12);
    g.SetFilterImage(m_filter, "albedo", m_inBuf, OIDN_FORMAT_FLOAT3, w, h, (size_t)plane, 12, (size_t)w * 12);
    g.SetFilterImage(m_filter, "normal", m_inBuf, OIDN_FORMAT_FLOAT3, w, h, (size_t)plane * 2, 12, (size_t)w * 12);
    g.SetFilterImage(m_filter, "output", m_outBuf, OIDN_FORMAT_FLOAT3, w, h, 0, 12, (size_t)w * 12);
    g.SetFilterBool(m_filter, "hdr", true);
    // the albedo and normal guides are accumulated like the colour (anti-aliased, nearly noise-free): cleanAux keeps the
    // fine detail they show (thin lines, texture edges); VP3D_OIDN_CLEANAUX set turns it off (comparisons)
    g.SetFilterBool(m_filter, "cleanAux", GetEnvironmentVariableA("VP3D_OIDN_CLEANAUX", nullptr, 0) == 0);
    g.SetFilterInt(m_filter, "quality", OIDN_QUALITY_HIGH);
    g.CommitFilter(m_filter);
    if (!check("filter")) { release(); return false; }
    m_w = w;
    m_h = h;
    m_status = m_shared ? "ready (GPU, shared memory)" : "ready (copies through the CPU)";
    return true;
}

bool Oidn::run() {
    if (!m_filter) return false;
    const auto t0 = std::chrono::high_resolution_clock::now();
    if (!m_shared) {
        void* p = nullptr;
        D3D12_RANGE r = {0, (SIZE_T)m_inBytes};
        if (FAILED(m_readback->Map(0, &r, &p))) return false;
        g.WriteBuffer(m_inBuf, 0, (size_t)m_inBytes, p);
        D3D12_RANGE none = {0, 0};
        m_readback->Unmap(0, &none);
    }
    g.ExecuteFilter(m_filter);
    if (!m_shared) {
        void* p = nullptr;
        D3D12_RANGE none = {0, 0};
        if (FAILED(m_upload->Map(0, &none, &p))) return false;
        g.ReadBuffer(m_outBuf, 0, (size_t)m_outBytes, p);
        m_upload->Unmap(0, nullptr);
    } else {
        g.SyncDevice(m_device);
    }
    const bool ok = check("denoise");
    m_lastMs = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    if (ok) ++m_runs;
    return ok;
}

} // namespace vp3d

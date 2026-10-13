#include "Renderer.h"
#include "shaders/Shared.h"
#include <cstring>
#include <fstream>

namespace vp3d {

namespace {

D3D12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE t) {
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = t;
    return p;
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

bool readFile(const std::wstring& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize((size_t)f.tellg());
    f.seekg(0);
    f.read(out.data(), out.size());
    return true;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?');
    return s;
}

// Slots of the UAV texture table (must match shaders).
enum TexSlot { TEX_ACCUM = 0, TEX_ALBEDO = 1, TEX_NORMALDEPTH = 2, TEX_DENOISE_A = 3, TEX_DENOISE_B = 4, TEX_OUTPUT = 5 };

} // namespace

Renderer::Buffer Renderer::createBuffer(uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                        D3D12_RESOURCE_STATES state) {
    Buffer b;
    b.size = size = std::max<uint64_t>(size, 256);
    auto hp = heapProps(heap);
    auto bd = bufferDesc(size, flags);
    if (heap == D3D12_HEAP_TYPE_UPLOAD) state = D3D12_RESOURCE_STATE_GENERIC_READ;
    if (heap == D3D12_HEAP_TYPE_READBACK) state = D3D12_RESOURCE_STATE_COPY_DEST;
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&b.res));
    if (heap == D3D12_HEAP_TYPE_UPLOAD) {
        D3D12_RANGE r = {0, 0};
        b.res->Map(0, &r, &b.mapped);
    }
    return b;
}

void Renderer::ensureUpload(Buffer& b, uint64_t size) {
    if (b.res && b.size >= size) return;
    uint64_t cap = std::max<uint64_t>(size * 3 / 2, 4096);
    b = createBuffer(cap, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

bool Renderer::init(HWND hwnd, uint32_t width, uint32_t height, const std::wstring& shaderDir, std::string* error) {
    m_shaderDir = shaderDir;
    UINT factoryFlags = 0;
    char dbg[8] = {};
    if (GetEnvironmentVariableA("VP3D_DEBUG", dbg, sizeof(dbg)) > 0) {
        ComPtr<ID3D12Debug> d;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) d->EnableDebugLayer();
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)))) {
        if (error) *error = "CreateDXGIFactory2 failed";
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 ad;
        adapter->GetDesc1(&ad);
        if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)))) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
            m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
            if (o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
                m_adapterName = narrow(ad.Description);
                break;
            }
            m_device.Reset();
        }
    }
    if (!m_device) {
        if (error) *error = "No GPU with DirectX Raytracing tier 1.1 found";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue));
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_cmd));
    m_cmd->Close();
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    m_width = std::max(1u, width);
    m_height = std::max(1u, height);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1))) {
        if (error) *error = "CreateSwapChainForHwnd failed";
        return false;
    }
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&m_swapChain);
    for (uint32_t i = 0; i < kBackBuffers; ++i) m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTexCount + 1;  // + shadow map SRV
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap));
    m_descSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!createPipelines(error)) return false;
    if (!createRasterPipelines(error)) return false;

    m_constants = createBuffer(256 * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);

    D3D12_QUERY_HEAP_DESC qhd = {};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = 2;
    m_device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_queryHeap));
    m_queryReadback = createBuffer(256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 freq = 1;
    m_queue->GetTimestampFrequency(&freq);
    m_tsFreq = (double)freq;
    return true;
}

bool Renderer::createPipelines(std::string* error) {
    // Root signature shared by all compute passes.
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = kTexCount;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER p[9] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[0].Descriptor.ShaderRegister = 0;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[1].Constants.ShaderRegister = 1;
    p[1].Constants.Num32BitValues = 4;
    for (int i = 0; i < 6; ++i) {
        p[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[2 + i].Descriptor.ShaderRegister = i;
    }
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[8].DescriptorTable.NumDescriptorRanges = 1;
    p[8].DescriptorTable.pDescriptorRanges = &range;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 9;
    rsd.pParameters = p;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (error) *error = err ? (const char*)err->GetBufferPointer() : "root signature";
        return false;
    }
    m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));

    auto make = [&](const wchar_t* file, ComPtr<ID3D12PipelineState>& pso) -> bool {
        std::vector<char> code;
        if (!readFile(m_shaderDir + L"\\" + file, code)) {
            if (error) *error = "Missing shader: " + narrow(m_shaderDir + L"\\" + file);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_rootSig.Get();
        d.CS = {code.data(), code.size()};
        if (FAILED(m_device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)))) {
            if (error) *error = "CreateComputePipelineState failed for " + narrow(file);
            return false;
        }
        return true;
    };
    return make(L"PathTrace.cso", m_psoTrace) && make(L"Denoise.cso", m_psoDenoise) && make(L"Tonemap.cso", m_psoTonemap);
}

void Renderer::createTargets(uint32_t w, uint32_t h) {
    m_rtW = w;
    m_rtH = h;
    const DXGI_FORMAT fmts[kTexCount] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                         DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                         DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM};
    auto hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kTexCount; ++i) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = fmts[i];
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (i == TEX_ALBEDO) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        m_tex[i].Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr, IID_PPV_ARGS(&m_tex[i]));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = fmts[i];
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)i * m_descSize;
        m_device->CreateUnorderedAccessView(m_tex[i].Get(), nullptr, &u, hnd);
    }
    createRasterTargets(w, h);
    m_sampleIndex = 0;
}

void Renderer::shutdown() {
    if (!m_device) return;
    waitIdle();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
}

void Renderer::resize(uint32_t width, uint32_t height) {
    if (!m_swapChain || width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    waitIdle();
    for (auto& b : m_backBuffers) b.Reset();
    m_swapChain->ResizeBuffers(kBackBuffers, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    for (uint32_t i = 0; i < kBackBuffers; ++i) m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
    m_width = width;
    m_height = height;
}

void Renderer::beginCommands() {
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
}

void Renderer::waitIdle() {
    if (!m_queue) return;
    ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void Renderer::executeAndWait() {
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
    waitIdle();
}

void Renderer::transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER br = {};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.StateBefore = a;
    br.Transition.StateAfter = b;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &br);
}

void Renderer::uavBarrier(ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER br = {};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    br.UAV.pResource = r;
    m_cmd->ResourceBarrier(1, &br);
}

void Renderer::setScene(const Scene& scene) {
    waitIdle();
    m_meshes.clear();
    m_sceneGeometryVersion = scene.geometryVersion;

    // Concatenate all meshes (+ the internal light quad) into one vertex/index buffer.
    std::vector<gpu::GpuVertex> verts;
    std::vector<uint32_t> inds;
    auto addMesh = [&](const MeshData& m) {
        MeshGpu g = {};
        g.indexOffset = (uint32_t)inds.size();
        g.vertexOffset = (uint32_t)verts.size();
        g.indexCount = (uint32_t)m.indices.size();
        g.vertexCount = (uint32_t)m.vertices.size();
        m.bounds(g.bmin, g.bmax);
        for (auto& v : m.vertices) verts.push_back({v.pos, v.nrm});
        inds.insert(inds.end(), m.indices.begin(), m.indices.end());
        m_meshes.push_back(g);
    };
    for (auto& m : scene.meshes) addMesh(m);
    m_quadMesh = (int)m_meshes.size();
    addMesh(makeQuadXY());

    uint64_t vbSize = verts.size() * sizeof(gpu::GpuVertex), ibSize = inds.size() * sizeof(uint32_t);
    Buffer stageV = createBuffer(vbSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    Buffer stageI = createBuffer(ibSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    memcpy(stageV.mapped, verts.data(), vbSize);
    memcpy(stageI.mapped, inds.data(), ibSize);
    m_vb = createBuffer(vbSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    m_ib = createBuffer(ibSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    beginCommands();
    m_cmd->CopyBufferRegion(m_vb.res.Get(), 0, stageV.res.Get(), 0, vbSize);
    m_cmd->CopyBufferRegion(m_ib.res.Get(), 0, stageI.res.Get(), 0, ibSize);
    const D3D12_RESOURCE_STATES geoRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    transition(m_vb.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, geoRead);
    transition(m_ib.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, geoRead);

    std::vector<Buffer> scratch;
    for (auto& g : m_meshes) {
        if (g.indexCount == 0) continue;
        D3D12_RAYTRACING_GEOMETRY_DESC geo = {};
        geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geo.Triangles.VertexBuffer.StartAddress = m_vb.va() + (uint64_t)g.vertexOffset * sizeof(gpu::GpuVertex);
        geo.Triangles.VertexBuffer.StrideInBytes = sizeof(gpu::GpuVertex);
        geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geo.Triangles.VertexCount = g.vertexCount;
        geo.Triangles.IndexBuffer = m_ib.va() + (uint64_t)g.indexOffset * 4;
        geo.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geo.Triangles.IndexCount = g.indexCount;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        in.NumDescs = 1;
        in.pGeometryDescs = &geo;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
        g.blas = createBuffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        scratch.push_back(createBuffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd = {};
        bd.Inputs = in;
        bd.DestAccelerationStructureData = g.blas.va();
        bd.ScratchAccelerationStructureData = scratch.back().va();
        m_cmd->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    }
    uavBarrier();
    executeAndWait();
    m_sampleIndex = 0;
}

void Renderer::buildTLAS(const Scene& scene, bool buildAS) {
    m_draws.clear();
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs;
    std::vector<gpu::GpuInstance> insts;
    std::vector<gpu::GpuMaterial> mats;
    std::vector<gpu::GpuLight> lights;
    m_objectToInstance.assign(scene.objects.size(), -1);

    for (const Material& m : scene.materials) {
        gpu::GpuMaterial g = {};
        g.baseColor = m.baseColor;
        g.roughness = m.roughness;
        g.emission = m.emission * m.emissionStrength;
        g.metallic = m.metallic;
        g.sssRadius = m.sssRadius;
        g.subsurface = m.subsurface;
        g.transmission = m.transmission;
        g.ior = m.ior;
        g.bevelRadius = m.bevelRadius;
        g.flags = m.flags;
        g.specular = m.specular;
        g.coat = m.coat;
        g.coatRoughness = m.coatRoughness;
        mats.push_back(g);
    }
    if (mats.empty()) mats.push_back(gpu::GpuMaterial{});

    auto addInstance = [&](int meshIdx, const Affine& xf, uint32_t matId, uint32_t lightId, uint32_t mask) -> int {
        if (meshIdx < 0 || meshIdx >= (int)m_meshes.size() || !m_meshes[meshIdx].blas.res) return -1;
        const MeshGpu& g = m_meshes[meshIdx];
        D3D12_RAYTRACING_INSTANCE_DESC d = {};
        memcpy(d.Transform, xf.m, sizeof(d.Transform));
        d.InstanceID = (UINT)insts.size();
        d.InstanceMask = mask;
        d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        d.AccelerationStructure = g.blas.va();
        descs.push_back(d);
        insts.push_back({g.indexOffset, g.vertexOffset, matId, lightId});
        uint32_t flags = lightId != NO_LIGHT ? (mask == MASK_LIGHT_VISIBLE ? 2u : 6u) : 0u;
        m_draws.push_back({meshIdx, xf, matId, (uint32_t)insts.size(), flags, 1.0f});
        return (int)insts.size() - 1;
    };

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const Object& o = scene.objects[i];
        if (!o.visible) continue;
        if (o.ghost) {
            uint32_t gm = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
            if (o.mesh >= 0 && o.mesh < (int)m_meshes.size()) m_draws.push_back({o.mesh, o.transform(), gm, 0u, 1u, o.ghostAlpha});
            continue;
        }
        uint32_t matId = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
        bool glass = matId < scene.materials.size() && scene.materials[matId].transmission > 0.5f;
        m_objectToInstance[i] = addInstance(o.mesh, o.transform(), matId, NO_LIGHT, glass ? MASK_GLASS : MASK_OPAQUE);
    }
    for (const AreaLight& l : scene.lights) {
        if (!l.enabled) continue;
        float3 U, V, N;
        l.basis(U, V, N);
        gpu::GpuMaterial lm = {};
        lm.emission = l.color * l.intensity;
        lm.roughness = 1.0f;
        lm.ior = 1.5f;
        uint32_t matId = (uint32_t)mats.size();
        mats.push_back(lm);
        Affine xf = Affine::fromBasis(U * l.width, V * l.height, N, l.position);
        uint32_t lightId = (uint32_t)lights.size();
        int inst = addInstance(m_quadMesh, xf, matId, lightId,
                               l.visibleToCamera ? MASK_LIGHT_VISIBLE : MASK_LIGHT_HIDDEN);
        gpu::GpuLight gl = {};
        gl.corner = l.position - U * (l.width * 0.5f) - V * (l.height * 0.5f);
        gl.edgeU = U * l.width;
        gl.edgeV = V * l.height;
        gl.area = l.width * l.height;
        gl.radiance = l.color * l.intensity;
        gl.instance = (uint32_t)inst;
        lights.push_back(gl);
    }
    m_lightCount = (uint32_t)lights.size();
    if (lights.empty()) lights.push_back(gpu::GpuLight{});
    if (insts.empty()) insts.push_back(gpu::GpuInstance{});

    ensureUpload(m_instanceDescs, std::max<size_t>(descs.size(), 1) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    ensureUpload(m_instances, insts.size() * sizeof(gpu::GpuInstance));
    ensureUpload(m_materials, mats.size() * sizeof(gpu::GpuMaterial));
    ensureUpload(m_lights, lights.size() * sizeof(gpu::GpuLight));
    if (!descs.empty()) memcpy(m_instanceDescs.mapped, descs.data(), descs.size() * sizeof(descs[0]));
    memcpy(m_instances.mapped, insts.data(), insts.size() * sizeof(insts[0]));
    memcpy(m_materials.mapped, mats.data(), mats.size() * sizeof(mats[0]));
    memcpy(m_lights.mapped, lights.data(), lights.size() * sizeof(lights[0]));

    // Scene bounds (for the raster shadow map), ignoring huge ground planes.
    {
        float3 mn(1e30f), mx(-1e30f);
        for (const Draw& d : m_draws) {
            const MeshGpu& g = m_meshes[d.mesh];
            if (length(g.bmax - g.bmin) > 150.0f || (d.flags & 2u)) continue;
            for (int k = 0; k < 8; ++k) {
                float3 p((k & 1) ? g.bmax.x : g.bmin.x, (k & 2) ? g.bmax.y : g.bmin.y, (k & 4) ? g.bmax.z : g.bmin.z);
                p = d.xf.point(p);
                mn = minv(mn, p);
                mx = maxv(mx, p);
            }
        }
        if (mn.x > mx.x) { mn = float3(-10); mx = float3(10); }
        m_sceneCenter = (mn + mx) * 0.5f;
        m_sceneRadius = std::max(length(mx - mn) * 0.5f, 1.0f);
    }
    if (!buildAS) return;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
    in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    in.NumDescs = (UINT)descs.size();
    in.InstanceDescs = m_instanceDescs.va();
    uint32_t needed = std::max<uint32_t>((uint32_t)descs.size(), 1);
    if (!m_tlas.res || needed > m_tlasCapacity) {
        m_tlasCapacity = std::max(needed * 2, 64u);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS cap = in;
        cap.NumDescs = m_tlasCapacity;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&cap, &info);
        m_tlas = createBuffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        m_tlasScratch = createBuffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd = {};
    bd.Inputs = in;
    bd.DestAccelerationStructureData = m_tlas.va();
    bd.ScratchAccelerationStructureData = m_tlasScratch.va();
    m_cmd->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    uavBarrier(m_tlas.res.Get());
}

void Renderer::render(const Scene& scene, const RenderSettings& rs, const ViewRect& vpIn, int selectedObject,
                      bool toBackBuffer) {
    ViewRect vp = vpIn;
    vp.x = std::max(0, std::min(vp.x, (int)m_width - 1));
    vp.y = std::max(0, std::min(vp.y, (int)m_height - 1));
    vp.w = std::max(1, std::min(vp.w, (int)m_width - vp.x));
    vp.h = std::max(1, std::min(vp.h, (int)m_height - vp.y));
    if ((uint32_t)vp.w != m_rtW || (uint32_t)vp.h != m_rtH) {
        waitIdle();
        createTargets(vp.w, vp.h);
    }
    if (scene.geometryVersion != m_sceneGeometryVersion) setScene(scene);

    beginCommands();
    buildTLAS(scene, !rs.raster);

    const Camera& cam = scene.camera;
    float3 right, up, fwd;
    cam.basis(right, up, fwd);
    gpu::FrameConstants fc = {};
    fc.camPos = cam.eye();
    fc.aperture = cam.apertureRadius();
    fc.camRight = right;
    fc.focusDist = std::max(cam.focusDistance, 1e-3f);
    fc.camUp = up;
    fc.tanHalfFovY = std::tan(cam.fovY * 0.5f);
    fc.camFwd = fwd;
    fc.aspect = (float)vp.w / (float)vp.h;
    fc.resolution = {(uint32_t)vp.w, (uint32_t)vp.h};
    fc.frameIndex = m_frameIndex++;
    fc.sampleIndex = m_sampleIndex;
    fc.maxBounces = (uint32_t)std::max(1, rs.maxBounces);
    fc.lightCount = m_lightCount;
    fc.samplesPerFrame = (uint32_t)std::max(1, rs.samplesPerFrame);
    fc.flags = (cam.dof ? FRAME_FLAG_DOF : 0) | (rs.bevel ? FRAME_FLAG_BEVEL : 0) | (rs.denoise ? FRAME_FLAG_DENOISE : 0) |
               (scene.env.visibleToCamera ? FRAME_FLAG_ENV_VISIBLE : 0);
    fc.envTop = scene.env.top;
    fc.envIntensity = scene.env.intensity;
    fc.envHorizon = scene.env.horizon;
    fc.exposure = rs.exposure;
    fc.envBottom = scene.env.bottom;
    fc.bevelScale = rs.bevelScale;
    int selInst = (selectedObject >= 0 && selectedObject < (int)m_objectToInstance.size()) ? m_objectToInstance[selectedObject] : -1;
    fc.selectedInstance = (uint32_t)(selInst + 1);
    fc.selMoreCount = 0;
    for (int o : m_extraSelection) {
        if (fc.selMoreCount >= 32 || o < 0 || o >= (int)m_objectToInstance.size() || m_objectToInstance[o] < 0) continue;
        reinterpret_cast<uint32_t*>(fc.selMore)[fc.selMoreCount++] = (uint32_t)(m_objectToInstance[o] + 1);
    }
    fc.fireflyClamp = rs.fireflyClamp;
    fc.sssScale = rs.sssScale;
    fc.gridScale = scene.gridScale;
    fc.denoiseStrength = rs.denoiseStrength;
    fc.lightScale = rs.lightScale;
    fc.vignette = rs.vignette;
    fc.bevelSamples = (float)std::max(1, rs.bevelSamples);
    memcpy(m_constants.mapped, &fc, sizeof(fc));

    ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    UINT gx = (vp.w + 7) / 8, gy = (vp.h + 7) / 8;
    if (rs.raster) {
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        recordRaster(scene, rs, vp);
        m_denoisedSlot = 0xFFFFFFFF;
        m_sampleIndex = (uint32_t)std::max(rs.maxSamples, 1);
        m_cmd->SetComputeRootSignature(m_rootSig.Get());
        m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
        m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
        m_cmd->SetPipelineState(m_psoTonemap.Get());
        uint32_t rc[4] = {TEX_DENOISE_A, 1u, (uint32_t)rs.tonemapper, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, rc, 0);
        m_cmd->Dispatch(gx, gy, 1);
    } else {
    m_cmd->SetComputeRootSignature(m_rootSig.Get());
    m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
    m_cmd->SetComputeRootShaderResourceView(2, m_tlas.va());
    m_cmd->SetComputeRootShaderResourceView(3, m_vb.va());
    m_cmd->SetComputeRootShaderResourceView(4, m_ib.va());
    m_cmd->SetComputeRootShaderResourceView(5, m_instances.va());
    m_cmd->SetComputeRootShaderResourceView(6, m_materials.va());
    m_cmd->SetComputeRootShaderResourceView(7, m_lights.va());
    m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());

    bool traced = false;
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    if (m_sampleIndex < (uint32_t)rs.maxSamples) {
        m_cmd->SetPipelineState(m_psoTrace.Get());
        m_cmd->Dispatch(gx, gy, 1);
        uavBarrier();
        m_sampleIndex += fc.samplesPerFrame;
        traced = true;
    }

    // Edge-aware a-trous denoiser (5 iterations), only when new samples arrived.
    uint32_t& denoisedSlot = m_denoisedSlot;
    if (rs.denoise) {
        if (traced || denoisedSlot == 0xFFFFFFFF) {
            m_cmd->SetPipelineState(m_psoDenoise.Get());
            uint32_t src = TEX_ACCUM, dst = TEX_DENOISE_A;
            for (uint32_t pass = 0; pass < 5; ++pass) {
                uint32_t c[4] = {src, dst, 1u << pass, pass == 0 ? 1u : 0u};
                m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
                m_cmd->Dispatch(gx, gy, 1);
                uavBarrier();
                src = dst;
                dst = (dst == TEX_DENOISE_A) ? TEX_DENOISE_B : TEX_DENOISE_A;
            }
            denoisedSlot = src;
        }
    } else {
        denoisedSlot = 0xFFFFFFFF;
    }

    m_cmd->SetPipelineState(m_psoTonemap.Get());
    uint32_t tc[4] = {rs.denoise ? denoisedSlot : 0xFFFFFFFFu, 0, (uint32_t)rs.tonemapper, 0};
    m_cmd->SetComputeRoot32BitConstants(1, 4, tc, 0);
    m_cmd->Dispatch(gx, gy, 1);
    }
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    m_cmd->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_queryReadback.res.Get(), 0);

    if (!toBackBuffer) {
        executeAndWait();
        return;
    }
    if (m_toTexture) {
        transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmd->Close();
        ID3D12CommandList* l[] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1, l);
        return;
    }

    // Copy the viewport image into the back buffer.
    ID3D12Resource* bb = m_backBuffers[backBufferIndex()].Get();
    ID3D12Resource* out = m_tex[TEX_OUTPUT].Get();
    transition(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = bb;
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sl.pResource = out;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box = {0, 0, 0, (UINT)vp.w, (UINT)vp.h, 1};
    m_cmd->CopyTextureRegion(&dl, vp.x, vp.y, 0, &sl, &box);
    transition(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(bb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
}

void Renderer::renderToTexture(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, int selectedObject) {
    m_toTexture = true;
    render(scene, rs, vp, selectedObject, true);
    m_toTexture = false;
}

void Renderer::present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
    waitIdle();
    void* p = nullptr;
    D3D12_RANGE r = {0, 16};
    if (SUCCEEDED(m_queryReadback.res->Map(0, &r, &p))) {
        uint64_t* ts = (uint64_t*)p;
        if (ts[1] > ts[0]) m_gpuMs = (float)((ts[1] - ts[0]) * 1000.0 / m_tsFreq);
        D3D12_RANGE w = {0, 0};
        m_queryReadback.res->Unmap(0, &w);
    }
}

bool Renderer::readBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    waitIdle();
    ID3D12Resource* bb = m_backBuffers[backBufferIndex()].Get();
    D3D12_RESOURCE_DESC d = bb->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = bb;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    executeAndWait();
    w = (uint32_t)d.Width;
    h = d.Height;
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &r, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(&rgba[(size_t)y * w * 4], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * 4);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

bool Renderer::readViewport(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    if (!m_tex[TEX_OUTPUT]) return false;
    waitIdle();
    w = m_rtW;
    h = m_rtH;
    D3D12_RESOURCE_DESC d = m_tex[TEX_OUTPUT]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = m_tex[TEX_OUTPUT].Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    executeAndWait();
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &r, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(&rgba[(size_t)y * w * 4], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * 4);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

} // namespace vp3d


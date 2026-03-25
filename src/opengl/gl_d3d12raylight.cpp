#include "opengl.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <stdint.h>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// ============================================================
// Logging / checks
// ============================================================

static void glRaytracingLog(const char* fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

static void glRaytracingFatal(const char* fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    MessageBoxA(nullptr, buffer, "glRaytracing Fatal", MB_OK | MB_ICONERROR);
    DebugBreak();
}

#define GLR_CHECK(x) \
    do { HRESULT _hr = (x); if (FAILED(_hr)) { glRaytracingFatal("HRESULT 0x%08X failed at %s:%d", (unsigned)_hr, __FILE__, __LINE__); return 0; } } while (0)

#define GLR_CHECKV(x) \
    do { HRESULT _hr = (x); if (FAILED(_hr)) { glRaytracingFatal("HRESULT 0x%08X failed at %s:%d", (unsigned)_hr, __FILE__, __LINE__); return; } } while (0)

// ============================================================
// Helpers
// ============================================================

static UINT64 glRaytracingAlignUp(UINT64 v, UINT64 a)
{
    return (v + (a - 1)) & ~(a - 1);
}

template<typename T>
static T glRaytracingClamp(T v, T lo, T hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static DXGI_FORMAT glRaytracingGetSrvFormatForDepth(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_D32_FLOAT:         return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:         return DXGI_FORMAT_R16_UNORM;
    default:                            return fmt;
    }
}

struct glRaytracingBuffer_t
{
    ComPtr<ID3D12Resource> resource;
    UINT64 size;
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA;

    glRaytracingBuffer_t()
    {
        size = 0;
        gpuVA = 0;
    }
};

static glRaytracingBuffer_t glRaytracingCreateBuffer(
    ID3D12Device* device,
    UINT64 size,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags)
{
    glRaytracingBuffer_t out;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = heapType;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;

    HRESULT hr = device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        initialState,
        nullptr,
        IID_PPV_ARGS(&out.resource));

    if (FAILED(hr))
    {
        glRaytracingFatal("CreateCommittedResource failed 0x%08X", (unsigned)hr);
        return out;
    }

    out.size = size;
    out.gpuVA = out.resource->GetGPUVirtualAddress();
    return out;
}

static void glRaytracingMapCopy(ID3D12Resource* res, const void* src, size_t bytes)
{
    void* dst = nullptr;
    HRESULT hr = res->Map(0, nullptr, &dst);
    if (FAILED(hr))
    {
        glRaytracingFatal("Map failed 0x%08X", (unsigned)hr);
        return;
    }

    memcpy(dst, src, bytes);
    res->Unmap(0, nullptr);
}

static void glRaytracingTransition(
    ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    if (!res || before == after)
        return;

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

static D3D12_CPU_DESCRIPTOR_HANDLE glRaytracingOffsetCpu(D3D12_CPU_DESCRIPTOR_HANDLE h, UINT stride, UINT idx)
{
    h.ptr += UINT64(stride) * UINT64(idx);
    return h;
}

static D3D12_GPU_DESCRIPTOR_HANDLE glRaytracingOffsetGpu(D3D12_GPU_DESCRIPTOR_HANDLE h, UINT stride, UINT idx)
{
    h.ptr += UINT64(stride) * UINT64(idx);
    return h;
}

// ============================================================
// Shared command context
// ============================================================

struct glRaytracingCmdContext_t
{
    ComPtr<ID3D12Device5> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList4> cmdList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent;
    UINT64 fenceValue;
    bool initialized;

    glRaytracingCmdContext_t()
    {
        fenceEvent = nullptr;
        fenceValue = 0;
        initialized = false;
    }
};

static glRaytracingCmdContext_t g_glRaytracingCmd;

static void glRaytracingWaitIdle(void)
{
    if (!g_glRaytracingCmd.queue || !g_glRaytracingCmd.fence)
        return;

    ++g_glRaytracingCmd.fenceValue;
    g_glRaytracingCmd.queue->Signal(g_glRaytracingCmd.fence.Get(), g_glRaytracingCmd.fenceValue);

    if (g_glRaytracingCmd.fence->GetCompletedValue() < g_glRaytracingCmd.fenceValue)
    {
        g_glRaytracingCmd.fence->SetEventOnCompletion(g_glRaytracingCmd.fenceValue, g_glRaytracingCmd.fenceEvent);
        WaitForSingleObject(g_glRaytracingCmd.fenceEvent, INFINITE);
    }
}

static int glRaytracingInitCmdContext(void)
{
    if (g_glRaytracingCmd.initialized)
        return 1;

    ID3D12Device* baseDevice = QD3D12_GetDevice();
    ID3D12CommandQueue* baseQueue = QD3D12_GetQueue();

    if (!baseDevice || !baseQueue)
    {
        glRaytracingFatal("glRaytracingInitCmdContext: missing device or queue");
        return 0;
    }

    GLR_CHECK(baseDevice->QueryInterface(IID_PPV_ARGS(&g_glRaytracingCmd.device)));
    g_glRaytracingCmd.queue = baseQueue;

    GLR_CHECK(g_glRaytracingCmd.device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&g_glRaytracingCmd.cmdAlloc)));

    GLR_CHECK(g_glRaytracingCmd.device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_glRaytracingCmd.cmdAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&g_glRaytracingCmd.cmdList)));

    GLR_CHECK(g_glRaytracingCmd.cmdList->Close());

    GLR_CHECK(g_glRaytracingCmd.device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&g_glRaytracingCmd.fence)));

    g_glRaytracingCmd.fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!g_glRaytracingCmd.fenceEvent)
    {
        glRaytracingFatal("CreateEventA failed");
        return 0;
    }

    g_glRaytracingCmd.initialized = true;
    return 1;
}

static void glRaytracingShutdownCmdContext(void)
{
    if (!g_glRaytracingCmd.initialized)
        return;

    glRaytracingWaitIdle();

    if (g_glRaytracingCmd.fenceEvent)
    {
        CloseHandle(g_glRaytracingCmd.fenceEvent);
        g_glRaytracingCmd.fenceEvent = nullptr;
    }

    g_glRaytracingCmd = glRaytracingCmdContext_t();
}

static int glRaytracingBeginCmd(void)
{
    glRaytracingWaitIdle();

    GLR_CHECK(g_glRaytracingCmd.cmdAlloc->Reset());
    GLR_CHECK(g_glRaytracingCmd.cmdList->Reset(g_glRaytracingCmd.cmdAlloc.Get(), nullptr));
    return 1;
}

static int glRaytracingEndCmd(void)
{
    GLR_CHECK(g_glRaytracingCmd.cmdList->Close());

    ID3D12CommandList* lists[] = { g_glRaytracingCmd.cmdList.Get() };
    g_glRaytracingCmd.queue->ExecuteCommandLists(1, lists);

    glRaytracingWaitIdle();
    return 1;
}

// ============================================================
// Scene builder state
// ============================================================

struct glRaytracingMeshRecord_t
{
    uint32_t handle;
    int      alive;

    glRaytracingMeshDesc_t descCpu;

    std::vector<glRaytracingVertex_t> verticesCpu;
    std::vector<uint32_t>             indicesCpu;

    glRaytracingBuffer_t vertexBuffer;
    glRaytracingBuffer_t indexBuffer;

    glRaytracingBuffer_t blasScratch;
    glRaytracingBuffer_t blasResult;

    UINT64 blasScratchSize;
    UINT64 blasResultSize;

    int blasBuilt;
    int dirty;

    glRaytracingMeshRecord_t()
    {
        handle = 0;
        alive = 0;
        memset(&descCpu, 0, sizeof(descCpu));
        blasScratchSize = 0;
        blasResultSize = 0;
        blasBuilt = 0;
        dirty = 0;
    }
};

struct glRaytracingInstanceRecord_t
{
    uint32_t handle;
    int      alive;
    glRaytracingInstanceDesc_t descCpu;
    int dirty;

    glRaytracingInstanceRecord_t()
    {
        handle = 0;
        alive = 0;
        memset(&descCpu, 0, sizeof(descCpu));
        dirty = 0;
    }
};

struct glRaytracingSceneState_t
{
    std::vector<glRaytracingMeshRecord_t> meshes;
    std::vector<glRaytracingInstanceRecord_t> instances;

    uint32_t nextMeshHandle;
    uint32_t nextInstanceHandle;

    glRaytracingBuffer_t instanceDescUpload;
    glRaytracingBuffer_t tlasScratch;
    glRaytracingBuffer_t tlasResult;

    UINT64 tlasScratchSize;
    UINT64 tlasResultSize;

    int initialized;

    glRaytracingSceneState_t()
    {
        nextMeshHandle = 1;
        nextInstanceHandle = 1;
        tlasScratchSize = 0;
        tlasResultSize = 0;
        initialized = 0;
    }
};

static glRaytracingSceneState_t g_glRaytracingScene;

static glRaytracingMeshRecord_t* glRaytracingFindMesh(uint32_t handle)
{
    for (size_t i = 0; i < g_glRaytracingScene.meshes.size(); ++i)
    {
        if (g_glRaytracingScene.meshes[i].alive && g_glRaytracingScene.meshes[i].handle == handle)
            return &g_glRaytracingScene.meshes[i];
    }
    return nullptr;
}

static const glRaytracingMeshRecord_t* glRaytracingFindMeshConst(uint32_t handle)
{
    for (size_t i = 0; i < g_glRaytracingScene.meshes.size(); ++i)
    {
        if (g_glRaytracingScene.meshes[i].alive && g_glRaytracingScene.meshes[i].handle == handle)
            return &g_glRaytracingScene.meshes[i];
    }
    return nullptr;
}

static glRaytracingInstanceRecord_t* glRaytracingFindInstance(uint32_t handle)
{
    for (size_t i = 0; i < g_glRaytracingScene.instances.size(); ++i)
    {
        if (g_glRaytracingScene.instances[i].alive && g_glRaytracingScene.instances[i].handle == handle)
            return &g_glRaytracingScene.instances[i];
    }
    return nullptr;
}

static int glRaytracingUploadMeshBuffers(glRaytracingMeshRecord_t* mesh)
{
    if (!mesh)
        return 0;

    if (mesh->verticesCpu.empty() || mesh->indicesCpu.empty())
        return 0;

    const UINT64 vbBytes = UINT64(mesh->verticesCpu.size()) * sizeof(glRaytracingVertex_t);
    const UINT64 ibBytes = UINT64(mesh->indicesCpu.size()) * sizeof(uint32_t);

    mesh->vertexBuffer = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        vbBytes,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    mesh->indexBuffer = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        ibBytes,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    if (!mesh->vertexBuffer.resource || !mesh->indexBuffer.resource)
        return 0;

    glRaytracingMapCopy(mesh->vertexBuffer.resource.Get(), mesh->verticesCpu.data(), (size_t)vbBytes);
    glRaytracingMapCopy(mesh->indexBuffer.resource.Get(), mesh->indicesCpu.data(), (size_t)ibBytes);

    return 1;
}

static int glRaytracingBuildMeshInternal(glRaytracingMeshRecord_t* mesh)
{
    if (!mesh)
        return 0;

    if (!mesh->vertexBuffer.resource || !mesh->indexBuffer.resource)
    {
        if (!glRaytracingUploadMeshBuffers(mesh))
            return 0;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Flags = mesh->descCpu.opaque
        ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
        : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

    geomDesc.Triangles.Transform3x4 = 0;
    geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geomDesc.Triangles.IndexCount = (UINT)mesh->indicesCpu.size();
    geomDesc.Triangles.VertexCount = (UINT)mesh->verticesCpu.size();
    geomDesc.Triangles.IndexBuffer = mesh->indexBuffer.gpuVA;
    geomDesc.Triangles.VertexBuffer.StartAddress = mesh->vertexBuffer.gpuVA;
    geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(glRaytracingVertex_t);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geomDesc;
    inputs.Flags = mesh->descCpu.allowUpdate
        ? (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
        : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    g_glRaytracingCmd.device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    if (prebuild.ResultDataMaxSizeInBytes == 0)
    {
        glRaytracingFatal("BLAS prebuild size is zero");
        return 0;
    }

    mesh->blasScratchSize = glRaytracingAlignUp(prebuild.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    mesh->blasResultSize = glRaytracingAlignUp(prebuild.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

    mesh->blasScratch = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        mesh->blasScratchSize,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    mesh->blasResult = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        mesh->blasResultSize,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    if (!mesh->blasScratch.resource || !mesh->blasResult.resource)
        return 0;

    if (!glRaytracingBeginCmd())
        return 0;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = mesh->blasScratch.gpuVA;
    buildDesc.DestAccelerationStructureData = mesh->blasResult.gpuVA;
    buildDesc.SourceAccelerationStructureData = 0;

    g_glRaytracingCmd.cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav = {};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = mesh->blasResult.resource.Get();
    g_glRaytracingCmd.cmdList->ResourceBarrier(1, &uav);

    if (!glRaytracingEndCmd())
        return 0;

    mesh->blasBuilt = 1;
    mesh->dirty = 0;
    return 1;
}

static int glRaytracingBuildSceneInternal(void)
{
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instDescs;

    for (size_t i = 0; i < g_glRaytracingScene.instances.size(); ++i)
    {
        const glRaytracingInstanceRecord_t& inst = g_glRaytracingScene.instances[i];
        if (!inst.alive)
            continue;

        const glRaytracingMeshRecord_t* mesh = glRaytracingFindMeshConst(inst.descCpu.meshHandle);
        if (!mesh || !mesh->blasBuilt || !mesh->blasResult.resource)
            continue;

        D3D12_RAYTRACING_INSTANCE_DESC d = {};
        memcpy(d.Transform, inst.descCpu.transform, sizeof(float) * 12);
        d.InstanceID = inst.descCpu.instanceID;
        d.InstanceMask = (UINT8)(inst.descCpu.mask ? inst.descCpu.mask : 0xFF);
        d.InstanceContributionToHitGroupIndex = 0;
        d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        d.AccelerationStructure = mesh->blasResult.gpuVA;

        instDescs.push_back(d);
    }

    if (instDescs.empty())
    {
        g_glRaytracingScene.instanceDescUpload.resource.Reset();
        g_glRaytracingScene.tlasScratch.resource.Reset();
        g_glRaytracingScene.tlasResult.resource.Reset();
        g_glRaytracingScene.tlasScratchSize = 0;
        g_glRaytracingScene.tlasResultSize = 0;
        return 1;
    }

    const UINT64 instBytes = glRaytracingAlignUp(
        UINT64(instDescs.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
        D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);

    g_glRaytracingScene.instanceDescUpload = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        instBytes,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    if (!g_glRaytracingScene.instanceDescUpload.resource)
        return 0;

    glRaytracingMapCopy(
        g_glRaytracingScene.instanceDescUpload.resource.Get(),
        instDescs.data(),
        size_t(UINT64(instDescs.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = (UINT)instDescs.size();
    inputs.InstanceDescs = g_glRaytracingScene.instanceDescUpload.gpuVA;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    g_glRaytracingCmd.device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    if (prebuild.ResultDataMaxSizeInBytes == 0)
    {
        glRaytracingFatal("TLAS prebuild size is zero");
        return 0;
    }

    g_glRaytracingScene.tlasScratchSize = glRaytracingAlignUp(prebuild.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    g_glRaytracingScene.tlasResultSize = glRaytracingAlignUp(prebuild.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

    g_glRaytracingScene.tlasScratch = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        g_glRaytracingScene.tlasScratchSize,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    g_glRaytracingScene.tlasResult = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        g_glRaytracingScene.tlasResultSize,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    if (!g_glRaytracingScene.tlasScratch.resource || !g_glRaytracingScene.tlasResult.resource)
        return 0;

    if (!glRaytracingBeginCmd())
        return 0;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = g_glRaytracingScene.tlasScratch.gpuVA;
    buildDesc.DestAccelerationStructureData = g_glRaytracingScene.tlasResult.gpuVA;
    buildDesc.SourceAccelerationStructureData = 0;

    g_glRaytracingCmd.cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav = {};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = g_glRaytracingScene.tlasResult.resource.Get();
    g_glRaytracingCmd.cmdList->ResourceBarrier(1, &uav);

    if (!glRaytracingEndCmd())
        return 0;

    for (size_t i = 0; i < g_glRaytracingScene.instances.size(); ++i)
    {
        if (g_glRaytracingScene.instances[i].alive)
            g_glRaytracingScene.instances[i].dirty = 0;
    }

    return 1;
}

// ============================================================
// Scene public API
// ============================================================

extern "C" int glRaytracingInit(void)
{
    if (g_glRaytracingScene.initialized)
        return 1;

    if (!glRaytracingInitCmdContext())
        return 0;

    g_glRaytracingScene.initialized = 1;
    glRaytracingLog("glRaytracingInit ok");
    return 1;
}

extern "C" void glRaytracingShutdown(void)
{
    if (!g_glRaytracingScene.initialized)
        return;

    g_glRaytracingScene = glRaytracingSceneState_t();
    glRaytracingShutdownCmdContext();
}

extern "C" void glRaytracingClear(void)
{
    g_glRaytracingScene.meshes.clear();
    g_glRaytracingScene.instances.clear();

    g_glRaytracingScene.instanceDescUpload.resource.Reset();
    g_glRaytracingScene.tlasScratch.resource.Reset();
    g_glRaytracingScene.tlasResult.resource.Reset();
    g_glRaytracingScene.tlasScratchSize = 0;
    g_glRaytracingScene.tlasResultSize = 0;
}

extern "C" glRaytracingMeshHandle_t glRaytracingCreateMesh(const glRaytracingMeshDesc_t* desc)
{
    if (!g_glRaytracingScene.initialized || !desc)
        return 0;

    if (!desc->vertices || !desc->indices || desc->vertexCount == 0 || desc->indexCount == 0)
        return 0;

    glRaytracingMeshRecord_t mesh;
    mesh.handle = g_glRaytracingScene.nextMeshHandle++;
    mesh.alive = 1;
    mesh.descCpu = *desc;
    mesh.verticesCpu.assign(desc->vertices, desc->vertices + desc->vertexCount);
    mesh.indicesCpu.assign(desc->indices, desc->indices + desc->indexCount);
    mesh.descCpu.vertices = nullptr;
    mesh.descCpu.indices = nullptr;
    mesh.dirty = 1;

    g_glRaytracingScene.meshes.push_back(mesh);
    return mesh.handle;
}

extern "C" int glRaytracingUpdateMesh(glRaytracingMeshHandle_t meshHandle, const glRaytracingMeshDesc_t* desc)
{
    if (!g_glRaytracingScene.initialized || !desc)
        return 0;

    glRaytracingMeshRecord_t* mesh = glRaytracingFindMesh(meshHandle);
    if (!mesh)
        return 0;

    if (!desc->vertices || !desc->indices || desc->vertexCount == 0 || desc->indexCount == 0)
        return 0;

    mesh->descCpu = *desc;
    mesh->verticesCpu.assign(desc->vertices, desc->vertices + desc->vertexCount);
    mesh->indicesCpu.assign(desc->indices, desc->indices + desc->indexCount);
    mesh->descCpu.vertices = nullptr;
    mesh->descCpu.indices = nullptr;

    mesh->vertexBuffer.resource.Reset();
    mesh->indexBuffer.resource.Reset();
    mesh->blasScratch.resource.Reset();
    mesh->blasResult.resource.Reset();
    mesh->blasBuilt = 0;
    mesh->dirty = 1;

    return 1;
}

extern "C" void glRaytracingDeleteMesh(glRaytracingMeshHandle_t meshHandle)
{
    glRaytracingMeshRecord_t* mesh = glRaytracingFindMesh(meshHandle);
    if (!mesh)
        return;

    for (size_t i = 0; i < g_glRaytracingScene.instances.size(); ++i)
    {
        if (g_glRaytracingScene.instances[i].alive && g_glRaytracingScene.instances[i].descCpu.meshHandle == meshHandle)
            g_glRaytracingScene.instances[i].alive = 0;
    }

    mesh->alive = 0;
}

extern "C" glRaytracingInstanceHandle_t glRaytracingCreateInstance(const glRaytracingInstanceDesc_t* desc)
{
    if (!g_glRaytracingScene.initialized || !desc)
        return 0;

    if (!glRaytracingFindMeshConst(desc->meshHandle))
        return 0;

    glRaytracingInstanceRecord_t inst;
    inst.handle = g_glRaytracingScene.nextInstanceHandle++;
    inst.alive = 1;
    inst.descCpu = *desc;
    inst.dirty = 1;

    g_glRaytracingScene.instances.push_back(inst);
    return inst.handle;
}

extern "C" int glRaytracingUpdateInstance(glRaytracingInstanceHandle_t instanceHandle, const glRaytracingInstanceDesc_t* desc)
{
    if (!g_glRaytracingScene.initialized || !desc)
        return 0;

    if (!glRaytracingFindMeshConst(desc->meshHandle))
        return 0;

    glRaytracingInstanceRecord_t* inst = glRaytracingFindInstance(instanceHandle);
    if (!inst)
        return 0;

    inst->descCpu = *desc;
    inst->dirty = 1;
    return 1;
}

extern "C" void glRaytracingDeleteInstance(glRaytracingInstanceHandle_t instanceHandle)
{
    glRaytracingInstanceRecord_t* inst = glRaytracingFindInstance(instanceHandle);
    if (!inst)
        return;

    inst->alive = 0;
}

extern "C" int glRaytracingBuildMesh(glRaytracingMeshHandle_t meshHandle)
{
    if (!g_glRaytracingScene.initialized)
        return 0;

    glRaytracingMeshRecord_t* mesh = glRaytracingFindMesh(meshHandle);
    if (!mesh)
        return 0;

    return glRaytracingBuildMeshInternal(mesh);
}

extern "C" int glRaytracingBuildAllMeshes(void)
{
    if (!g_glRaytracingScene.initialized)
        return 0;

    for (size_t i = 0; i < g_glRaytracingScene.meshes.size(); ++i)
    {
        glRaytracingMeshRecord_t& mesh = g_glRaytracingScene.meshes[i];
        if (!mesh.alive)
            continue;

        if (!mesh.blasBuilt || mesh.dirty)
        {
            if (!glRaytracingBuildMeshInternal(&mesh))
                return 0;
        }
    }

    return 1;
}

extern "C" int glRaytracingBuildScene(void)
{
    if (!g_glRaytracingScene.initialized)
        return 0;

    if (!glRaytracingBuildAllMeshes())
        return 0;

    return glRaytracingBuildSceneInternal();
}

extern "C" ID3D12Resource* glRaytracingGetTopLevelAS(void)
{
    return g_glRaytracingScene.tlasResult.resource.Get();
}

extern "C" uint32_t glRaytracingGetMeshCount(void)
{
    uint32_t count = 0;
    for (size_t i = 0; i < g_glRaytracingScene.meshes.size(); ++i)
    {
        if (g_glRaytracingScene.meshes[i].alive)
            ++count;
    }
    return count;
}

extern "C" uint32_t glRaytracingGetInstanceCount(void)
{
    uint32_t count = 0;
    for (size_t i = 0; i < g_glRaytracingScene.instances.size(); ++i)
    {
        if (g_glRaytracingScene.instances[i].alive)
            ++count;
    }
    return count;
}

// ============================================================
// Lighting state
// ============================================================

struct glRaytracingLightingConstants_t
{
    float invViewProj[16];
    float invViewMatrix[16];
    float cameraPos[4];
    float ambientColor[4];
    float screenSize[4];
    float normalReconstructZ;
    uint32_t lightCount;
    uint32_t enableSpecular;
    uint32_t enableHalfLambert;
    float shadowBias;
};

struct glRaytracingLightingState_t
{
    std::vector<glRaytracingLight_t> cpuLights;
    glRaytracingLightingConstants_t constants;

    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    UINT descriptorStride;

    glRaytracingBuffer_t constantBuffer;
    glRaytracingBuffer_t lightBuffer;

    ComPtr<ID3D12RootSignature> globalRootSig;
    ComPtr<ID3D12RootSignature> localRootSig;

    ComPtr<ID3D12StateObject> rtStateObject;
    ComPtr<ID3D12StateObjectProperties> rtStateProps;

    glRaytracingBuffer_t raygenTable;
    glRaytracingBuffer_t missTable;
    glRaytracingBuffer_t hitTable;

    bool initialized;

    glRaytracingLightingState_t()
    {
        memset(&constants, 0, sizeof(constants));
        descriptorStride = 0;
        initialized = false;
    }
};

static glRaytracingLightingState_t g_glRaytracingLighting;
static const char* g_glRaytracingLightingHlsl = R"(
struct Light
{
    float3 position;
    float radius;
    float3 color;
    float intensity;
};

struct ShadowPayload
{
    uint hit;
};

cbuffer LightingCB : register(b0)
{
    float4x4 gInvViewProj;
    float4x4 gInvViewMatrix;
    float4   gCameraPos;
    float4   gAmbientColor;
    float4   gScreenSize;
    float    gNormalReconstructZ;
    uint     gLightCount;
    uint     gEnableSpecular;
    uint     gEnableHalfLambert;
    float    gShadowBias;
};

StructuredBuffer<Light> gLights : register(t0);
Texture2D<float4>       gAlbedoTex   : register(t1);
Texture2D<float>        gDepthTex    : register(t2);
Texture2D<float4>       gNormalTex   : register(t3);
Texture2D<float4>       gPositionTex : register(t4);
RaytracingAccelerationStructure gSceneBVH : register(t5);
RWTexture2D<float4>     gOutputTex   : register(u0);

float3 LoadScenePosition(uint2 pixel)
{
    float4 p = gPositionTex.Load(int3(pixel, 0));
    return p.xyz;
}

float3 LoadSceneNormal(uint2 pixel)
{
    float4 nSample = gNormalTex.Load(int3(pixel, 0));
    float3 N = nSample.xyz;
    return normalize(N);
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.hit = 0;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hit = 1;
}

float TraceShadow(float3 origin, float3 dir, float maxT)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = maxT;

    ShadowPayload payload;
    payload.hit = 0;

    TraceRay(
        gSceneBVH,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);

    return (payload.hit != 0) ? 0.0 : 1.0;
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 Hammersley2D(uint i, uint N, float rand)
{
    float e1 = frac((float)i / (float)N + rand);

    uint bits = i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);

    float e2 = (float)bits * 2.3283064365386963e-10;
    return float2(e1, e2);
}

float2 ConcentricSampleDisk(float2 u)
{
    float2 uOffset = 2.0 * u - 1.0;

    if (uOffset.x == 0.0 && uOffset.y == 0.0)
        return float2(0.0, 0.0);

    float r, theta;
    if (abs(uOffset.x) > abs(uOffset.y))
    {
        r = uOffset.x;
        theta = (3.14159265 / 4.0) * (uOffset.y / uOffset.x);
    }
    else
    {
        r = uOffset.y;
        theta = (3.14159265 / 2.0) - (3.14159265 / 4.0) * (uOffset.x / uOffset.y);
    }

    return r * float2(cos(theta), sin(theta));
}

void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.z) < 0.999) ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float3 CosineSampleHemisphere(float2 u)
{
    float2 d = ConcentricSampleDisk(u);
    float z = sqrt(saturate(1.0 - dot(d, d)));
    return float3(d.x, d.y, z);
}

float TraceSoftShadow(float3 worldPos, float3 N, Light Lgt, float3 toLight, float dist)
{
    const uint SHADOW_SAMPLES = 8;

    float3 L = toLight / max(dist, 1e-6);

    float3 tangent, bitangent;
    BuildOrthonormalBasis(L, tangent, bitangent);

    float areaRadius = max(Lgt.radius * 0.08, 2.0);

    float shadowAccum = 0.0;
    float rand = Hash12(worldPos.xy + float2(worldPos.z, dist));

    [unroll]
    for (uint s = 0; s < SHADOW_SAMPLES; ++s)
    {
        float2 xi = Hammersley2D(s, SHADOW_SAMPLES, rand);
        float2 d  = ConcentricSampleDisk(xi) * areaRadius;

        float3 sampleLightPos = Lgt.position + tangent * d.x + bitangent * d.y;
        float3 sampleVec      = sampleLightPos - worldPos;
        float  sampleDist     = length(sampleVec);

        if (sampleDist <= 1e-4)
        {
            shadowAccum += 1.0;
            continue;
        }

        float3 sampleDir = sampleVec / sampleDist;

        float3 shadowOrigin = worldPos + N * (gShadowBias * 0.75) + sampleDir * (gShadowBias * 0.75);
        float  shadowTMax   = max(sampleDist - gShadowBias * 2.0, 0.001);

        shadowAccum += TraceShadow(shadowOrigin, sampleDir, shadowTMax);
    }

    return shadowAccum / (float)SHADOW_SAMPLES;
}

float ComputeAmbientOcclusion(float3 worldPos, float3 N, uint2 pixel)
{
    const uint AO_SAMPLES = 24;
    const float AO_RADIUS = 32.0;

    float3 tangent, bitangent;
    BuildOrthonormalBasis(N, tangent, bitangent);

    float rand = Hash12((float2)pixel + worldPos.xy + worldPos.zz);

    float visibility = 0.0;

    [unroll]
    for (uint i = 0; i < AO_SAMPLES; ++i)
    {
        float2 xi = Hammersley2D(i, AO_SAMPLES, rand);
        float3 h  = CosineSampleHemisphere(xi);

        float3 aoDir =
            tangent   * h.x +
            bitangent * h.y +
            N         * h.z;

        aoDir = normalize(aoDir);

        float3 aoOrigin = worldPos + N * (gShadowBias * 2.0) + aoDir * (gShadowBias * 1.5);

        visibility += TraceShadow(aoOrigin, aoDir, AO_RADIUS);
    }

    visibility /= (float)AO_SAMPLES;
    visibility = saturate(pow(visibility, 1.5));

    return visibility;
}

float ComputeSkyVisibility(float3 worldPos, float3 N, uint2 pixel)
{
    const uint SKY_SAMPLES = 8;
    const float SKY_TMAX   = 1000000.0;

    float3 tangent, bitangent;
    BuildOrthonormalBasis(N, tangent, bitangent);

    float rand = Hash12((float2)pixel * 1.37 + worldPos.xy + float2(worldPos.z, dot(N.xy, N.xy)));

    float vis = 0.0;

    [unroll]
    for (uint i = 0; i < SKY_SAMPLES; ++i)
    {
        float2 xi = Hammersley2D(i, SKY_SAMPLES, rand);
        float3 h  = CosineSampleHemisphere(xi);

        float3 skyDir =
            tangent   * h.x +
            bitangent * h.y +
            N         * h.z;

        skyDir = normalize(skyDir);

        // only count rays that actually go upward/outside
        if (skyDir.z <= 0.05)
            continue;

        float3 skyOrigin = worldPos + N * (gShadowBias * 2.0) + skyDir * (gShadowBias * 2.0);
        vis += TraceShadow(skyOrigin, skyDir, SKY_TMAX);
    }

    vis /= (float)SKY_SAMPLES;
    return saturate(vis);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;

    if (pixel.x >= (uint)gScreenSize.x || pixel.y >= (uint)gScreenSize.y)
        return;

    float4 albedoSample = gAlbedoTex.Load(int3(pixel, 0));
    float depthSample   = gDepthTex.Load(int3(pixel, 0));

    if (depthSample <= 0.0 || depthSample >= 1.0)
    {
        gOutputTex[pixel] = albedoSample;
        return;
    }

    float3 albedo   = albedoSample.rgb;
    float3 worldPos = LoadScenePosition(pixel);
    float3 N        = LoadSceneNormal(pixel);
    float3 V        = normalize(gCameraPos.xyz - worldPos);

    float ao      = ComputeAmbientOcclusion(worldPos, N, pixel);
    float skyVis  = ComputeSkyVisibility(worldPos, N, pixel);

    float upness = saturate(N.z * 0.5 + 0.5);

    float3 skyColor =
        float3(1, 1, 1) * (0.35 + 0.65 * upness);

    float skyStrength = 2.0;

    float3 lightingAccum = albedo * 0.03;

    lightingAccum += albedo * skyColor * (skyStrength * skyVis);

    [loop]
    for (uint i = 0; i < gLightCount; ++i)
    {
        Light Lgt = gLights[i];

        float3 toLight = Lgt.position - worldPos;
        float  distSq  = dot(toLight, toLight);
        float  dist    = sqrt(max(distSq, 1e-6));
        float3 L       = toLight / dist;

        float radius = max(Lgt.radius, 1e-4);

        float atten = saturate((radius - dist) / radius);
        float NdotL = saturate(dot(N, L));

        float shadow = 1.0;
        if (NdotL > 0.0001 && atten > 0.0 && dist > 0.01)
        {
            shadow = TraceSoftShadow(worldPos, N, Lgt, toLight, dist);
        }

        float3 diffuse = albedo * Lgt.color * (Lgt.intensity * atten * NdotL * shadow);
        lightingAccum += diffuse;
    }

    gOutputTex[pixel] = float4(lightingAccum  * ao, albedoSample.a);
}
)";

static ComPtr<IDxcBlob> glRaytracingLightingCompileLibrary(const char* src)
{
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> includeHandler;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr))
    {
        glRaytracingFatal("DxcCreateInstance utils failed 0x%08X", (unsigned)hr);
        return nullptr;
    }

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr))
    {
        glRaytracingFatal("DxcCreateInstance compiler failed 0x%08X", (unsigned)hr);
        return nullptr;
    }

    hr = utils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr))
    {
        glRaytracingFatal("CreateDefaultIncludeHandler failed 0x%08X", (unsigned)hr);
        return nullptr;
    }

    DxcBuffer source = {};
    source.Ptr = src;
    source.Size = strlen(src);
    source.Encoding = DXC_CP_UTF8;

    const wchar_t* args[] =
    {
        L"-T", L"lib_6_3",
        L"-Zi",
        L"-Qembed_debug",
        L"-O3",
        L"-all_resources_bound"
    };

    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&source, args, _countof(args), includeHandler.Get(), IID_PPV_ARGS(&result));
    if (FAILED(hr))
    {
        glRaytracingFatal("DXC compile failed 0x%08X", (unsigned)hr);
        return nullptr;
    }

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
    {
        OutputDebugStringA(errors->GetStringPointer());
        OutputDebugStringA("\n");
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status))
    {
        glRaytracingFatal("DXIL compile status failed 0x%08X", (unsigned)status);
        return nullptr;
    }

    ComPtr<IDxcBlob> dxil;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxil), nullptr);
    return dxil;
}

static int glRaytracingLightingCreateDescriptorHeap(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = 7;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    GLR_CHECK(g_glRaytracingCmd.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_glRaytracingLighting.descriptorHeap)));
    g_glRaytracingLighting.descriptorStride =
        g_glRaytracingCmd.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return 1;
}

static int glRaytracingLightingCreateRootSignatures(void)
{
    {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};

        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 6;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;

        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[3] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = _countof(params);
        rsd.pParameters = params;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig;
        ComPtr<ID3DBlob> err;
        GLR_CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        GLR_CHECK(g_glRaytracingCmd.device->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&g_glRaytracingLighting.globalRootSig)));
    }

    {
        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        ComPtr<ID3DBlob> sig;
        ComPtr<ID3DBlob> err;
        GLR_CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        GLR_CHECK(g_glRaytracingCmd.device->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&g_glRaytracingLighting.localRootSig)));
    }

    return 1;
}

static int glRaytracingLightingCreateBuffers(void)
{
    g_glRaytracingLighting.constantBuffer = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        glRaytracingAlignUp(sizeof(glRaytracingLightingConstants_t), 256),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    g_glRaytracingLighting.lightBuffer = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        sizeof(glRaytracingLight_t) * GL_RAYTRACING_MAX_LIGHTS,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    return g_glRaytracingLighting.constantBuffer.resource && g_glRaytracingLighting.lightBuffer.resource;
}

static void glRaytracingLightingUpdateConstants(void)
{
    glRaytracingMapCopy(
        g_glRaytracingLighting.constantBuffer.resource.Get(),
        &g_glRaytracingLighting.constants,
        sizeof(g_glRaytracingLighting.constants));
}

static void glRaytracingLightingUpdateLights(void)
{
    if (g_glRaytracingLighting.cpuLights.empty())
        return;

    glRaytracingMapCopy(
        g_glRaytracingLighting.lightBuffer.resource.Get(),
        g_glRaytracingLighting.cpuLights.data(),
        g_glRaytracingLighting.cpuLights.size() * sizeof(glRaytracingLight_t));
}

static void glRaytracingLightingCreatePersistentLightSRV(void)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = GL_RAYTRACING_MAX_LIGHTS;
    srv.Buffer.StructureByteStride = sizeof(glRaytracingLight_t);
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE base = g_glRaytracingLighting.descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    g_glRaytracingCmd.device->CreateShaderResourceView(
        g_glRaytracingLighting.lightBuffer.resource.Get(),
        &srv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 0));
}

static int glRaytracingLightingCreateStateObject(void)
{
    ComPtr<IDxcBlob> dxil = glRaytracingLightingCompileLibrary(g_glRaytracingLightingHlsl);
    if (!dxil)
        return 0;

    D3D12_EXPORT_DESC exports[3] = {};
    exports[0].Name = L"RayGen";
    exports[1].Name = L"ShadowMiss";
    exports[2].Name = L"ShadowClosestHit";

    D3D12_DXIL_LIBRARY_DESC libDesc = {};
    D3D12_SHADER_BYTECODE libBytecode = {};
    libBytecode.pShaderBytecode = dxil->GetBufferPointer();
    libBytecode.BytecodeLength = dxil->GetBufferSize();
    libDesc.DXILLibrary = libBytecode;
    libDesc.NumExports = _countof(exports);
    libDesc.pExports = exports;

    D3D12_HIT_GROUP_DESC hitGroup = {};
    hitGroup.HitGroupExport = L"ShadowHitGroup";
    hitGroup.ClosestHitShaderImport = L"ShadowClosestHit";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(uint32_t);
    shaderConfig.MaxAttributeSizeInBytes = 8;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRS = {};
    globalRS.pGlobalRootSignature = g_glRaytracingLighting.globalRootSig.Get();

    D3D12_LOCAL_ROOT_SIGNATURE localRS = {};
    localRS.pLocalRootSignature = g_glRaytracingLighting.localRootSig.Get();

    D3D12_STATE_SUBOBJECT subobjects[8] = {};
    UINT sub = 0;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[sub].pDesc = &libDesc;
    ++sub;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[sub].pDesc = &hitGroup;
    ++sub;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[sub].pDesc = &shaderConfig;
    ++sub;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[sub].pDesc = &globalRS;
    ++sub;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    subobjects[sub].pDesc = &localRS;
    ++sub;

    LPCWSTR localExports[] = { L"RayGen", L"ShadowMiss", L"ShadowHitGroup" };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc = {};
    assoc.pSubobjectToAssociate = &subobjects[4];
    assoc.NumExports = _countof(localExports);
    assoc.pExports = localExports;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subobjects[sub].pDesc = &assoc;
    ++sub;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipeConfig = {};
    pipeConfig.MaxTraceRecursionDepth = 1;

    subobjects[sub].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[sub].pDesc = &pipeConfig;
    ++sub;

    D3D12_STATE_OBJECT_DESC soDesc = {};
    soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    soDesc.NumSubobjects = sub;
    soDesc.pSubobjects = subobjects;

    GLR_CHECK(g_glRaytracingCmd.device->CreateStateObject(&soDesc, IID_PPV_ARGS(&g_glRaytracingLighting.rtStateObject)));
    GLR_CHECK(g_glRaytracingLighting.rtStateObject.As(&g_glRaytracingLighting.rtStateProps));
    return 1;
}

static int glRaytracingLightingCreateShaderTables(void)
{
    void* raygenId = g_glRaytracingLighting.rtStateProps->GetShaderIdentifier(L"RayGen");
    void* missId = g_glRaytracingLighting.rtStateProps->GetShaderIdentifier(L"ShadowMiss");
    void* hitId = g_glRaytracingLighting.rtStateProps->GetShaderIdentifier(L"ShadowHitGroup");

    if (!raygenId || !missId || !hitId)
    {
        glRaytracingFatal("Failed to fetch shader identifiers");
        return 0;
    }

    const UINT shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    const UINT recordSize = (UINT)glRaytracingAlignUp(shaderIdSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    g_glRaytracingLighting.raygenTable = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        recordSize,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    g_glRaytracingLighting.missTable = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        recordSize,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    g_glRaytracingLighting.hitTable = glRaytracingCreateBuffer(
        g_glRaytracingCmd.device.Get(),
        recordSize,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE);

    if (!g_glRaytracingLighting.raygenTable.resource ||
        !g_glRaytracingLighting.missTable.resource ||
        !g_glRaytracingLighting.hitTable.resource)
    {
        return 0;
    }

    uint8_t temp[256] = {};

    memset(temp, 0, sizeof(temp));
    memcpy(temp, raygenId, shaderIdSize);
    glRaytracingMapCopy(g_glRaytracingLighting.raygenTable.resource.Get(), temp, recordSize);

    memset(temp, 0, sizeof(temp));
    memcpy(temp, missId, shaderIdSize);
    glRaytracingMapCopy(g_glRaytracingLighting.missTable.resource.Get(), temp, recordSize);

    memset(temp, 0, sizeof(temp));
    memcpy(temp, hitId, shaderIdSize);
    glRaytracingMapCopy(g_glRaytracingLighting.hitTable.resource.Get(), temp, recordSize);

    return 1;
}

static void glRaytracingLightingCreatePerPassDescriptors(const glRaytracingLightingPassDesc_t* pass)
{
    D3D12_CPU_DESCRIPTOR_HANDLE base = g_glRaytracingLighting.descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrv = {};
    albedoSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    albedoSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    albedoSrv.Format = pass->albedoFormat;
    albedoSrv.Texture2D.MipLevels = 1;
    g_glRaytracingCmd.device->CreateShaderResourceView(
        pass->albedoTexture,
        &albedoSrv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 1));

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Format = glRaytracingGetSrvFormatForDepth(pass->depthFormat);
    depthSrv.Texture2D.MipLevels = 1;
    g_glRaytracingCmd.device->CreateShaderResourceView(
        pass->depthTexture,
        &depthSrv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 2));

    D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv = {};
    normalSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    normalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    normalSrv.Format = pass->normalFormat;
    normalSrv.Texture2D.MipLevels = 1;
    g_glRaytracingCmd.device->CreateShaderResourceView(
        pass->normalTexture,
        &normalSrv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 3));

    D3D12_SHADER_RESOURCE_VIEW_DESC positionSrv = {};
    positionSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    positionSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    positionSrv.Format = pass->positionFormat;
    positionSrv.Texture2D.MipLevels = 1;
    g_glRaytracingCmd.device->CreateShaderResourceView(
        pass->positionTexture,
        &positionSrv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 4));

    D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrv = {};
    tlasSrv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    tlasSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    tlasSrv.RaytracingAccelerationStructure.Location = pass->topLevelAS->GetGPUVirtualAddress();
    g_glRaytracingCmd.device->CreateShaderResourceView(
        nullptr,
        &tlasSrv,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 5));

    D3D12_UNORDERED_ACCESS_VIEW_DESC outputUav = {};
    outputUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    outputUav.Format = pass->outputFormat;
    g_glRaytracingCmd.device->CreateUnorderedAccessView(
        pass->outputTexture,
        nullptr,
        &outputUav,
        glRaytracingOffsetCpu(base, g_glRaytracingLighting.descriptorStride, 6));
}

// ============================================================
// Lighting public API
// ============================================================

extern "C" bool glRaytracingLightingInit(void)
{
    if (g_glRaytracingLighting.initialized)
        return true;

    if (!glRaytracingInitCmdContext())
        return false;

    if (!glRaytracingLightingCreateDescriptorHeap())
        return false;

    if (!glRaytracingLightingCreateRootSignatures())
        return false;

    if (!glRaytracingLightingCreateBuffers())
        return false;

    glRaytracingLightingCreatePersistentLightSRV();

    if (!glRaytracingLightingCreateStateObject())
        return false;

    if (!glRaytracingLightingCreateShaderTables())
        return false;

    memset(&g_glRaytracingLighting.constants, 0, sizeof(g_glRaytracingLighting.constants));
    g_glRaytracingLighting.constants.ambientColor[0] = 0.08f;
    g_glRaytracingLighting.constants.ambientColor[1] = 0.08f;
    g_glRaytracingLighting.constants.ambientColor[2] = 0.09f;
    g_glRaytracingLighting.constants.ambientColor[3] = 1.0f;
    g_glRaytracingLighting.constants.enableSpecular = 1;
    g_glRaytracingLighting.constants.enableHalfLambert = 1;
    g_glRaytracingLighting.constants.normalReconstructZ = 1.0f;
    g_glRaytracingLighting.constants.shadowBias = 1.5f;

    glRaytracingLightingUpdateConstants();

    g_glRaytracingLighting.initialized = true;
    glRaytracingLog("glRaytracingLightingInit ok");
    return true;
}

extern "C" void glRaytracingLightingShutdown(void)
{
    if (!g_glRaytracingLighting.initialized)
        return;

    g_glRaytracingLighting = glRaytracingLightingState_t();
}

extern "C" bool glRaytracingLightingIsInitialized(void)
{
    return g_glRaytracingLighting.initialized;
}

extern "C" void glRaytracingLightingClearLights(void)
{
    g_glRaytracingLighting.cpuLights.clear();
    g_glRaytracingLighting.constants.lightCount = 0;
    glRaytracingLightingUpdateConstants();
}

extern "C" bool glRaytracingLightingAddLight(const glRaytracingLight_t* light)
{
    if (!g_glRaytracingLighting.initialized || !light)
        return false;

    if (g_glRaytracingLighting.cpuLights.size() >= GL_RAYTRACING_MAX_LIGHTS)
        return false;

    g_glRaytracingLighting.cpuLights.push_back(*light);
    g_glRaytracingLighting.constants.lightCount = (uint32_t)g_glRaytracingLighting.cpuLights.size();

    glRaytracingLightingUpdateLights();
    glRaytracingLightingUpdateConstants();
    return true;
}

extern "C" void glRaytracingLightingSetAmbient(float r, float g, float b, float intensity)
{
    g_glRaytracingLighting.constants.ambientColor[0] = r;
    g_glRaytracingLighting.constants.ambientColor[1] = g;
    g_glRaytracingLighting.constants.ambientColor[2] = b;
    g_glRaytracingLighting.constants.ambientColor[3] = intensity;
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingSetCameraPosition(float x, float y, float z)
{
    g_glRaytracingLighting.constants.cameraPos[0] = x;
    g_glRaytracingLighting.constants.cameraPos[1] = y;
    g_glRaytracingLighting.constants.cameraPos[2] = z;
    g_glRaytracingLighting.constants.cameraPos[3] = 1.0f;
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingSetInvViewProjMatrix(const float* m16)
{
    if (!m16)
        return;

    memcpy(g_glRaytracingLighting.constants.invViewProj, m16, sizeof(float) * 16);
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingSetInvViewMatrix(const float* m16)
{
    if (!m16)
        return;

    memcpy(g_glRaytracingLighting.constants.invViewMatrix, m16, sizeof(float) * 16);
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingSetNormalReconstructSign(float signValue)
{
    g_glRaytracingLighting.constants.normalReconstructZ = signValue;
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingEnableSpecular(int enable)
{
    g_glRaytracingLighting.constants.enableSpecular = enable ? 1u : 0u;
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingEnableHalfLambert(int enable)
{
    g_glRaytracingLighting.constants.enableHalfLambert = enable ? 1u : 0u;
    glRaytracingLightingUpdateConstants();
}

extern "C" void glRaytracingLightingSetShadowBias(float bias)
{
    g_glRaytracingLighting.constants.shadowBias = bias;
    glRaytracingLightingUpdateConstants();
}

extern "C" bool glRaytracingLightingExecute(const glRaytracingLightingPassDesc_t* pass)
{
    if (!g_glRaytracingLighting.initialized || !pass)
        return false;

    if (!pass->albedoTexture ||
        !pass->depthTexture ||
        !pass->normalTexture ||
        !pass->positionTexture ||
        !pass->outputTexture ||
        !pass->topLevelAS)
    {
        return false;
    }

    if (pass->width == 0 || pass->height == 0)
        return false;

    g_glRaytracingLighting.constants.screenSize[0] = (float)pass->width;
    g_glRaytracingLighting.constants.screenSize[1] = (float)pass->height;
    g_glRaytracingLighting.constants.screenSize[2] = 1.0f / (float)pass->width;
    g_glRaytracingLighting.constants.screenSize[3] = 1.0f / (float)pass->height;
    g_glRaytracingLighting.constants.lightCount =
        (uint32_t)glRaytracingClamp<size_t>(g_glRaytracingLighting.cpuLights.size(), 0, GL_RAYTRACING_MAX_LIGHTS);

    glRaytracingLightingUpdateLights();
    glRaytracingLightingUpdateConstants();
    glRaytracingLightingCreatePerPassDescriptors(pass);

    if (!glRaytracingBeginCmd())
        return false;

    glRaytracingTransition(
        g_glRaytracingCmd.cmdList.Get(),
        pass->outputTexture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { g_glRaytracingLighting.descriptorHeap.Get() };
    g_glRaytracingCmd.cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    g_glRaytracingCmd.cmdList->SetComputeRootSignature(g_glRaytracingLighting.globalRootSig.Get());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = g_glRaytracingLighting.descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    g_glRaytracingCmd.cmdList->SetComputeRootDescriptorTable(
        0,
        glRaytracingOffsetGpu(gpuBase, g_glRaytracingLighting.descriptorStride, 0));
    g_glRaytracingCmd.cmdList->SetComputeRootDescriptorTable(
        1,
        glRaytracingOffsetGpu(gpuBase, g_glRaytracingLighting.descriptorStride, 6));
    g_glRaytracingCmd.cmdList->SetComputeRootConstantBufferView(
        2,
        g_glRaytracingLighting.constantBuffer.gpuVA);

    g_glRaytracingCmd.cmdList->SetPipelineState1(g_glRaytracingLighting.rtStateObject.Get());

    const UINT shaderRecordSize =
        (UINT)glRaytracingAlignUp(
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    D3D12_DISPATCH_RAYS_DESC rays = {};
    rays.RayGenerationShaderRecord.StartAddress = g_glRaytracingLighting.raygenTable.gpuVA;
    rays.RayGenerationShaderRecord.SizeInBytes = shaderRecordSize;

    rays.MissShaderTable.StartAddress = g_glRaytracingLighting.missTable.gpuVA;
    rays.MissShaderTable.SizeInBytes = shaderRecordSize;
    rays.MissShaderTable.StrideInBytes = shaderRecordSize;

    rays.HitGroupTable.StartAddress = g_glRaytracingLighting.hitTable.gpuVA;
    rays.HitGroupTable.SizeInBytes = shaderRecordSize;
    rays.HitGroupTable.StrideInBytes = shaderRecordSize;

    rays.Width = pass->width;
    rays.Height = pass->height;
    rays.Depth = 1;

    g_glRaytracingCmd.cmdList->DispatchRays(&rays);

    D3D12_RESOURCE_BARRIER uav = {};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = pass->outputTexture;
    g_glRaytracingCmd.cmdList->ResourceBarrier(1, &uav);

    ID3D12Resource* backBuffer = QD3D12_GetCurrentBackBuffer();
    if (backBuffer)
    {
        glRaytracingTransition(
            g_glRaytracingCmd.cmdList.Get(),
            pass->outputTexture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

        glRaytracingTransition(
            g_glRaytracingCmd.cmdList.Get(),
            backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST);

        g_glRaytracingCmd.cmdList->CopyResource(backBuffer, pass->outputTexture);

        glRaytracingTransition(
            g_glRaytracingCmd.cmdList.Get(),
            backBuffer,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT);

        glRaytracingTransition(
            g_glRaytracingCmd.cmdList.Get(),
            pass->outputTexture,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    else
    {
        glRaytracingTransition(
            g_glRaytracingCmd.cmdList.Get(),
            pass->outputTexture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    if (!glRaytracingEndCmd())
        return false;

    return true;
}

extern "C" glRaytracingLight_t glRaytracingLightingMakePointLight(
    float px, float py, float pz,
    float radius,
    float r, float g, float b,
    float intensity)
{
    glRaytracingLight_t l = {};
    l.position.x = px;
    l.position.y = py;
    l.position.z = pz;
    l.radius = radius;

    l.color.x = r;
    l.color.y = g;
    l.color.z = b;
    l.intensity = intensity;
    return l;
}

extern "C" uint32_t glRaytracingLightingGetLightCount(void)
{
    return (uint32_t)g_glRaytracingLighting.cpuLights.size();
}


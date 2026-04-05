// gl_d3d12shim.cpp
//

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <immintrin.h>
#include <stdint.h>
#include <vector>
#include <string.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

#include "opengl.h"

static D3D12_GPU_DESCRIPTOR_HANDLE QD3D12_SrvGpu(UINT index);
static void QD3D12_CreateDSV();
static D3D12_CPU_DESCRIPTOR_HANDLE QD3D12_SrvCpu(UINT index);
static Mat4 CurrentModelMatrix();

static void QD3D12_Log(const char* fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

static void QD3D12_Fatal(const char* fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    MessageBoxA(nullptr, buffer, "QD3D12 Fatal", MB_OK | MB_ICONERROR);
    DebugBreak();
}

#define QD3D12_CHECK(x) do { HRESULT _hr = (x); if (FAILED(_hr)) { QD3D12_Fatal("HRESULT failed 0x%08X at %s:%d", (unsigned)_hr, __FILE__, __LINE__); } } while(0)

struct GLOcclusionQuery
{
    GLuint id = 0;
    bool   active = false;
    bool   pending = false;
    bool   resultReady = false;

    UINT   heapIndex = UINT_MAX;
    UINT64 result = 0;
    UINT64 submittedFence = 0;
};

struct QueryMarker
{
    enum Type
    {
        Begin,
        End
    };

    Type   type = Begin;
    GLuint id = 0;
};

static const UINT QD3D12_MaxQueries = 2048;

// ============================================================
// SECTION 3: D3D12 renderer structs
// ============================================================

static const UINT QD3D12_MaxTextureUnits = 2;
static const UINT QD3D12_FrameCount = 2;
static const UINT QD3D12_MaxTextures = 4096;
static const UINT QD3D12_UploadBufferSize = 64 * 1024 * 1024;

enum PipelineMode
{
    PIPE_OPAQUE_TEX = 0,
    PIPE_ALPHA_TEST_TEX,
    PIPE_BLEND_TEX,
    PIPE_OPAQUE_UNTEX,
    PIPE_BLEND_UNTEX,
    PIPE_COUNT
};

static void QD3D12_FlushQueuedBatches();
static PipelineMode PickPipeline(bool useTex0, bool useTex1);
static Mat4 CurrentMVP();

struct GLVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u0, v0;
    float u1, v1;
    float r, g, b, a;
};

struct RetiredResource
{
    UINT64 fenceValue = 0;
    ComPtr<ID3D12Resource> resource;
};

static std::vector<RetiredResource> g_retiredResources;

struct FrameResources
{
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    UINT64 fenceValue = 0;
};

struct UploadRing
{
    ComPtr<ID3D12Resource> resource[QD3D12_FrameCount];
    uint8_t* cpuBase[QD3D12_FrameCount] = {};
    D3D12_GPU_VIRTUAL_ADDRESS gpuBase[QD3D12_FrameCount] = {};
    UINT size = 0;
    UINT offset = 0;
};

struct TextureResource
{
    GLuint glId = 0;
    int width = 0;
    int height = 0;
    GLenum format = GL_RGBA;
    GLenum minFilter = GL_LINEAR;
    GLenum magFilter = GL_LINEAR;
    GLenum wrapS = GL_REPEAT;
    GLenum wrapT = GL_REPEAT;

    std::vector<uint8_t> sysmem;

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12Resource> texture;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
    UINT srvIndex = UINT_MAX;
    bool gpuValid = false;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
};

struct DrawConstants
{
    Mat4 mvp;
    Mat4 modelMatrix;

    float alphaRef;

    float useTex0;
    float useTex1;
    float tex1IsLightmap;

    float texEnvMode0;
    float texEnvMode1;
    float geometryFlag;
    float _pad1;

    float fogEnabled;
    float fogMode;
    float fogDensity;
    float fogStart;

    float fogEnd;
    float _fogPad0;
    float _fogPad1;
    float _fogPad2;

    float fogColor[4];
};

struct GLBufferObject
{
    GLuint               id = 0;
    GLenum               target = 0;
    GLbitfield           storageFlags = 0;
    std::vector<uint8_t> data;

    bool       mapped = false;
    GLintptr   mappedOffset = 0;
    GLsizeiptr mappedLength = 0;
    GLbitfield mappedAccess = 0;
};

const char* vendor = "Justin Marshall";
const char* renderer = "Quake D3D12 Wrapper";
const char* version = "1.1-quake-d3d12";
const char* extensions = "GL_SGIS_multitexture GL_ARB_multitexture GL_EXT_texture_env_add";

enum TexEnvModeShader
{
    TEXENV_MODULATE = 0,
    TEXENV_REPLACE = 1,
    TEXENV_DECAL = 2,
    TEXENV_BLEND = 3,
    TEXENV_ADD = 4
};

static float MapTexEnvMode(GLenum mode)
{
    switch (mode)
    {
    case GL_REPLACE:  return (float)TEXENV_REPLACE;
    case GL_BLEND:    return (float)TEXENV_BLEND;
#ifdef GL_ADD
    case GL_ADD:      return (float)TEXENV_ADD;
#endif
    case GL_MODULATE:
    default:          return (float)TEXENV_MODULATE;
    }
}


struct BatchKey
{
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;

    PipelineMode pipeline = PIPE_OPAQUE_UNTEX;
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT tex0SrvIndex = 0;
    UINT tex1SrvIndex = 0;

    float alphaRef = 0.0f;
    float useTex0 = 0.0f;
    float useTex1 = 0.0f;
    float tex1IsLightmap = 0.0f;
    float texEnvMode0 = 0.0f;
    float texEnvMode1 = 0.0f;

    GLenum blendSrc = GL_ONE;
    GLenum blendDst = GL_ZERO;

    bool depthTest = true;
    bool depthWrite = true;
    GLenum depthFunc = GL_LEQUAL;

    Mat4 mvp;
    Mat4 modelMatrix;

    float geometryFlag;
};

static bool ViewportEquals(const D3D12_VIEWPORT& a, const D3D12_VIEWPORT& b)
{
    return a.TopLeftX == b.TopLeftX &&
        a.TopLeftY == b.TopLeftY &&
        a.Width == b.Width &&
        a.Height == b.Height &&
        a.MinDepth == b.MinDepth &&
        a.MaxDepth == b.MaxDepth;
}

static bool RectEquals(const D3D12_RECT& a, const D3D12_RECT& b)
{
    return a.left == b.left &&
        a.top == b.top &&
        a.right == b.right &&
        a.bottom == b.bottom;
}

static bool BatchKeyEquals(const BatchKey& a, const BatchKey& b)
{
    return
        ViewportEquals(a.viewport, b.viewport) &&
        RectEquals(a.scissor, b.scissor) &&
        a.pipeline == b.pipeline &&
        a.topology == b.topology &&
        a.tex0SrvIndex == b.tex0SrvIndex &&
        a.tex1SrvIndex == b.tex1SrvIndex &&
        a.alphaRef == b.alphaRef &&
        a.useTex0 == b.useTex0 &&
        a.useTex1 == b.useTex1 &&
        a.tex1IsLightmap == b.tex1IsLightmap &&
        a.texEnvMode0 == b.texEnvMode0 &&
        a.texEnvMode1 == b.texEnvMode1 &&
        a.blendSrc == b.blendSrc &&
        a.blendDst == b.blendDst &&
        a.depthTest == b.depthTest &&
        a.depthWrite == b.depthWrite &&
        a.depthFunc == b.depthFunc &&
        a.geometryFlag == b.geometryFlag &&
        memcmp(a.mvp.m, b.mvp.m, sizeof(a.mvp.m)) == 0 &&
        memcmp(a.modelMatrix.m, b.modelMatrix.m, sizeof(a.modelMatrix.m)) == 0;
}

struct QueuedBatch
{
    BatchKey key;
    std::vector<GLVertex> verts;
    size_t markerBegin = 0;
    size_t markerEnd = 0;
};
struct GLState
{
    HWND hwnd = nullptr;
    UINT width = 640;
    UINT height = 480;

    std::unordered_map<GLuint, GLOcclusionQuery> queries;
    GLuint                                       nextQueryId = 1;
    GLuint                                       currentQuery = 0;

    std::vector<QueryMarker> queryMarkers;

    ComPtr<ID3D12QueryHeap> occlusionQueryHeap;
    ComPtr<ID3D12Resource>  occlusionReadback;
    uint64_t* occlusionReadbackCpu = nullptr;

    std::unordered_map<GLuint, GLBufferObject> buffers;
    GLuint                                     nextBufferId = 1;
    GLuint                                     boundArrayBuffer = 0;
    GLuint                                     boundElementArrayBuffer = 0;

    Mat4 modelMatrix = Mat4::Identity();

    float currentGeometryFlag = 0.0f;

    bool fog = false;
    GLenum fogMode = GL_EXP;
    GLfloat fogDensity = 1.0f;
    GLfloat fogStart = 0.0f;
    GLfloat fogEnd = 1.0f;
    GLfloat fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLenum fogHint = GL_DONT_CARE;

    GLclampd depthRangeNear = 0.0;
    GLclampd depthRangeFar = 1.0;

    std::vector<QueuedBatch> queuedBatches;
    bool frameOpen = false;

    GLenum depthFunc = GL_LEQUAL;
    UINT64 nextFenceValue = 1;

    struct ClientArrayState {
        GLint size = 4;
        GLenum type = GL_FLOAT;
        GLsizei stride = 0;
        const uint8_t* ptr = nullptr;
        bool enabled = false;
    };

    ClientArrayState vertexArray;
    ClientArrayState normalArray;   // <-- add this
    ClientArrayState colorArray;
    ClientArrayState texCoordArray[QD3D12_MaxTextureUnits];
    GLuint clientActiveTextureUnit = 0;

    bool scissorTest = false;
    GLint scissorX = 0;
    GLint scissorY = 0;
    GLsizei scissorW = 640;
    GLsizei scissorH = 480;

    GLenum lastError = GL_NO_ERROR;

    GLuint stencilMask = ~0u;
    GLint clearStencilValue = 0;
    GLenum stencilFunc = GL_ALWAYS;
    GLint stencilRef = 0;
    GLuint stencilFuncMask = ~0u;
    GLenum stencilSFail = GL_KEEP;
    GLenum stencilDPFail = GL_KEEP;
    GLenum stencilDPPass = GL_KEEP;

    GLdouble clipPlane0[4] = { 0.0, 0.0, 0.0, 0.0 };
    bool clipPlane0Enabled = false;

    GLfloat polygonOffsetFactor = 0.0f;
    GLfloat polygonOffsetUnits = 0.0f;

    GLclampd clearDepthValue = 1.0;
    GLboolean colorMaskR = GL_TRUE;
    GLboolean colorMaskG = GL_TRUE;
    GLboolean colorMaskB = GL_TRUE;
    GLboolean colorMaskA = GL_TRUE;

    float clearColor[4] = { 0, 0, 0, 1 };
    bool blend = false;
    bool alphaTest = false;
    bool depthTest = true;
    bool cullFace = false;
    bool texture2D[QD3D12_MaxTextureUnits] = { true, false };
    bool depthWrite = true;
    GLenum blendSrc = GL_SRC_ALPHA;
    GLenum blendDst = GL_ONE_MINUS_SRC_ALPHA;
    GLenum alphaFunc = GL_GREATER;
    float alphaRef = 0.666f;
    GLenum cullMode = GL_FRONT;
    GLenum shadeModel = GL_FLAT;
    GLenum drawBuffer = GL_BACK;
    GLenum readBuffer = GL_BACK;

    GLenum texEnvMode[QD3D12_MaxTextureUnits] = { GL_MODULATE, GL_MODULATE };

    GLint viewportX = 0;
    GLint viewportY = 0;
    GLsizei viewportW = 640;
    GLsizei viewportH = 480;

    GLuint boundTexture[QD3D12_MaxTextureUnits] = {};
    GLuint activeTextureUnit = 0;

    GLenum currentPrim = 0;
    bool inBeginEnd = false;
    float curU[QD3D12_MaxTextureUnits] = {};
    float curV[QD3D12_MaxTextureUnits] = {};
    float curColor[4] = { 1, 1, 1, 1 };
    std::vector<GLVertex> immediateVerts;

    GLenum matrixMode = GL_MODELVIEW;
    std::vector<Mat4> modelStack{ Mat4::Identity() };
    std::vector<Mat4> projStack{ Mat4::Identity() };
    std::vector<Mat4> texStack[QD3D12_MaxTextureUnits] = { { Mat4::Identity() }, { Mat4::Identity() } };

    std::unordered_map<GLuint, TextureResource> textures;
    GLuint nextTextureId = 1;
    UINT nextSrvIndex = 1;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    UINT frameIndex = 0;

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvStride = 0;
    std::array<ComPtr<ID3D12Resource>, QD3D12_FrameCount> backBuffers;

    // MRT 1: normals
    std::array<ComPtr<ID3D12Resource>, QD3D12_FrameCount> normalBuffers;
    D3D12_RESOURCE_STATES normalBufferState[QD3D12_FrameCount] = {};

    // MRT 2: world-space positions
    std::array<ComPtr<ID3D12Resource>, QD3D12_FrameCount> positionBuffers;
    D3D12_RESOURCE_STATES positionBufferState[QD3D12_FrameCount] = {};

    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depthBuffer;

    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvStride = 0;

    // SRVs for normal MRTs
    UINT normalSrvIndex[QD3D12_FrameCount] = { UINT_MAX, UINT_MAX };
    D3D12_CPU_DESCRIPTOR_HANDLE normalSrvCpu[QD3D12_FrameCount]{};
    D3D12_GPU_DESCRIPTOR_HANDLE normalSrvGpu[QD3D12_FrameCount]{};

    // SRVs for position MRTs
    UINT positionSrvIndex[QD3D12_FrameCount] = { UINT_MAX, UINT_MAX };
    D3D12_CPU_DESCRIPTOR_HANDLE positionSrvCpu[QD3D12_FrameCount]{};
    D3D12_GPU_DESCRIPTOR_HANDLE positionSrvGpu[QD3D12_FrameCount]{};

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    FrameResources frames[QD3D12_FrameCount];

    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;

    UploadRing upload;

    ComPtr<ID3D12RootSignature> rootSig;

    ComPtr<ID3DBlob> vsMainBlob;
    ComPtr<ID3DBlob> psMainBlob;
    ComPtr<ID3DBlob> psAlphaBlob;
    ComPtr<ID3DBlob> psUntexturedBlob;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};

    D3D12_RESOURCE_STATES backBufferState[QD3D12_FrameCount] = {};

    TextureResource whiteTexture;

    GLenum defaultMinFilter = GL_LINEAR;
    GLenum defaultMagFilter = GL_LINEAR;
    GLenum defaultWrapS = GL_REPEAT;
    GLenum defaultWrapT = GL_REPEAT;
};

static GLState g_gl;
static GLuint g_lightingTextureId = 0;
static TextureResource* g_lightingTexture = nullptr;
static D3D12_RESOURCE_STATES  g_lightingTextureState = D3D12_RESOURCE_STATE_COMMON;

static D3D12_CPU_DESCRIPTOR_HANDLE CurrentPositionRTV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gl.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T((QD3D12_FrameCount * 2) + g_gl.frameIndex) * SIZE_T(g_gl.rtvStride);
    return h;
}

static GLBufferObject* QD3D12_GetBuffer(GLuint id)
{
    if (id == 0)
        return nullptr;

    auto it = g_gl.buffers.find(id);
    if (it == g_gl.buffers.end())
        return nullptr;

    return &it->second;
}

static const uint8_t* QD3D12_ResolveArrayPointer(const void* ptr)
{
    if (g_gl.boundArrayBuffer != 0)
    {
        GLBufferObject* bo = QD3D12_GetBuffer(g_gl.boundArrayBuffer);
        if (!bo)
            return nullptr;

        const size_t offset = (size_t)ptr;
        if (offset > bo->data.size())
            return nullptr;

        return bo->data.data() + offset;
    }

    return reinterpret_cast<const uint8_t*>(ptr);
}

static const void* QD3D12_ResolveElementPointer(const void* ptr, GLenum indexType, GLsizei count)
{
    if (g_gl.boundElementArrayBuffer != 0)
    {
        GLBufferObject* bo = QD3D12_GetBuffer(g_gl.boundElementArrayBuffer);
        if (!bo)
            return nullptr;

        size_t indexSize = 0;
        switch (indexType)
        {
        case GL_UNSIGNED_INT:
            indexSize = sizeof(GLuint);
            break;
        case GL_UNSIGNED_SHORT:
            indexSize = sizeof(GLushort);
            break;
        case GL_UNSIGNED_BYTE:
            indexSize = sizeof(GLubyte);
            break;
        default:
            return nullptr;
        }

        const size_t offset = (size_t)ptr;
        const size_t bytesNeeded = (size_t)count * indexSize;
        if (offset > bo->data.size() || bytesNeeded > (bo->data.size() - offset))
            return nullptr;

        return bo->data.data() + offset;
    }

    return ptr;
}

static void QD3D12_RetireResource(ComPtr<ID3D12Resource>& res)
{
    if (!res)
        return;

    RetiredResource rr{};
    rr.resource = res;

    // If no frame has been submitted yet, force it to live until next GPU idle.
    UINT64 fenceValue = 0;

    if (g_gl.frameOpen)
    {
        fenceValue = g_gl.frames[g_gl.frameIndex].fenceValue;
        if (fenceValue == 0)
            fenceValue = g_gl.nextFenceValue;
    }
    else
    {
        fenceValue = g_gl.nextFenceValue;
    }

    rr.fenceValue = fenceValue;
    g_retiredResources.push_back(std::move(rr));

    res.Reset();
}

static UINT64 QD3D12_CurrentSubmissionFenceValue()
{
    return g_gl.nextFenceValue;
}

static void QD3D12_CollectRetiredResources()
{
    const UINT64 completed = g_gl.fence ? g_gl.fence->GetCompletedValue() : 0;

    size_t write = 0;
    for (size_t read = 0; read < g_retiredResources.size(); ++read)
    {
        if (g_retiredResources[read].fenceValue <= completed)
        {
            // let ComPtr drop here
        }
        else
        {
            if (write != read)
                g_retiredResources[write] = std::move(g_retiredResources[read]);
            ++write;
        }
    }
    g_retiredResources.resize(write);
}

// ============================================================
// SECTION 4: shaders
// ============================================================

static const char* kQuakeWrapperHLSL = R"HLSL(
cbuffer DrawCB : register(b0)
{
    float4x4 gMVP;
    float4x4 gModelMatrix;

    float gAlphaRef;
    float gUseTex0;
    float gUseTex1;
    float gTex1IsLightmap;
    float gTexEnvMode0;
    float gTexEnvMode1;
    float geometryFlag;
    float gPad1;

    float gFogEnabled;
    float gFogMode;
    float gFogDensity;
    float gFogStart;

    float gFogEnd;
    float gFogPad0;
    float gFogPad1;
    float gFogPad2;

    float4 gFogColor;
};

Texture2D gTex0 : register(t0);
Texture2D gTex1 : register(t1);
SamplerState gSamp0 : register(s0);
SamplerState gSamp1 : register(s1);

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 col : COLOR0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 col : COLOR0;
    float fogCoord : TEXCOORD2;
    float3 objPos : TEXCOORD3;
    float3 normal : TEXCOORD4;
    nointerpolation float4 attr : TEXCOORD5;
};

struct PSOut
{
    float4 color    : SV_Target0;
    float4 normal   : SV_Target1;
    float4 position : SV_Target2;
};

VSOut VSMain(VSIn i)
{
    VSOut o;

    float4 worldPos = mul(gModelMatrix, float4(i.pos, 1.0));
    float4 clipPos  = mul(gMVP, float4(i.pos, 1.0));

    // Transform normal by model matrix as direction
    float3 worldNormal = mul((float3x3)gModelMatrix, i.normal);


    o.pos = clipPos;
    o.fogCoord = abs(clipPos.z / max(abs(clipPos.w), 0.00001));
    o.uv0 = i.uv0;
    o.uv1 = i.uv1;
    o.col = i.col;
    o.objPos = worldPos.xyz;
    o.normal = worldNormal;
    o.attr = float4(geometryFlag,0, 0, 0);

    return o;
}

float4 ApplyTexEnv(float4 currentColor, float4 texel, float mode)
{
    if (mode < 0.5)
    {
        return currentColor * texel;
    }
    else if (mode < 1.5)
    {
        return texel;
    }
    else if (mode < 2.5)
    {
        float3 rgb = lerp(currentColor.rgb, texel.rgb, texel.a);
        return float4(rgb, currentColor.a);
    }
    else if (mode < 3.5)
    {
        float3 rgb = lerp(currentColor.rgb, texel.rgb, texel.rgb);
        return float4(rgb, currentColor.a * texel.a);
    }
    else
    {
        return float4(currentColor.rgb + texel.rgb, currentColor.a * texel.a);
    }
}

float TinyNoise(int2 p)
{
    uint n = (uint(p.x) * 1973u) ^ (uint(p.y) * 9277u) ^ 0x68bc21ebu;
    n = (n << 13u) ^ n;
    return 1.0 - float((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 1073741824.0;
}

float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));

    float2 u = f * f * (3.0 - 2.0 * f);

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float OrganicFilmGrain(float2 screenPos, float2 uv, float3 color)
{
    float luma = dot(color, float3(0.299, 0.587, 0.114));

    float g0 = Noise2D(uv * 96.0 + screenPos * 0.015);
    float g1 = Noise2D(uv * 173.0 + screenPos * 0.031);
    float g2 = Noise2D(screenPos * 0.35);

    float grain = g0 * 0.5 + g1 * 0.35 + g2 * 0.15;
    grain = grain - 0.5;
    grain *= lerp(0.35, 1.0, 1.0 - luma);

    return grain;
}

float FilmGrain(int2 p)
{
    uint n = uint(p.x * 374761393 + p.y * 668265263);
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= (n >> 16u);
    return float(n & 255u) / 255.0;
}

float3 ApplySoftwareRendererLook(float3 c)
{
    c = saturate(c);

    float luma0 = dot(c, float3(0.299, 0.587, 0.114));

    float3 tinted = c * float3(1.01, 1.00, 0.995);

    float luma1 = dot(tinted, float3(0.299, 0.587, 0.114));
    tinted = lerp(luma1.xxx, tinted, 1.10);

    float luma2 = dot(tinted, float3(0.299, 0.587, 0.114));
    if (luma2 > 0.0001)
        tinted *= (luma0 / luma2);

    return saturate(tinted);
}

float ComputeFogFactor(float fogCoord)
{
    if (gFogEnabled < 0.5)
        return 1.0;

    float f = 1.0;

    if (gFogMode < 0.5)
    {
        float denom = max(gFogEnd - gFogStart, 0.00001);
        f = (gFogEnd - fogCoord) / denom;
    }
    else if (gFogMode < 1.5)
    {
        f = exp(-gFogDensity * fogCoord);
    }
    else
    {
        float d = gFogDensity * fogCoord;
        f = exp(-(d * d));
    }

    return saturate(f);
}

float4 ApplyFog(float4 color, float fogCoord)
{
    float fogFactor = ComputeFogFactor(fogCoord);
    color.rgb = lerp(gFogColor.rgb, color.rgb, fogFactor);
    return color;
}

float4 BuildTexturedColor(VSOut i)
{
    float4 outColor = i.col;

    float2 uv0 = i.uv0;
    float2 uv1 = i.uv1;

    float n = TinyNoise(int2(i.pos.xy)) * 0.0005;
    uv0 += float2(n, -n);
    uv1 += float2(-n, n);

    if (gUseTex0 > 0.5)
    {
        float4 tex0 = gTex0.Sample(gSamp0, uv0);
        outColor = ApplyTexEnv(outColor, tex0, gTexEnvMode0);
    }

    if (gUseTex1 > 0.5)
    {
        float4 tex1 = gTex1.Sample(gSamp1, uv1);
        outColor = outColor * tex1;
    }

    outColor.xyz = ApplySoftwareRendererLook(outColor.xyz);
    outColor = ApplyFog(outColor, i.fogCoord);

    return outColor;
}

PSOut PSMain(VSOut i)
{
    PSOut o;
    o.color = BuildTexturedColor(i);
    o.normal = float4(i.normal, i.attr.x);
    o.position = float4(i.objPos, 1.0);
    return o;
}

PSOut PSMainAlphaTest(VSOut i)
{
    PSOut o;
    o.color = BuildTexturedColor(i);
    clip(o.color.a - gAlphaRef);
    o.normal = float4(i.normal, i.attr.x);
    o.position = float4(i.objPos, 1.0);
    return o;
}

PSOut PSMainUntextured(VSOut i)
{
    PSOut o;
    o.color = ApplyFog(i.col, i.fogCoord);
    o.normal = float4(i.normal, i.attr.x);
    o.position = float4(i.objPos, 1.0);
    return o;
}
)HLSL";

static HDC g_qd3d12CurrentDC = nullptr;
static QD3D12_HGLRC g_qd3d12CurrentRC = nullptr;

static size_t QD3D12_TypeSize(GLenum type) {
    switch (type) {
    case GL_BYTE: return sizeof(GLbyte);
    case GL_UNSIGNED_BYTE: return sizeof(GLubyte);
    case GL_SHORT: return sizeof(GLshort);
    case GL_UNSIGNED_SHORT: return sizeof(GLushort);
    case GL_INT: return sizeof(GLint);
    case GL_UNSIGNED_INT: return sizeof(GLuint);
    case GL_FLOAT: return sizeof(GLfloat);
    case GL_DOUBLE: return sizeof(GLdouble);
    default: return 0;
    }
}

static inline float QD3D12_ReadScalarFast(const uint8_t* p, GLenum type)
{
    switch (type)
    {
    case GL_FLOAT:           return *(const float*)p;
    case GL_DOUBLE:          return (float)(*(const double*)p);
    case GL_INT:             return (float)(*(const GLint*)p);
    case GL_UNSIGNED_INT:    return (float)(*(const GLuint*)p);
    case GL_SHORT:           return (float)(*(const GLshort*)p);
    case GL_UNSIGNED_SHORT:  return (float)(*(const GLushort*)p);
    case GL_BYTE:            return (float)(*(const GLbyte*)p);
    case GL_UNSIGNED_BYTE:   return (float)(*(const GLubyte*)p);
    default:                 return 0.0f;
    }
}

static void QD3D12_FetchArrayVertex(GLint idx, GLVertex& out)
{
    // Direct init is much cheaper than memset + patching fields.
    out.px = 0.0f; out.py = 0.0f; out.pz = 0.0f;
    out.nx = 0.0f; out.ny = 0.0f; out.nz = 1.0f;
    out.r = 1.0f; out.g = 1.0f; out.b = 1.0f; out.a = 1.0f;
    out.u0 = 0.0f; out.v0 = 0.0f;
    out.u1 = 0.0f; out.v1 = 0.0f;

    //
    // Position
    //
    const auto& va = g_gl.vertexArray;
    if (va.enabled && va.ptr)
    {
        const size_t typeSize = (size_t)QD3D12_TypeSize(va.type);
        const size_t elemSize = (size_t)va.size * typeSize;
        const size_t stride = va.stride ? (size_t)va.stride : elemSize;
        const uint8_t* p = va.ptr + stride * (size_t)idx;

        switch (va.type)
        {
        case GL_FLOAT:
        {
            const float* f = (const float*)p;
            if (va.size > 0) out.px = f[0];
            if (va.size > 1) out.py = f[1];
            if (va.size > 2) out.pz = f[2];
            break;
        }
        case GL_DOUBLE:
        {
            const double* f = (const double*)p;
            if (va.size > 0) out.px = (float)f[0];
            if (va.size > 1) out.py = (float)f[1];
            if (va.size > 2) out.pz = (float)f[2];
            break;
        }
        default:
            if (va.size > 0) out.px = QD3D12_ReadScalarFast(p + 0 * typeSize, va.type);
            if (va.size > 1) out.py = QD3D12_ReadScalarFast(p + 1 * typeSize, va.type);
            if (va.size > 2) out.pz = QD3D12_ReadScalarFast(p + 2 * typeSize, va.type);
            break;
        }
    }

    //
    // Normal
    //
    const auto& na = g_gl.normalArray;
    if (na.enabled && na.ptr)
    {
        const size_t typeSize = (size_t)QD3D12_TypeSize(na.type);
        const size_t stride = na.stride ? (size_t)na.stride : (3 * typeSize);
        const uint8_t* p = na.ptr + stride * (size_t)idx;

        switch (na.type)
        {
        case GL_FLOAT:
        {
            const float* f = (const float*)p;
            out.nx = f[0];
            out.ny = f[1];
            out.nz = f[2];
            break;
        }
        case GL_DOUBLE:
        {
            const double* f = (const double*)p;
            out.nx = (float)f[0];
            out.ny = (float)f[1];
            out.nz = (float)f[2];
            break;
        }
        default:
            out.nx = QD3D12_ReadScalarFast(p + 0 * typeSize, na.type);
            out.ny = QD3D12_ReadScalarFast(p + 1 * typeSize, na.type);
            out.nz = QD3D12_ReadScalarFast(p + 2 * typeSize, na.type);
            break;
        }
    }

    //
    // Color
    //
    const auto& ca = g_gl.colorArray;
    if (ca.enabled && ca.ptr)
    {
        const size_t typeSize = (size_t)QD3D12_TypeSize(ca.type);
        const size_t elemSize = (size_t)ca.size * typeSize;
        const size_t stride = ca.stride ? (size_t)ca.stride : elemSize;
        const uint8_t* p = ca.ptr + stride * (size_t)idx;

        if (ca.type == GL_UNSIGNED_BYTE)
        {
            static const float kInv255 = 1.0f / 255.0f;
            if (ca.size > 0) out.r = p[0] * kInv255;
            if (ca.size > 1) out.g = p[1] * kInv255;
            if (ca.size > 2) out.b = p[2] * kInv255;
            if (ca.size > 3) out.a = p[3] * kInv255;
        }
        else if (ca.type == GL_FLOAT)
        {
            const float* f = (const float*)p;
            if (ca.size > 0) out.r = f[0];
            if (ca.size > 1) out.g = f[1];
            if (ca.size > 2) out.b = f[2];
            if (ca.size > 3) out.a = f[3];
        }
        else if (ca.type == GL_DOUBLE)
        {
            const double* f = (const double*)p;
            if (ca.size > 0) out.r = (float)f[0];
            if (ca.size > 1) out.g = (float)f[1];
            if (ca.size > 2) out.b = (float)f[2];
            if (ca.size > 3) out.a = (float)f[3];
        }
        else
        {
            if (ca.size > 0) out.r = QD3D12_ReadScalarFast(p + 0 * typeSize, ca.type);
            if (ca.size > 1) out.g = QD3D12_ReadScalarFast(p + 1 * typeSize, ca.type);
            if (ca.size > 2) out.b = QD3D12_ReadScalarFast(p + 2 * typeSize, ca.type);
            if (ca.size > 3) out.a = QD3D12_ReadScalarFast(p + 3 * typeSize, ca.type);
        }
    }

    //
    // Texcoord 0
    //
    {
        const auto& tc = g_gl.texCoordArray[0];
        if (tc.enabled && tc.ptr)
        {
            const size_t typeSize = (size_t)QD3D12_TypeSize(tc.type);
            const size_t elemSize = (size_t)tc.size * typeSize;
            const size_t stride = tc.stride ? (size_t)tc.stride : elemSize;
            const uint8_t* p = tc.ptr + stride * (size_t)idx;

            switch (tc.type)
            {
            case GL_FLOAT:
            {
                const float* f = (const float*)p;
                if (tc.size > 0) out.u0 = f[0];
                if (tc.size > 1) out.v0 = f[1];
                break;
            }
            case GL_DOUBLE:
            {
                const double* f = (const double*)p;
                if (tc.size > 0) out.u0 = (float)f[0];
                if (tc.size > 1) out.v0 = (float)f[1];
                break;
            }
            default:
                if (tc.size > 0) out.u0 = QD3D12_ReadScalarFast(p + 0 * typeSize, tc.type);
                if (tc.size > 1) out.v0 = QD3D12_ReadScalarFast(p + 1 * typeSize, tc.type);
                break;
            }
        }
    }

    //
    // Texcoord 1
    //
    {
        const auto& tc = g_gl.texCoordArray[1];
        if (tc.enabled && tc.ptr)
        {
            const size_t typeSize = (size_t)QD3D12_TypeSize(tc.type);
            const size_t elemSize = (size_t)tc.size * typeSize;
            const size_t stride = tc.stride ? (size_t)tc.stride : elemSize;
            const uint8_t* p = tc.ptr + stride * (size_t)idx;

            switch (tc.type)
            {
            case GL_FLOAT:
            {
                const float* f = (const float*)p;
                if (tc.size > 0) out.u1 = f[0];
                if (tc.size > 1) out.v1 = f[1];
                break;
            }
            case GL_DOUBLE:
            {
                const double* f = (const double*)p;
                if (tc.size > 0) out.u1 = (float)f[0];
                if (tc.size > 1) out.v1 = (float)f[1];
                break;
            }
            default:
                if (tc.size > 0) out.u1 = QD3D12_ReadScalarFast(p + 0 * typeSize, tc.type);
                if (tc.size > 1) out.v1 = QD3D12_ReadScalarFast(p + 1 * typeSize, tc.type);
                break;
            }
        }
    }
}

static D3D12_PRIMITIVE_TOPOLOGY GetDrawTopology(GLenum originalMode)
{
    switch (originalMode)
    {
    case GL_POINTS:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

    case GL_LINES:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;

    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;

    case GL_TRIANGLE_STRIP:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    case GL_TRIANGLES:
    default:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static BatchKey BuildCurrentBatchKey(GLenum originalMode, const TextureResource* tex0, const TextureResource* tex1)
{
    const bool useTex0 = g_gl.texture2D[0];
    const bool useTex1 = g_gl.texture2D[1];

    BatchKey key{};
    key.pipeline = PickPipeline(useTex0, useTex1);
    key.topology = GetDrawTopology(originalMode);
    key.tex0SrvIndex = tex0 ? tex0->srvIndex : 0;
    key.tex1SrvIndex = tex1 ? tex1->srvIndex : 0;
    key.alphaRef = g_gl.alphaRef;
    key.useTex0 = useTex0 ? 1.0f : 0.0f;
    key.useTex1 = useTex1 ? 1.0f : 0.0f;
    key.viewport = g_gl.viewport;
    key.scissor = g_gl.scissor;
    key.tex1IsLightmap = useTex1 ? 1.0f : 0.0f;
    key.texEnvMode0 = MapTexEnvMode(g_gl.texEnvMode[0]);
    key.texEnvMode1 = MapTexEnvMode(g_gl.texEnvMode[1]);
    key.blendSrc = g_gl.blendSrc;
    key.blendDst = g_gl.blendDst;
    key.depthTest = g_gl.depthTest;
    key.depthWrite = g_gl.depthWrite;
    key.depthFunc = g_gl.depthFunc;
    key.mvp = CurrentMVP();
    key.modelMatrix = CurrentModelMatrix();
    key.geometryFlag = g_gl.currentGeometryFlag;
    return key;
}

extern "C" void APIENTRY glSelectTextureSGIS(GLenum texture)
{
    switch (texture)
    {
    case GL_TEXTURE0_SGIS: g_gl.activeTextureUnit = 0; break;
    case GL_TEXTURE1_SGIS: g_gl.activeTextureUnit = 1; break;
    default: g_gl.activeTextureUnit = 0; break;
    }
}

extern "C" void APIENTRY glMTexCoord2fSGIS(GLenum texture, GLfloat s, GLfloat t)
{
    GLuint oldUnit = g_gl.activeTextureUnit;

    switch (texture)
    {
    case GL_TEXTURE0_SGIS: g_gl.activeTextureUnit = 0; break;
    case GL_TEXTURE1_SGIS: g_gl.activeTextureUnit = 1; break;
    default: g_gl.activeTextureUnit = 0; break;
    }

    g_gl.curU[g_gl.activeTextureUnit] = s;
    g_gl.curV[g_gl.activeTextureUnit] = t;

    g_gl.activeTextureUnit = oldUnit;
}

extern "C" void APIENTRY glActiveTextureARB(GLenum texture)
{
    if (texture >= GL_TEXTURE0_ARB)
        g_gl.activeTextureUnit = ClampValue<GLuint>((GLuint)(texture - GL_TEXTURE0_ARB), 0, QD3D12_MaxTextureUnits - 1);
    else
        g_gl.activeTextureUnit = 0;
}

extern "C" void APIENTRY glMultiTexCoord2fARB(GLenum texture, GLfloat s, GLfloat t)
{
    GLuint unit = 0;
    if (texture >= GL_TEXTURE0_ARB)
        unit = ClampValue<GLuint>((GLuint)(texture - GL_TEXTURE0_ARB), 0, QD3D12_MaxTextureUnits - 1);

    g_gl.curU[unit] = s;
    g_gl.curV[unit] = t;
}


// ============================================================
// SECTION 5: utility mapping
// ============================================================

static std::vector<Mat4>& QD3D12_CurrentMatrixStack()
{
    switch (g_gl.matrixMode)
    {
    case GL_PROJECTION:
        return g_gl.projStack;

    case GL_TEXTURE:
        return g_gl.texStack[g_gl.activeTextureUnit];

    case GL_MODELVIEW:
    default:
        return g_gl.modelStack;
    }
}

static int BytesPerPixel(GLenum format, GLenum type)
{
    if (type != GL_UNSIGNED_BYTE)
        return 4;

    switch (format)
    {
    case GL_ALPHA:
    case GL_LUMINANCE:
        return 1;
    case GL_RGB:
        return 3;
    case GL_RGBA:
    default:
        return 4;
    }
}

static DXGI_FORMAT MapTextureFormat(GLenum format)
{
    switch (format)
    {
    case GL_ALPHA:
    case GL_LUMINANCE:
        return DXGI_FORMAT_R8_UNORM;
    case GL_RGB:
    case GL_RGBA:
    default:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

static D3D12_BLEND MapBlendAlpha(GLenum v) {
    switch (v) {
    case GL_ZERO:                  return D3D12_BLEND_ZERO;
    case GL_ONE:                   return D3D12_BLEND_ONE;

    case GL_SRC_ALPHA:             return D3D12_BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:   return D3D12_BLEND_INV_SRC_ALPHA;
    case GL_DST_ALPHA:             return D3D12_BLEND_DEST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA:   return D3D12_BLEND_INV_DEST_ALPHA;

        // Color factors are illegal in alpha slots.
        // Fold them to something reasonable.
    case GL_SRC_COLOR:             return D3D12_BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_COLOR:   return D3D12_BLEND_INV_SRC_ALPHA;
    case GL_DST_COLOR:             return D3D12_BLEND_DEST_ALPHA;
    case GL_ONE_MINUS_DST_COLOR:   return D3D12_BLEND_INV_DEST_ALPHA;

    case GL_SRC_ALPHA_SATURATE:    return D3D12_BLEND_ONE;

    default:                       return D3D12_BLEND_ONE;
    }
}

static D3D12_BLEND MapBlend(GLenum v)
{
    switch (v)
    {
    case GL_ZERO:                  return D3D12_BLEND_ZERO;
    case GL_ONE:                   return D3D12_BLEND_ONE;
    case GL_SRC_COLOR:             return D3D12_BLEND_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR:   return D3D12_BLEND_INV_SRC_COLOR;
    case GL_DST_COLOR:             return D3D12_BLEND_DEST_COLOR;
    case GL_ONE_MINUS_DST_COLOR:   return D3D12_BLEND_INV_DEST_COLOR;
    case GL_SRC_ALPHA:             return D3D12_BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA:   return D3D12_BLEND_INV_SRC_ALPHA;
    case GL_DST_ALPHA:             return D3D12_BLEND_DEST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA:   return D3D12_BLEND_INV_DEST_ALPHA;
    case GL_SRC_ALPHA_SATURATE:    return D3D12_BLEND_SRC_ALPHA_SAT;
    default:                       return D3D12_BLEND_ONE;
    }
}

static D3D12_COMPARISON_FUNC MapCompare(GLenum f)
{
    switch (f)
    {
    case GL_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
    case GL_LESS: return D3D12_COMPARISON_FUNC_LESS;
    case GL_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
    case GL_LEQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case GL_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
    case GL_NOTEQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case GL_GEQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case GL_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
    default: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

static D3D12_CULL_MODE MapCull(GLenum m)
{
    switch (m)
    {
    case GL_FRONT: return D3D12_CULL_MODE_FRONT;
    case GL_BACK: return D3D12_CULL_MODE_BACK;
    default: return D3D12_CULL_MODE_NONE;
    }
}

static D3D12_FILTER MapFilter(GLenum minFilter, GLenum magFilter)
{
    const bool linearMin = (minFilter == GL_LINEAR || minFilter == GL_LINEAR_MIPMAP_NEAREST || minFilter == GL_LINEAR_MIPMAP_LINEAR);
    const bool linearMag = (magFilter == GL_LINEAR);

    if (linearMin && linearMag)
        return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    if (!linearMin && !linearMag)
        return D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (linearMin && !linearMag)
        return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
}

static D3D12_TEXTURE_ADDRESS_MODE MapAddress(GLenum wrap)
{
    switch (wrap)
    {
    case GL_CLAMP: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case GL_REPEAT:
    default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY MapPrimitive(GLenum mode)
{
    switch (mode)
    {
    case GL_POINTS:         return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case GL_LINES:          return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case GL_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case GL_TRIANGLES:      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case GL_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE GetTopologyTypeFromTopology(D3D12_PRIMITIVE_TOPOLOGY topo)
{
    switch (topo)
    {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

    case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    default:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

// ============================================================
// SECTION 6: upload helpers
// ============================================================

static void QD3D12_WaitForGPU()
{
    const UINT64 signalValue = g_gl.nextFenceValue++;
    QD3D12_CHECK(g_gl.queue->Signal(g_gl.fence.Get(), signalValue));

    if (g_gl.fence->GetCompletedValue() < signalValue)
    {
        QD3D12_CHECK(g_gl.fence->SetEventOnCompletion(signalValue, g_gl.fenceEvent));
        WaitForSingleObject(g_gl.fenceEvent, INFINITE);
    }
}

static void QD3D12_WaitForFrame(UINT frameIndex)
{
    FrameResources& fr = g_gl.frames[frameIndex];
    if (fr.fenceValue != 0 && g_gl.fence->GetCompletedValue() < fr.fenceValue)
    {
        QD3D12_CHECK(g_gl.fence->SetEventOnCompletion(fr.fenceValue, g_gl.fenceEvent));
        WaitForSingleObject(g_gl.fenceEvent, INFINITE);
    }
}

static void QD3D12_ResetUploadRing()
{
    g_gl.upload.offset = 0;
}

struct UploadAlloc
{
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    UINT offset = 0;
};

static UploadAlloc QD3D12_AllocUpload(UINT bytes, UINT alignment)
{
    if (alignment == 0)
        alignment = 1;

    UINT alignedOffset = (g_gl.upload.offset + (alignment - 1)) & ~(alignment - 1);

    if (alignedOffset + bytes > g_gl.upload.size)
    {
        QD3D12_Fatal(
            "Per-frame upload buffer overflow: need %u bytes, aligned offset %u, size %u",
            bytes, alignedOffset, g_gl.upload.size);
    }

    UploadAlloc out;
    out.offset = alignedOffset;
    out.cpu = g_gl.upload.cpuBase[g_gl.frameIndex] + alignedOffset;
    out.gpu = g_gl.upload.gpuBase[g_gl.frameIndex] + alignedOffset;

    g_gl.upload.offset = alignedOffset + bytes;
    return out;
}

// ============================================================
// SECTION 7: D3D12 initialization
// ============================================================

static void QD3D12_CreateOcclusionQueryObjects()
{
    D3D12_QUERY_HEAP_DESC qh{};
    qh.Count = QD3D12_MaxQueries;
    qh.NodeMask = 0;
    qh.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    QD3D12_CHECK(g_gl.device->CreateQueryHeap(&qh, IID_PPV_ARGS(&g_gl.occlusionQueryHeap)));

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(UINT64) * QD3D12_MaxQueries;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    QD3D12_CHECK(
        g_gl.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_gl.occlusionReadback)));

    QD3D12_CHECK(g_gl.occlusionReadback->Map(0, nullptr, (void**)&g_gl.occlusionReadbackCpu));
}

static void QD3D12_CreateDevice()
{
#if defined(_DEBUG)
    // {
    //     ComPtr<ID3D12Debug> debug;
    //     if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    //         debug->EnableDebugLayer();
    // }
#endif

    QD3D12_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&g_gl.factory)));
    QD3D12_CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_gl.device)));

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    QD3D12_CHECK(g_gl.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_gl.queue)));
}

static void QD3D12_CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount = QD3D12_FrameCount;
    sd.Width = g_gl.width;
    sd.Height = g_gl.height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> sc1;
    QD3D12_CHECK(g_gl.factory->CreateSwapChainForHwnd(
        g_gl.queue.Get(),
        g_gl.hwnd,
        &sd,
        nullptr,
        nullptr,
        &sc1));

    QD3D12_CHECK(sc1.As(&g_gl.swapChain));
    g_gl.frameIndex = g_gl.swapChain->GetCurrentBackBufferIndex();
}

static void QD3D12_CreateRTVs()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = QD3D12_FrameCount * 3; // backbuffer + normal MRT + position MRT per frame
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    QD3D12_CHECK(g_gl.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_gl.rtvHeap)));

    g_gl.rtvStride = g_gl.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gl.rtvHeap->GetCPUDescriptorHandleForHeapStart();

    //
    // 0 .. FrameCount-1 : swap chain backbuffers
    //
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        QD3D12_CHECK(g_gl.swapChain->GetBuffer(i, IID_PPV_ARGS(&g_gl.backBuffers[i])));
        g_gl.device->CreateRenderTargetView(g_gl.backBuffers[i].Get(), nullptr, h);
        g_gl.backBufferState[i] = D3D12_RESOURCE_STATE_PRESENT;
        h.ptr += g_gl.rtvStride;
    }

    //
    // FrameCount .. 2*FrameCount-1 : normal MRTs
    //
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = g_gl.width;
        rd.Height = g_gl.height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clear.Color[0] = 0.5f;
        clear.Color[1] = 0.5f;
        clear.Color[2] = 1.0f;
        clear.Color[3] = 1.0f;

        QD3D12_CHECK(g_gl.device->CreateCommittedResource(
            &hp,
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear,
            IID_PPV_ARGS(&g_gl.normalBuffers[i])));

        g_gl.device->CreateRenderTargetView(g_gl.normalBuffers[i].Get(), nullptr, h);
        g_gl.normalBufferState[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        h.ptr += g_gl.rtvStride;
    }

    //
    // 2*FrameCount .. 3*FrameCount-1 : world position MRTs
    //
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = g_gl.width;
        rd.Height = g_gl.height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clear.Color[0] = 0.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 0.0f;

        QD3D12_CHECK(g_gl.device->CreateCommittedResource(
            &hp,
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear,
            IID_PPV_ARGS(&g_gl.positionBuffers[i])));

        g_gl.device->CreateRenderTargetView(g_gl.positionBuffers[i].Get(), nullptr, h);
        g_gl.positionBufferState[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        h.ptr += g_gl.rtvStride;
    }

    //
    // Normal SRVs
    //
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        if (g_gl.normalSrvIndex[i] == UINT_MAX)
        {
            g_gl.normalSrvIndex[i] = g_gl.nextSrvIndex++;
            g_gl.normalSrvCpu[i] = QD3D12_SrvCpu(g_gl.normalSrvIndex[i]);
            g_gl.normalSrvGpu[i] = QD3D12_SrvGpu(g_gl.normalSrvIndex[i]);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;

        g_gl.device->CreateShaderResourceView(
            g_gl.normalBuffers[i].Get(),
            &sd,
            g_gl.normalSrvCpu[i]);
    }

    //
    // Position SRVs
    //
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        if (g_gl.positionSrvIndex[i] == UINT_MAX)
        {
            g_gl.positionSrvIndex[i] = g_gl.nextSrvIndex++;
            g_gl.positionSrvCpu[i] = QD3D12_SrvCpu(g_gl.positionSrvIndex[i]);
            g_gl.positionSrvGpu[i] = QD3D12_SrvGpu(g_gl.positionSrvIndex[i]);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;

        g_gl.device->CreateShaderResourceView(
            g_gl.positionBuffers[i].Get(),
            &sd,
            g_gl.positionSrvCpu[i]);
    }
}

static void QD3D12_CreateDSV()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    QD3D12_CHECK(g_gl.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_gl.dsvHeap)));

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = g_gl.width;
    rd.Height = g_gl.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    QD3D12_CHECK(g_gl.device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&g_gl.depthBuffer)));

    g_gl.device->CreateDepthStencilView(g_gl.depthBuffer.Get(), nullptr, g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

static void QD3D12_CreateSrvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = QD3D12_MaxTextures;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    QD3D12_CHECK(g_gl.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_gl.srvHeap)));
    g_gl.srvStride = g_gl.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

static D3D12_CPU_DESCRIPTOR_HANDLE QD3D12_SrvCpu(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gl.srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * SIZE_T(g_gl.srvStride);
    return h;
}

static D3D12_GPU_DESCRIPTOR_HANDLE QD3D12_SrvGpu(UINT index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = g_gl.srvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * UINT64(g_gl.srvStride);
    return h;
}

static void QD3D12_CreateCommandObjects()
{
    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
        QD3D12_CHECK(g_gl.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_gl.frames[i].cmdAlloc)));

    QD3D12_CHECK(g_gl.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_gl.frames[0].cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&g_gl.cmdList)));
    QD3D12_CHECK(g_gl.cmdList->Close());

    QD3D12_CHECK(g_gl.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_gl.fence)));
    g_gl.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}
static void QD3D12_CreateUploadRing()
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = QD3D12_UploadBufferSize;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    g_gl.upload.size = QD3D12_UploadBufferSize;
    g_gl.upload.offset = 0;

    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        QD3D12_CHECK(g_gl.device->CreateCommittedResource(
            &hp,
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&g_gl.upload.resource[i])));

        g_gl.upload.gpuBase[i] = g_gl.upload.resource[i]->GetGPUVirtualAddress();

        QD3D12_CHECK(g_gl.upload.resource[i]->Map(
            0,
            nullptr,
            reinterpret_cast<void**>(&g_gl.upload.cpuBase[i])));
    }
}

static ComPtr<ID3DBlob> CompileShaderVariant(const char* entry, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompile(kQuakeWrapperHLSL, strlen(kQuakeWrapperHLSL), nullptr, nullptr, nullptr,
        entry, target, flags, 0, &blob, &err);
    if (FAILED(hr))
        QD3D12_Fatal("Shader compile failed for %s: %s", entry, err ? (const char*)err->GetBufferPointer() : "unknown");
    return blob;
}
static void QD3D12_CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0;
    range0.RegisterSpace = 0;
    range0.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1;
    range1.RegisterSpace = 0;
    range1.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samps[2] = {};

    samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samps[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samps[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samps[0].ShaderRegister = 0;
    samps[0].RegisterSpace = 0;
    samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samps[0].MaxLOD = D3D12_FLOAT32_MAX;

    samps[1] = samps[0];
    samps[1].ShaderRegister = 1;
    samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 3;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samps;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    QD3D12_CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    QD3D12_CHECK(g_gl.device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_gl.rootSig)));
}


static void QD3D12_CompileShaders()
{
    g_gl.vsMainBlob = CompileShaderVariant("VSMain", "vs_5_0");
    g_gl.psMainBlob = CompileShaderVariant("PSMain", "ps_5_0");
    g_gl.psAlphaBlob = CompileShaderVariant("PSMainAlphaTest", "ps_5_0");
    g_gl.psUntexturedBlob = CompileShaderVariant("PSMainUntextured", "ps_5_0");
}

static const D3D12_INPUT_ELEMENT_DESC kGLVertexInputLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, (UINT)offsetof(GLVertex, px), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, (UINT)offsetof(GLVertex, nx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, (UINT)offsetof(GLVertex, u0), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, (UINT)offsetof(GLVertex, u1), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(GLVertex, r),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static UINT8 BuildColorWriteMask()
{
    UINT8 mask = 0;
    if (g_gl.colorMaskR) mask |= D3D12_COLOR_WRITE_ENABLE_RED;
    if (g_gl.colorMaskG) mask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    if (g_gl.colorMaskB) mask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (g_gl.colorMaskA) mask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    return mask;
}

static D3D12_CPU_DESCRIPTOR_HANDLE CurrentNormalRTV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gl.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(QD3D12_FrameCount + g_gl.frameIndex) * SIZE_T(g_gl.rtvStride);
    return h;
}
static D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPSODesc(
    PipelineMode mode,
    ID3DBlob* vs,
    ID3DBlob* ps,
    GLenum srcBlendGL,
    GLenum dstBlendGL,
    bool depthTest,
    bool depthWrite,
    GLenum depthFuncGL,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topoType)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = g_gl.rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    d.InputLayout.pInputElementDescs = kGLVertexInputLayout;
    d.InputLayout.NumElements = _countof(kGLVertexInputLayout);

    d.PrimitiveTopologyType = topoType;

    d.NumRenderTargets = 3;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;

    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;

    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.FrontCounterClockwise = FALSE;
    d.RasterizerState.DepthClipEnable = TRUE;

    d.BlendState.RenderTarget[0].RenderTargetWriteMask = BuildColorWriteMask();
    d.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    d.BlendState.AlphaToCoverageEnable = FALSE;
    d.BlendState.IndependentBlendEnable = TRUE;

    for (int i = 0; i < 3; ++i)
    {
        auto& rt = d.BlendState.RenderTarget[i];
        rt.BlendEnable = FALSE;
        rt.LogicOpEnable = FALSE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    }

    if (mode == PIPE_BLEND_TEX || mode == PIPE_BLEND_UNTEX)
    {
        auto& rt = d.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = MapBlend(srcBlendGL);
        rt.DestBlend = MapBlend(dstBlendGL);
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = MapBlendAlpha(srcBlendGL);
        rt.DestBlendAlpha = MapBlendAlpha(dstBlendGL);
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    d.DepthStencilState.DepthEnable = depthTest ? TRUE : FALSE;
    d.DepthStencilState.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = MapCompare(depthFuncGL);
    d.DepthStencilState.StencilEnable = FALSE;

    return d;
}

static void QD3D12_CreatePSOs()
{

}

static uint64_t MakePSOKey(
    PipelineMode mode,
    GLenum src,
    GLenum dst,
    bool depthTest,
    bool depthWrite,
    GLenum depthFunc,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topoType)
{
    return
        (uint64_t(uint32_t(mode)) << 56ull) |
        (uint64_t(uint32_t(src) & 0xFF) << 48ull) |
        (uint64_t(uint32_t(dst) & 0xFF) << 40ull) |
        (uint64_t(depthTest ? 1 : 0) << 39ull) |
        (uint64_t(depthWrite ? 1 : 0) << 38ull) |
        (uint64_t(uint32_t(topoType) & 0x3) << 36ull) |
        (uint64_t(uint32_t(depthFunc) & 0xFFFF) << 16ull);
}

static std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> g_psoCache;

static ID3D12PipelineState* QD3D12_GetPSO(
    PipelineMode mode,
    GLenum srcBlendGL,
    GLenum dstBlendGL,
    bool depthTest,
    bool depthWrite,
    GLenum depthFuncGL,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topoType)
{
    const uint64_t key = MakePSOKey(
        mode, srcBlendGL, dstBlendGL, depthTest, depthWrite, depthFuncGL, topoType);

    auto it = g_psoCache.find(key);
    if (it != g_psoCache.end())
        return it->second.Get();

    ID3DBlob* psBlob = nullptr;
    switch (mode)
    {
    case PIPE_ALPHA_TEST_TEX: psBlob = g_gl.psAlphaBlob.Get(); break;
    case PIPE_OPAQUE_UNTEX:
    case PIPE_BLEND_UNTEX:    psBlob = g_gl.psUntexturedBlob.Get(); break;
    case PIPE_OPAQUE_TEX:
    case PIPE_BLEND_TEX:
    default:                  psBlob = g_gl.psMainBlob.Get(); break;
    }

    auto desc = BuildPSODesc(
        mode,
        g_gl.vsMainBlob.Get(),
        psBlob,
        srcBlendGL,
        dstBlendGL,
        depthTest,
        depthWrite,
        depthFuncGL,
        topoType);

    ComPtr<ID3D12PipelineState> newPSO;
    QD3D12_CHECK(g_gl.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&newPSO)));

    ID3D12PipelineState* out = newPSO.Get();
    g_psoCache.emplace(key, std::move(newPSO));
    return out;
}

static void QD3D12_CreateWhiteTexture()
{
    g_gl.whiteTexture.glId = 0;
    g_gl.whiteTexture.width = 1;
    g_gl.whiteTexture.height = 1;
    g_gl.whiteTexture.format = GL_RGBA;
    g_gl.whiteTexture.sysmem = { 255, 255, 255, 255 };
    g_gl.whiteTexture.srvIndex = 0;
    g_gl.whiteTexture.srvCpu = QD3D12_SrvCpu(0);
    g_gl.whiteTexture.srvGpu = QD3D12_SrvGpu(0);

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 1;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES hpDef{};
    hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
    QD3D12_CHECK(g_gl.device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_gl.whiteTexture.texture)));

    g_gl.whiteTexture.state = D3D12_RESOURCE_STATE_COPY_DEST;

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    g_gl.device->CreateShaderResourceView(g_gl.whiteTexture.texture.Get(), &sd, g_gl.whiteTexture.srvCpu);

    // Upload 1x1 white using a one-time command list.
    QD3D12_CHECK(g_gl.frames[g_gl.frameIndex].cmdAlloc->Reset());
    QD3D12_CHECK(g_gl.cmdList->Reset(g_gl.frames[g_gl.frameIndex].cmdAlloc.Get(), nullptr));

    const UINT64 uploadPitch = 256;
    const UINT64 uploadSize = uploadPitch;
    UploadAlloc alloc = QD3D12_AllocUpload((UINT)uploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    memset(alloc.cpu, 0, uploadSize);
    ((uint8_t*)alloc.cpu)[0] = 255;
    ((uint8_t*)alloc.cpu)[1] = 255;
    ((uint8_t*)alloc.cpu)[2] = 255;
    ((uint8_t*)alloc.cpu)[3] = 255;

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = g_gl.whiteTexture.texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = g_gl.upload.resource[g_gl.frameIndex].Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = alloc.offset;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 1;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = (UINT)uploadPitch;

    g_gl.cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = g_gl.whiteTexture.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_gl.cmdList->ResourceBarrier(1, &b);
    g_gl.whiteTexture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    QD3D12_CHECK(g_gl.cmdList->Close());
    ID3D12CommandList* lists[] = { g_gl.cmdList.Get() };
    g_gl.queue->ExecuteCommandLists(1, lists);
    QD3D12_WaitForGPU();

    g_gl.whiteTexture.gpuValid = true;
    QD3D12_ResetUploadRing();
}

static void QD3D12_UpdateViewportState()
{
    g_gl.viewport.TopLeftX = (float)g_gl.viewportX;
    g_gl.viewport.TopLeftY = (float)(g_gl.height - (g_gl.viewportY + g_gl.viewportH));
    g_gl.viewport.Width = (float)g_gl.viewportW;
    g_gl.viewport.Height = (float)g_gl.viewportH;
    g_gl.viewport.MinDepth = (float)ClampValue<GLclampd>(g_gl.depthRangeNear, 0.0, 1.0);
    g_gl.viewport.MaxDepth = (float)ClampValue<GLclampd>(g_gl.depthRangeFar, 0.0, 1.0);

    g_gl.scissor.left = g_gl.viewportX;
    g_gl.scissor.top = g_gl.height - (g_gl.viewportY + g_gl.viewportH);
    g_gl.scissor.right = g_gl.viewportX + g_gl.viewportW;
    g_gl.scissor.bottom = g_gl.height - g_gl.viewportY;
}

bool QD3D12_InitForQuake(HWND hwnd, int width, int height)
{
    g_gl.hwnd = hwnd;
    g_gl.width = (UINT)width;
    g_gl.height = (UINT)height;
    g_gl.viewportW = width;
    g_gl.viewportH = height;

    QD3D12_CreateDevice();
    QD3D12_CreateSwapChain();

    // SRV heap must exist before any code that allocates SRV indices/handles.
    QD3D12_CreateSrvHeap();

    QD3D12_CreateRTVs();
    QD3D12_CreateDSV();
    QD3D12_CreateCommandObjects();
    QD3D12_CreateUploadRing();
    QD3D12_CompileShaders();
    QD3D12_CreateRootSignature();
    QD3D12_CreatePSOs();
    QD3D12_UpdateViewportState();
    QD3D12_CreateWhiteTexture();
    QD3D12_CreateOcclusionQueryObjects();

    QD3D12_Log("QD3D12 initialized: %ux%u", g_gl.width, g_gl.height);
    return true;
}

void QD3D12_ShutdownForQuake()
{
    QD3D12_WaitForGPU();

    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        if (g_gl.upload.resource[i])
            g_gl.upload.resource[i]->Unmap(0, nullptr);
    }

    if (g_gl.fenceEvent)
        CloseHandle(g_gl.fenceEvent);

    g_gl = GLState{};
}

// ============================================================
// SECTION 8: frame control
// ============================================================

static D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gl.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(g_gl.frameIndex) * SIZE_T(g_gl.rtvStride);
    return h;
}

void QD3D12_BeginFrame()
{
    g_gl.queuedBatches.clear();
    g_gl.frameIndex = g_gl.swapChain->GetCurrentBackBufferIndex();
    QD3D12_WaitForFrame(g_gl.frameIndex);
    QD3D12_ResetUploadRing();

    FrameResources& fr = g_gl.frames[g_gl.frameIndex];
    QD3D12_CHECK(fr.cmdAlloc->Reset());
    QD3D12_CHECK(g_gl.cmdList->Reset(fr.cmdAlloc.Get(), nullptr));

    if (g_gl.backBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.backBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.backBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.backBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (g_gl.normalBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.normalBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.normalBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.normalBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    g_gl.cmdList->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    g_gl.cmdList->RSSetViewports(1, &g_gl.viewport);
    g_gl.cmdList->RSSetScissorRects(1, &g_gl.scissor);

    const float normalClear[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    g_gl.cmdList->ClearRenderTargetView(CurrentNormalRTV(), normalClear, 0, nullptr);
}

void QD3D12_EndFrame()
{
    QD3D12_FlushQueuedBatches();

    if (g_gl.backBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_PRESENT)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.backBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.backBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.backBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_PRESENT;
    }

    QD3D12_CHECK(g_gl.cmdList->Close());
    ID3D12CommandList* lists[] = { g_gl.cmdList.Get() };
    g_gl.queue->ExecuteCommandLists(1, lists);
}

void QD3D12_Present()
{
    QD3D12_CHECK(g_gl.swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING));

    FrameResources& fr = g_gl.frames[g_gl.frameIndex];
    const UINT64 signalValue = g_gl.nextFenceValue++;
    QD3D12_CHECK(g_gl.queue->Signal(g_gl.fence.Get(), signalValue));
    fr.fenceValue = signalValue;
}

void QD3D12_SwapBuffers(HDC hdc) {
    QD3D12_EndFrame();
    QD3D12_Present();
    QD3D12_BeginFrame();
    QD3D12_CollectRetiredResources();
}

// ============================================================
// SECTION 9: texture upload/update
// ============================================================

static void EnsureTextureResource(TextureResource& tex)
{
    const DXGI_FORMAT dxgiFormat = tex.dxgiFormat;

    bool needsRecreate = false;

    if (!tex.texture)
    {
        needsRecreate = true;
    }
    else
    {
        D3D12_RESOURCE_DESC desc = tex.texture->GetDesc();
        if ((int)desc.Width != tex.width ||
            (int)desc.Height != tex.height ||
            desc.Format != dxgiFormat)
        {
            QD3D12_RetireResource(tex.texture);
            tex.gpuValid = false;
            tex.state = D3D12_RESOURCE_STATE_COPY_DEST;
            needsRecreate = true;
        }
    }

    if (tex.width <= 0 || tex.height <= 0)
        return;

    if (!needsRecreate)
        return;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT)tex.width;
    rd.Height = (UINT)tex.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = dxgiFormat;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    QD3D12_CHECK(g_gl.device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&tex.texture)));

    tex.state = D3D12_RESOURCE_STATE_COPY_DEST;

    if (tex.srvIndex == UINT_MAX)
    {
        tex.srvIndex = g_gl.nextSrvIndex++;
        tex.srvCpu = QD3D12_SrvCpu(tex.srvIndex);
        tex.srvGpu = QD3D12_SrvGpu(tex.srvIndex);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Format = dxgiFormat;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    g_gl.device->CreateShaderResourceView(tex.texture.Get(), &sd, tex.srvCpu);

    tex.gpuValid = false;
}

static TextureResource* QD3D12_EnsureLightingTexture(int width, int height)
{
    if (g_lightingTextureId == 0)
    {
        g_lightingTextureId = g_gl.nextTextureId++;

        TextureResource tex{};
        tex.glId = g_lightingTextureId;
        tex.width = (int)width;
        tex.height = (int)height;
        tex.format = GL_RGBA;
        tex.dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex.minFilter = GL_LINEAR;
        tex.magFilter = GL_LINEAR;
        tex.wrapS = GL_CLAMP;
        tex.wrapT = GL_CLAMP;

        tex.srvIndex = g_gl.nextSrvIndex++;
        tex.srvCpu = QD3D12_SrvCpu(tex.srvIndex);
        tex.srvGpu = QD3D12_SrvGpu(tex.srvIndex);

        auto it = g_gl.textures.emplace(g_lightingTextureId, std::move(tex)).first;
        g_lightingTexture = &it->second;
    }

    if (!g_lightingTexture)
    {
        auto it = g_gl.textures.find(g_lightingTextureId);
        if (it == g_gl.textures.end())
            return nullptr;
        g_lightingTexture = &it->second;
    }

    const bool needsCreate =
        !g_lightingTexture->texture ||
        g_lightingTexture->width != (int)width ||
        g_lightingTexture->height != (int)height ||
        ((g_lightingTexture->texture->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0);

    if (!needsCreate)
        return g_lightingTexture;

    if (g_lightingTexture->texture)
    {
        QD3D12_RetireResource(g_lightingTexture->texture);
    }

    g_lightingTexture->width = (int)width;
    g_lightingTexture->height = (int)height;
    g_lightingTexture->sysmem.clear();

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Alignment = 0;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.SampleDesc.Quality = 0;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    QD3D12_CHECK(g_gl.device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&g_lightingTexture->texture)));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Format = rd.Format;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;

    g_gl.device->CreateShaderResourceView(
        g_lightingTexture->texture.Get(),
        &srv,
        g_lightingTexture->srvCpu);

    g_lightingTexture->gpuValid = true;
    g_lightingTexture->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    g_lightingTextureState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    return g_lightingTexture;
}

static void ConvertToRGBA8(const TextureResource& tex, std::vector<uint8_t>& outRGBA)
{
    const int pixelCount = tex.width * tex.height;
    outRGBA.resize((size_t)pixelCount * 4);

    if (tex.sysmem.empty())
    {
        std::fill(outRGBA.begin(), outRGBA.end(), 255);
        return;
    }

    const uint8_t* src = tex.sysmem.data();
    uint8_t* dst = outRGBA.data();

    if (tex.format == GL_RGBA)
    {
        memcpy(dst, src, outRGBA.size());
        return;
    }

    if (tex.format == GL_RGB)
    {
#if defined(__SSSE3__) || (defined(_M_IX86_FP) || defined(_M_X64))
        // SSSE3 path: process 4 RGB pixels (12 bytes) -> 16 RGBA bytes at a time.
        // Output:
        // [r0 g0 b0 255 r1 g1 b1 255 r2 g2 b2 255 r3 g3 b3 255]
        //
        // We load 16 bytes even though we only logically consume 12. That means
        // the source buffer must have at least 4 readable bytes past the final
        // 12-byte chunk if you try to run this on the very last block. To keep it
        // safe, only use SIMD while at least 16 source bytes remain.
        //
        // So the SIMD loop runs while (i + 4) <= pixelCount AND enough source bytes
        // remain for a safe 16-byte load.
        const __m128i alphaMask = _mm_set1_epi32(0xFF000000);

        int i = 0;
        int srcByteOffset = 0;
        const int srcBytes = pixelCount * 3;

        // Need 16 readable bytes from src + srcByteOffset
        while (i + 4 <= pixelCount && srcByteOffset + 16 <= srcBytes)
        {
            __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + srcByteOffset));

            // Pull RGB triples into 32-bit lanes:
            // lane0 = [r0 g0 b0 x]
            // lane1 = [r1 g1 b1 x]
            // lane2 = [r2 g2 b2 x]
            // lane3 = [r3 g3 b3 x]
            const __m128i shuffled = _mm_shuffle_epi8(
                in,
                _mm_setr_epi8(
                    0, 1, 2, char(0x80),
                    3, 4, 5, char(0x80),
                    6, 7, 8, char(0x80),
                    9, 10, 11, char(0x80)
                )
            );

            const __m128i out = _mm_or_si128(shuffled, alphaMask);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), out);

            i += 4;
            srcByteOffset += 12;
        }

        for (; i < pixelCount; ++i)
        {
            dst[i * 4 + 0] = src[i * 3 + 0];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
#else
        for (int i = 0; i < pixelCount; ++i)
        {
            dst[i * 4 + 0] = src[i * 3 + 0];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
#endif
        return;
    }

    if (tex.format == GL_ALPHA)
    {
#if defined(__AVX2__) || defined(_M_X64)
        int i = 0;

        // 32 pixels at a time
#if defined(__AVX2__)
        const __m256i white = _mm256_set1_epi32(0x00FFFFFF);

        for (; i + 32 <= pixelCount; i += 32)
        {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));

            __m256i lo16 = _mm256_unpacklo_epi8(_mm256_setzero_si256(), a);
            __m256i hi16 = _mm256_unpackhi_epi8(_mm256_setzero_si256(), a);

            __m256i p0 = _mm256_unpacklo_epi16(white, lo16);
            __m256i p1 = _mm256_unpackhi_epi16(white, lo16);
            __m256i p2 = _mm256_unpacklo_epi16(white, hi16);
            __m256i p3 = _mm256_unpackhi_epi16(white, hi16);

            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 0) * 4), p0);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 8) * 4), p1);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 16) * 4), p2);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 24) * 4), p3);
        }
#endif

        for (; i < pixelCount; ++i)
        {
            dst[i * 4 + 0] = 255;
            dst[i * 4 + 1] = 255;
            dst[i * 4 + 2] = 255;
            dst[i * 4 + 3] = src[i];
        }
#else
        for (int i = 0; i < pixelCount; ++i)
        {
            dst[i * 4 + 0] = 255;
            dst[i * 4 + 1] = 255;
            dst[i * 4 + 2] = 255;
            dst[i * 4 + 3] = src[i];
        }
#endif
        return;
    }

    if (tex.format == GL_LUMINANCE || tex.format == GL_INTENSITY)
    {
        const bool intensity = (tex.format == GL_INTENSITY);

#if defined(__AVX2__) || defined(_M_X64)
        int i = 0;

#if defined(__AVX2__)
        for (; i + 32 <= pixelCount; i += 32)
        {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));

            __m256i lo16 = _mm256_unpacklo_epi8(v, v);
            __m256i hi16 = _mm256_unpackhi_epi8(v, v);

            __m256i p0 = _mm256_unpacklo_epi16(lo16, lo16);
            __m256i p1 = _mm256_unpackhi_epi16(lo16, lo16);
            __m256i p2 = _mm256_unpacklo_epi16(hi16, hi16);
            __m256i p3 = _mm256_unpackhi_epi16(hi16, hi16);

            if (!intensity)
            {
                // Force alpha to 255 for luminance
                const __m256i alphaMask = _mm256_set1_epi32(0xFF000000);
                const __m256i rgbMask = _mm256_set1_epi32(0x00FFFFFF);

                p0 = _mm256_or_si256(_mm256_and_si256(p0, rgbMask), alphaMask);
                p1 = _mm256_or_si256(_mm256_and_si256(p1, rgbMask), alphaMask);
                p2 = _mm256_or_si256(_mm256_and_si256(p2, rgbMask), alphaMask);
                p3 = _mm256_or_si256(_mm256_and_si256(p3, rgbMask), alphaMask);
            }

            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 0) * 4), p0);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 8) * 4), p1);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 16) * 4), p2);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (i + 24) * 4), p3);
        }
#endif

        for (; i < pixelCount; ++i)
        {
            const uint8_t v = src[i];
            dst[i * 4 + 0] = v;
            dst[i * 4 + 1] = v;
            dst[i * 4 + 2] = v;
            dst[i * 4 + 3] = intensity ? v : 255;
        }
#else
        for (int i = 0; i < pixelCount; ++i)
        {
            const uint8_t v = src[i];
            dst[i * 4 + 0] = v;
            dst[i * 4 + 1] = v;
            dst[i * 4 + 2] = v;
            dst[i * 4 + 3] = intensity ? v : 255;
        }
#endif
        return;
    }

    std::fill(outRGBA.begin(), outRGBA.end(), 255);
}
static void UploadTexture(TextureResource& tex)
{
    if (!tex.texture)
        return;

    const UINT srcRowBytes = (UINT)(tex.width * 4);
    const UINT rowPitch = (srcRowBytes + 255u) & ~255u;
    const UINT uploadSize = rowPitch * (UINT)tex.height;
    UploadAlloc alloc = QD3D12_AllocUpload(uploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    uint8_t* dstBase = (uint8_t*)alloc.cpu;

    if (tex.sysmem.empty())
    {
        for (int y = 0; y < tex.height; ++y)
        {
            uint8_t* dst = dstBase + (size_t)y * rowPitch;
            memset(dst, 255, tex.width * 4);
        }
    }
    else if (tex.format == GL_RGBA)
    {
        const uint8_t* srcBase = tex.sysmem.data();
        for (int y = 0; y < tex.height; ++y)
        {
            memcpy(dstBase + (size_t)y * rowPitch, srcBase + (size_t)y * tex.width * 4, tex.width * 4);
        }
    }
    else if (tex.format == GL_RGB)
    {
        const uint8_t* srcBase = tex.sysmem.data();

        for (int y = 0; y < tex.height; ++y)
        {
            const uint8_t* src = srcBase + (size_t)y * tex.width * 3;
            uint8_t* dst = dstBase + (size_t)y * rowPitch;

#if defined(__SSSE3__) || defined(_M_X64)
            const int count = tex.width;
            int x = 0;
            int srcByteOffset = 0;
            const int srcBytes = count * 3;

            const __m128i alphaMask = _mm_set1_epi32(0xFF000000);

            while (x + 4 <= count && srcByteOffset + 16 <= srcBytes)
            {
                __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + srcByteOffset));

                __m128i shuffled = _mm_shuffle_epi8(
                    in,
                    _mm_setr_epi8(
                        0, 1, 2, char(0x80),
                        3, 4, 5, char(0x80),
                        6, 7, 8, char(0x80),
                        9, 10, 11, char(0x80)
                    )
                );

                __m128i out = _mm_or_si128(shuffled, alphaMask);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x * 4), out);

                x += 4;
                srcByteOffset += 12;
            }

            for (; x < count; ++x)
            {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
#else
            for (int x = 0; x < tex.width; ++x)
            {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
#endif
        }
    }
    else if (tex.format == GL_ALPHA)
    {
        const uint8_t* srcBase = tex.sysmem.data();

        for (int y = 0; y < tex.height; ++y)
        {
            const uint8_t* src = srcBase + (size_t)y * tex.width;
            uint8_t* dst = dstBase + (size_t)y * rowPitch;

#if defined(__AVX2__)
            int x = 0;
            const __m256i white = _mm256_set1_epi32(0x00FFFFFF);

            for (; x + 32 <= tex.width; x += 32)
            {
                __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + x));

                __m256i lo16 = _mm256_unpacklo_epi8(_mm256_setzero_si256(), a);
                __m256i hi16 = _mm256_unpackhi_epi8(_mm256_setzero_si256(), a);

                __m256i p0 = _mm256_unpacklo_epi16(white, lo16);
                __m256i p1 = _mm256_unpackhi_epi16(white, lo16);
                __m256i p2 = _mm256_unpacklo_epi16(white, hi16);
                __m256i p3 = _mm256_unpackhi_epi16(white, hi16);

                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 0) * 4), p0);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 8) * 4), p1);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 16) * 4), p2);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 24) * 4), p3);
            }

            for (; x < tex.width; ++x)
            {
                dst[x * 4 + 0] = 255;
                dst[x * 4 + 1] = 255;
                dst[x * 4 + 2] = 255;
                dst[x * 4 + 3] = src[x];
            }
#else
            for (int x = 0; x < tex.width; ++x)
            {
                dst[x * 4 + 0] = 255;
                dst[x * 4 + 1] = 255;
                dst[x * 4 + 2] = 255;
                dst[x * 4 + 3] = src[x];
            }
#endif
        }
    }
    else if (tex.format == GL_LUMINANCE || tex.format == GL_INTENSITY)
    {
        const bool intensity = (tex.format == GL_INTENSITY);
        const uint8_t* srcBase = tex.sysmem.data();

        for (int y = 0; y < tex.height; ++y)
        {
            const uint8_t* src = srcBase + (size_t)y * tex.width;
            uint8_t* dst = dstBase + (size_t)y * rowPitch;

#if defined(__AVX2__)
            int x = 0;

            for (; x + 32 <= tex.width; x += 32)
            {
                __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + x));

                __m256i lo16 = _mm256_unpacklo_epi8(v, v);
                __m256i hi16 = _mm256_unpackhi_epi8(v, v);

                __m256i p0 = _mm256_unpacklo_epi16(lo16, lo16);
                __m256i p1 = _mm256_unpackhi_epi16(lo16, lo16);
                __m256i p2 = _mm256_unpacklo_epi16(hi16, hi16);
                __m256i p3 = _mm256_unpackhi_epi16(hi16, hi16);

                if (!intensity)
                {
                    const __m256i rgbMask = _mm256_set1_epi32(0x00FFFFFF);
                    const __m256i alphaMask = _mm256_set1_epi32(0xFF000000);

                    p0 = _mm256_or_si256(_mm256_and_si256(p0, rgbMask), alphaMask);
                    p1 = _mm256_or_si256(_mm256_and_si256(p1, rgbMask), alphaMask);
                    p2 = _mm256_or_si256(_mm256_and_si256(p2, rgbMask), alphaMask);
                    p3 = _mm256_or_si256(_mm256_and_si256(p3, rgbMask), alphaMask);
                }

                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 0) * 4), p0);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 8) * 4), p1);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 16) * 4), p2);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + (x + 24) * 4), p3);
            }

            for (; x < tex.width; ++x)
            {
                uint8_t v = src[x];
                dst[x * 4 + 0] = v;
                dst[x * 4 + 1] = v;
                dst[x * 4 + 2] = v;
                dst[x * 4 + 3] = intensity ? v : 255;
            }
#else
            for (int x = 0; x < tex.width; ++x)
            {
                uint8_t v = src[x];
                dst[x * 4 + 0] = v;
                dst[x * 4 + 1] = v;
                dst[x * 4 + 2] = v;
                dst[x * 4 + 3] = intensity ? v : 255;
            }
#endif
        }
    }
    else
    {
        for (int y = 0; y < tex.height; ++y)
        {
            uint8_t* dst = dstBase + (size_t)y * rowPitch;
            memset(dst, 255, tex.width * 4);
        }
    }

    if (tex.state != D3D12_RESOURCE_STATE_COPY_DEST)
    {
        D3D12_RESOURCE_BARRIER toCopy{};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = tex.texture.Get();
        toCopy.Transition.StateBefore = tex.state;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &toCopy);

        tex.state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = g_gl.upload.resource[g_gl.frameIndex].Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Offset = alloc.offset;
    srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srcLoc.PlacedFootprint.Footprint.Width = tex.width;
    srcLoc.PlacedFootprint.Footprint.Height = tex.height;
    srcLoc.PlacedFootprint.Footprint.Depth = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = tex.texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    g_gl.cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER toSrv{};
    toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrv.Transition.pResource = tex.texture.Get();
    toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_gl.cmdList->ResourceBarrier(1, &toSrv);

    tex.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    tex.gpuValid = true;
}

// ============================================================
// SECTION 10: immediate mode conversion
// ============================================================

static void ExpandImmediate(GLenum mode, const std::vector<GLVertex>& src, std::vector<GLVertex>& out)
{
    out.clear();

    switch (mode)
    {
    case GL_TRIANGLES:
    case GL_LINES:
    case GL_LINE_STRIP:
        out = src;
        return;

    case GL_TRIANGLE_STRIP:
        for (size_t i = 2; i < src.size(); ++i)
        {
            if ((i & 1) == 0)
            {
                out.push_back(src[i - 2]);
                out.push_back(src[i - 1]);
                out.push_back(src[i]);
            }
            else
            {
                out.push_back(src[i - 1]);
                out.push_back(src[i - 2]);
                out.push_back(src[i]);
            }
        }
        return;

    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (size_t i = 2; i < src.size(); ++i)
        {
            out.push_back(src[0]);
            out.push_back(src[i - 1]);
            out.push_back(src[i]);
        }
        return;

    case GL_QUADS:
        for (size_t i = 0; i + 3 < src.size(); i += 4)
        {
            out.push_back(src[i + 0]);
            out.push_back(src[i + 1]);
            out.push_back(src[i + 2]);
            out.push_back(src[i + 0]);
            out.push_back(src[i + 2]);
            out.push_back(src[i + 3]);
        }
        return;

    case GL_LINE_LOOP:
        if (src.size() >= 2)
        {
            out = src;
            out.push_back(src[0]);
        }
        return;

    default:
        out = src;
        return;
    }
}

static PipelineMode PickPipeline(bool useTex0, bool useTex1)
{
    const bool textured = useTex0 || useTex1;

    if (g_gl.blend)
        return textured ? PIPE_BLEND_TEX : PIPE_BLEND_UNTEX;

    if (g_gl.alphaTest)
        return textured ? PIPE_ALPHA_TEST_TEX : PIPE_OPAQUE_UNTEX;

    return textured ? PIPE_OPAQUE_TEX : PIPE_OPAQUE_UNTEX;
}

static void SetDynamicFixedFunctionState(ID3D12GraphicsCommandList* cl)
{
    cl->SetGraphicsRootSignature(g_gl.rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = { g_gl.srvHeap.Get() };
    cl->SetDescriptorHeaps(1, heaps);
    cl->RSSetViewports(1, &g_gl.viewport);
    cl->RSSetScissorRects(1, &g_gl.scissor);
}

static Mat4 CurrentMVP()
{
    return Mat4::Multiply(g_gl.projStack.back(), g_gl.modelStack.back());
}

void glLoadModelMatrixf(const float* m16)
{
    Mat4 m = Mat4::Identity();

    if (m16)
    {
        memcpy(m.m, m16, sizeof(float) * 16);
    }

    memcpy(g_gl.modelMatrix.m, m.m, sizeof(g_gl.modelMatrix.m));
}

extern "C" void APIENTRY glMultMatrixf(const GLfloat* m)
{
    if (!m)
        return;

    Mat4 rhs{};
    memcpy(rhs.m, m, sizeof(rhs.m));

    auto& top = QD3D12_CurrentMatrixStack().back();
    top = Mat4::Multiply(top, rhs);
}

static Mat4 CurrentModelMatrix()
{
    return g_gl.modelMatrix;
}

static void QueueExpandedVertices(GLenum originalMode, const std::vector<GLVertex>& verts)
{
    if (verts.empty())
        return;

    const bool useTex0 = g_gl.texture2D[0];
    const bool useTex1 = g_gl.texture2D[1];

    TextureResource* tex0 = &g_gl.whiteTexture;
    TextureResource* tex1 = &g_gl.whiteTexture;

    if (useTex0)
    {
        auto it0 = g_gl.textures.find(g_gl.boundTexture[0]);
        if (it0 != g_gl.textures.end())
            tex0 = &it0->second;
    }

    if (useTex1)
    {
        auto it1 = g_gl.textures.find(g_gl.boundTexture[1]);
        if (it1 != g_gl.textures.end())
            tex1 = &it1->second;
    }

    // Make sure any referenced textures exist on GPU before end-of-frame draw execution.
    if (!tex0->gpuValid && tex0 != &g_gl.whiteTexture)
    {
        EnsureTextureResource(*tex0);
        UploadTexture(*tex0);
    }

    if (!tex1->gpuValid && tex1 != &g_gl.whiteTexture)
    {
        EnsureTextureResource(*tex1);
        UploadTexture(*tex1);
    }

    BatchKey key = BuildCurrentBatchKey(originalMode, tex0, tex1);

    const size_t markerCursor = g_gl.queryMarkers.size();

    if (!g_gl.queuedBatches.empty() && BatchKeyEquals(g_gl.queuedBatches.back().key, key) && g_gl.queuedBatches.back().markerEnd == markerCursor)
    {
        auto& dst = g_gl.queuedBatches.back().verts;
        dst.insert(dst.end(), verts.begin(), verts.end());
    }
    else
    {
        QueuedBatch batch{};
        batch.key = key;
        batch.verts = verts;
        batch.markerBegin = markerCursor;
        batch.markerEnd = markerCursor;
        g_gl.queuedBatches.push_back(std::move(batch));
    }
}

static void FlushImmediate(GLenum mode, const std::vector<GLVertex>& src)
{
    std::vector<GLVertex> expanded;
    ExpandImmediate(mode, src, expanded);
    QueueExpandedVertices(mode, expanded);
}

static void QD3D12_EmitQueryMarkers(size_t beginIdx, size_t endIdx)
{
    for (size_t i = beginIdx; i < endIdx; ++i)
    {
        const QueryMarker& m = g_gl.queryMarkers[i];
        auto               it = g_gl.queries.find(m.id);
        if (it == g_gl.queries.end())
            continue;

        GLOcclusionQuery& q = it->second;
        if (q.heapIndex == UINT_MAX)
            continue;

        if (m.type == QueryMarker::Begin)
        {
            g_gl.cmdList->BeginQuery(g_gl.occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_OCCLUSION, q.heapIndex);
        }
        else
        {
            g_gl.cmdList->EndQuery(g_gl.occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_OCCLUSION, q.heapIndex);

            g_gl.cmdList->ResolveQueryData(
                g_gl.occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_OCCLUSION, q.heapIndex, 1, g_gl.occlusionReadback.Get(), sizeof(UINT64) * q.heapIndex);

            q.submittedFence = QD3D12_CurrentSubmissionFenceValue();
        }
    }
}

static void QD3D12_FlushQueuedBatches()
{
    if (g_gl.queuedBatches.empty())
        return;

    SetDynamicFixedFunctionState(g_gl.cmdList.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    g_gl.cmdList->OMSetRenderTargets(3, rtvs, FALSE, &dsv);

    ID3D12PipelineState* lastPSO = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE lastTex0{};
    D3D12_GPU_DESCRIPTOR_HANDLE lastTex1{};
    bool haveLastTex0 = false;
    bool haveLastTex1 = false;
    D3D12_PRIMITIVE_TOPOLOGY lastTopo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    for (size_t i = 0; i < g_gl.queuedBatches.size(); ++i)
    {
        const QueuedBatch& batch = g_gl.queuedBatches[i];
        if (batch.verts.empty())
            continue;

        QD3D12_EmitQueryMarkers(batch.markerBegin, batch.markerEnd);

        g_gl.cmdList->RSSetViewports(1, &batch.key.viewport);
        g_gl.cmdList->RSSetScissorRects(1, &batch.key.scissor);

        UploadAlloc vbAlloc = QD3D12_AllocUpload((UINT)(batch.verts.size() * sizeof(GLVertex)), 256);
        memcpy(vbAlloc.cpu, batch.verts.data(), batch.verts.size() * sizeof(GLVertex));

        UploadAlloc cbAlloc = QD3D12_AllocUpload(sizeof(DrawConstants), 256);
        DrawConstants* dc = reinterpret_cast<DrawConstants*>(cbAlloc.cpu);
        memset(dc, 0, sizeof(*dc));

        dc->mvp = batch.key.mvp;
        dc->geometryFlag = batch.key.geometryFlag;
        dc->modelMatrix = batch.key.modelMatrix;
        dc->alphaRef = batch.key.alphaRef;
        dc->useTex0 = batch.key.useTex0;
        dc->useTex1 = batch.key.useTex1;
        dc->tex1IsLightmap = batch.key.tex1IsLightmap;
        dc->texEnvMode0 = batch.key.texEnvMode0;
        dc->texEnvMode1 = batch.key.texEnvMode1;

        dc->fogEnabled = g_gl.fog ? 1.0f : 0.0f;

        switch (g_gl.fogMode)
        {
        case GL_LINEAR: dc->fogMode = 0.0f; break;
        case GL_EXP:    dc->fogMode = 1.0f; break;
        case GL_EXP2:   dc->fogMode = 2.0f; break;
        default:        dc->fogMode = 1.0f; break;
        }

        dc->fogDensity = g_gl.fogDensity;
        dc->fogStart = g_gl.fogStart;
        dc->fogEnd = g_gl.fogEnd;
        dc->fogColor[0] = g_gl.fogColor[0];
        dc->fogColor[1] = g_gl.fogColor[1];
        dc->fogColor[2] = g_gl.fogColor[2];
        dc->fogColor[3] = g_gl.fogColor[3];

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = vbAlloc.gpu;
        vbv.SizeInBytes = (UINT)(batch.verts.size() * sizeof(GLVertex));
        vbv.StrideInBytes = sizeof(GLVertex);

        ID3D12PipelineState* pso = nullptr;
        const D3D12_PRIMITIVE_TOPOLOGY_TYPE topoType =
            GetTopologyTypeFromTopology(batch.key.topology);

        switch (batch.key.pipeline)
        {
        case PIPE_BLEND_TEX:
        case PIPE_BLEND_UNTEX:
            pso = QD3D12_GetPSO(
                batch.key.pipeline,
                batch.key.blendSrc,
                batch.key.blendDst,
                batch.key.depthTest,
                batch.key.depthWrite,
                batch.key.depthFunc,
                topoType);
            break;

        case PIPE_ALPHA_TEST_TEX:
        case PIPE_OPAQUE_UNTEX:
        case PIPE_OPAQUE_TEX:
        default:
            pso = QD3D12_GetPSO(
                batch.key.pipeline,
                batch.key.blendSrc,
                batch.key.blendDst,
                batch.key.depthTest,
                batch.key.depthWrite,
                batch.key.depthFunc,
                topoType);
            break;
        }

        if (pso != lastPSO)
        {
            g_gl.cmdList->SetPipelineState(pso);
            lastPSO = pso;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE tex0Gpu = QD3D12_SrvGpu(batch.key.tex0SrvIndex);
        D3D12_GPU_DESCRIPTOR_HANDLE tex1Gpu = QD3D12_SrvGpu(batch.key.tex1SrvIndex);

        if (!haveLastTex0 || tex0Gpu.ptr != lastTex0.ptr)
        {
            g_gl.cmdList->SetGraphicsRootDescriptorTable(1, tex0Gpu);
            lastTex0 = tex0Gpu;
            haveLastTex0 = true;
        }

        if (!haveLastTex1 || tex1Gpu.ptr != lastTex1.ptr)
        {
            g_gl.cmdList->SetGraphicsRootDescriptorTable(2, tex1Gpu);
            lastTex1 = tex1Gpu;
            haveLastTex1 = true;
        }

        g_gl.cmdList->SetGraphicsRootConstantBufferView(0, cbAlloc.gpu);

        if (batch.key.topology != lastTopo)
        {
            g_gl.cmdList->IASetPrimitiveTopology(batch.key.topology);
            lastTopo = batch.key.topology;
        }

        g_gl.cmdList->IASetVertexBuffers(0, 1, &vbv);
        g_gl.cmdList->DrawInstanced((UINT)batch.verts.size(), 1, 0, 0);
    }

    g_gl.queuedBatches.clear();
}

// ============================================================
// SECTION 11: GL exports
// ============================================================

extern "C" const GLubyte* APIENTRY glGetString(GLenum name)
{
    switch (name)
    {
    case GL_VENDOR: return (const GLubyte*)vendor;
    case GL_RENDERER: return (const GLubyte*)renderer;
    case GL_VERSION: return (const GLubyte*)version;
    case GL_EXTENSIONS: return (const GLubyte*)extensions;
    default: return (const GLubyte*)"";
    }
}

extern "C" void APIENTRY glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    g_gl.clearColor[0] = r;
    g_gl.clearColor[1] = g;
    g_gl.clearColor[2] = b;
    g_gl.clearColor[3] = a;
}

static D3D12_RECT QD3D12_GetActiveClearRect()
{
    if (g_gl.scissorTest)
    {
        D3D12_RECT r{};
        r.left = ClampValue<LONG>(g_gl.scissorX, 0, (LONG)g_gl.width);
        r.right = ClampValue<LONG>(g_gl.scissorX + g_gl.scissorW, 0, (LONG)g_gl.width);

        // OpenGL scissor is bottom-left origin, D3D12 is top-left origin.
        const LONG topGL = g_gl.scissorY + g_gl.scissorH;
        const LONG bottomGL = g_gl.scissorY;

        r.top = ClampValue<LONG>((LONG)g_gl.height - topGL, 0, (LONG)g_gl.height);
        r.bottom = ClampValue<LONG>((LONG)g_gl.height - bottomGL, 0, (LONG)g_gl.height);

        if (r.right < r.left)   std::swap(r.right, r.left);
        if (r.bottom < r.top)   std::swap(r.bottom, r.top);
        return r;
    }

    D3D12_RECT r{};
    r.left = 0;
    r.top = 0;
    r.right = (LONG)g_gl.width;
    r.bottom = (LONG)g_gl.height;
    return r;
}

static void QD3D12_EnsureFrameOpen()
{
    if (!g_gl.frameOpen)
    {
        QD3D12_BeginFrame();
        g_gl.frameOpen = true;
    }
}

extern "C" void APIENTRY glClear(GLbitfield mask)
{
    if (mask == 0)
        return;

    QD3D12_FlushQueuedBatches();

    if (g_gl.backBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.backBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.backBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.backBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (g_gl.normalBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.normalBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.normalBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.normalBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart();

    g_gl.cmdList->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    g_gl.cmdList->RSSetViewports(1, &g_gl.viewport);

    const D3D12_RECT clearRect = QD3D12_GetActiveClearRect();
    g_gl.cmdList->RSSetScissorRects(1, &clearRect);

    if (mask & GL_COLOR_BUFFER_BIT)
    {
        const float cc[4] =
        {
            g_gl.clearColor[0],
            g_gl.clearColor[1],
            g_gl.clearColor[2],
            g_gl.clearColor[3]
        };

        g_gl.cmdList->ClearRenderTargetView(
            CurrentRTV(),
            cc,
            1,
            &clearRect);

        const float normalClear[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
        g_gl.cmdList->ClearRenderTargetView(
            CurrentNormalRTV(),
            normalClear,
            1,
            &clearRect);
    }

    if (mask & GL_DEPTH_BUFFER_BIT)
    {
        g_gl.cmdList->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_DEPTH,
            (FLOAT)ClampValue<GLclampd>(g_gl.clearDepthValue, 0.0, 1.0),
            0,
            1,
            &clearRect);
    }

    if (mask & GL_STENCIL_BUFFER_BIT)
    {
        g_gl.cmdList->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_STENCIL,
            (FLOAT)ClampValue<GLclampd>(g_gl.clearDepthValue, 0.0, 1.0),
            (UINT8)(g_gl.clearStencilValue & 0xFF),
            1,
            &clearRect);
    }
}

extern "C" void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    g_gl.viewportX = x;
    g_gl.viewportY = y;
    g_gl.viewportW = width;
    g_gl.viewportH = height;
    QD3D12_UpdateViewportState();
}

extern "C" void APIENTRY glEnable(GLenum cap)
{
    switch (cap)
    {
    case GL_BLEND: g_gl.blend = true; break;
    case GL_ALPHA_TEST: g_gl.alphaTest = true; break;
    case GL_DEPTH_TEST: g_gl.depthTest = true; break;
    case GL_CULL_FACE: g_gl.cullFace = true; break;
    case GL_TEXTURE_2D: g_gl.texture2D[g_gl.activeTextureUnit] = true; break;
    case GL_FOG: g_gl.fog = true; break;
    default: break;
    }
}

extern "C" void APIENTRY glDisable(GLenum cap)
{
    switch (cap)
    {
    case GL_BLEND: g_gl.blend = false; break;
    case GL_ALPHA_TEST: g_gl.alphaTest = false; break;
    case GL_DEPTH_TEST: g_gl.depthTest = false; break;
    case GL_CULL_FACE: g_gl.cullFace = false; break;
    case GL_TEXTURE_2D: g_gl.texture2D[g_gl.activeTextureUnit] = false; break;
    case GL_FOG: g_gl.fog = false; break;
    default: break;
    }
}

extern "C" void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    g_gl.blendSrc = sfactor;
    g_gl.blendDst = dfactor;
}

extern "C" void APIENTRY glAlphaFunc(GLenum func, GLclampf ref)
{
    g_gl.alphaFunc = func;
    g_gl.alphaRef = ref;
}

extern "C" void APIENTRY glDepthMask(GLboolean flag)
{
    g_gl.depthWrite = (flag != 0);
}

extern "C" void APIENTRY glDepthRange(GLclampd zNear, GLclampd zFar)
{
    g_gl.depthRangeNear = ClampValue<GLclampd>(zNear, 0.0, 1.0);
    g_gl.depthRangeFar = ClampValue<GLclampd>(zFar, 0.0, 1.0);
    QD3D12_UpdateViewportState();
}

extern "C" void APIENTRY glCullFace(GLenum mode)
{
    g_gl.cullMode = mode;
}

extern "C" void APIENTRY glPolygonMode(GLenum, GLenum)
{
}

extern "C" void APIENTRY glShadeModel(GLenum mode)
{
    g_gl.shadeModel = mode;
}

extern "C" void APIENTRY glHint(GLenum target, GLenum mode)
{
    if (target == GL_FOG_HINT)
        g_gl.fogHint = mode;
}

extern "C" void APIENTRY glFinish(void)
{
    if (!g_gl.device || !g_gl.queue || !g_gl.cmdList)
        return;

    //
    // Flush any queued GL batches into the current command list first.
    //
    QD3D12_FlushQueuedBatches();

    //
    // Close and submit the current command list, but do NOT present.
    // glFinish should block until all submitted GPU work is complete,
    // then reopen the same frame command list so rendering can continue.
    //
    QD3D12_CHECK(g_gl.cmdList->Close());

    ID3D12CommandList* lists[] = { g_gl.cmdList.Get() };
    g_gl.queue->ExecuteCommandLists(1, lists);

    //
    // Wait for this submission to fully complete before resetting the allocator.
    // This avoids:
    // "The command allocator was reset after the command list was recorded."
    //
    const UINT64 signalValue = g_gl.nextFenceValue++;
    QD3D12_CHECK(g_gl.queue->Signal(g_gl.fence.Get(), signalValue));

    if (g_gl.fence->GetCompletedValue() < signalValue)
    {
        QD3D12_CHECK(g_gl.fence->SetEventOnCompletion(signalValue, g_gl.fenceEvent));
        WaitForSingleObject(g_gl.fenceEvent, INFINITE);
    }

    //
    // Reopen the same allocator/list now that the GPU is done with it.
    //
    FrameResources& fr = g_gl.frames[g_gl.frameIndex];
    fr.fenceValue = signalValue;

    QD3D12_CHECK(fr.cmdAlloc->Reset());
    QD3D12_CHECK(g_gl.cmdList->Reset(fr.cmdAlloc.Get(), nullptr));

    //
    // Rebind render targets and raster state because Reset() clears command-list state.
    //
    if (g_gl.backBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.backBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.backBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.backBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (g_gl.normalBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.normalBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.normalBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.normalBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (g_gl.positionBufferState[g_gl.frameIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_gl.positionBuffers[g_gl.frameIndex].Get();
        b.Transition.StateBefore = g_gl.positionBufferState[g_gl.frameIndex];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_gl.cmdList->ResourceBarrier(1, &b);
        g_gl.positionBufferState[g_gl.frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    g_gl.cmdList->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    g_gl.cmdList->RSSetViewports(1, &g_gl.viewport);
    g_gl.cmdList->RSSetScissorRects(1, &g_gl.scissor);
}

extern "C" void APIENTRY glMatrixMode(GLenum mode)
{
    g_gl.matrixMode = mode;
}

extern "C" void APIENTRY glLoadIdentity(void)
{
    QD3D12_CurrentMatrixStack().back() = Mat4::Identity();
}

extern "C" void APIENTRY glPushMatrix(void)
{
    auto& s = QD3D12_CurrentMatrixStack();
    s.push_back(s.back());
}

extern "C" void APIENTRY glPopMatrix(void)
{
    auto& s = QD3D12_CurrentMatrixStack();
    if (s.size() > 1)
        s.pop_back();
}

extern "C" void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    auto& t = QD3D12_CurrentMatrixStack().back();
    t = Mat4::Multiply(t, Mat4::Translation(x, y, z));
}

extern "C" void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    auto& t = QD3D12_CurrentMatrixStack().back();
    t = Mat4::Multiply(t, Mat4::RotationAxisDeg(angle, x, y, z));
}

extern "C" void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    auto& t = QD3D12_CurrentMatrixStack().back();
    t = Mat4::Multiply(t, Mat4::Scale(x, y, z));
}

extern "C" void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    auto& t = QD3D12_CurrentMatrixStack().back();
    t = Mat4::Multiply(t, Mat4::Ortho(left, right, bottom, top, zNear, zFar));
}

extern "C" void APIENTRY glBegin(GLenum mode)
{
    assert(!g_gl.inBeginEnd);
    g_gl.inBeginEnd = true;
    g_gl.currentPrim = mode;
    g_gl.immediateVerts.clear();
}

extern "C" void APIENTRY glEnd(void)
{
    assert(g_gl.inBeginEnd);
    g_gl.inBeginEnd = false;
    FlushImmediate(g_gl.currentPrim, g_gl.immediateVerts);
    g_gl.immediateVerts.clear();
}

extern "C" void APIENTRY glVertex2f(GLfloat x, GLfloat y)
{
    glVertex3f(x, y, 0.0f);
}

extern "C" void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    GLVertex v{};
    v.px = x;
    v.py = y;
    v.pz = z;

    v.u0 = g_gl.curU[0];
    v.v0 = g_gl.curV[0];
    v.u1 = g_gl.curU[1];
    v.v1 = g_gl.curV[1];

    v.r = g_gl.curColor[0];
    v.g = g_gl.curColor[1];
    v.b = g_gl.curColor[2];
    v.a = g_gl.curColor[3];
    g_gl.immediateVerts.push_back(v);
}

extern "C" void APIENTRY glVertex3fv(const GLfloat* v)
{
    glVertex3f(v[0], v[1], v[2]);
}

extern "C" void APIENTRY glTexCoord2f(GLfloat s, GLfloat t)
{
    g_gl.curU[g_gl.activeTextureUnit] = s;
    g_gl.curV[g_gl.activeTextureUnit] = t;
}

extern "C" void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    g_gl.curColor[0] = r;
    g_gl.curColor[1] = g;
    g_gl.curColor[2] = b;
    g_gl.curColor[3] = 1.0f;
}

extern "C" void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    g_gl.curColor[0] = r;
    g_gl.curColor[1] = g;
    g_gl.curColor[2] = b;
    g_gl.curColor[3] = a;
}

extern "C" void APIENTRY glGenTextures(GLsizei n, GLuint* textures)
{
    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint id = g_gl.nextTextureId++;
        TextureResource tex{};
        tex.glId = id;
        tex.srvIndex = UINT_MAX;
        tex.minFilter = g_gl.defaultMinFilter;
        tex.magFilter = g_gl.defaultMagFilter;
        tex.wrapS = g_gl.defaultWrapS;
        tex.wrapT = g_gl.defaultWrapT;

        g_gl.textures[id] = tex;
        textures[i] = id;
    }
}

extern "C" void APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures)
{
    for (GLsizei i = 0; i < n; ++i)
    {
        const GLuint deadId = textures[i];

        for (UINT unit = 0; unit < QD3D12_MaxTextureUnits; ++unit)
        {
            if (g_gl.boundTexture[unit] == deadId)
                g_gl.boundTexture[unit] = 0;
        }

        auto it = g_gl.textures.find(deadId);
        if (it != g_gl.textures.end())
        {
            QD3D12_RetireResource(it->second.texture);
            it->second.gpuValid = false;
            g_gl.textures.erase(it);
        }
    }
}

extern "C" void APIENTRY glBindTexture(GLenum, GLuint texture)
{
    g_gl.boundTexture[g_gl.activeTextureUnit] = texture;

    if (texture == 0)
        return;

    auto& textures = g_gl.textures;
    auto it = textures.find(texture);
    if (it == textures.end())
    {
        TextureResource tex{};
        tex.glId = texture;
        tex.srvIndex = UINT_MAX;
        tex.minFilter = g_gl.defaultMinFilter;
        tex.magFilter = g_gl.defaultMagFilter;
        tex.wrapS = g_gl.defaultWrapS;
        tex.wrapT = g_gl.defaultWrapT;
        textures.emplace(texture, tex);
    }
}

extern "C" void APIENTRY glLoadMatrixf(const GLfloat* m)
{
    if (!m)
        return;

    auto& top = QD3D12_CurrentMatrixStack().back();
    memcpy(top.m, m, sizeof(top.m));
}

extern "C" void APIENTRY glGetIntegerv(GLenum pname, GLint* params)
{
    if (!params)
        return;

    switch (pname)
    {
    case GL_MAX_TEXTURES_SGIS:
    case GL_MAX_ACTIVE_TEXTURES_ARB:
        *params = (GLint)QD3D12_MaxTextureUnits;
        break;

    case GL_VIEWPORT:
        params[0] = g_gl.viewportX;
        params[1] = g_gl.viewportY;
        params[2] = (GLint)g_gl.viewportW;
        params[3] = (GLint)g_gl.viewportH;
        break;

    case GL_FOG_MODE:
        *params = (GLint)g_gl.fogMode;
        break;
    case GL_FOG_HINT:
        *params = (GLint)g_gl.fogHint;
        break;

    case GL_SELECTED_TEXTURE_SGIS:
        *params = (GLint)(GL_TEXTURE0_SGIS + g_gl.activeTextureUnit);
        break;

    case GL_ACTIVE_TEXTURE_ARB:
        *params = (GLint)(GL_TEXTURE0_ARB + g_gl.activeTextureUnit);
        break;

    case GL_CLIENT_ACTIVE_TEXTURE_ARB:
        *params = (GLint)(GL_TEXTURE0_ARB + g_gl.clientActiveTextureUnit);
        break;

    case GL_MAX_TEXTURE_SIZE:
        *params = 4096;
        break;

    default:
        *params = 0;
        break;
    }
}

extern "C" void APIENTRY glGetFloatv(GLenum pname, GLfloat* params)
{
    if (!params)
        return;

    switch (pname)
    {
    case GL_FOG_DENSITY:
        params[0] = g_gl.fogDensity;
        break;
    case GL_FOG_START:
        params[0] = g_gl.fogStart;
        break;
    case GL_FOG_END:
        params[0] = g_gl.fogEnd;
        break;
    case GL_FOG_COLOR:
        params[0] = g_gl.fogColor[0];
        params[1] = g_gl.fogColor[1];
        params[2] = g_gl.fogColor[2];
        params[3] = g_gl.fogColor[3];
        break;
    case GL_MODELVIEW_MATRIX:
        memcpy(params, g_gl.modelStack.back().m, sizeof(GLfloat) * 16);
        break;
    case GL_PROJECTION_MATRIX:
        memcpy(params, g_gl.projStack.back().m, sizeof(GLfloat) * 16);
        break;
    default:
        memset(params, 0, sizeof(GLfloat) * 16);
        break;
    }
}

extern "C" void APIENTRY glGetDoublev(GLenum pname, GLdouble* params)
{
    if (!params)
        return;

    switch (pname)
    {
    case GL_MODELVIEW_MATRIX:
        for (int i = 0; i < 16; ++i)
            params[i] = (GLdouble)g_gl.modelStack.back().m[i];
        break;

    case GL_PROJECTION_MATRIX:
        for (int i = 0; i < 16; ++i)
            params[i] = (GLdouble)g_gl.projStack.back().m[i];
        break;

    default:
        for (int i = 0; i < 16; ++i)
            params[i] = 0.0;
        break;
    }
}

extern "C" void APIENTRY glFrustum(GLdouble left, GLdouble right,
    GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    auto Quantize = [](float v, float steps) -> float
        {
            return floorf(v * steps + 0.5f) / steps;
        };

    const float l = (float)left;
    const float r = (float)right;
    const float b = (float)bottom;
    const float t = (float)top;
    const float n = (float)zNear;
    const float fz = (float)zFar;

    const float invW = 1.0f / (r - l);
    const float invH = 1.0f / (t - b);
    const float invD = 1.0f / (fz - n);

    float xScale = (2.0f * n) * invW;
    float yScale = (2.0f * n) * invH;
    float xCenter = (r + l) * invW;
    float yCenter = (t + b) * invH;
    float zScale = -(fz + n) * invD;
    float zTrans = -(2.0f * fz * n) * invD;

    //
    // Intentionally degrade precision a bit to feel less "perfect OpenGL".
    // Tune these to taste.
    //
    // Lower numbers = chunkier / more old-school distortion feel.
    //
    const float scaleQuant = 256.0f;
    const float centerQuant = 128.0f;
    const float depthQuant = 1024.0f;

    xScale = Quantize(xScale, scaleQuant);
    yScale = Quantize(yScale, scaleQuant);
    xCenter = Quantize(xCenter, centerQuant);
    yCenter = Quantize(yCenter, centerQuant);

    // Optional: make depth a little less precise / less "modern perfect".
    zScale = Quantize(zScale, depthQuant);
    zTrans = Quantize(zTrans, depthQuant);

    Mat4 proj{};
    proj.m[0] = xScale;
    proj.m[5] = yScale;
    proj.m[8] = xCenter;
    proj.m[9] = yCenter;
    proj.m[10] = zScale;
    proj.m[11] = -1.0f;
    proj.m[14] = zTrans;

    auto& topMat = QD3D12_CurrentMatrixStack().back();
    topMat = Mat4::Multiply(topMat, proj);
}

extern "C" void APIENTRY glDepthFunc(GLenum func)
{
    g_gl.depthFunc = func;
}

extern "C" void APIENTRY glColor4fv(const GLfloat* v)
{
    if (!v)
        return;

    g_gl.curColor[0] = v[0];
    g_gl.curColor[1] = v[1];
    g_gl.curColor[2] = v[2];
    g_gl.curColor[3] = v[3];
}

extern "C" void APIENTRY glTexParameterf(GLenum, GLenum pname, GLfloat param)
{
    GLenum value = (GLenum)param;
    GLuint bound = g_gl.boundTexture[g_gl.activeTextureUnit];

    if (bound == 0 || g_gl.textures.empty())
    {
        switch (pname)
        {
        case GL_TEXTURE_MIN_FILTER: g_gl.defaultMinFilter = value; break;
        case GL_TEXTURE_MAG_FILTER: g_gl.defaultMagFilter = value; break;
        case GL_TEXTURE_WRAP_S:     g_gl.defaultWrapS = value; break;
        case GL_TEXTURE_WRAP_T:     g_gl.defaultWrapT = value; break;
        default: break;
        }
        return;
    }

    auto it = g_gl.textures.find(bound);
    if (it == g_gl.textures.end())
        return;

    TextureResource& tex = it->second;
    switch (pname)
    {
    case GL_TEXTURE_MIN_FILTER: tex.minFilter = value; break;
    case GL_TEXTURE_MAG_FILTER: tex.magFilter = value; break;
    case GL_TEXTURE_WRAP_S:     tex.wrapS = value; break;
    case GL_TEXTURE_WRAP_T:     tex.wrapT = value; break;
    default: break;
    }
}

extern "C" void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
#if 0
    if (target != GL_TEXTURE_ENV)
        return;

    if (pname != GL_TEXTURE_ENV_MODE)
        return;

    GLenum mode = (GLenum)param;
    switch (mode)
    {
    case GL_MODULATE:
    case GL_REPLACE:
    case GL_BLEND:
#ifdef GL_ADD
    case GL_ADD:
#endif
        g_gl.texEnvMode[g_gl.activeTextureUnit] = mode;
        break;

    default:
        // Quake mostly uses MODULATE; fall back safely.
        g_gl.texEnvMode[g_gl.activeTextureUnit] = GL_MODULATE;
        break;
    }
#endif
}

extern "C" void APIENTRY glTexImage2D(GLenum, GLint, GLint internalFormat,
    GLsizei width, GLsizei height, GLint, GLenum format, GLenum type, const GLvoid* pixels)
{
    auto it = g_gl.textures.find(g_gl.boundTexture[g_gl.activeTextureUnit]);
    if (it == g_gl.textures.end())
        return;

    TextureResource& tex = it->second;

    tex.width = width;
    tex.height = height;
    tex.format = (format != 0) ? format : (GLenum)internalFormat;

    const int bpp = BytesPerPixel(tex.format, type);
    tex.sysmem.resize((size_t)width * (size_t)height * (size_t)bpp);
    if (pixels)
        memcpy(tex.sysmem.data(), pixels, tex.sysmem.size());
    else
        memset(tex.sysmem.data(), 0, tex.sysmem.size());

    EnsureTextureResource(tex);
    tex.gpuValid = false;
}

extern "C" void APIENTRY glTexSubImage2D(GLenum, GLint, GLint xoffset, GLint yoffset,
    GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels)
{
    auto it = g_gl.textures.find(g_gl.boundTexture[g_gl.activeTextureUnit]);
    if (it == g_gl.textures.end() || !pixels)
        return;

    TextureResource& tex = it->second;
    if (tex.width <= 0 || tex.height <= 0)
        return;

    const int bpp = BytesPerPixel(format, type);
    if (tex.sysmem.empty())
        tex.sysmem.resize((size_t)tex.width * (size_t)tex.height * (size_t)bpp);

    const uint8_t* src = (const uint8_t*)pixels;
    for (int row = 0; row < height; ++row)
    {
        size_t dstOff = ((size_t)(yoffset + row) * (size_t)tex.width + (size_t)xoffset) * (size_t)bpp;
        size_t srcOff = (size_t)row * (size_t)width * (size_t)bpp;
        memcpy(tex.sysmem.data() + dstOff, src + srcOff, (size_t)width * (size_t)bpp);
    }

    tex.gpuValid = false;
}

extern "C" void APIENTRY glReadPixels(GLint, GLint, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* data)
{
    if (!data)
        return;
    memset(data, 0, (size_t)width * (size_t)height * (size_t)BytesPerPixel(format, type));
}

extern "C" void APIENTRY glDrawBuffer(GLenum mode)
{
    g_gl.drawBuffer = mode;
}

extern "C" void APIENTRY glReadBuffer(GLenum mode)
{
    g_gl.readBuffer = mode;
}

// ============================================================
// SECTION 12: optional convenience for Quake code
// ============================================================

void QD3D12_DrawArrays(GLenum mode, const GLVertex* verts, size_t count)
{
    std::vector<GLVertex> tmp(verts, verts + count);
    FlushImmediate(mode, tmp);
}

void QD3D12_Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    QD3D12_WaitForGPU();

    g_gl.queuedBatches.clear();

    for (UINT i = 0; i < QD3D12_FrameCount; ++i)
    {
        g_gl.backBuffers[i].Reset();
        g_gl.normalBuffers[i].Reset();
    }

    g_gl.depthBuffer.Reset();

    QD3D12_CHECK(g_gl.swapChain->ResizeBuffers(QD3D12_FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));

    g_gl.width = width;
    g_gl.height = height;
    g_gl.viewportW = width;
    g_gl.viewportH = height;
    g_gl.frameIndex = g_gl.swapChain->GetCurrentBackBufferIndex();

    QD3D12_CreateRTVs();
    QD3D12_CreateDSV();
    g_psoCache.clear();
    QD3D12_UpdateViewportState();
}

extern "C" GLenum APIENTRY glGetError(void) {
    GLenum e = g_gl.lastError;
    g_gl.lastError = GL_NO_ERROR;
    return e;
}

extern "C" void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    g_gl.scissorX = x;
    g_gl.scissorY = y;
    g_gl.scissorW = width;
    g_gl.scissorH = height;
}

extern "C" void APIENTRY glClearDepth(GLclampd depth) {
    g_gl.clearDepthValue = depth;
}

extern "C" void APIENTRY glClipPlane(GLenum plane, const GLdouble* equation) {
    if (plane == GL_CLIP_PLANE0 && equation) {
        memcpy(g_gl.clipPlane0, equation, sizeof(g_gl.clipPlane0));
    }
}

extern "C" void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    g_gl.polygonOffsetFactor = factor;
    g_gl.polygonOffsetUnits = units;
}

extern "C" void APIENTRY glTexCoord2fv(const GLfloat* v) {
    if (!v) return;
    glTexCoord2f(v[0], v[1]);
}

extern "C" void APIENTRY glColor4ubv(const GLubyte* v) {
    if (!v) return;
    g_gl.curColor[0] = v[0] / 255.0f;
    g_gl.curColor[1] = v[1] / 255.0f;
    g_gl.curColor[2] = v[2] / 255.0f;
    g_gl.curColor[3] = v[3] / 255.0f;
}

extern "C" void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    if (!params) return;
    glTexParameterf(target, pname, params[0]);
}

extern "C" void APIENTRY glStencilMask(GLuint mask) {
    g_gl.stencilMask = mask;
}

extern "C" void APIENTRY glClearStencil(GLint s) {
    g_gl.clearStencilValue = s;
}

extern "C" void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    g_gl.stencilFunc = func;
    g_gl.stencilRef = ref;
    g_gl.stencilFuncMask = mask;
}

extern "C" void APIENTRY glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    g_gl.stencilSFail = sfail;
    g_gl.stencilDPFail = dpfail;
    g_gl.stencilDPPass = dppass;
}

extern "C" void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    g_gl.colorMaskR = r;
    g_gl.colorMaskG = g;
    g_gl.colorMaskB = b;
    g_gl.colorMaskA = a;
}

extern "C" void APIENTRY glClientActiveTextureARB(GLenum texture) {
    if (texture >= GL_TEXTURE0_ARB)
        g_gl.clientActiveTextureUnit = ClampValue<GLuint>((GLuint)(texture - GL_TEXTURE0_ARB), 0, QD3D12_MaxTextureUnits - 1);
    else
        g_gl.clientActiveTextureUnit = 0;
}

extern "C" void APIENTRY glLockArraysEXT(GLint first, GLsizei count) {
    (void)first;
    (void)count;
}

extern "C" void APIENTRY glUnlockArraysEXT(void) {
}

extern "C" void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void* pointer)
{
    g_gl.normalArray.size = 3;
    g_gl.normalArray.type = type;
    g_gl.normalArray.stride = stride;
    g_gl.normalArray.ptr = QD3D12_ResolveArrayPointer(pointer);
}

extern "C" void APIENTRY glEnableClientState(GLenum array)
{
    switch (array)
    {
    case GL_VERTEX_ARRAY:
        g_gl.vertexArray.enabled = true;
        break;
    case GL_NORMAL_ARRAY:
        g_gl.normalArray.enabled = true;
        break;
    case GL_COLOR_ARRAY:
        g_gl.colorArray.enabled = true;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        g_gl.texCoordArray[g_gl.clientActiveTextureUnit].enabled = true;
        break;
    default:
        break;
    }
}

extern "C" void APIENTRY glDisableClientState(GLenum array)
{
    switch (array)
    {
    case GL_VERTEX_ARRAY:
        g_gl.vertexArray.enabled = false;
        break;
    case GL_NORMAL_ARRAY:
        g_gl.normalArray.enabled = false;
        break;
    case GL_COLOR_ARRAY:
        g_gl.colorArray.enabled = false;
        break;
    case GL_TEXTURE_COORD_ARRAY:
        g_gl.texCoordArray[g_gl.clientActiveTextureUnit].enabled = false;
        break;
    default:
        break;
    }
}

extern "C" void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)
{
    g_gl.vertexArray.size = size;
    g_gl.vertexArray.type = type;
    g_gl.vertexArray.stride = stride;
    g_gl.vertexArray.ptr = QD3D12_ResolveArrayPointer(ptr);
}

extern "C" void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)
{
    g_gl.colorArray.size = size;
    g_gl.colorArray.type = type;
    g_gl.colorArray.stride = stride;
    g_gl.colorArray.ptr = QD3D12_ResolveArrayPointer(ptr);
}

extern "C" void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr)
{
    auto& tc = g_gl.texCoordArray[g_gl.clientActiveTextureUnit];
    tc.size = size;
    tc.type = type;
    tc.stride = stride;
    tc.ptr = QD3D12_ResolveArrayPointer(ptr);
}

extern "C" void APIENTRY glArrayElement(GLint i) {
    GLVertex v{};
    QD3D12_FetchArrayVertex(i, v);
    g_gl.immediateVerts.push_back(v);
}

extern "C" void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
    if (count <= 0)
        return;

    const void* resolvedIndices = QD3D12_ResolveElementPointer(indices, type, count);
    if (!resolvedIndices)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    std::vector<GLVertex> verts;
    verts.reserve((size_t)count);

    if (type == GL_UNSIGNED_INT)
    {
        const GLuint* idx = (const GLuint*)resolvedIndices;
        for (GLsizei i = 0; i < count; ++i)
        {
            GLVertex v{};
            QD3D12_FetchArrayVertex((GLint)idx[i], v);
            verts.push_back(v);
        }
    }
    else if (type == GL_UNSIGNED_SHORT)
    {
        const GLushort* idx = (const GLushort*)resolvedIndices;
        for (GLsizei i = 0; i < count; ++i)
        {
            GLVertex v{};
            QD3D12_FetchArrayVertex((GLint)idx[i], v);
            verts.push_back(v);
        }
    }
    else if (type == GL_UNSIGNED_BYTE)
    {
        const GLubyte* idx = (const GLubyte*)resolvedIndices;
        for (GLsizei i = 0; i < count; ++i)
        {
            GLVertex v{};
            QD3D12_FetchArrayVertex((GLint)idx[i], v);
            verts.push_back(v);
        }
    }
    else
    {
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    FlushImmediate(mode, verts);
}

extern "C" PROC WINAPI qd3d12_wglGetProcAddress(LPCSTR name) {
    if (!name) return nullptr;

    struct ProcMap { const char* name; PROC proc; };
    static const ProcMap table[] = {
        { "glActiveTextureARB",         (PROC)glActiveTextureARB },
        { "glClientActiveTextureARB",   (PROC)glClientActiveTextureARB },
        { "glMultiTexCoord2fARB",       (PROC)glMultiTexCoord2fARB },
        { "glSelectTextureSGIS",        (PROC)glSelectTextureSGIS },
        { "glMTexCoord2fSGIS",          (PROC)glMTexCoord2fSGIS },
        { "glLockArraysEXT",            (PROC)glLockArraysEXT },
        { "glUnlockArraysEXT",          (PROC)glUnlockArraysEXT },
        { "wglSwapIntervalEXT",         (PROC)qd3d12_wglSwapIntervalEXT },
        { "wglGetDeviceGammaRamp3DFX",  (PROC)qd3d12_wglGetDeviceGammaRamp3DFX },
        { "wglSetDeviceGammaRamp3DFX",  (PROC)qd3d12_wglSetDeviceGammaRamp3DFX },
        { "glBindTextureEXT",           (PROC)glBindTextureEXT },
    };

    for (size_t i = 0; i < _countof(table); ++i) {
        if (!strcmp(name, table[i].name))
            return table[i].proc;
    }
    return nullptr;
}

extern "C" void APIENTRY glFogf(GLenum pname, GLfloat param)
{
    switch (pname)
    {
    case GL_FOG_MODE:
        g_gl.fogMode = (GLenum)param;
        break;
    case GL_FOG_DENSITY:
        g_gl.fogDensity = param;
        break;
    case GL_FOG_START:
        g_gl.fogStart = param;
        break;
    case GL_FOG_END:
        g_gl.fogEnd = param;
        break;
    default:
        g_gl.lastError = GL_INVALID_ENUM;
        break;
    }
}

extern "C" void APIENTRY glFogi(GLenum pname, GLint param)
{
    glFogf(pname, (GLfloat)param);
}

extern "C" void APIENTRY glFogfv(GLenum pname, const GLfloat* params)
{
    if (!params)
        return;

    switch (pname)
    {
    case GL_FOG_MODE:
        g_gl.fogMode = (GLenum)params[0];
        break;
    case GL_FOG_DENSITY:
        g_gl.fogDensity = params[0];
        break;
    case GL_FOG_START:
        g_gl.fogStart = params[0];
        break;
    case GL_FOG_END:
        g_gl.fogEnd = params[0];
        break;
    case GL_FOG_COLOR:
        g_gl.fogColor[0] = params[0];
        g_gl.fogColor[1] = params[1];
        g_gl.fogColor[2] = params[2];
        g_gl.fogColor[3] = params[3];
        break;
    default:
        g_gl.lastError = GL_INVALID_ENUM;
        break;
    }
}

extern "C" void APIENTRY glFogiv(GLenum pname, const GLint* params)
{
    if (!params)
        return;

    switch (pname)
    {
    case GL_FOG_MODE:
        g_gl.fogMode = (GLenum)params[0];
        break;
    case GL_FOG_DENSITY:
        g_gl.fogDensity = (GLfloat)params[0];
        break;
    case GL_FOG_START:
        g_gl.fogStart = (GLfloat)params[0];
        break;
    case GL_FOG_END:
        g_gl.fogEnd = (GLfloat)params[0];
        break;
    case GL_FOG_COLOR:
        g_gl.fogColor[0] = (GLfloat)params[0];
        g_gl.fogColor[1] = (GLfloat)params[1];
        g_gl.fogColor[2] = (GLfloat)params[2];
        g_gl.fogColor[3] = (GLfloat)params[3];
        break;
    default:
        g_gl.lastError = GL_INVALID_ENUM;
        break;
    }
}

extern "C" ID3D12Device* QD3D12_GetDevice(void)
{
    return g_gl.device.Get();
}

extern "C" ID3D12CommandQueue* QD3D12_GetQueue(void)
{
    return g_gl.queue.Get();
}

extern "C" ID3D12GraphicsCommandList* QD3D12_GetCommandList(void)
{
    return g_gl.cmdList.Get();
}

extern "C" ID3D12CommandAllocator* QD3D12_GetFrameCommandAllocator(void)
{
    return g_gl.frames[g_gl.frameIndex].cmdAlloc.Get();
}

extern "C" UINT QD3D12_GetFrameIndex(void)
{
    return g_gl.frameIndex;
}

extern "C" void QD3D12_WaitForGPU_External(void)
{
    QD3D12_WaitForGPU();
}

extern "C" ID3D12Resource* glRaytracingGetTopLevelAS(void);

static void QD3D12_TransitionResource(
    ID3D12GraphicsCommandList* cl,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES& trackedState,
    D3D12_RESOURCE_STATES newState)
{
    if (!res || trackedState == newState)
        return;

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = trackedState;
    b.Transition.StateAfter = newState;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);

    trackedState = newState;
}

extern "C" void glLightScene(void)
{
    int width = (int)g_gl.width;
    int height = (int)g_gl.height;

    if (width <= 0 || height <= 0)
        return;

    if (!g_gl.device || !g_gl.cmdList)
        return;

    TextureResource* lightingTex = QD3D12_EnsureLightingTexture(width, height);
    if (!lightingTex || !lightingTex->texture)
        return;

    glRaytracingBuildScene();

    QD3D12_FlushQueuedBatches();

    ID3D12GraphicsCommandList* cl = g_gl.cmdList.Get();
    ID3D12Resource* sceneColor = g_gl.backBuffers[g_gl.frameIndex].Get();
    ID3D12Resource* sceneNormal = g_gl.normalBuffers[g_gl.frameIndex].Get();
    ID3D12Resource* scenePosition = g_gl.positionBuffers[g_gl.frameIndex].Get();
    ID3D12Resource* sceneDepth = g_gl.depthBuffer.Get();
    ID3D12Resource* tlas = glRaytracingGetTopLevelAS();

    if (!sceneColor || !sceneNormal || !scenePosition || !sceneDepth || !tlas)
        return;

    //
    // 1) Transition G-buffer inputs for DXR reads.
    //
    QD3D12_TransitionResource(
        cl,
        sceneColor,
        g_gl.backBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    QD3D12_TransitionResource(
        cl,
        sceneNormal,
        g_gl.normalBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    QD3D12_TransitionResource(
        cl,
        scenePosition,
        g_gl.positionBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    QD3D12_TransitionResource(
        cl,
        sceneDepth,
        depthState,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    QD3D12_TransitionResource(
        cl,
        lightingTex->texture.Get(),
        g_lightingTextureState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    //
    // 2) Run DXR lighting.
    //
    glRaytracingLightingPassDesc_t pass = {};
    pass.albedoTexture = sceneColor;
    pass.albedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    pass.normalTexture = sceneNormal;
    pass.normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    pass.positionTexture = scenePosition;
    pass.positionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    pass.depthTexture = sceneDepth;
    pass.depthFormat = DXGI_FORMAT_D32_FLOAT;

    pass.outputTexture = lightingTex->texture.Get();
    pass.outputFormat = lightingTex->dxgiFormat;

    pass.topLevelAS = tlas;
    pass.width = (uint32_t)width;
    pass.height = (uint32_t)height;

    if (!glRaytracingLightingExecute(&pass))
    {
        // restore states before bailing
        QD3D12_TransitionResource(
            cl,
            sceneColor,
            g_gl.backBufferState[g_gl.frameIndex],
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        QD3D12_TransitionResource(
            cl,
            sceneNormal,
            g_gl.normalBufferState[g_gl.frameIndex],
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        QD3D12_TransitionResource(
            cl,
            scenePosition,
            g_gl.positionBufferState[g_gl.frameIndex],
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        return;
    }

    //
    // 3) Transition lighting result for raster composite and restore scene RTs.
    //
    QD3D12_TransitionResource(
        cl,
        lightingTex->texture.Get(),
        g_lightingTextureState,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    QD3D12_TransitionResource(
        cl,
        sceneColor,
        g_gl.backBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    QD3D12_TransitionResource(
        cl,
        sceneNormal,
        g_gl.normalBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    QD3D12_TransitionResource(
        cl,
        scenePosition,
        g_gl.positionBufferState[g_gl.frameIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    //
    // 4) Composite lighting back onto scene color.
    //
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_gl.dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };
    cl->OMSetRenderTargets(3, rtvs, FALSE, &dsv);

    GLVertex fsq[6];
    fsq[0] = { -1.0f, -1.0f, 0.0f, 0, 1, 0, 0, 1, 1, 1, 1 };
    fsq[1] = { -1.0f,  1.0f, 0.0f, 0, 0, 0, 0, 1, 1, 1, 1 };
    fsq[2] = { 1.0f,  1.0f, 0.0f, 1, 0, 0, 0, 1, 1, 1, 1 };
    fsq[3] = { -1.0f, -1.0f, 0.0f, 0, 1, 0, 0, 1, 1, 1, 1 };
    fsq[4] = { 1.0f,  1.0f, 0.0f, 1, 0, 0, 0, 1, 1, 1, 1 };
    fsq[5] = { 1.0f, -1.0f, 0.0f, 1, 1, 0, 0, 1, 1, 1, 1 };

    const bool oldBlend = g_gl.blend;
    const GLenum oldSrc = g_gl.blendSrc;
    const GLenum oldDst = g_gl.blendDst;
    const bool oldDepthTest = g_gl.depthTest;
    const bool oldDepthWrite = g_gl.depthWrite;
    const bool oldTex0 = g_gl.texture2D[0];
    const GLuint oldTex0Id = g_gl.boundTexture[0];

    g_gl.blend = true;
    g_gl.blendSrc = GL_ONE;
    g_gl.blendDst = GL_ONE;
    g_gl.depthTest = false;
    g_gl.depthWrite = false;
    g_gl.texture2D[0] = true;
    g_gl.boundTexture[0] = g_lightingTextureId;

    QD3D12_DrawArrays(GL_TRIANGLES, fsq, 6);
    QD3D12_FlushQueuedBatches();

    g_gl.blend = oldBlend;
    g_gl.blendSrc = oldSrc;
    g_gl.blendDst = oldDst;
    g_gl.depthTest = oldDepthTest;
    g_gl.depthWrite = oldDepthWrite;
    g_gl.texture2D[0] = oldTex0;
    g_gl.boundTexture[0] = oldTex0Id;

    //
    // 5) Restore MRT binding for anything else before EndFrame closes.
    //
    D3D12_CPU_DESCRIPTOR_HANDLE restoreRtvs[3] =
    {
        CurrentRTV(),
        CurrentNormalRTV(),
        CurrentPositionRTV()
    };
    cl->OMSetRenderTargets(3, restoreRtvs, FALSE, &dsv);
}

ID3D12Resource* QD3D12_GetCurrentBackBuffer()
{
    if (!g_gl.swapChain)
        return nullptr;

    const UINT index = g_gl.frameIndex;
    if (index >= QD3D12_FrameCount)
        return nullptr;

    return g_gl.backBuffers[index].Get();
}

extern "C" void APIENTRY glGeometryFlagf(GLfloat flag)
{
    g_gl.currentGeometryFlag = flag;
}

extern "C" void APIENTRY glGenBuffers(GLsizei n, GLuint* buffers)
{
    if (n < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    if (!buffers)
        return;

    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint         id = g_gl.nextBufferId++;
        GLBufferObject bo{};
        bo.id = id;
        g_gl.buffers.emplace(id, std::move(bo));
        buffers[i] = id;
    }
}

extern "C" void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* buffers)
{
    if (n < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    if (!buffers)
        return;

    for (GLsizei i = 0; i < n; ++i)
    {
        const GLuint id = buffers[i];
        if (id == 0)
            continue;

        if (g_gl.boundArrayBuffer == id)
            g_gl.boundArrayBuffer = 0;

        if (g_gl.boundElementArrayBuffer == id)
            g_gl.boundElementArrayBuffer = 0;

        g_gl.buffers.erase(id);
    }
}

extern "C" GLboolean APIENTRY glIsBuffer(GLuint buffer) { return g_gl.buffers.find(buffer) != g_gl.buffers.end() ? GL_TRUE : GL_FALSE; }

extern "C" void APIENTRY glBindBuffer(GLenum target, GLuint buffer)
{
    switch (target)
    {
    case GL_ARRAY_BUFFER:
        g_gl.boundArrayBuffer = buffer;
        break;

    case GL_ELEMENT_ARRAY_BUFFER:
        g_gl.boundElementArrayBuffer = buffer;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    if (buffer == 0)
        return;

    auto it = g_gl.buffers.find(buffer);
    if (it == g_gl.buffers.end())
    {
        GLBufferObject bo{};
        bo.id = buffer;
        bo.target = target;
        g_gl.buffers.emplace(buffer, std::move(bo));
    }
    else
    {
        it->second.target = target;
    }
}

extern "C" void APIENTRY glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags)
{
    GLuint bound = 0;

    switch (target)
    {
    case GL_ARRAY_BUFFER:
        bound = g_gl.boundArrayBuffer;
        break;

    case GL_ELEMENT_ARRAY_BUFFER:
        bound = g_gl.boundElementArrayBuffer;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    if (size < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    if (bound == 0)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    GLBufferObject* bo = QD3D12_GetBuffer(bound);
    if (!bo)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    bo->target = target;
    bo->storageFlags = flags;
    bo->data.resize((size_t)size);

    if (size > 0)
    {
        if (data)
            memcpy(bo->data.data(), data, (size_t)size);
        else
            memset(bo->data.data(), 0, (size_t)size);
    }
}

extern "C" void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
{
    (void)usage;
    glBufferStorage(target, size, data, 0);
}

extern "C" void APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
{
    GLuint bound = 0;

    switch (target)
    {
    case GL_ARRAY_BUFFER:
        bound = g_gl.boundArrayBuffer;
        break;

    case GL_ELEMENT_ARRAY_BUFFER:
        bound = g_gl.boundElementArrayBuffer;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    if (offset < 0 || size < 0 || !data)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    GLBufferObject* bo = QD3D12_GetBuffer(bound);
    if (!bo)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    if ((size_t)offset > bo->data.size() || (size_t)size > (bo->data.size() - (size_t)offset))
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    memcpy(bo->data.data() + (size_t)offset, data, (size_t)size);
}

extern "C" void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count <= 0)
        return;

    if (first < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    std::vector<GLVertex> verts;
    verts.reserve((size_t)count);

    for (GLsizei i = 0; i < count; ++i)
    {
        GLVertex v{};
        QD3D12_FetchArrayVertex(first + i, v);
        verts.push_back(v);
    }

    FlushImmediate(mode, verts);
}

extern "C" void* APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    GLuint bound = 0;

    switch (target)
    {
    case GL_ARRAY_BUFFER:
        bound = g_gl.boundArrayBuffer;
        break;

    case GL_ELEMENT_ARRAY_BUFFER:
        bound = g_gl.boundElementArrayBuffer;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        return nullptr;
    }

    if (offset < 0 || length < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return nullptr;
    }

    GLBufferObject* bo = QD3D12_GetBuffer(bound);
    if (!bo)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return nullptr;
    }

    if (bo->mapped)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return nullptr;
    }

    if ((size_t)offset > bo->data.size() || (size_t)length > (bo->data.size() - (size_t)offset))
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return nullptr;
    }

    // optional light validation
    if ((access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)) == 0)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return nullptr;
    }

    // emulate invalidation/orphan-ish behavior on CPU backing store
#ifdef GL_MAP_INVALIDATE_RANGE_BIT
    if ((access & GL_MAP_INVALIDATE_RANGE_BIT) && length > 0)
    {
        memset(bo->data.data() + offset, 0, (size_t)length);
    }
#endif

#ifdef GL_MAP_INVALIDATE_BUFFER_BIT
    if ((access & GL_MAP_INVALIDATE_BUFFER_BIT) && !bo->data.empty())
    {
        memset(bo->data.data(), 0, bo->data.size());
    }
#endif

    bo->mapped = true;
    bo->mappedOffset = offset;
    bo->mappedLength = length;
    bo->mappedAccess = access;

    return bo->data.data() + offset;
}

extern "C" GLboolean APIENTRY glUnmapBuffer(GLenum target)
{
    GLuint bound = 0;

    switch (target)
    {
    case GL_ARRAY_BUFFER:
        bound = g_gl.boundArrayBuffer;
        break;

    case GL_ELEMENT_ARRAY_BUFFER:
        bound = g_gl.boundElementArrayBuffer;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        return GL_FALSE;
    }

    GLBufferObject* bo = QD3D12_GetBuffer(bound);
    if (!bo)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return GL_FALSE;
    }

    if (!bo->mapped)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return GL_FALSE;
    }

    bo->mapped = false;
    bo->mappedOffset = 0;
    bo->mappedLength = 0;
    bo->mappedAccess = 0;

    return GL_TRUE;
}

extern "C" void APIENTRY glGenQueries(GLsizei n, GLuint* ids)
{
    if (n < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    if (!ids)
        return;

    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint           id = g_gl.nextQueryId++;
        GLOcclusionQuery q{};
        q.id = id;
        q.heapIndex = id % QD3D12_MaxQueries;  // simple scheme; good enough if IDs stay bounded
        g_gl.queries.emplace(id, q);
        ids[i] = id;
    }
}

extern "C" void APIENTRY glDeleteQueries(GLsizei n, const GLuint* ids)
{
    if (n < 0)
    {
        g_gl.lastError = GL_INVALID_VALUE;
        return;
    }

    if (!ids)
        return;

    for (GLsizei i = 0; i < n; ++i)
    {
        GLuint id = ids[i];
        if (id == 0)
            continue;

        if (g_gl.currentQuery == id)
        {
            g_gl.lastError = GL_INVALID_OPERATION;
            return;
        }

        g_gl.queries.erase(id);
    }
}

extern "C" GLboolean APIENTRY glIsQuery(GLuint id) { return g_gl.queries.find(id) != g_gl.queries.end() ? GL_TRUE : GL_FALSE; }

extern "C" void APIENTRY glBeginQuery(GLenum target, GLuint id)
{
    if (target != GL_SAMPLES_PASSED)
    {
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    auto it = g_gl.queries.find(id);
    if (it == g_gl.queries.end() || id == 0)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    if (g_gl.currentQuery != 0)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    GLOcclusionQuery& q = it->second;
    q.active = true;
    q.pending = false;
    q.resultReady = false;
    q.result = 0;

    g_gl.currentQuery = id;

    QueryMarker m{};
    m.type = QueryMarker::Begin;
    m.id = id;
    g_gl.queryMarkers.push_back(m);

    if (!g_gl.queuedBatches.empty())
        g_gl.queuedBatches.back().markerEnd = g_gl.queryMarkers.size();
}

extern "C" void APIENTRY glEndQuery(GLenum target)
{
    if (target != GL_SAMPLES_PASSED)
    {
        g_gl.lastError = GL_INVALID_ENUM;
        return;
    }

    if (g_gl.currentQuery == 0)
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    auto it = g_gl.queries.find(g_gl.currentQuery);
    if (it == g_gl.queries.end())
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        g_gl.currentQuery = 0;
        return;
    }

    it->second.active = false;
    it->second.pending = true;

    QueryMarker m{};
    m.type = QueryMarker::End;
    m.id = g_gl.currentQuery;
    g_gl.queryMarkers.push_back(m);

    g_gl.currentQuery = 0;

    if (!g_gl.queuedBatches.empty())
        g_gl.queuedBatches.back().markerEnd = g_gl.queryMarkers.size();
}

static void QD3D12_UpdateQueryResult(GLOcclusionQuery& q)
{
    if (!q.pending || q.resultReady)
        return;

    if (q.submittedFence == 0)
        return;

    if (g_gl.fence->GetCompletedValue() < q.submittedFence)
        return;

    q.result = g_gl.occlusionReadbackCpu[q.heapIndex];
    q.resultReady = true;
    q.pending = false;
}

extern "C" void APIENTRY glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params)
{
    if (!params)
        return;

    auto it = g_gl.queries.find(id);
    if (it == g_gl.queries.end())
    {
        g_gl.lastError = GL_INVALID_OPERATION;
        return;
    }

    GLOcclusionQuery& q = it->second;
    QD3D12_UpdateQueryResult(q);

    switch (pname)
    {
    case GL_QUERY_RESULT_AVAILABLE:
        *params = q.resultReady ? GL_TRUE : GL_FALSE;
        break;

    case GL_QUERY_RESULT:
        if (!q.resultReady)
        {
            QD3D12_WaitForGPU();
            QD3D12_UpdateQueryResult(q);
        }
        *params = (GLuint)q.result;
        break;

    default:
        g_gl.lastError = GL_INVALID_ENUM;
        break;
    }
}

extern "C" void APIENTRY glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params)
{
    if (!params)
        return;

    GLuint u = 0;
    glGetQueryObjectuiv(id, pname, &u);
    *params = (GLint)u;
}
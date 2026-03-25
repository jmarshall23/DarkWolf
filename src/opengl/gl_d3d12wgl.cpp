#include "opengl.h"

// Your D3D12 wrapper exports
extern bool QD3D12_InitForQuake(HWND hwnd, int width, int height);
extern void QD3D12_ShutdownForQuake(void);
extern void QD3D12_BeginFrame();
extern "C" void APIENTRY glSelectTextureSGIS(GLenum texture);
extern "C" void APIENTRY glMTexCoord2fSGIS(GLenum texture, GLfloat s, GLfloat t);
extern "C" void APIENTRY glActiveTextureARB(GLenum texture);
extern "C" void APIENTRY glMultiTexCoord2fARB(GLenum texture, GLfloat s, GLfloat t);

// Existing GL wrapper function from your compatibility layer
extern "C" void APIENTRY glBindTexture(unsigned int target, unsigned int texture);

struct QD3D12FakeContext
{
    HDC   dc;
    HWND  hwnd;
    bool  initialized;
};

static QD3D12FakeContext* g_currentContext = nullptr;
static HDC                g_currentDC = nullptr;

static void QD3D12_GetClientSize(HWND hwnd, int& w, int& h)
{
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    if (w <= 0) w = 640;
    if (h <= 0) h = 480;
}

extern "C" QD3D12_HGLRC WINAPI qd3d12_wglCreateContext(HDC hdc)
{
    if (!hdc)
        return nullptr;

    HWND hwnd = WindowFromDC(hdc);
    if (!hwnd)
        return nullptr;

    QD3D12FakeContext* ctx = new QD3D12FakeContext();
    ctx->dc = hdc;
    ctx->hwnd = hwnd;
    ctx->initialized = false;
    return (QD3D12_HGLRC)ctx;
}

extern "C" BOOL WINAPI qd3d12_wglMakeCurrent(HDC hdc, QD3D12_HGLRC hglrc)
{
    if (!hdc || !hglrc)
    {
        g_currentDC = nullptr;
        g_currentContext = nullptr;
        return TRUE;
    }

    QD3D12FakeContext* ctx = (QD3D12FakeContext*)hglrc;
    ctx->dc = hdc;

    if (!ctx->initialized)
    {
        int w = 640, h = 480;
        QD3D12_GetClientSize(ctx->hwnd, w, h);

        if (!QD3D12_InitForQuake(ctx->hwnd, w, h))
            return FALSE;

        if(!glRaytracingInit())
            return FALSE;

        if (!glRaytracingLightingInit())
            return FALSE;

        ctx->initialized = true;
       
        QD3D12_BeginFrame();
    }

    g_currentDC = hdc;
    g_currentContext = ctx;
    return TRUE;
}

extern "C" HDC WINAPI qd3d12_wglGetCurrentDC(void)
{
    return g_currentDC;
}

extern "C" QD3D12_HGLRC WINAPI qd3d12_wglGetCurrentContext(void)
{
    return (QD3D12_HGLRC)g_currentContext;
}

extern "C" BOOL WINAPI qd3d12_wglDeleteContext(QD3D12_HGLRC hglrc)
{
    if (!hglrc)
        return FALSE;

    QD3D12FakeContext* ctx = (QD3D12FakeContext*)hglrc;

    if (ctx == g_currentContext)
    {
        if (ctx->initialized)
            QD3D12_ShutdownForQuake();

        g_currentContext = nullptr;
        g_currentDC = nullptr;
    }

    delete ctx;
    return TRUE;
}

extern "C" void APIENTRY glBindTextureEXT(unsigned int target, unsigned int texture)
{
    glBindTexture(target, texture);
}

extern "C" int WINAPI qd3d12_wglDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd) {
    return DescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
}

extern "C" BOOL WINAPI qd3d12_wglSetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR* ppfd) {
    return 0;
}

extern "C" BOOL WINAPI qd3d12_wglSwapIntervalEXT(int interval) {
    (void)interval;
    return TRUE;
}

extern "C" BOOL WINAPI qd3d12_wglGetDeviceGammaRamp3DFX(HDC hdc, LPVOID ramp) {
    return FALSE;
}

extern "C" BOOL WINAPI qd3d12_wglSetDeviceGammaRamp3DFX(HDC hdc, LPVOID ramp) {
    return FALSE;
}


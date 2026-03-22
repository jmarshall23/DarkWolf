#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef unsigned int GLenum;
    typedef unsigned char GLboolean;
    typedef unsigned int GLbitfield;
    typedef void GLvoid;
    typedef signed char GLbyte;
    typedef short GLshort;
    typedef int GLint;
    typedef int GLsizei;
    typedef unsigned char GLubyte;
    typedef unsigned short GLushort;
    typedef unsigned int GLuint;
    typedef float GLfloat;
    typedef float GLclampf;
    typedef double GLdouble;
    typedef double GLclampd;
    typedef ptrdiff_t GLsizeiptr;
    typedef char GLchar;
    typedef intptr_t GLintptr;

#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif

    /* core string/state */
#ifndef GL_VENDOR
#define GL_VENDOR 0x1F00
#endif
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif
#ifndef GL_VERSION
#define GL_VERSION 0x1F02
#endif
#ifndef GL_EXTENSIONS
#define GL_EXTENSIONS 0x1F03
#endif

#ifndef GL_DONT_CARE
#define GL_DONT_CARE 0x1100
#endif

/* matrix */
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_TEXTURE
#define GL_TEXTURE 0x1702
#endif

/* texture/env */
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200
#endif
#ifndef GL_MODULATE
#define GL_MODULATE 0x2100
#endif
#ifndef GL_REPLACE
#define GL_REPLACE 0x1E01
#endif
#ifndef GL_DECAL
#define GL_DECAL 0x2101
#endif
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_ADD
#define GL_ADD 0x0104
#endif

/* tests / state */
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST 0x0B90
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0 0x3000
#endif

#ifndef GL_FOG
#define GL_FOG                          0x0B60
#endif
#ifndef GL_FOG_INDEX
#define GL_FOG_INDEX                    0x0B61
#endif
#ifndef GL_FOG_DENSITY
#define GL_FOG_DENSITY                  0x0B62
#endif
#ifndef GL_FOG_START
#define GL_FOG_START                    0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END                      0x0B64
#endif
#ifndef GL_FOG_MODE
#define GL_FOG_MODE                     0x0B65
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR                    0x0B66
#endif
#ifndef GL_FOG_HINT
#define GL_FOG_HINT                     0x0C54
#endif
#ifndef GL_EXP
#define GL_EXP                          0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2                         0x0801
#endif

/* faces */
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif

/* fill/shade */
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FLAT
#define GL_FLAT 0x1D00
#endif
#ifndef GL_SMOOTH
#define GL_SMOOTH 0x1D01
#endif

/* clear bits */
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif
#ifndef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT 0x00000400
#endif
#ifndef GL_ACCUM_BUFFER_BIT
#define GL_ACCUM_BUFFER_BIT 0x00000200
#endif

/* primitive modes */
#ifndef GL_POINTS
#define GL_POINTS 0x0000
#endif
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_LINE_LOOP
#define GL_LINE_LOOP 0x0002
#endif
#ifndef GL_LINE_STRIP
#define GL_LINE_STRIP 0x0003
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_TRIANGLE_FAN
#define GL_TRIANGLE_FAN 0x0006
#endif
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_QUAD_STRIP
#define GL_QUAD_STRIP 0x0008
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009
#endif

/* compare funcs */
#ifndef GL_NEVER
#define GL_NEVER 0x0200
#endif
#ifndef GL_LESS
#define GL_LESS 0x0201
#endif
#ifndef GL_EQUAL
#define GL_EQUAL 0x0202
#endif
#ifndef GL_LEQUAL
#define GL_LEQUAL 0x0203
#endif
#ifndef GL_GREATER
#define GL_GREATER 0x0204
#endif
#ifndef GL_NOTEQUAL
#define GL_NOTEQUAL 0x0205
#endif
#ifndef GL_GEQUAL
#define GL_GEQUAL 0x0206
#endif
#ifndef GL_ALWAYS
#define GL_ALWAYS 0x0207
#endif

/* blending */
#ifndef GL_ZERO
#define GL_ZERO 0
#endif
#ifndef GL_ONE
#define GL_ONE 1
#endif
#ifndef GL_SRC_COLOR
#define GL_SRC_COLOR 0x0300
#endif
#ifndef GL_ONE_MINUS_SRC_COLOR
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_DST_ALPHA
#define GL_DST_ALPHA 0x0304
#endif
#ifndef GL_ONE_MINUS_DST_ALPHA
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#endif
#ifndef GL_DST_COLOR
#define GL_DST_COLOR 0x0306
#endif
#ifndef GL_ONE_MINUS_DST_COLOR
#define GL_ONE_MINUS_DST_COLOR 0x0307
#endif
#ifndef GL_SRC_ALPHA_SATURATE
#define GL_SRC_ALPHA_SATURATE 0x0308
#endif

/* stencil ops */
#ifndef GL_KEEP
#define GL_KEEP 0x1E00
#endif
#ifndef GL_INCR
#define GL_INCR 0x1E02
#endif
#ifndef GL_DECR
#define GL_DECR 0x1E03
#endif

/* filters/wrap */
#ifndef GL_NEAREST
#define GL_NEAREST 0x2600
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_NEAREST_MIPMAP_NEAREST
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#endif
#ifndef GL_LINEAR_MIPMAP_NEAREST
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#endif
#ifndef GL_NEAREST_MIPMAP_LINEAR
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif

/* data types */
#ifndef GL_BYTE
#define GL_BYTE 0x1400
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_SHORT
#define GL_SHORT 0x1402
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_INT
#define GL_INT 0x1404
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_DOUBLE
#define GL_DOUBLE 0x140A
#endif

/* formats */
#ifndef GL_COLOR_INDEX
#define GL_COLOR_INDEX 0x1900
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX 0x1901
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_ALPHA
#define GL_ALPHA 0x1906
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_INTENSITY
#define GL_INTENSITY 0x8049
#endif

/* sized formats */
#ifndef GL_RGB4_S3TC
#define GL_RGB4_S3TC 0x83A1
#endif
#ifndef GL_RGB5
#define GL_RGB5 0x8050
#endif
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

/* hints / queries / errors */
#ifndef GL_PERSPECTIVE_CORRECTION_HINT
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#endif
#ifndef GL_FASTEST
#define GL_FASTEST 0x1101
#endif
#ifndef GL_NICEST
#define GL_NICEST 0x1102
#endif
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#endif
#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_INVALID_ENUM
#define GL_INVALID_ENUM 0x0500
#endif
#ifndef GL_INVALID_VALUE
#define GL_INVALID_VALUE 0x0501
#endif
#ifndef GL_INVALID_OPERATION
#define GL_INVALID_OPERATION 0x0502
#endif
#ifndef GL_STACK_OVERFLOW
#define GL_STACK_OVERFLOW 0x0503
#endif
#ifndef GL_STACK_UNDERFLOW
#define GL_STACK_UNDERFLOW 0x0504
#endif
#ifndef GL_OUT_OF_MEMORY
#define GL_OUT_OF_MEMORY 0x0505
#endif

/* client arrays */
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY 0x8074
#endif
#ifndef GL_NORMAL_ARRAY
#define GL_NORMAL_ARRAY 0x8075
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY 0x8076
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY 0x8078
#endif

/* multitexture */
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB 0x84C1
#endif
#ifndef GL_TEXTURE2_ARB
#define GL_TEXTURE2_ARB 0x84C2
#endif
#ifndef GL_TEXTURE3_ARB
#define GL_TEXTURE3_ARB 0x84C3
#endif
#ifndef GL_ACTIVE_TEXTURE_ARB
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE_ARB
#define GL_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_MAX_ACTIVE_TEXTURES_ARB
#define GL_MAX_ACTIVE_TEXTURES_ARB 0x84E2
#endif

/* SGIS multitexture */
#ifndef GL_TEXTURE0_SGIS
#define GL_TEXTURE0_SGIS 0x835E
#endif
#ifndef GL_TEXTURE1_SGIS
#define GL_TEXTURE1_SGIS 0x835F
#endif
#ifndef GL_TEXTURE2_SGIS
#define GL_TEXTURE2_SGIS 0x8360
#endif
#ifndef GL_TEXTURE3_SGIS
#define GL_TEXTURE3_SGIS 0x8361
#endif
#ifndef GL_SELECTED_TEXTURE_SGIS
#define GL_SELECTED_TEXTURE_SGIS 0x835C
#endif
#ifndef GL_MAX_TEXTURES_SGIS
#define GL_MAX_TEXTURES_SGIS 0x83B9
#endif

#ifndef GL_SGIS_multitexture
#define GL_SGIS_multitexture 1
#endif

    typedef void* QD3D12_HGLRC;

    /* proc typedefs used by win_glimp.c */
    typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int interval);
    typedef BOOL(WINAPI* PFNWGLGETDEVICEGAMMARAMP3DFXPROC)(HDC hDC, LPVOID ramp);
    typedef BOOL(WINAPI* PFNWGLSETDEVICEGAMMARAMP3DFXPROC)(HDC hDC, LPVOID ramp);

    typedef void (APIENTRY* PFNGLMULTITEXCOORD2FARBPROC)(GLenum texture, GLfloat s, GLfloat t);
    typedef void (APIENTRY* PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
    typedef void (APIENTRY* PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum texture);
    typedef void (APIENTRY* PFNGLLOCKARRAYSEXTPROC)(GLint first, GLsizei count);
    typedef void (APIENTRY* PFNGLUNLOCKARRAYSEXTPROC)(void);

    QD3D12_HGLRC WINAPI qd3d12_wglCreateContext(HDC hdc);
    BOOL         WINAPI qd3d12_wglMakeCurrent(HDC hdc, QD3D12_HGLRC hglrc);
    PROC         WINAPI qd3d12_wglGetProcAddress(LPCSTR name);
    HDC          WINAPI qd3d12_wglGetCurrentDC(void);
    QD3D12_HGLRC WINAPI qd3d12_wglGetCurrentContext(void);
    BOOL         WINAPI qd3d12_wglDeleteContext(QD3D12_HGLRC hglrc);
    void               QD3D12_SwapBuffers(HDC hdc);

    /* optional helpers for legacy codepaths */
    int  WINAPI qd3d12_wglDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
    BOOL WINAPI qd3d12_wglSetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR* ppfd);
    BOOL WINAPI qd3d12_wglSwapIntervalEXT(int interval);
    BOOL WINAPI qd3d12_wglGetDeviceGammaRamp3DFX(HDC hdc, LPVOID ramp);
    BOOL WINAPI qd3d12_wglSetDeviceGammaRamp3DFX(HDC hdc, LPVOID ramp);

    /* EXT alias */
    void APIENTRY glBindTextureEXT(GLenum target, GLuint texture);

    /* only redirect in normal compilation units, not win_qgl.c */
#ifndef QD3D12_NO_WGL_REDIRECTS
#define wglCreateContext         qd3d12_wglCreateContext
#define wglMakeCurrent           qd3d12_wglMakeCurrent
#define wglGetProcAddress        qd3d12_wglGetProcAddress
#define wglGetCurrentDC          qd3d12_wglGetCurrentDC
#define wglGetCurrentContext     qd3d12_wglGetCurrentContext
#define wglDeleteContext         qd3d12_wglDeleteContext
#define wglDescribePixelFormat   qd3d12_wglDescribePixelFormat
#define wglSetPixelFormat        qd3d12_wglSetPixelFormat
#define wglSwapIntervalEXT       qd3d12_wglSwapIntervalEXT
#define wglGetDeviceGammaRamp3DFX qd3d12_wglGetDeviceGammaRamp3DFX
#define wglSetDeviceGammaRamp3DFX qd3d12_wglSetDeviceGammaRamp3DFX
#define SwapBuffers(x)           QD3D12_SwapBuffers(x)
#endif

#define glTexParameteri glTexParameterf

#ifndef QD3D12_NO_GL_PROTOTYPES

    WINGDIAPI void APIENTRY glLoadMatrixf(const GLfloat* m);
    WINGDIAPI void APIENTRY glGetFloatv(GLenum pname, GLfloat* params);
    WINGDIAPI void APIENTRY glGetIntegerv(GLenum pname, GLint* params);
    WINGDIAPI void APIENTRY glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
    WINGDIAPI void APIENTRY glDepthFunc(GLenum func);
    WINGDIAPI void APIENTRY glColor4fv(const GLfloat* v);
    WINGDIAPI const GLubyte* APIENTRY glGetString(GLenum name);
    WINGDIAPI void APIENTRY glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
    WINGDIAPI void APIENTRY glClear(GLbitfield mask);
    WINGDIAPI void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
    WINGDIAPI void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
    WINGDIAPI GLenum APIENTRY glGetError(void);

    WINGDIAPI void APIENTRY glEnable(GLenum cap);
    WINGDIAPI void APIENTRY glDisable(GLenum cap);
    WINGDIAPI void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
    WINGDIAPI void APIENTRY glAlphaFunc(GLenum func, GLclampf ref);
    WINGDIAPI void APIENTRY glDepthMask(GLboolean flag);
    WINGDIAPI void APIENTRY glDepthRange(GLclampd zNear, GLclampd zFar);
    WINGDIAPI void APIENTRY glClearDepth(GLclampd depth);
    WINGDIAPI void APIENTRY glCullFace(GLenum mode);
    WINGDIAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode);
    WINGDIAPI void APIENTRY glShadeModel(GLenum mode);
    WINGDIAPI void APIENTRY glHint(GLenum target, GLenum mode);
    WINGDIAPI void APIENTRY glFinish(void);
    WINGDIAPI void APIENTRY glClipPlane(GLenum plane, const GLdouble* equation);
    WINGDIAPI void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units);

    WINGDIAPI void APIENTRY glMatrixMode(GLenum mode);
    WINGDIAPI void APIENTRY glLoadIdentity(void);
    WINGDIAPI void APIENTRY glPushMatrix(void);
    WINGDIAPI void APIENTRY glPopMatrix(void);
    WINGDIAPI void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
    WINGDIAPI void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
    WINGDIAPI void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
    WINGDIAPI void APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);

    WINGDIAPI void APIENTRY glBegin(GLenum mode);
    WINGDIAPI void APIENTRY glEnd(void);
    WINGDIAPI void APIENTRY glVertex2f(GLfloat x, GLfloat y);
    WINGDIAPI void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
    WINGDIAPI void APIENTRY glVertex3fv(const GLfloat* v);
    WINGDIAPI void APIENTRY glTexCoord2f(GLfloat s, GLfloat t);
    WINGDIAPI void APIENTRY glTexCoord2fv(const GLfloat* v);
    WINGDIAPI void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b);
    WINGDIAPI void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    WINGDIAPI void APIENTRY glColor4ubv(const GLubyte* v);

    WINGDIAPI void APIENTRY glGenTextures(GLsizei n, GLuint* textures);
    WINGDIAPI void APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures);
    WINGDIAPI void APIENTRY glBindTexture(GLenum target, GLuint texture);
    WINGDIAPI void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
    WINGDIAPI void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
    WINGDIAPI void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
    WINGDIAPI void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalFormat,
        GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels);
    WINGDIAPI void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
        GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels);
    WINGDIAPI void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
        GLenum format, GLenum type, GLvoid* data);
    WINGDIAPI void APIENTRY glDrawBuffer(GLenum mode);
    WINGDIAPI void APIENTRY glReadBuffer(GLenum mode);

    WINGDIAPI void APIENTRY glEnableClientState(GLenum array);
    WINGDIAPI void APIENTRY glDisableClientState(GLenum array);
    WINGDIAPI void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
    WINGDIAPI void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
    WINGDIAPI void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
    WINGDIAPI void APIENTRY glArrayElement(GLint i);
    WINGDIAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);

    WINGDIAPI void APIENTRY glStencilMask(GLuint mask);
    WINGDIAPI void APIENTRY glClearStencil(GLint s);
    WINGDIAPI void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
    WINGDIAPI void APIENTRY glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
    WINGDIAPI void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);

    WINGDIAPI void APIENTRY glActiveTextureARB(GLenum texture);
    WINGDIAPI void APIENTRY glClientActiveTextureARB(GLenum texture);
    WINGDIAPI void APIENTRY glMultiTexCoord2fARB(GLenum texture, GLfloat s, GLfloat t);
    WINGDIAPI void APIENTRY glSelectTextureSGIS(GLenum texture);
    WINGDIAPI void APIENTRY glMTexCoord2fSGIS(GLenum texture, GLfloat s, GLfloat t);
    WINGDIAPI void APIENTRY glLockArraysEXT(GLint first, GLsizei count);
    WINGDIAPI void APIENTRY glUnlockArraysEXT(void);

    WINGDIAPI void APIENTRY glFogiv(GLenum pname, const GLint* params);
    WINGDIAPI void APIENTRY glFogfv(GLenum pname, const GLfloat* params);
    WINGDIAPI void APIENTRY glFogi(GLenum pname, GLint param);
    WINGDIAPI void APIENTRY glFogf(GLenum pname, GLfloat param);

#endif /* QD3D12_NO_GL_PROTOTYPES */

#ifdef __cplusplus
}
#endif
typedef struct Q3Vec3
{
    float x, y, z;
} Q3Vec3;

typedef struct Q3Triangle
{
    Q3Vec3 a, b, c;
} Q3Triangle;

typedef struct Q3AABB
{
    Q3Vec3 mins;
    Q3Vec3 maxs;
} Q3AABB;

typedef struct Q3Ray
{
    Q3Vec3 origin;
    Q3Vec3 dir;      /* should be normalized by caller or helper */
    float   tMin;
    float   tMax;
    uint32_t mask;
} Q3Ray;

typedef struct Q3HitResult
{
    int      hit;
    int      hitWorld;
    uint32_t entityId;        /* 0xFFFFFFFF if world */
    float    t;
    Q3Vec3   position;
    Q3Vec3   barycentrics;    /* x = bary.x, y = bary.y, z unused */
    uint32_t primitiveIndex;
    uint32_t instanceIndex;
    uint32_t geometryIndex;
} Q3HitResult;

#ifdef __cplusplus
extern "C" {
#endif
    

#ifdef __cplusplus
};
#endif
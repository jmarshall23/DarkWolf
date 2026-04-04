#pragma once

#define __gl_h_

#include <stddef.h>
#include <stdint.h>
#include <d3d12.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <math.h>

template<typename T>
static T ClampValue(T v, T lo, T hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

struct Mat4
{
    float m[16];

    static Mat4 Identity()
    {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }


    static bool Invert(const Mat4* in, Mat4* out)
    {
        const float* m = in->m;
        float inv[16];

        inv[0] = m[5] * m[10] * m[15] -
            m[5] * m[11] * m[14] -
            m[9] * m[6] * m[15] +
            m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] -
            m[13] * m[7] * m[10];

        inv[4] = -m[4] * m[10] * m[15] +
            m[4] * m[11] * m[14] +
            m[8] * m[6] * m[15] -
            m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] +
            m[12] * m[7] * m[10];

        inv[8] = m[4] * m[9] * m[15] -
            m[4] * m[11] * m[13] -
            m[8] * m[5] * m[15] +
            m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] -
            m[12] * m[7] * m[9];

        inv[12] = -m[4] * m[9] * m[14] +
            m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] +
            m[12] * m[6] * m[9];

        inv[1] = -m[1] * m[10] * m[15] +
            m[1] * m[11] * m[14] +
            m[9] * m[2] * m[15] -
            m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] +
            m[13] * m[3] * m[10];

        inv[5] = m[0] * m[10] * m[15] -
            m[0] * m[11] * m[14] -
            m[8] * m[2] * m[15] +
            m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] -
            m[12] * m[3] * m[10];

        inv[9] = -m[0] * m[9] * m[15] +
            m[0] * m[11] * m[13] +
            m[8] * m[1] * m[15] -
            m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] +
            m[12] * m[3] * m[9];

        inv[13] = m[0] * m[9] * m[14] -
            m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] -
            m[12] * m[2] * m[9];

        inv[2] = m[1] * m[6] * m[15] -
            m[1] * m[7] * m[14] -
            m[5] * m[2] * m[15] +
            m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] -
            m[13] * m[3] * m[6];

        inv[6] = -m[0] * m[6] * m[15] +
            m[0] * m[7] * m[14] +
            m[4] * m[2] * m[15] -
            m[4] * m[3] * m[14] -
            m[12] * m[2] * m[7] +
            m[12] * m[3] * m[6];

        inv[10] = m[0] * m[5] * m[15] -
            m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] -
            m[12] * m[3] * m[5];

        inv[14] = -m[0] * m[5] * m[14] +
            m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] +
            m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] +
            m[1] * m[7] * m[10] +
            m[5] * m[2] * m[11] -
            m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] +
            m[9] * m[3] * m[6];

        inv[7] = m[0] * m[6] * m[11] -
            m[0] * m[7] * m[10] -
            m[4] * m[2] * m[11] +
            m[4] * m[3] * m[10] +
            m[8] * m[2] * m[7] -
            m[8] * m[3] * m[6];

        inv[11] = -m[0] * m[5] * m[11] +
            m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] +
            m[8] * m[3] * m[5];

        inv[15] = m[0] * m[5] * m[10] -
            m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] -
            m[8] * m[2] * m[5];

        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (det == 0.0f)
            return false;

        det = 1.0f / det;
        for (int i = 0; i < 16; ++i)
            out->m[i] = inv[i] * det;

        return true;
    }


    static Mat4 Multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};

        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                r.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }

        return r;
    }

    static Mat4 Translation(float x, float y, float z)
    {
        Mat4 r = Identity();
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    static Mat4 Scale(float x, float y, float z)
    {
        Mat4 r{};
        r.m[0] = x;
        r.m[5] = y;
        r.m[10] = z;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 RotationAxisDeg(float angleDeg, float x, float y, float z)
    {
        float len = sqrtf(x * x + y * y + z * z);
        if (len <= 0.000001f)
            return Identity();

        x /= len;
        y /= len;
        z /= len;

        const float rad = angleDeg * 3.1415926535f / 180.0f;
        const float c = cosf(rad);
        const float s = sinf(rad);
        const float t = 1.0f - c;

        Mat4 r = Identity();
        r.m[0] = t * x * x + c;
        r.m[1] = t * x * y + s * z;
        r.m[2] = t * x * z - s * y;
        r.m[4] = t * x * y - s * z;
        r.m[5] = t * y * y + c;
        r.m[6] = t * y * z + s * x;
        r.m[8] = t * x * z + s * y;
        r.m[9] = t * y * z - s * x;
        r.m[10] = t * z * z + c;
        return r;
    }

    static Mat4 Ortho(double left, double right, double bottom, double top, double zNear, double zFar)
    {
        Mat4 r{};
        r.m[0] = (float)(2.0 / (right - left));
        r.m[5] = (float)(2.0 / (top - bottom));
        r.m[10] = (float)(1.0 / (zFar - zNear));
        r.m[12] = (float)(-(right + left) / (right - left));
        r.m[13] = (float)(-(top + bottom) / (top - bottom));
        r.m[14] = (float)(-zNear / (zFar - zNear));
        r.m[15] = 1.0f;
        return r;
    }
};

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

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif

#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY 0x8865
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

#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif

#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif

#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif

#ifndef GL_MAP_FLUSH_EXPLICIT_BIT
#define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#endif

#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
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

#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX 0x0BA7
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
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
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
    WINGDIAPI void APIENTRY glLoadModelMatrixf(const float* m16);
    WINGDIAPI void APIENTRY glGetFloatv(GLenum pname, GLfloat* params);
    WINGDIAPI void APIENTRY glMultMatrixf(const GLfloat *m);
    WINGDIAPI void APIENTRY glGetDoublev(GLenum pname, GLdouble *params);
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
    WINGDIAPI void APIENTRY          glGenBuffers(GLsizei n, GLuint *buffers);
    WINGDIAPI void APIENTRY          glDeleteBuffers(GLsizei n, const GLuint *buffers);
    WINGDIAPI void APIENTRY          glBindBuffer(GLenum target, GLuint buffer);
    WINGDIAPI void APIENTRY          glBufferStorage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
    WINGDIAPI void APIENTRY          glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    WINGDIAPI void APIENTRY          glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
    WINGDIAPI void APIENTRY          glDrawArrays(GLenum mode, GLint first, GLsizei count);
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
    enum GeometryFlag_t {
        GEOMETRY_FLAG_NONE = 0,
        GEOMETRY_FLAG_SKELETAL,
        GEOMETRY_FLAG_UNLIT
    };
    WINGDIAPI void APIENTRY glGeometryFlagf(GLfloat flag);
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
    WINGDIAPI void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void* pointer);
    WINGDIAPI void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
    WINGDIAPI void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
    WINGDIAPI void APIENTRY glArrayElement(GLint i);
    WINGDIAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);

    WINGDIAPI void APIENTRY glStencilMask(GLuint mask);
    WINGDIAPI void APIENTRY glClearStencil(GLint s);
    WINGDIAPI void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask);
    WINGDIAPI void APIENTRY glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
    WINGDIAPI void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);

    WINGDIAPI void *APIENTRY      glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
    WINGDIAPI GLboolean APIENTRY glUnmapBuffer(GLenum target);

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

    WINGDIAPI void APIENTRY      glGenQueries(GLsizei n, GLuint *ids);
    WINGDIAPI void APIENTRY      glDeleteQueries(GLsizei n, const GLuint *ids);
    WINGDIAPI GLboolean APIENTRY glIsQuery(GLuint id);
    WINGDIAPI void APIENTRY      glBeginQuery(GLenum target, GLuint id);
    WINGDIAPI void APIENTRY      glEndQuery(GLenum target);
    WINGDIAPI void APIENTRY      glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params);
    WINGDIAPI void APIENTRY      glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params);
    WINGDIAPI void APIENTRY      glGetIntegerv(GLenum pname, GLint *params);

#endif /* QD3D12_NO_GL_PROTOTYPES */

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
extern "C" {
#endif
    // ============================================================
// Scene types
// ============================================================

    typedef struct glRaytracingVertex_s
    {
        float xyz[3];
        float normal[3];
        float st[2];
    } glRaytracingVertex_t;

    typedef struct glRaytracingMeshDesc_s
    {
        const glRaytracingVertex_t* vertices;
        uint32_t                    vertexCount;

        const uint32_t* indices;
        uint32_t                    indexCount;

        int                         allowUpdate;
        int                         opaque;
    } glRaytracingMeshDesc_t;

    typedef struct glRaytracingInstanceDesc_s
    {
        uint32_t meshHandle;
        uint32_t instanceID;
        uint32_t mask;
        float    transform[12]; // 3x4 row-major
    } glRaytracingInstanceDesc_t;

    typedef uint32_t glRaytracingMeshHandle_t;
    typedef uint32_t glRaytracingInstanceHandle_t;

    // ============================================================
    // Lighting types
    // ============================================================

    static const uint32_t GL_RAYTRACING_MAX_LIGHTS = 256;

    typedef struct glRaytracingVec3_s
    {
        float x, y, z;
    } glRaytracingVec3_t;

    typedef struct glRaytracingLight_s
    {
        glRaytracingVec3_t position;
        float              radius;

        glRaytracingVec3_t color;
        float              intensity;

        glRaytracingVec3_t   normal;        // rect normal (ignored for point)
        uint32_t             type;          // POINT or RECT

        glRaytracingVec3_t   axisU;         // rect local X axis, normalized
        float                halfWidth;     // half extent along axisU

        glRaytracingVec3_t   axisV;         // rect local Y axis, normalized
        float                halfHeight;    // half extent along axisV

        uint32_t             samples;       // rect sample count
        uint32_t             twoSided;      // 0/1
        float                persistant;
        float                pad1;
    } glRaytracingLight_t;

    typedef struct glRaytracingLightingPassDesc_s
    {
        ID3D12Resource* albedoTexture;
        DXGI_FORMAT     albedoFormat;

        ID3D12Resource* depthTexture;
        DXGI_FORMAT     depthFormat;

        ID3D12Resource* normalTexture;
        DXGI_FORMAT     normalFormat;

        ID3D12Resource* positionTexture;
        DXGI_FORMAT     positionFormat;

        ID3D12Resource* outputTexture;
        DXGI_FORMAT     outputFormat;

        ID3D12Resource* topLevelAS;

        uint32_t        width;
        uint32_t        height;
    } glRaytracingLightingPassDesc_t;

    // ============================================================
    // Scene API
    // ============================================================

    int                           glRaytracingInit(void);
    void                          glRaytracingShutdown(void);
    void                          glRaytracingClear(void);

    glRaytracingMeshHandle_t      glRaytracingCreateMesh(const glRaytracingMeshDesc_t* desc);
    int                           glRaytracingUpdateMesh(glRaytracingMeshHandle_t meshHandle, const glRaytracingMeshDesc_t* desc);
    void                          glRaytracingDeleteMesh(glRaytracingMeshHandle_t meshHandle);

    glRaytracingInstanceHandle_t  glRaytracingCreateInstance(const glRaytracingInstanceDesc_t* desc);
    int                           glRaytracingUpdateInstance(glRaytracingInstanceHandle_t instanceHandle, const glRaytracingInstanceDesc_t* desc);
    void                          glRaytracingDeleteInstance(glRaytracingInstanceHandle_t instanceHandle);

    int                           glRaytracingBuildMesh(glRaytracingMeshHandle_t meshHandle);
    int                           glRaytracingBuildAllMeshes(void);
    int                           glRaytracingBuildScene(void);

    uint32_t                      glRaytracingGetMeshCount(void);
    uint32_t                      glRaytracingGetInstanceCount(void);

    // ============================================================
    // Lighting API
    // ============================================================

    bool                          glRaytracingLightingInit(void);
    void                          glRaytracingLightingShutdown(void);
    bool                          glRaytracingLightingIsInitialized(void);

    void                          glRaytracingLightingClearLights(bool clearPersistant);
    bool                          glRaytracingLightingAddLight(const glRaytracingLight_t* light);

    void                          glRaytracingLightingSetAmbient(float r, float g, float b, float intensity);
    void                          glRaytracingLightingSetCameraPosition(float x, float y, float z);
    void                          glRaytracingLightingSetInvViewProjMatrix(const float* m16);
   void                         glRaytracingLightingSetInvViewMatrix(const float* m16);
    void                          glRaytracingLightingSetNormalReconstructSign(float signValue);
    void                          glRaytracingLightingEnableSpecular(int enable);
    void                          glRaytracingLightingEnableHalfLambert(int enable);
    void                          glRaytracingLightingSetShadowBias(float bias);

    int                          glRaytracingBuildSceneIfDirty(void);

    bool                          glRaytracingLightingExecute(const glRaytracingLightingPassDesc_t* pass);

    glRaytracingLight_t           glRaytracingLightingMakePointLight(
        float px, float py, float pz,
        float radius,
        float r, float g, float b,
        float intensity);

    glRaytracingLight_t glRaytracingLightingMakeRectLight(
        float px, float py, float pz,
        float nx, float ny, float nz,
        float ux, float uy, float uz,
        float vx, float vy, float vz,
        float halfWidth, float halfHeight,
        float r, float g, float b,
        float intensity,
        uint32_t samples,
        uint32_t twoSided);

    uint32_t                      glRaytracingLightingGetLightCount(void);

    void glLightScene(void);

    ID3D12Device* QD3D12_GetDevice(void);
    ID3D12CommandQueue* QD3D12_GetQueue(void);
    ID3D12GraphicsCommandList* QD3D12_GetCommandList(void);

    ID3D12Resource* QD3D12_GetCurrentBackBuffer();
#ifdef __cplusplus
};
#endif

static float glRaytracingSqrtf(float x)
{
    return (x > 0.0f) ? sqrtf(x) : 0.0f;
}

static void glRaytracingNormalize3(float& x, float& y, float& z)
{
    const float lenSq = x * x + y * y + z * z;
    if (lenSq > 1e-20f)
    {
        const float invLen = 1.0f / glRaytracingSqrtf(lenSq);
        x *= invLen;
        y *= invLen;
        z *= invLen;
    }
    else
    {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
    }
}

static void glRaytracingCross3(
    float ax, float ay, float az,
    float bx, float by, float bz,
    float& outX, float& outY, float& outZ)
{
    outX = ay * bz - az * by;
    outY = az * bx - ax * bz;
    outZ = ax * by - ay * bx;
}

static const int GL_RAYTRACING_LIGHT_TYPE_POINT = 0;
static const int GL_RAYTRACING_LIGHT_TYPE_RECT = 1;

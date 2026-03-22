/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// win_syscon.c

#include "../client/client.h"
#include "win_local.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <string.h>
#include <stddef.h>

#define COPY_ID         1
#define QUIT_ID         2
#define CLEAR_ID        3

#define ERRORBOX_ID     10
#define ERRORTEXT_ID    11

#define EDIT_ID         100
#define INPUT_ID        101

#define SYS_CONSOLE_CLASS   "Q3WinConsole_x64"
#define SYS_CONSOLE_TITLE   "Quake III Arena Console"

#define SYS_CLIENT_WIDTH    540
#define SYS_CLIENT_HEIGHT   450

#define SYS_PAD             6
#define SYS_TOP_BANNER_H    30
#define SYS_INPUT_H         22
#define SYS_BUTTON_W        72
#define SYS_BUTTON_H        24
#define SYS_BUTTON_GAP      5
#define SYS_BOTTOM_GAP      6
#define SYS_SECTION_GAP     6

#define SYS_CONSOLE_BG      RGB(0x12, 0x18, 0x28)
#define SYS_CONSOLE_TEXT    RGB(0xD8, 0xE6, 0xFF)
#define SYS_ERROR_BG        RGB(0x44, 0x44, 0x44)
#define SYS_ERROR_TEXT_A    RGB(0xFF, 0x40, 0x40)
#define SYS_ERROR_TEXT_B    RGB(0x80, 0xFF, 0x80)

typedef struct
{
    HWND        hWnd;
    HWND        hwndBuffer;

    HWND        hwndButtonClear;
    HWND        hwndButtonCopy;
    HWND        hwndButtonQuit;

    HWND        hwndErrorBox;
    HWND        hwndErrorText;

    HBITMAP     hbmLogo;
    HBITMAP     hbmClearBitmap;

    HBRUSH      hbrEditBackground;
    HBRUSH      hbrErrorBackground;

    HFONT       hfBufferFont;
    HFONT       hfButtonFont;

    HWND        hwndInputLine;

    char        errorString[80];
    char        consoleText[512];
    char        returnedText[512];

    int         visLevel;
    qboolean    quitOnClose;
    int         windowWidth;
    int         windowHeight;

    WNDPROC     SysInputLineWndProc;
} WinConData;

static WinConData s_wcd;

static void Con_ClearEdit(HWND hEdit);
static void Con_AppendLineToInputBuffer(const char* text);
static void Con_LayoutChildren(HWND hWnd);
static HFONT Con_CreateUIFont(HWND hWnd, int pointSize, const char* face, int weight, int fixedPitch);
static void Con_DestroyResources(void);
static void Con_CreateChildWindows(HWND hWnd);
static void Con_EnsureVisibleCaret(void);

static LRESULT WINAPI ConWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LONG WINAPI InputLineWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static void Con_ClearEdit(HWND hEdit)
{
    if (!hEdit)
    {
        return;
    }

    SendMessageA(hEdit, EM_SETSEL, 0, -1);
    SendMessageA(hEdit, EM_REPLACESEL, FALSE, (LPARAM)"");
}

static void Con_AppendLineToInputBuffer(const char* text)
{
    size_t curLen;
    size_t maxLen;
    size_t remain;

    if (!text || !text[0])
    {
        return;
    }

    curLen = strlen(s_wcd.consoleText);
    maxLen = sizeof(s_wcd.consoleText) - 1;

    if (curLen < maxLen)
    {
        remain = maxLen - curLen;
        strncat(s_wcd.consoleText, text, remain);

        curLen = strlen(s_wcd.consoleText);
        if (curLen + 1 < maxLen)
        {
            strcat(s_wcd.consoleText, "\n");
        }
    }
}

static void Con_LayoutChildren(HWND hWnd)
{
    RECT rc;
    int clientW, clientH;
    int errorTop, errorHeight;
    int bufferTop;
    int buttonsY, inputY, bufferBottom, bufferHeight;

    if (!hWnd)
    {
        return;
    }

    GetClientRect(hWnd, &rc);

    clientW = rc.right - rc.left;
    clientH = rc.bottom - rc.top;

    s_wcd.windowWidth = clientW;
    s_wcd.windowHeight = clientH;

    errorTop = SYS_PAD;
    errorHeight = s_wcd.hwndErrorBox ? SYS_TOP_BANNER_H : 0;

    bufferTop = SYS_PAD + errorHeight + (errorHeight > 0 ? SYS_SECTION_GAP : 0);

    buttonsY = clientH - SYS_BOTTOM_GAP - SYS_BUTTON_H;
    inputY = buttonsY - SYS_SECTION_GAP - SYS_INPUT_H;
    bufferBottom = inputY - SYS_SECTION_GAP;
    bufferHeight = (bufferBottom > bufferTop) ? (bufferBottom - bufferTop) : 32;

    if (s_wcd.hwndErrorBox)
    {
        MoveWindow(
            s_wcd.hwndErrorBox,
            SYS_PAD,
            errorTop,
            clientW - (SYS_PAD * 2),
            SYS_TOP_BANNER_H,
            TRUE
        );
    }

    if (s_wcd.hwndBuffer)
    {
        MoveWindow(
            s_wcd.hwndBuffer,
            SYS_PAD,
            bufferTop,
            clientW - (SYS_PAD * 2),
            bufferHeight,
            TRUE
        );
    }

    if (s_wcd.hwndInputLine)
    {
        MoveWindow(
            s_wcd.hwndInputLine,
            SYS_PAD,
            inputY,
            clientW - (SYS_PAD * 2),
            SYS_INPUT_H,
            TRUE
        );
    }

    if (s_wcd.hwndButtonCopy)
    {
        MoveWindow(
            s_wcd.hwndButtonCopy,
            SYS_PAD,
            buttonsY,
            SYS_BUTTON_W,
            SYS_BUTTON_H,
            TRUE
        );
    }

    if (s_wcd.hwndButtonClear)
    {
        MoveWindow(
            s_wcd.hwndButtonClear,
            SYS_PAD + SYS_BUTTON_W + SYS_BUTTON_GAP,
            buttonsY,
            SYS_BUTTON_W,
            SYS_BUTTON_H,
            TRUE
        );
    }

    if (s_wcd.hwndButtonQuit)
    {
        MoveWindow(
            s_wcd.hwndButtonQuit,
            clientW - SYS_PAD - SYS_BUTTON_W,
            buttonsY,
            SYS_BUTTON_W,
            SYS_BUTTON_H,
            TRUE
        );
    }
}

static HFONT Con_CreateUIFont(HWND hWnd, int pointSize, const char* face, int weight, int fixedPitch)
{
    HDC hDC;
    int height;
    HFONT hFont;

    hDC = GetDC(hWnd);
    if (!hDC)
    {
        return NULL;
    }

    height = -MulDiv(pointSize, GetDeviceCaps(hDC, LOGPIXELSY), 72);
    ReleaseDC(hWnd, hDC);

    hFont = CreateFontA(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        (fixedPitch ? FIXED_PITCH : VARIABLE_PITCH) | FF_DONTCARE,
        face
    );

    return hFont;
}

static void Con_DestroyResources(void)
{
    if (s_wcd.hfBufferFont)
    {
        DeleteObject(s_wcd.hfBufferFont);
        s_wcd.hfBufferFont = NULL;
    }

    if (s_wcd.hfButtonFont)
    {
        if (s_wcd.hfButtonFont != s_wcd.hfBufferFont)
        {
            DeleteObject(s_wcd.hfButtonFont);
        }
        s_wcd.hfButtonFont = NULL;
    }

    if (s_wcd.hbrEditBackground)
    {
        DeleteObject(s_wcd.hbrEditBackground);
        s_wcd.hbrEditBackground = NULL;
    }

    if (s_wcd.hbrErrorBackground)
    {
        DeleteObject(s_wcd.hbrErrorBackground);
        s_wcd.hbrErrorBackground = NULL;
    }

    if (s_wcd.hbmLogo)
    {
        DeleteObject(s_wcd.hbmLogo);
        s_wcd.hbmLogo = NULL;
    }

    if (s_wcd.hbmClearBitmap)
    {
        DeleteObject(s_wcd.hbmClearBitmap);
        s_wcd.hbmClearBitmap = NULL;
    }
}

static void Con_CreateChildWindows(HWND hWnd)
{
    s_wcd.hwndInputLine = CreateWindowExA(
        0,
        "edit",
        "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hWnd,
        (HMENU)(INT_PTR)INPUT_ID,
        g_wv.hInstance,
        NULL
    );

    s_wcd.hwndButtonCopy = CreateWindowExA(
        0,
        "button",
        "Copy",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd,
        (HMENU)(INT_PTR)COPY_ID,
        g_wv.hInstance,
        NULL
    );

    s_wcd.hwndButtonClear = CreateWindowExA(
        0,
        "button",
        "Clear",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd,
        (HMENU)(INT_PTR)CLEAR_ID,
        g_wv.hInstance,
        NULL
    );

    s_wcd.hwndButtonQuit = CreateWindowExA(
        0,
        "button",
        "Quit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd,
        (HMENU)(INT_PTR)QUIT_ID,
        g_wv.hInstance,
        NULL
    );

    s_wcd.hwndBuffer = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "edit",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 0, 0,
        hWnd,
        (HMENU)(INT_PTR)EDIT_ID,
        g_wv.hInstance,
        NULL
    );

    if (s_wcd.hfBufferFont)
    {
        SendMessageA(s_wcd.hwndBuffer, WM_SETFONT, (WPARAM)s_wcd.hfBufferFont, TRUE);
        SendMessageA(s_wcd.hwndInputLine, WM_SETFONT, (WPARAM)s_wcd.hfBufferFont, TRUE);
    }

    if (s_wcd.hfButtonFont)
    {
        SendMessageA(s_wcd.hwndButtonCopy, WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, TRUE);
        SendMessageA(s_wcd.hwndButtonClear, WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, TRUE);
        SendMessageA(s_wcd.hwndButtonQuit, WM_SETFONT, (WPARAM)s_wcd.hfButtonFont, TRUE);
    }

    s_wcd.SysInputLineWndProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(
        s_wcd.hwndInputLine,
        GWLP_WNDPROC,
        (LONG_PTR)InputLineWndProc
    );
}

static void Con_EnsureVisibleCaret(void)
{
    if (!s_wcd.hwndBuffer)
    {
        return;
    }

    SendMessageA(s_wcd.hwndBuffer, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageA(s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0);
}

static LRESULT WINAPI ConWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char* cmdString;
    static qboolean s_timePolarity = qfalse;

    switch (uMsg)
    {
    case WM_CREATE:
        s_wcd.hbrEditBackground = CreateSolidBrush(SYS_CONSOLE_BG);
        s_wcd.hbrErrorBackground = CreateSolidBrush(SYS_ERROR_BG);
        SetTimer(hWnd, 1, 1000, NULL);
        return 0;

    case WM_SIZE:
        Con_LayoutChildren(hWnd);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            if (s_wcd.hwndInputLine)
            {
                SetFocus(s_wcd.hwndInputLine);
            }
        }

        if (com_viewlog && (com_dedicated && !com_dedicated->integer))
        {
            if (com_viewlog->integer == 1)
            {
                if (HIWORD(wParam))
                {
                    Cvar_Set("viewlog", "2");
                }
            }
            else if (com_viewlog->integer == 2)
            {
                if (!HIWORD(wParam))
                {
                    Cvar_Set("viewlog", "1");
                }
            }
        }
        return 0;

    case WM_CLOSE:
        if (com_dedicated && com_dedicated->integer)
        {
            cmdString = CopyString("quit");
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, (int)strlen(cmdString) + 1, cmdString);
        }
        else if (s_wcd.quitOnClose)
        {
            PostQuitMessage(0);
        }
        else
        {
            Sys_ShowConsole(0, qfalse);
            Cvar_Set("viewlog", "0");
        }
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            s_timePolarity = !s_timePolarity;
            if (s_wcd.hwndErrorBox)
            {
                InvalidateRect(s_wcd.hwndErrorBox, NULL, FALSE);
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case COPY_ID:
            if (s_wcd.hwndBuffer)
            {
                SendMessageA(s_wcd.hwndBuffer, EM_SETSEL, 0, -1);
                SendMessageA(s_wcd.hwndBuffer, WM_COPY, 0, 0);
            }
            return 0;

        case QUIT_ID:
            if (s_wcd.quitOnClose)
            {
                PostQuitMessage(0);
            }
            else
            {
                cmdString = CopyString("quit");
                Sys_QueEvent(0, SE_CONSOLE, 0, 0, (int)strlen(cmdString) + 1, cmdString);
            }
            return 0;

        case CLEAR_ID:
            Con_ClearEdit(s_wcd.hwndBuffer);
            UpdateWindow(s_wcd.hwndBuffer);
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == s_wcd.hwndBuffer)
        {
            SetBkColor((HDC)wParam, SYS_CONSOLE_BG);
            SetTextColor((HDC)wParam, SYS_CONSOLE_TEXT);
            return (LRESULT)s_wcd.hbrEditBackground;
        }
        else if ((HWND)lParam == s_wcd.hwndErrorBox)
        {
            SetBkColor((HDC)wParam, SYS_ERROR_BG);
            SetTextColor((HDC)wParam, s_timePolarity ? SYS_ERROR_TEXT_A : SYS_ERROR_TEXT_B);
            return (LRESULT)s_wcd.hbrErrorBackground;
        }
        break;

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        Con_DestroyResources();
        return 0;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

LONG WINAPI InputLineWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char inputBuffer[1024];

    switch (uMsg)
    {
    case WM_KILLFOCUS:
        if ((HWND)wParam == s_wcd.hWnd || (HWND)wParam == s_wcd.hwndErrorBox)
        {
            SetFocus(hWnd);
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;

    case WM_CHAR:
        if (wParam == VK_RETURN)
        {
            GetWindowTextA(s_wcd.hwndInputLine, inputBuffer, sizeof(inputBuffer));

            if (inputBuffer[0])
            {
                Con_AppendLineToInputBuffer(inputBuffer);
                Sys_Print(va("]%s\n", inputBuffer));
            }

            SetWindowTextA(s_wcd.hwndInputLine, "");
            return 0;
        }
        break;
    }

    return CallWindowProcA(s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam);
}

/*
** Sys_CreateConsole
*/
void Sys_CreateConsole(void)
{
    WNDCLASSEXA wc;
    RECT rect;
    DWORD style;
    DWORD exStyle;
    int swidth, sheight;
    int wndW, wndH;
    int posX, posY;
    HDC hDC;

    if (s_wcd.hWnd)
    {
        return;
    }

    memset(&wc, 0, sizeof(wc));
    memset(&rect, 0, sizeof(rect));

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX;
    exStyle = WS_EX_APPWINDOW;

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ConWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_wv.hInstance;
    wc.hIcon = LoadIcon(g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = SYS_CONSOLE_CLASS;
    wc.hIconSm = wc.hIcon;

    RegisterClassExA(&wc);

    rect.left = 0;
    rect.top = 0;
    rect.right = SYS_CLIENT_WIDTH;
    rect.bottom = SYS_CLIENT_HEIGHT;
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    hDC = GetDC(GetDesktopWindow());
    swidth = GetDeviceCaps(hDC, HORZRES);
    sheight = GetDeviceCaps(hDC, VERTRES);
    ReleaseDC(GetDesktopWindow(), hDC);

    wndW = rect.right - rect.left;
    wndH = rect.bottom - rect.top;
    posX = (swidth - wndW) / 2;
    posY = (sheight - wndH) / 2;

    memset(&s_wcd, 0, sizeof(s_wcd));
    s_wcd.windowWidth = SYS_CLIENT_WIDTH;
    s_wcd.windowHeight = SYS_CLIENT_HEIGHT;
    s_wcd.visLevel = 0;
    s_wcd.quitOnClose = qfalse;

    s_wcd.hWnd = CreateWindowExA(
        exStyle,
        SYS_CONSOLE_CLASS,
        SYS_CONSOLE_TITLE,
        style,
        posX,
        posY,
        wndW,
        wndH,
        NULL,
        NULL,
        g_wv.hInstance,
        NULL
    );

    if (!s_wcd.hWnd)
    {
        return;
    }

    s_wcd.hfBufferFont = Con_CreateUIFont(s_wcd.hWnd, 10, "Consolas", FW_NORMAL, 1);
    if (!s_wcd.hfBufferFont)
    {
        s_wcd.hfBufferFont = Con_CreateUIFont(s_wcd.hWnd, 10, "Courier New", FW_NORMAL, 1);
    }

    s_wcd.hfButtonFont = Con_CreateUIFont(s_wcd.hWnd, 9, "Segoe UI", FW_NORMAL, 0);
    if (!s_wcd.hfButtonFont)
    {
        s_wcd.hfButtonFont = s_wcd.hfBufferFont;
    }

    Con_CreateChildWindows(s_wcd.hWnd);
    Con_LayoutChildren(s_wcd.hWnd);

    ShowWindow(s_wcd.hWnd, SW_SHOWDEFAULT);
    UpdateWindow(s_wcd.hWnd);
    SetForegroundWindow(s_wcd.hWnd);

    if (s_wcd.hwndInputLine)
    {
        SetFocus(s_wcd.hwndInputLine);
    }

    s_wcd.visLevel = 1;
}

/*
** Sys_DestroyConsole
*/
void Sys_DestroyConsole(void)
{
    if (s_wcd.hWnd)
    {
        ShowWindow(s_wcd.hWnd, SW_HIDE);
        DestroyWindow(s_wcd.hWnd);
        s_wcd.hWnd = NULL;
    }

    s_wcd.hwndBuffer = NULL;
    s_wcd.hwndButtonClear = NULL;
    s_wcd.hwndButtonCopy = NULL;
    s_wcd.hwndButtonQuit = NULL;
    s_wcd.hwndErrorBox = NULL;
    s_wcd.hwndErrorText = NULL;
    s_wcd.hwndInputLine = NULL;
    s_wcd.SysInputLineWndProc = NULL;
    s_wcd.visLevel = 0;
}

/*
** Sys_ShowConsole
*/
void Sys_ShowConsole(int visLevel, qboolean quitOnClose)
{
    s_wcd.quitOnClose = quitOnClose;

    if (visLevel == s_wcd.visLevel)
    {
        return;
    }

    s_wcd.visLevel = visLevel;

    if (!s_wcd.hWnd)
    {
        return;
    }

    switch (visLevel)
    {
    case 0:
        ShowWindow(s_wcd.hWnd, SW_HIDE);
        break;

    case 1:
        ShowWindow(s_wcd.hWnd, SW_SHOWNORMAL);
        if (s_wcd.hwndBuffer)
        {
            SendMessageA(s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0x7fffffff);
            Con_EnsureVisibleCaret();
        }
        break;

    case 2:
        ShowWindow(s_wcd.hWnd, SW_MINIMIZE);
        break;

    default:
        Sys_Error("Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel);
        break;
    }
}

/*
** Sys_ConsoleInput
*/
char* Sys_ConsoleInput(void)
{
    if (s_wcd.consoleText[0] == 0)
    {
        return NULL;
    }

    Q_strncpyz(s_wcd.returnedText, s_wcd.consoleText, sizeof(s_wcd.returnedText));
    s_wcd.consoleText[0] = 0;

    return s_wcd.returnedText;
}

/*
** Conbuf_AppendText
*/
void Conbuf_AppendText(const char* pMsg)
{
#define CONSOLE_BUFFER_SIZE 16384

    char buffer[CONSOLE_BUFFER_SIZE * 2];
    char* b;
    const char* msg;
    int bufLen;
    int i;
    static unsigned long s_totalChars = 0;

    if (!s_wcd.hwndBuffer || !pMsg)
    {
        return;
    }

    b = buffer;
    i = 0;

    if (strlen(pMsg) > CONSOLE_BUFFER_SIZE - 1)
    {
        msg = pMsg + strlen(pMsg) - CONSOLE_BUFFER_SIZE + 1;
    }
    else
    {
        msg = pMsg;
    }

    while (msg[i] && ((size_t)(b - buffer) < sizeof(buffer) - 2))
    {
        if (msg[i] == '\n' && msg[i + 1] == '\r')
        {
            b[0] = '\r';
            b[1] = '\n';
            b += 2;
            i++;
        }
        else if (msg[i] == '\r' || msg[i] == '\n')
        {
            b[0] = '\r';
            b[1] = '\n';
            b += 2;
        }
        else if (Q_IsColorString(&msg[i]))
        {
            i++;
        }
        else
        {
            *b++ = msg[i];
        }
        i++;
    }

    *b = '\0';
    bufLen = (int)(b - buffer);

    s_totalChars += (unsigned long)bufLen;

    if (s_totalChars > 0x7fff)
    {
        SendMessageA(s_wcd.hwndBuffer, EM_SETSEL, 0, -1);
        SendMessageA(s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM)"");
        s_totalChars = (unsigned long)bufLen;
    }

    SendMessageA(s_wcd.hwndBuffer, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageA(s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM)buffer);
    Con_EnsureVisibleCaret();
}

/*
** Sys_SetErrorText
*/
void Sys_SetErrorText(const char* buf)
{
    Q_strncpyz(s_wcd.errorString, buf ? buf : "", sizeof(s_wcd.errorString));

    if (!s_wcd.hWnd)
    {
        return;
    }

    if (!s_wcd.hwndErrorBox)
    {
        s_wcd.hwndErrorBox = CreateWindowExA(
            0,
            "static",
            s_wcd.errorString,
            WS_CHILD | WS_VISIBLE | SS_SUNKEN | SS_LEFTNOWORDWRAP,
            0, 0, 0, 0,
            s_wcd.hWnd,
            (HMENU)(INT_PTR)ERRORBOX_ID,
            g_wv.hInstance,
            NULL
        );

        if (s_wcd.hfBufferFont)
        {
            SendMessageA(s_wcd.hwndErrorBox, WM_SETFONT, (WPARAM)s_wcd.hfBufferFont, TRUE);
        }

        if (s_wcd.hwndInputLine)
        {
            DestroyWindow(s_wcd.hwndInputLine);
            s_wcd.hwndInputLine = NULL;
        }

        Con_LayoutChildren(s_wcd.hWnd);
    }
    else
    {
        SetWindowTextA(s_wcd.hwndErrorBox, s_wcd.errorString);
    }

    InvalidateRect(s_wcd.hwndErrorBox, NULL, TRUE);
}
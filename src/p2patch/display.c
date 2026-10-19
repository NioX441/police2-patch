/*
 * display.c - Fullscreen and window scaling.
 *
 * Problem: in fullscreen mode the game sizes its window to the desktop resolution and asks
 * Direct3D 7 for a back buffer of the same size. On current Windows versions the Direct3D 7
 * HAL device refuses render targets wider or taller than 2048 pixels (CreateDevice fails with
 * DDERR_INVALIDOBJECT), so on 1440p/4K/ultrawide screens the game aborts with
 * "Failed initializing Asphyre device".
 *
 * Fix: the game keeps working with a "logical" client size (at most 2048x2048) while the real
 * window covers the whole monitor (borderless fullscreen) or is scaled up (windowed mode).
 * The game's DirectDraw Present() blits the back buffer to the rectangle returned by
 * GetWindowInfo(), which stretches the image. The window APIs the game uses to query its size
 * and mouse positions are translated between logical and physical coordinates.
 */
#include "patch.h"
#include "game.h"
#include <math.h>

#define MAX_D3D7_RT 2048

static HWND    g_mainWnd;
static WNDPROC g_origProc;
static BOOL    g_virtual;      /* coordinate translation active        */
static BOOL    g_fullscreen;
static int     g_Lw = 1024, g_Lh = 768;   /* logical client size  */
static double  g_winScale = 1.0;

/* original user32 functions (our DLL imports them directly, so these are the real ones) */
#define REAL(fn) fn

/* ---------------------------------------------------------------------------------------- */

static void MonitorRects(HWND h, RECT *mon, RECT *work)
{
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR m = MonitorFromWindow(h ? h : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoA(m, &mi);
    if (mon) *mon = mi.rcMonitor;
    if (work) *work = mi.rcWork;
}

/* Destination rectangle of the game image inside the physical client area (client coords). */
static void DestRect(RECT *r)
{
    RECT c;
    REAL(GetClientRect)(g_mainWnd, &c);
    int cw = c.right, ch = c.bottom;
    if (g_fullscreen && g_cfg.keepAspect) {
        double s = min((double)cw / g_Lw, (double)ch / g_Lh);
        int w = (int)floor(g_Lw * s + 0.5), h = (int)floor(g_Lh * s + 0.5);
        r->left = (cw - w) / 2;
        r->top = (ch - h) / 2;
        r->right = r->left + w;
        r->bottom = r->top + h;
    } else {
        SetRect(r, 0, 0, cw, ch);
    }
    if (r->right <= r->left) r->right = r->left + 1;
    if (r->bottom <= r->top) r->bottom = r->top + 1;
}

static void ToLogical(POINT *p)
{
    RECT d;
    DestRect(&d);
    p->x = MulDiv(p->x - d.left, g_Lw, d.right - d.left);
    p->y = MulDiv(p->y - d.top, g_Lh, d.bottom - d.top);
}

static void ToPhysical(POINT *p)
{
    RECT d;
    DestRect(&d);
    p->x = d.left + MulDiv(p->x, d.right - d.left, g_Lw);
    p->y = d.top + MulDiv(p->y, d.bottom - d.top, g_Lh);
}

static BOOL IsMain(HWND h) { return g_virtual && h && h == g_mainWnd && !IsIconic(h); }

/* ---------------------------------------------------------------------------------------- */
/* Logical sizes                                                                               */
/* ---------------------------------------------------------------------------------------- */

static void ComputeFullscreenSize(void)
{
    RECT mon;
    MonitorRects(g_mainWnd, &mon, NULL);
    int mw = mon.right - mon.left, mh = mon.bottom - mon.top;
    int w, h;
    if (g_cfg.fullscreenWidth > 0) {
        w = g_cfg.fullscreenWidth;
        h = g_cfg.fullscreenHeight;
    } else {
        w = mw;
        h = mh;
        double s = max((double)w / MAX_D3D7_RT, (double)h / MAX_D3D7_RT);
        if (s > 1.0) {
            w = (int)(w / s);
            h = (int)(h / s);
        }
        /* very wide screens: keep at least 768 lines for the user interface */
        if (h < 768 && mh >= 768) {
            h = 768;
            w = min(MAX_D3D7_RT, MulDiv(768, mw, mh));
        }
    }
    if (w > MAX_D3D7_RT) { h = MulDiv(h, MAX_D3D7_RT, w); w = MAX_D3D7_RT; }
    if (h > MAX_D3D7_RT) { w = MulDiv(w, MAX_D3D7_RT, h); h = MAX_D3D7_RT; }
    g_Lw = max(640, w) & ~1;
    g_Lh = max(480, h) & ~1;
    Log("Fullscreen: monitor %dx%d, render resolution %dx%d", mw, mh, g_Lw, g_Lh);
}

static void FrameSize(HWND h, int *fw, int *fh)
{
    RECT r = { 0, 0, 100, 100 };
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
    *fw = (r.right - r.left) - 100;
    *fh = (r.bottom - r.top) - 100;
}

static void ComputeWindowScale(void)
{
    RECT work;
    MonitorRects(g_mainWnd, NULL, &work);
    int fw = 16, fh = 39;
    if (g_mainWnd) FrameSize(g_mainWnd, &fw, &fh);
    double s = g_cfg.windowScale;
    double fit = min((double)(work.right - work.left - fw) / g_Lw,
                     (double)(work.bottom - work.top - fh) / g_Lh);
    if (s <= 0.0) s = floor(fit * 4.0) / 4.0;   /* auto: largest multiple of 0.25 that fits */
    if (s > fit) s = fit;
    if (s < 1.0) s = 1.0;
    g_winScale = s;
    Log("Windowed: logical %dx%d, scale %.2f", g_Lw, g_Lh, s);
}

/* Called from TMainForm.GameInit (replaces TScreen.Width / TScreen.Height / constants). */
static int __cdecl Init_FullscreenWidth(void)
{
    g_fullscreen = TRUE;
    ComputeFullscreenSize();
    g_virtual = TRUE;
    return g_Lw;
}

static int __cdecl Init_FullscreenHeight(void) { return g_Lh; }

static int __cdecl Init_WindowWidth(void)
{
    g_fullscreen = FALSE;
    g_Lw = 1024;
    g_Lh = 768;
    ComputeWindowScale();
    g_virtual = TRUE;
    return g_Lw;
}

static int __cdecl Init_WindowHeight(void) { return g_Lh; }

/* "mov edx, imm32" replacements: return the value in edx */
static __declspec(naked) void Thunk_WindowWidth(void)
{
    __asm {
        call Init_WindowWidth
        mov  edx, eax
        ret
    }
}

static __declspec(naked) void Thunk_WindowHeight(void)
{
    __asm {
        call Init_WindowHeight
        mov  edx, eax
        ret
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Window procedure                                                                            */
/* ---------------------------------------------------------------------------------------- */

static void EnforceGeometry(HWND h, WINDOWPOS *wp)
{
    if (!g_virtual || IsIconic(h) || wp->x <= -32000) return;
    if (g_fullscreen) {
        RECT mon;
        MonitorRects(h, &mon, NULL);
        wp->x = mon.left;
        wp->y = mon.top;
        wp->cx = mon.right - mon.left;
        wp->cy = mon.bottom - mon.top;
        wp->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    } else {
        int fw, fh;
        FrameSize(h, &fw, &fh);
        wp->cx = (int)floor(g_Lw * g_winScale + 0.5) + fw;
        wp->cy = (int)floor(g_Lh * g_winScale + 0.5) + fh;
        wp->flags &= ~SWP_NOSIZE;
        if (!IsWindowVisible(h)) {   /* centre the window until it is shown */
            RECT work;
            MonitorRects(h, NULL, &work);
            wp->x = work.left + max(0, ((work.right - work.left) - wp->cx) / 2);
            wp->y = work.top + max(0, ((work.bottom - work.top) - wp->cy) / 2);
            wp->flags &= ~SWP_NOMOVE;
        }
    }
}

static LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_virtual && h == g_mainWnd) {
        switch (msg) {
        case WM_WINDOWPOSCHANGING: {
            LRESULT r = CallWindowProcW(g_origProc, h, msg, wp, lp);
            EnforceGeometry(h, (WINDOWPOS *)lp);
            return r;
        }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) lp = MAKELPARAM(g_Lw, g_Lh);
            break;
        case WM_ERASEBKGND: {
            RECT c;
            REAL(GetClientRect)(h, &c);
            FillRect((HDC)wp, &c, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return 1;
        }
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: {
            POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
            ToLogical(&p);
            if (p.x < 0) p.x = 0;
            if (p.y < 0) p.y = 0;
            if (p.x >= g_Lw) p.x = g_Lw - 1;
            if (p.y >= g_Lh) p.y = g_Lh - 1;
            lp = MAKELPARAM(p.x, p.y);
            break;
        }
        }
    }
    LRESULT r = CallWindowProcW(g_origProc, h, msg, wp, lp);
    if (msg == WM_NCDESTROY && h == g_mainWnd) g_mainWnd = NULL;
    return r;
}

/* ---------------------------------------------------------------------------------------- */
/* API hooks (import table of police2.exe)                                                     */
/* ---------------------------------------------------------------------------------------- */

static HWND WINAPI Hook_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x,
                                       int y, int w, int h, HWND parent, HMENU menu,
                                       HINSTANCE inst, LPVOID param)
{
    HWND wnd = CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (wnd && !(style & WS_CHILD)) {
        WCHAR c[64];
        if (GetClassNameW(wnd, c, 64) && lstrcmpW(c, L"TMainForm") == 0) {
            g_mainWnd = wnd;
            g_origProc = (WNDPROC)SetWindowLongW(wnd, GWL_WNDPROC, (LONG)MainWndProc);
            Log("Main window %p subclassed", wnd);
        }
    }
    return wnd;
}

static BOOL WINAPI Hook_GetClientRect(HWND h, LPRECT r)
{
    BOOL ok = REAL(GetClientRect)(h, r);
    if (ok && IsMain(h)) SetRect(r, 0, 0, g_Lw, g_Lh);
    return ok;
}

static BOOL WINAPI Hook_GetWindowRect(HWND h, LPRECT r)
{
    BOOL ok = REAL(GetWindowRect)(h, r);
    if (ok && IsMain(h)) {
        RECT c;
        REAL(GetClientRect)(h, &c);
        int fw = (r->right - r->left) - c.right, fh = (r->bottom - r->top) - c.bottom;
        r->right = r->left + fw + g_Lw;
        r->bottom = r->top + fh + g_Lh;
    }
    return ok;
}

static BOOL WINAPI Hook_ScreenToClient(HWND h, LPPOINT p)
{
    BOOL ok = REAL(ScreenToClient)(h, p);
    if (ok && IsMain(h)) ToLogical(p);
    return ok;
}

static BOOL WINAPI Hook_ClientToScreen(HWND h, LPPOINT p)
{
    if (IsMain(h)) ToPhysical(p);
    return REAL(ClientToScreen)(h, p);
}

/* used by the DirectDraw Present(): blit target = the (letterboxed) image rectangle */
static BOOL WINAPI Hook_GetWindowInfo(HWND h, PWINDOWINFO wi)
{
    BOOL ok = REAL(GetWindowInfo)(h, wi);
    if (ok && IsMain(h)) {
        RECT d;
        DestRect(&d);
        OffsetRect(&d, wi->rcClient.left, wi->rcClient.top);
        wi->rcClient = d;
    }
    return ok;
}

/* ---------------------------------------------------------------------------------------- */

BOOL Display_LogicalSize(int *w, int *h)
{
    if (!g_virtual) return FALSE;
    *w = g_Lw;
    *h = g_Lh;
    return TRUE;
}

HWND Display_MainWnd(void) { return g_virtual ? g_mainWnd : NULL; }

BOOL Display_DestRect(RECT *r)
{
    if (!g_virtual || !g_mainWnd) return FALSE;
    DestRect(r);
    return TRUE;
}

void Display_EarlyInit(void)
{
    if (!g_cfg.dpiAware) return;
    typedef BOOL (WINAPI *SetCtx_t)(HANDLE);
    HMODULE u = GetModuleHandleA("user32.dll");
    SetCtx_t setCtx = (SetCtx_t)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (setCtx && setCtx((HANDLE)-2 /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */)) {
        Log("DPI awareness: system aware");
        return;
    }
    SetProcessDPIAware();
}

void Display_Install(void)
{
    BOOL ok = TRUE;
    ok &= RedirectCall(CALL_GAMEINIT_SCREENW, 0x0052AE2C, (void *)Init_FullscreenWidth);
    ok &= RedirectCall(CALL_GAMEINIT_SCREENH, 0x0052AE24, (void *)Init_FullscreenHeight);
    static const uint8_t movW[] = { 0xBA, 0x00, 0x04, 0x00, 0x00 };   /* mov edx, 1024 */
    static const uint8_t movH[] = { 0xBA, 0x00, 0x03, 0x00, 0x00 };   /* mov edx, 768  */
    if (CheckBytes(PATCH_GAMEINIT_WINW, movW, 5) && CheckBytes(PATCH_GAMEINIT_WINH, movH, 5)) {
        PatchCall(PATCH_GAMEINIT_WINW, (void *)Thunk_WindowWidth, 5);
        PatchCall(PATCH_GAMEINIT_WINH, (void *)Thunk_WindowHeight, 5);
    } else {
        ok = FALSE;
    }
    if (!ok) {
        Log("Display: GameInit not as expected, display fix disabled");
        return;
    }
    HookIat(IAT_CreateWindowExW, "user32.dll", "CreateWindowExW", (void *)Hook_CreateWindowExW);
    HookIat(IAT_GetClientRect, "user32.dll", "GetClientRect", (void *)Hook_GetClientRect);
    HookIat(IAT_GetWindowRect, "user32.dll", "GetWindowRect", (void *)Hook_GetWindowRect);
    HookIat(IAT_ScreenToClient, "user32.dll", "ScreenToClient", (void *)Hook_ScreenToClient);
    HookIat(IAT_ClientToScreen, "user32.dll", "ClientToScreen", (void *)Hook_ClientToScreen);
    HookIat(IAT_GetWindowInfo, "user32.dll", "GetWindowInfo", (void *)Hook_GetWindowInfo);
    Log("Display fix installed");
}

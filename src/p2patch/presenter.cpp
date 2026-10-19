/*
 * presenter.cpp - High quality presentation of the game image.
 *
 * The game renders with Direct3D 7 into an off-screen back buffer and copies it to the window
 * with IDirectDrawSurface7::Blt. When the window is larger than the back buffer (fullscreen on
 * big monitors, scaled window) DirectDraw stretches with nearest neighbour sampling, which
 * looks blocky. This module replaces TDX7Device.Present: the back buffer is copied into a
 * Direct3D 11 texture and drawn to a flip-model swap chain with a selectable filter
 * (Catmull-Rom bicubic by default). If Direct3D 11 is not available the original DirectDraw
 * path is used.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>

#include "patch.h"
extern "C" HWND Display_MainWnd(void);
extern "C" BOOL Display_DestRect(RECT *r);

#include "upscale_vs.h"
#include "upscale_ps.h"

#define ADDR_DX7Device_Present 0x00593FC8
#define DEV_BACKBUFFER         0x18

struct Params { float texW, texH, scaleX, scaleY; int filter; float pad[3]; };

static ID3D11Device           *g_dev;
static ID3D11DeviceContext    *g_ctx;
static IDXGISwapChain1        *g_swap;
static ID3D11RenderTargetView *g_rtv;
static ID3D11Texture2D        *g_tex;
static ID3D11ShaderResourceView *g_srv;
static ID3D11VertexShader     *g_vs;
static ID3D11PixelShader      *g_ps;
static ID3D11SamplerState     *g_samp[2];
static ID3D11Buffer           *g_cb;
static HWND  g_wnd;
static int   g_bufW, g_bufH, g_texW, g_texH;
static BOOL  g_failed;
static BOOL  g_flipModel;

#define RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)

static void Shutdown(void)
{
    RELEASE(g_srv); RELEASE(g_tex); RELEASE(g_rtv); RELEASE(g_swap); RELEASE(g_cb);
    RELEASE(g_samp[0]); RELEASE(g_samp[1]); RELEASE(g_ps); RELEASE(g_vs);
    RELEASE(g_ctx); RELEASE(g_dev);
}

static BOOL CreateDevice(HWND wnd)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, fl, 3,
                                 D3D11_SDK_VERSION, &g_dev, NULL, &g_ctx))) {
        Log("Presenter: no Direct3D 11 device, using DirectDraw output");
        return FALSE;
    }

    IDXGIDevice *dxgiDev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    g_dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDev);
    if (dxgiDev) dxgiDev->GetAdapter(&adapter);
    if (adapter) adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);
    if (!factory) {
        RELEASE(adapter); RELEASE(dxgiDev);
        return FALSE;
    }

    RECT c;
    GetClientRect(wnd, &c);   /* real API: our DLL is not hooked */
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = max(1, c.right);
    sd.Height = max(1, c.bottom);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = factory->CreateSwapChainForHwnd(g_dev, wnd, &sd, NULL, NULL, &g_swap);
    g_flipModel = SUCCEEDED(hr);
    if (FAILED(hr)) {   /* Windows 8.x and older */
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.BufferCount = 1;
        hr = factory->CreateSwapChainForHwnd(g_dev, wnd, &sd, NULL, NULL, &g_swap);
    }
    if (SUCCEEDED(hr)) factory->MakeWindowAssociation(wnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    RELEASE(factory); RELEASE(adapter); RELEASE(dxgiDev);
    if (FAILED(hr)) {
        Log("Presenter: CreateSwapChainForHwnd failed (%08X)", hr);
        return FALSE;
    }
    g_bufW = sd.Width;
    g_bufH = sd.Height;

    if (FAILED(g_dev->CreateVertexShader(g_VSMain, sizeof(g_VSMain), NULL, &g_vs)) ||
        FAILED(g_dev->CreatePixelShader(g_PSMain, sizeof(g_PSMain), NULL, &g_ps)))
        return FALSE;

    D3D11_SAMPLER_DESC smp = {};
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    g_dev->CreateSamplerState(&smp, &g_samp[0]);
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    g_dev->CreateSamplerState(&smp, &g_samp[1]);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Params);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateBuffer(&bd, NULL, &g_cb))) return FALSE;

    Log("Presenter: Direct3D 11 output active (%s, filter %d)",
        g_flipModel ? "flip model" : "blt model", g_cfg.filter);
    return TRUE;
}

static BOOL EnsureTargets(void)
{
    RECT c;
    GetClientRect(g_wnd, &c);
    int w = max(1, c.right), h = max(1, c.bottom);
    if (g_rtv && w == g_bufW && h == g_bufH) return TRUE;
    RELEASE(g_rtv);
    if (w != g_bufW || h != g_bufH) {
        g_ctx->OMSetRenderTargets(0, NULL, NULL);
        if (FAILED(g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return FALSE;
        g_bufW = w;
        g_bufH = h;
    }
    ID3D11Texture2D *bb = NULL;
    if (FAILED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) return FALSE;
    HRESULT hr = g_dev->CreateRenderTargetView(bb, NULL, &g_rtv);
    bb->Release();
    return SUCCEEDED(hr);
}

static BOOL EnsureTexture(int w, int h)
{
    if (g_tex && w == g_texW && h == g_texH) return TRUE;
    RELEASE(g_srv);
    RELEASE(g_tex);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateTexture2D(&td, NULL, &g_tex))) return FALSE;
    if (FAILED(g_dev->CreateShaderResourceView(g_tex, NULL, &g_srv))) return FALSE;
    g_texW = w;
    g_texH = h;
    return TRUE;
}

/* IDirectDrawSurface7 vtable: Lock = 25, Unlock = 32 */
typedef HRESULT (WINAPI *Lock_t)(void *, RECT *, void *, DWORD, HANDLE);
typedef HRESULT (WINAPI *Unlock_t)(void *, RECT *);

static BOOL CopyBackBuffer(void *surf)
{
    uint8_t desc[0x7C] = { 0 };
    *(DWORD *)desc = sizeof(desc);
    void **vt = *(void ***)surf;
    if (FAILED(((Lock_t)vt[25])(surf, NULL, desc, 0x1 /*WAIT*/ | 0x10 /*READONLY*/, NULL)))
        return FALSE;
    int h = *(int *)(desc + 0x08), w = *(int *)(desc + 0x0C), pitch = *(int *)(desc + 0x10);
    int bpp = *(int *)(desc + 0x48 + 0x0C);
    const uint8_t *src = *(const uint8_t **)(desc + 0x24);
    BOOL ok = FALSE;
    if (bpp == 32 && src && EnsureTexture(w, h)) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            for (int y = 0; y < h; y++)
                memcpy((uint8_t *)m.pData + y * m.RowPitch, src + y * pitch, w * 4);
            g_ctx->Unmap(g_tex, 0);
            ok = TRUE;
        }
    }
    ((Unlock_t)vt[32])(surf, NULL);
    return ok;
}

/* returns 1/0 = presented (success/failure), -1 = not handled (use DirectDraw) */
static int __cdecl Present(uint8_t *device)
{
    if (g_failed) return -1;
    HWND wnd = Display_MainWnd();
    void *bb = FIELD(device, DEV_BACKBUFFER, void *);
    if (!wnd || !bb) return -1;
    if (wnd != g_wnd) {               /* first frame or window recreated */
        Shutdown();
        g_wnd = wnd;
        if (!CreateDevice(wnd)) {
            Shutdown();
            g_failed = TRUE;
            return -1;
        }
    }
    if (IsIconic(wnd)) return 1;
    if (!EnsureTargets() || !CopyBackBuffer(bb)) return -1;

    RECT d;
    if (!Display_DestRect(&d)) SetRect(&d, 0, 0, g_bufW, g_bufH);

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        Params *p = (Params *)m.pData;
        p->texW = (float)g_texW;
        p->texH = (float)g_texH;
        p->scaleX = (float)(d.right - d.left) / g_texW;
        p->scaleY = (float)(d.bottom - d.top) / g_texH;
        p->filter = g_cfg.filter;
        g_ctx->Unmap(g_cb, 0);
    }

    static const float black[4] = { 0, 0, 0, 1 };
    g_ctx->OMSetRenderTargets(1, &g_rtv, NULL);
    g_ctx->ClearRenderTargetView(g_rtv, black);
    D3D11_VIEWPORT vp = { (float)d.left, (float)d.top, (float)(d.right - d.left),
                          (float)(d.bottom - d.top), 0.0f, 1.0f };
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->IASetInputLayout(NULL);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, NULL, 0);
    g_ctx->PSSetShader(g_ps, NULL, 0);
    g_ctx->PSSetShaderResources(0, 1, &g_srv);
    g_ctx->PSSetSamplers(0, 2, g_samp);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->Draw(3, 0);

    HRESULT hr = g_swap->Present(g_cfg.vsync ? 1 : 0, 0);
    if (FAILED(hr)) {
        Log("Presenter: Present failed (%08X), switching back to DirectDraw output", hr);
        Shutdown();
        g_failed = TRUE;
        return -1;
    }
    return 1;
}

/* Trampoline: the first 6 bytes of TDX7Device.Present followed by a jump back */
static __declspec(naked) void Orig_Present(void)
{
    __asm {
        push ebx
        push esi
        push edi
        add  esp, -0x4C
        push ADDR_DX7Device_Present + 6
        ret
    }
}

static __declspec(naked) void Thunk_Present(void)
{
    __asm {
        push eax
        call Present
        pop  ecx
        cmp  eax, -1
        je   fallback
        ret
    fallback:
        mov  eax, ecx
        jmp  Orig_Present
    }
}

extern "C" void Presenter_Install(void)
{
    if (g_cfg.filter < 0) return;     /* Filter=directdraw */
    static const uint8_t prologue[] = { 0x53, 0x56, 0x57, 0x83, 0xC4, 0xB4 };
    if (CheckBytes(ADDR_DX7Device_Present, prologue, sizeof(prologue)) &&
        PatchJmp(ADDR_DX7Device_Present, (void *)Thunk_Present, sizeof(prologue)))
        Log("Presenter installed");
}

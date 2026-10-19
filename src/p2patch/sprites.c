/*
 * sprites.c - Correct visibility test of the Asphyre sprite engine.
 *
 * TSprite.Draw only draws a sprite when it is inside the visible area. The original test is
 *     X > WorldX - Width/2 - (VisibleWidth/2 - 256)/Zoom      (left)
 *     X < WorldX - Width/2 + (VisibleWidth + 256)/Zoom        (right)
 * where WorldX is the world position of the LEFT screen edge and X the top-left corner of
 * the sprite. Large sprites such as shadows and building overlays were dropped although
 * their right part was still on screen (and vice versa at the right edge), so shadows popped
 * in a few pixels away from the window border. The test below only culls sprites that are
 * certainly invisible, taking the sprite size, offsets and scaling into account.
 */
#include "patch.h"
#include "game.h"
#include <math.h>

BOOL Display_LogicalSize(int *w, int *h);

#define CULL_MARGIN 128.0f

static int __cdecl SpriteVisible(const uint8_t *spr)
{
    if (g_cfg.fixSpriteCulling == 2) return 1;   /* debug: draw everything */
    const uint8_t *eng = FIELD(spr, SPR_ENGINE, const uint8_t *);
    float zoom = FIELD(eng, ENG_ZOOM, float);
    if (!(zoom > 0.0001f)) zoom = 1.0f;

    /* scale and draw offsets only exist in TSpriteEx and descendants: check the instance size */
    int instSize = *(const int *)(*(const uint8_t **)spr - 52);   /* VMT: vmtInstanceSize */
    BOOL ex = instSize >= SPR_DRAW_OFS_Y + 4;
    float sx = ex ? fabsf(FIELD(spr, SPR_SCALE_X, float)) : 1.0f;
    float sy = ex ? fabsf(FIELD(spr, SPR_SCALE_Y, float)) : 1.0f;
    if (!(sx >= 1.0f) || sx > 64.0f) sx = sx > 64.0f ? 64.0f : 1.0f;
    if (!(sy >= 1.0f) || sy > 64.0f) sy = sy > 64.0f ? 64.0f : 1.0f;

    float w = (float)FIELD(spr, SPR_WIDTH, int) * sx;
    float h = (float)FIELD(spr, SPR_HEIGHT, int) * sy;
    /* drawing position as used by TSpriteEx.DoDraw: X + OffsetX + DrawOffsetX (+0x118) */
    float x = FIELD(spr, SPR_X, float) + FIELD(spr, SPR_OFFSET_X, float);
    float y = FIELD(spr, SPR_Y, float) + FIELD(spr, SPR_OFFSET_Y, float);
    if (ex) {
        x += FIELD(spr, SPR_DRAW_OFS_X, float);
        y += FIELD(spr, SPR_DRAW_OFS_Y, float);
    }
    if (!(x == x) || !(y == y)) return 1;   /* NaN: let the game decide */

    float left   = FIELD(eng, ENG_WORLD_X, float);
    float top    = FIELD(eng, ENG_WORLD_Y, float);
    /* The sprite engines use VisibleWidth/Height = client size + up to 1024 pixels margin
       (to compensate the wrong original test). With a correct test the real client size is
       enough - this avoids drawing up to 3x more sprites when the static layers are redrawn
       while the camera moves. */
    int cw, ch;
    if (!Display_LogicalSize(&cw, &ch)) {
        cw = FIELD(eng, ENG_VISIBLE_W, int);
        ch = FIELD(eng, ENG_VISIBLE_H, int);
    }
    float right  = left + (float)cw / zoom;
    float bottom = top + (float)ch / zoom;
    float m = CULL_MARGIN / zoom;

    /* The image covers at most [x - w, x + 2w] x [y - h, y + 2h] (centred, top-left or
       bottom anchored); everything that might touch the screen is drawn. */
    if (x + 2 * w < left - m || x - w > right + m) return 0;
    if (y + 2 * h < top - m || y - h > bottom + m) return 0;
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* Interpolation for frames drawn between two game frames (RenderFPS > GameFPS)              */
/*                                                                                            */
/* Moving sprites of the dynamic layer (cars, people) are drawn at the position between the   */
/* last two game frames that matches the time of the frame - one game frame (33 ms) behind,   */
/* but smooth. Their real position is restored right after drawing.                          */
/* ------------------------------------------------------------------------------------------ */

#define FORM_ENGINE_DYNAMIC 0x4A8
#define INTERP_SLOTS        8192
#define INTERP_MAX_STEP     200.0f      /* larger jumps are not interpolated (teleports) */

typedef struct {
    const uint8_t *spr;
    unsigned tick;
    float prevX, prevY, curX, curY, saveX, saveY;
    int active;
} InterpEntry;

static InterpEntry g_interp[INTERP_SLOTS];
static unsigned g_tick = 1;
static BOOL     g_logicFrame, g_interpOn;
static float    g_alpha;
static const uint8_t *g_dynEngine;

void Interp_SetFrame(BOOL logicTick, float alpha, void *form)
{
    g_interpOn = TRUE;
    g_logicFrame = logicTick;
    if (logicTick) g_tick++;
    g_alpha = alpha;
    g_dynEngine = form ? FIELD(form, FORM_ENGINE_DYNAMIC, const uint8_t *) : NULL;
}

static InterpEntry *InterpLookup(const uint8_t *spr)
{
    unsigned h = ((uintptr_t)spr >> 4) * 2654435761u;
    for (int i = 0; i < 16; i++) {
        InterpEntry *e = &g_interp[(h + i) & (INTERP_SLOTS - 1)];
        if (e->spr == spr) return e;
        if (!e->spr || e->tick + 2 < g_tick) {              /* free or stale slot */
            e->spr = spr;
            e->tick = 0;
            e->active = 0;
            return e;
        }
    }
    return NULL;
}

static void __cdecl InterpBegin(uint8_t *spr)
{
    if (!g_interpOn || FIELD(spr, SPR_ENGINE, const uint8_t *) != g_dynEngine) return;
    InterpEntry *e = InterpLookup(spr);
    if (!e) return;
    float x = FIELD(spr, SPR_X, float), y = FIELD(spr, SPR_Y, float);
    if (g_logicFrame && e->tick == g_tick - 1) {            /* moved by this game frame */
        e->prevX = e->curX; e->prevY = e->curY;
        e->curX = x; e->curY = y;
        e->tick = g_tick;
    } else if (e->tick != g_tick || x != e->curX || y != e->curY) {
        e->prevX = e->curX = x;                            /* new or moved otherwise */
        e->prevY = e->curY = y;
        e->tick = g_tick;
    }
    float dx = e->curX - e->prevX, dy = e->curY - e->prevY;
    if (dx == 0.0f && dy == 0.0f) return;
    if (fabsf(dx) > INTERP_MAX_STEP || fabsf(dy) > INTERP_MAX_STEP) return;
    e->saveX = x;
    e->saveY = y;
    e->active = 1;
    FIELD(spr, SPR_X, float) = e->prevX + dx * g_alpha;
    FIELD(spr, SPR_Y, float) = e->prevY + dy * g_alpha;
}

static void __cdecl InterpEnd(uint8_t *spr)
{
    if (!g_interpOn || FIELD(spr, SPR_ENGINE, const uint8_t *) != g_dynEngine) return;
    InterpEntry *e = InterpLookup(spr);
    if (!e || !e->active) return;
    FIELD(spr, SPR_X, float) = e->saveX;
    FIELD(spr, SPR_Y, float) = e->saveY;
    e->active = 0;
}

/* visible path of TSprite.Draw: DoDraw with the interpolated position */
static __declspec(naked) void Thunk_DrawVisible(void)
{
    __asm {
        push ebx
        call InterpBegin
        add  esp, 4
        mov  eax, ebx
        mov  edx, [eax]
        call dword ptr [edx + 4]            /* DoDraw */
        push ebx
        call InterpEnd
        add  esp, 4
        push PATCH_CULL_DRAW + 7
        ret
    }
}

static __declspec(naked) void Thunk_Cull(void)
{
    __asm {
        push ebx                 /* ebx = TSprite */
        call SpriteVisible
        add  esp, 4
        test eax, eax
        jz   invisible
        jmp  Thunk_DrawVisible
    invisible:
        push PATCH_CULL_SKIP
        ret
    }
}

void Sprites_Install(void)
{
    if (!g_cfg.fixSpriteCulling) return;
    if (PatchJmp(PATCH_CULL_BEGIN, (void *)Thunk_Cull, 5))
        Log("Sprite culling fix installed");
}

/* ------------------------------------------------------------------------------------------ */
/* Seams between map tiles when zoomed                                                         */
/*                                                                                             */
/* The DX7 canvas (0x00594854, called whenever the render states are reset) enables bilinear   */
/* texture filtering but leaves the texture addressing at its default WRAP. At zoom factors    */
/* other than 1 the filter therefore mixes texels of the opposite texture border into the      */
/* edge pixels of every tile - visible as thin lines between map tiles, best seen at night     */
/* when the darkness layer is drawn. CLAMP addressing removes them.                            */
/* ------------------------------------------------------------------------------------------ */

#define ADDR_DX7Canvas_ResetStates 0x00594854
#define ADDR_D3DDEVICE7_VAR        0x00626744   /* IDirect3DDevice7* */

typedef HRESULT (WINAPI *SetTSS_t)(void *dev, DWORD stage, DWORD type, DWORD value);

static void __cdecl SetClampAddressing(void)
{
    void *dev = *(void **)ADDR_D3DDEVICE7_VAR;
    if (!dev) return;
    SetTSS_t setTss = (SetTSS_t)(*(void ***)dev)[37];   /* IDirect3DDevice7::SetTextureStageState */
    setTss(dev, 0, 13 /* D3DTSS_ADDRESSU */, 3 /* D3DTADDRESS_CLAMP */);
    setTss(dev, 0, 14 /* D3DTSS_ADDRESSV */, 3);
}

/* trampoline: original first instructions (push ebx; mov ebx, [0x626e18]), then continue */
static __declspec(naked) void Orig_ResetStates(void)
{
    __asm {
        push ebx
        mov  ebx, dword ptr ds:[0x00626E18]
        push ADDR_DX7Canvas_ResetStates + 7
        ret
    }
}

static __declspec(naked) void Thunk_ResetStates(void)
{
    __asm {
        call Orig_ResetStates
        push eax
        call SetClampAddressing
        pop  eax
        ret
    }
}

void TileSeams_Install(void)
{
    static const uint8_t prologue[] = { 0x53, 0x8B, 0x1D, 0x18, 0x6E, 0x62, 0x00 };
    if (CheckBytes(ADDR_DX7Canvas_ResetStates, prologue, sizeof(prologue)) &&
        PatchJmp(ADDR_DX7Canvas_ResetStates, (void *)Thunk_ResetStates, sizeof(prologue)))
        Log("Tile seam fix installed");
}

/* ------------------------------------------------------------------------------------------ */
/* Shadows missing along the bottom screen edge                                                */
/*                                                                                             */
/* Buildings, trees and other static objects are pre-rendered into a screen-sized render       */
/* target (layer 2, render target 4). TMainForm.RenderFromBGLayer_2 draws that layer twice:    */
/* first darkened and moved up by 25 pixels (this IS the shadow), then normally on top. The    */
/* shadow copy can only contain what is inside the layer, so the bottom 25 pixels of the       */
/* screen never get shadows - objects below the screen edge would have to cast them.          */
/*                                                                                             */
/* Fix: the layer is rendered a second time into an own render target with the camera 25      */
/* pixels lower, and this target is drawn without the offset. Same image, but complete.        */
/* ------------------------------------------------------------------------------------------ */

#define ADDR_RenderTargets_VAR      0x0062710C   /* ^TAsphyreRenderTargets               */
#define ADDR_RenderTargets_Add      0x005633AC
#define ADDR_RenderTargets_GetTex   0x005631C8
#define ADDR_Device_RenderTo        0x0054072C
#define ADDR_RenderTOBGLayer_2      0x005F5D24
#define CALL_RENDERTO_LAYER2        0x005F4D8C   /* RenderTo(.., RT 4, RenderTOBGLayer_2)  */
#define CALL_GETTEX_SHADOW          0x005F566A   /* GetTexture(4) before the shadow draw   */
#define PUSH_SHADOW_TOP             0x005F5687   /* push -25.0 (Top of the shadow quad)    */
#define FORM_ENGINE_LAYER2          0x4A4
#define SHADOW_OFFSET               25.0f

static void *g_shadowTex;      /* own render target, NULL until created */
static int   g_shadowIdx = -1;

static void *RenderTargets(void) { return **(void ***)ADDR_RenderTargets_VAR; }

/* function TAsphyreRenderTargets.Add(Count, Width, Height, Format, DepthStencil,
   Mipmapping, Antialias): Integer - stack layout taken from the call in GameInit */
static int RT_Add(void *list, int w, int h)
{
    int r;
    __asm {
        push h
        push 2
        push 0
        push 0
        push 0
        mov  ecx, w
        mov  edx, 1
        mov  eax, list
        mov  ebx, ADDR_RenderTargets_Add
        call ebx
        mov  r, eax
    }
    return r;
}

static void *RT_Get(void *list, int idx)
{
    void *r;
    __asm {
        mov  edx, idx
        mov  eax, list
        mov  ecx, ADDR_RenderTargets_GetTex
        call ecx
        mov  r, eax
    }
    return r;
}

/* procedure TAsphyreDevice.RenderTo(Handler: TNotifyEvent; Background: Cardinal;
   FillBk: Boolean; Texture) - as called in TMainForm.TimerEvent */
static void RenderTo(void *device, void *tex, uintptr_t code, void *data)
{
    __asm {
        push data
        push code
        push tex
        mov  cl, 1
        xor  edx, edx
        mov  eax, device
        mov  ebx, ADDR_Device_RenderTo
        call ebx
    }
}

static BOOL ShadowTargetValid(void)
{
    if (!g_shadowTex || g_shadowIdx < 0) return FALSE;
    void *list = RenderTargets();
    void **arr = list ? *(void ***)((uint8_t *)list + 4) : NULL;
    if (!arr) return FALSE;
    int n = ((int *)arr)[-1];                 /* dynamic array length */
    return g_shadowIdx < n && arr[g_shadowIdx] == g_shadowTex;
}

static void __cdecl RenderShadowLayer(uint8_t *form)
{
    static int failed;
    if (failed) return;
    if (!ShadowTargetValid()) {
        int w, h;
        void *list = RenderTargets();
        if (!list || !Display_LogicalSize(&w, &h)) return;
        g_shadowIdx = RT_Add(list, w, h);
        g_shadowTex = g_shadowIdx >= 0 ? RT_Get(list, g_shadowIdx) : NULL;
        if (!g_shadowTex) {
            failed = 1;
            Log("Shadow edge fix: render target %dx%d could not be created", w, h);
            return;
        }
        /* the shadow quad is drawn without offset from now on */
        static const uint8_t zero[4] = { 0, 0, 0, 0 };
        WriteMem(PUSH_SHADOW_TOP + 1, zero, 4);
        Log("Shadow edge fix: render target %d (%dx%d) created", g_shadowIdx, w, h);
    }
    uint8_t *eng = FIELD(form, FORM_ENGINE_LAYER2, uint8_t *);
    float zoom = FIELD(eng, ENG_ZOOM, float);
    if (!(zoom > 0.0001f)) zoom = 1.0f;
    float y = FIELD(eng, ENG_WORLD_Y, float);
    FIELD(eng, ENG_WORLD_Y, float) = y + SHADOW_OFFSET / zoom;
    RenderTo(**(void ***)0x00626EC4, g_shadowTex, ADDR_RenderTOBGLayer_2, form);
    FIELD(eng, ENG_WORLD_Y, float) = y;
}

/* replaces "call TAsphyreDevice.RenderTo" for layer 2: [esp+4] texture, +8 code, +12 data */
static __declspec(naked) void Thunk_RenderLayer2(void)
{
    __asm {
        push ebx                           /* callee-saved in Delphi */
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        mov  ebx, ADDR_Device_RenderTo
        call ebx
        pop  ebx
        push dword ptr [esp + 12]          /* form */
        call RenderShadowLayer
        add  esp, 4
        ret  12
    }
}

/* replaces "call TAsphyreRenderTargets.GetTexture" (index 4) before the shadow draw */
static __declspec(naked) void Thunk_GetShadowTexture(void)
{
    __asm {
        push eax
        push edx
        call ShadowTargetValid
        test eax, eax
        pop  edx
        pop  eax
        jz   original
        mov  eax, g_shadowTex
        ret
    original:
        mov  ecx, ADDR_RenderTargets_GetTex
        jmp  ecx
    }
}

void ShadowEdge_Install(void)
{
    static const uint8_t push25[] = { 0x68, 0x00, 0x00, 0xC8, 0xC1 };
    if (!g_cfg.fixSpriteCulling) return;
    if (!CheckBytes(PUSH_SHADOW_TOP, push25, sizeof(push25))) return;
    if (RedirectCall(CALL_RENDERTO_LAYER2, ADDR_Device_RenderTo, (void *)Thunk_RenderLayer2) &&
        RedirectCall(CALL_GETTEX_SHADOW, ADDR_RenderTargets_GetTex, (void *)Thunk_GetShadowTexture))
        Log("Shadow edge fix installed");
}

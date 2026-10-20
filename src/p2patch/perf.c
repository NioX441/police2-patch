/*
 * perf.c - Performance fixes.
 *
 * The game logic runs once per rendered frame, so every slow frame slows the whole simulation
 * down (cars and officers move slower while the camera is dragged).
 *
 * 1) Screen-to-cell lookup (0x006109E8): converts the mouse position into the map cell under
 *    the cursor. The original tests all 288 x 288 = 82,944 cells (square root and four
 *    roundings per cell) and is called several times per frame from the HUD (once for each
 *    station, camera and speed camera) - about 70 % of the frame time on a large city.
 *    The replacement inverts the isometric projection and only tests the 9 x 9 cells around
 *    the result (same distance metric, threshold and tie-breaking order at zoom 1). When
 *    zoomed out, the original searched in zoom-scaled cell indices that could not reach the
 *    right and bottom part of the map (no selection square there); the replacement works
 *    in map coordinates at every zoom level.
 *
 * 2) Frame pacing: the Asphyre timer uses Sleep() with the default 15.6 ms system timer
 *    resolution, which produces frame intervals between 17 and 35 ms. A 1 ms timer resolution
 *    makes the pacing (and therefore the game speed) even.
 */
#include "patch.h"
#include "game.h"
#include <math.h>
#include <mmsystem.h>

#define ADDR_ScreenToCell   0x006109E8
#define ADDR_MAINFORM_PTR   0x00626C10   /* -> TMainForm variable */
#define MAINFORM_ENGINE1    0x4A8        /* TSpriteEngine of the city layer */
#define ADDR_MAP_OFFSET_X   0x00A9E69C
#define ADDR_MAP_OFFSET_Y   0x00A9E6A0
#define ADDR_HOVER_X        0x00A9E6C0
#define ADDR_HOVER_Y        0x00A9E6C4
#define PATCH_HUD_ZOOM_CHECK 0x005F9C10   /* RenderTOHud: jb (zoom < 0.75: no selection square) */
#define PATCH_CLICK_ZOOM_CHECK 0x005F16BD /* FormMouseDown: jb (zoom < 0.75: map clicks ignored) */

static __inline int RoundD(double v) { return (int)lrint(v); }   /* banker's rounding like Delphi */

static int __cdecl ScreenToCell(int mx, int my, int *cellX, int setHover, int *cellY)
{
    uint8_t *form = **(uint8_t ***)ADDR_MAINFORM_PTR;
    uint8_t *eng = FIELD(form, MAINFORM_ENGINE1, uint8_t *);
    double z = FIELD(eng, ENG_ZOOM, float);
    if (!(z > 0.01)) z = 1.0;
    int best = 60;
    /* world position under the mouse */
    double tx = mx / z + *(int *)ADDR_MAP_OFFSET_X;
    double ty = my / z + *(int *)ADDR_MAP_OFFSET_Y;
    int found = 0;

    if ((char)setHover) {
        *(int *)ADDR_HOVER_X = -1;
        *(int *)ADDR_HOVER_Y = -1;
    }

    /* continuous inverse of  px = 32*(x - y + 1),  py = 16*(x + y + 1) */
    double u = tx / 32.0, v = ty / 16.0;
    int cx = (int)floor((u + v) * 0.5 - 1.0 + 0.5);
    int cy = (int)floor((v - u) * 0.5 + 0.5);
    int x0 = max(0, cx - 4), x1 = min(MAP_SIZE - 1, cx + 4);
    int y0 = max(0, cy - 4), y1 = min(MAP_SIZE - 1, cy + 4);

    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            int px = 32 * (x - y + 1);
            int py = 16 * (x + y + 1);
            double dx = px - tx, dy = py - ty;
            int d = RoundD(sqrt(dx * dx + dy * dy));
            if (d < best) {
                best = d;
                *cellX = x;
                *cellY = y;
                found = 1;
                if ((char)setHover) {
                    *(int *)ADDR_HOVER_X = x;
                    *(int *)ADDR_HOVER_Y = y;
                }
            }
        }
    }
    return found;
}

/* Delphi: function(mx, my: Integer; var cx: Integer {eax, edx, ecx}; flag: Boolean; var cy) */
static __declspec(naked) void Thunk_ScreenToCell(void)
{
    __asm {
        push dword ptr [esp + 8]     /* cy pointer  */
        push dword ptr [esp + 8]     /* flag        */
        push ecx                     /* cx pointer  */
        push edx                     /* my          */
        push eax                     /* mx          */
        call ScreenToCell
        add  esp, 20
        ret  8
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Frame limiter                                                                               */
/* ---------------------------------------------------------------------------------------- */

#define ADDR_TimerEvent          0x005F454C   /* TMainForm.TimerEvent (one game tick + frame)  */
#define PATCH_SET_TIMEREVENT     0x005F305F   /* GameInit: mov [eax+18h], offset TimerEvent    */
#define PATCH_ASPHYRE_SLEEP_JAE  0x005920A9   /* TAsphyreTimer idle: jae (skip the sleep)       */

static LARGE_INTEGER g_freq, g_next;

static void __cdecl WaitNextFrame(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG period = g_freq.QuadPart / g_cfg.gameFps;
    if (g_next.QuadPart == 0 || now.QuadPart - g_next.QuadPart > period * 4) {
        g_next.QuadPart = now.QuadPart;           /* first frame or far behind (loading) */
    } else {
        for (;;) {
            LONGLONG left = g_next.QuadPart - now.QuadPart;
            if (left <= 0) break;
            DWORD ms = (DWORD)(left * 1000 / g_freq.QuadPart);
            if (ms > 1) Sleep(ms - 1);
            else YieldProcessor();
            QueryPerformanceCounter(&now);
        }
    }
    g_next.QuadPart += period;
    Gameplay_Tick();
}

/* TNotifyEvent: eax = Self (TMainForm), edx = Sender */
static __declspec(naked) void Thunk_TimerEvent(void)
{
    __asm {
        push eax
        push edx
        call WaitNextFrame
        pop  edx
        pop  eax
        mov  ecx, ADDR_TimerEvent
        jmp  ecx
    }
}

static void InstallFrameLimiter(void)
{
    if (g_cfg.gameFps <= 0) return;
    static const uint8_t setEvent[] = { 0xC7, 0x40, 0x18, 0x4C, 0x45, 0x5F, 0x00 };
    static const uint8_t jae[] = { 0x73, 0x21 };
    if (!CheckBytes(PATCH_SET_TIMEREVENT, setEvent, sizeof(setEvent)) ||
        !CheckBytes(PATCH_ASPHYRE_SLEEP_JAE, jae, sizeof(jae)))
        return;
    QueryPerformanceFrequency(&g_freq);
    uint32_t thunk = (uint32_t)(uintptr_t)Thunk_TimerEvent;
    WriteMem(PATCH_SET_TIMEREVENT + 3, &thunk, 4);
    static const uint8_t jmp[] = { 0xEB, 0x21 };      /* never use the imprecise Asphyre sleep */
    WriteMem(PATCH_ASPHYRE_SLEEP_JAE, jmp, sizeof(jmp));
    Log("Frame limiter: %d game frames per second", g_cfg.gameFps);
}

void Perf_Install(void)
{
    timeBeginPeriod(1);
    InstallFrameLimiter();
    static const uint8_t prologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xCC, 0x53, 0x56, 0x57 };
    if (CheckBytes(ADDR_ScreenToCell, prologue, sizeof(prologue)) &&
        PatchJmp(ADDR_ScreenToCell, (void *)Thunk_ScreenToCell, 5)) {
        Log("Performance fixes installed");
        /* The HUD skipped the selection square below zoom 0.75 (the original lookup did not
           work there); with the new lookup it is drawn at every zoom level. */
        static const uint8_t jb[] = { 0x0F, 0x82, 0x30, 0x16, 0x00, 0x00 };
        static const uint8_t nop6[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        if (CheckBytes(PATCH_HUD_ZOOM_CHECK, jb, sizeof(jb)))
            WriteMem(PATCH_HUD_ZOOM_CHECK, nop6, sizeof(nop6));
        /* Same for clicks on the map (patrol routes, checkpoints, selecting units). */
        static const uint8_t jbClick[] = { 0x0F, 0x82, 0xE9, 0x16, 0x00, 0x00 };
        if (CheckBytes(PATCH_CLICK_ZOOM_CHECK, jbClick, sizeof(jbClick)))
            WriteMem(PATCH_CLICK_ZOOM_CHECK, nop6, sizeof(nop6));
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Camera limits when zoomed out                                                            */
/*                                                                                          */
/* TMainForm.SetWorldPosition keeps the camera inside the city with "x <= 3105 - ClientWidth" */
/* and "y <= 7675 - ClientHeight", ignoring the zoom factor. Zoomed out, the visible area is  */
/* ClientWidth / zoom wide, so near the right and bottom edge of the city the view showed a  */
/* black area beyond the end of the map, with buildings and shadows cut off at a straight    */
/* line. The client size used for these limits is now divided by the zoom factor.           */
/* ---------------------------------------------------------------------------------------- */

#define ADDR_GetClientWidth   0x00491520
#define ADDR_GetClientHeight  0x00491560

static int __cdecl ScaleByZoom(int size, const uint8_t *form)
{
    const uint8_t *eng = FIELD(form, MAINFORM_ENGINE1, const uint8_t *);
    float z = eng ? FIELD(eng, ENG_ZOOM, float) : 1.0f;
    if (!(z > 0.05f) || z >= 1.0f) return size;
    return (int)(size / z + 0.5f);
}

static __declspec(naked) void Thunk_ZoomedClientWidth(void)
{
    __asm {
        push eax
        mov  ecx, ADDR_GetClientWidth
        call ecx
        pop  ecx
        push ecx
        push eax
        call ScaleByZoom
        add  esp, 8
        ret
    }
}

static __declspec(naked) void Thunk_ZoomedClientHeight(void)
{
    __asm {
        push eax
        mov  ecx, ADDR_GetClientHeight
        call ecx
        pop  ecx
        push ecx
        push eax
        call ScaleByZoom
        add  esp, 8
        ret
    }
}

void CameraLimits_Install(void)
{
    BOOL ok = RedirectCall(0x005F5308, ADDR_GetClientWidth, (void *)Thunk_ZoomedClientWidth) &&
              RedirectCall(0x005F531A, ADDR_GetClientWidth, (void *)Thunk_ZoomedClientWidth) &&
              RedirectCall(0x005F5338, ADDR_GetClientHeight, (void *)Thunk_ZoomedClientHeight) &&
              RedirectCall(0x005F534A, ADDR_GetClientHeight, (void *)Thunk_ZoomedClientHeight);
    if (ok) Log("Camera limit fix installed");
}

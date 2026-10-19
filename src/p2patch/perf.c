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

static LARGE_INTEGER g_freq, g_next, g_logicNext;
static int g_renderFps;                 /* 0 = one frame per game tick (original) */
static volatile LONG g_skipLogic;       /* read by Thunk_LogicGate */
static void *g_form;

#define ADDR_HUD_BLINK      0x00A9E641  /* byte counter, +1/-1 per TimerEvent call */
#define ADDR_HUD_BLINK_DIR  0x00A9E642

/* Waits for the next frame and decides whether it is a game tick (logic + render) or a
   render-only frame. Game ticks keep the original rate of gameFps per second. */
static void __cdecl WaitNextFrame(void *form)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_form = form;
    int fps = g_renderFps > g_cfg.gameFps ? g_renderFps : g_cfg.gameFps;
    LONGLONG period = g_freq.QuadPart / fps;
    LONGLONG logicPeriod = g_freq.QuadPart / g_cfg.gameFps;
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

    if (g_renderFps <= g_cfg.gameFps) {
        g_skipLogic = 0;
        Gameplay_Tick();
        return;
    }
    if (g_logicNext.QuadPart == 0 || now.QuadPart - g_logicNext.QuadPart > logicPeriod * 4)
        g_logicNext.QuadPart = now.QuadPart;
    BOOL logic = now.QuadPart >= g_logicNext.QuadPart;
    if (logic) g_logicNext.QuadPart += logicPeriod;
    /* time since the last game tick in game ticks (0..1) */
    float alpha = 1.0f - (float)(g_logicNext.QuadPart - now.QuadPart) / (float)logicPeriod;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    g_skipLogic = !logic;
    Interp_SetFrame(logic, alpha, form);
    if (logic) {
        Gameplay_Tick();
    } else {
        /* the HUD blink counter advances on every call: keep its speed */
        uint8_t *c = (uint8_t *)ADDR_HUD_BLINK;
        *c += *(uint8_t *)ADDR_HUD_BLINK_DIR ? 1 : -1;
    }
}

/* TNotifyEvent: eax = Self (TMainForm), edx = Sender */
static __declspec(naked) void Thunk_TimerEvent(void)
{
    __asm {
        push eax
        push edx
        push eax
        call WaitNextFrame
        add  esp, 4
        pop  edx
        pop  eax
        mov  ecx, ADDR_TimerEvent
        jmp  ecx
    }
}

/* replaces "cmp byte ptr [eax+12h], 1 / jne skip" in TimerEvent: the game logic block runs
   only when the game is running and this frame is a game tick */
#define PATCH_LOGIC_GATE   0x005F478B
#define LOGIC_BLOCK        0x005F4795
#define LOGIC_SKIP         0x005F4CF4

static __declspec(naked) void Thunk_LogicGate(void)
{
    __asm {
        cmp  g_skipLogic, 0
        jne  skip
        cmp  byte ptr [eax + 0x12], 1
        jne  skip
        push LOGIC_BLOCK
        ret
    skip:
        push LOGIC_SKIP
        ret
    }
}

/* Animations that advance once per call (pulsing crime markers, siren lights, day/night
   fade) would run faster with more frames: on render-only frames they are skipped. */
#define PATCH_CRIME_PULSE     0x005ED94F   /* TMainForm.ShowCrimes: marker brightness +-10   */
#define CRIME_PULSE_NEXT      0x005ED954
#define CRIME_PULSE_SKIP      0x005ED98F
#define PATCH_SIREN_ANIM      0x005A478F   /* siren light pattern counters                  */
#define SIREN_ANIM_NEXT       0x005A4794
#define SIREN_ANIM_SKIP       0x005A47B0
#define CALL_EFFECT_COUNTERS  0x005F5D4C   /* RenderTOBGLayer_Effect: call 005A3974         */
#define ADDR_EffectCounters   0x005A3974
#define CALL_DAYNIGHT_FADE    0x005F5432   /* RenderEvent: call 005E3BB4                    */
#define ADDR_DayNightFade     0x005E3BB4

static __declspec(naked) void Thunk_CrimePulse(void)
{
    __asm {
        cmp  g_skipLogic, 0
        jne  skip
        mov  eax, dword ptr ds:[0x00626C90]
        push CRIME_PULSE_NEXT
        ret
    skip:
        push CRIME_PULSE_SKIP
        ret
    }
}

static __declspec(naked) void Thunk_SirenAnim(void)
{
    __asm {
        cmp  g_skipLogic, 0
        jne  skip
        mov  eax, dword ptr ds:[0x00626F5C]
        push SIREN_ANIM_NEXT
        ret
    skip:
        push SIREN_ANIM_SKIP
        ret
    }
}

static __declspec(naked) void Thunk_EffectCounters(void)
{
    __asm {
        cmp  g_skipLogic, 0
        jne  skip
        push ADDR_EffectCounters
    skip:
        ret
    }
}

static __declspec(naked) void Thunk_DayNightFade(void)
{
    __asm {
        cmp  g_skipLogic, 0
        jne  skip
        push ADDR_DayNightFade
    skip:
        ret
    }
}

static void InstallAnimationRate(void)
{
    static const uint8_t pulse[] = { 0xA1, 0x90, 0x6C, 0x62, 0x00 };
    static const uint8_t siren[] = { 0xA1, 0x5C, 0x6F, 0x62, 0x00 };
    if (CheckBytes(PATCH_CRIME_PULSE, pulse, sizeof(pulse)))
        PatchJmp(PATCH_CRIME_PULSE, (void *)Thunk_CrimePulse, 5);
    if (CheckBytes(PATCH_SIREN_ANIM, siren, sizeof(siren)))
        PatchJmp(PATCH_SIREN_ANIM, (void *)Thunk_SirenAnim, 5);
    RedirectCall(CALL_EFFECT_COUNTERS, ADDR_EffectCounters, (void *)Thunk_EffectCounters);
    RedirectCall(CALL_DAYNIGHT_FADE, ADDR_DayNightFade, (void *)Thunk_DayNightFade);
}

static int MonitorRefreshRate(void)
{
    DEVMODEA dm = { 0 };
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (int)dm.dmDisplayFrequency;
    return 60;
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

    g_renderFps = g_cfg.renderFps < 0 ? MonitorRefreshRate() : g_cfg.renderFps;
    if (g_renderFps > 360) g_renderFps = 360;
    static const uint8_t gate[] = { 0x80, 0x78, 0x12, 0x01, 0x0F, 0x85, 0x5F, 0x05, 0x00, 0x00 };
    if (g_renderFps > g_cfg.gameFps && CheckBytes(PATCH_LOGIC_GATE, gate, sizeof(gate)) &&
        PatchJmp(PATCH_LOGIC_GATE, (void *)Thunk_LogicGate, sizeof(gate)))
    {
        InstallAnimationRate();
        Log("Render rate: %d frames per second (game logic: %d)", g_renderFps, g_cfg.gameFps);
    } else
        g_renderFps = 0;
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

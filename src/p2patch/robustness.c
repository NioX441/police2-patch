/*
 * robustness.c - Safe handling (and logging) of two latent bugs of the original code.
 *
 * 1) Cell screen position (0x00610E60 / 0x00610E90): returns the isometric screen X/Y of a
 *    map cell. For coordinates outside the 288x288 map the original returns an undefined
 *    register value. The HUD does this every frame with the "nothing selected" marker -1, so
 *    markers could appear at random positions. Now -1 yields a position far outside the
 *    screen (nothing visible), other invalid coordinates are clamped to the nearest valid
 *    cell; every call site is logged once.
 *
 * 2) Team window "add officer" / "remove officer" (TGUIpGroup.AddButtonClickEvent and
 *    RemoveButtonClickEvent): the team is looked up by its id; if it does not exist any more
 *    (e.g. it was dissolved while its window was open), the original continues with an
 *    uninitialised list index and can add the officer to a random team or raise an error.
 *    Now the click is ignored and logged.
 */
#include "patch.h"
#include "game.h"

#define ADDR_CellScreenX        0x00610E60
#define ADDR_CellScreenY        0x00610E90
#define ADDR_GroupAddClick      0x005BD824
#define ADDR_GroupRemoveClick   0x005BD9D8
#define GUIPGROUP_GROUP_ID      0xE4
#define ADDR_GROUPLIST_VAR      0x0062F50C

/* ---- 1) cell screen position ------------------------------------------------------------- */

static int __cdecl CellOutOfRange(int off, int x, int y, uintptr_t caller)
{
    /* log every call site once */
    static uintptr_t seen[64];
    int i;
    for (i = 0; i < 64 && seen[i] && seen[i] != caller; i++) {}
    if (i < 64 && !seen[i]) {
        seen[i] = caller;
        Log("Original bug: screen position of map cell %d,%d (outside the map) requested from %08X",
            x, y, (unsigned)caller - 5);
    }
    /* -1 is the game's "nothing selected" marker: return a position far outside the screen
       so that nothing becomes visible; other values are clamped to the nearest valid cell */
    if (x == -1 || y == -1) return -1000000;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= MAP_SIZE) x = MAP_SIZE - 1;
    if (y >= MAP_SIZE) y = MAP_SIZE - 1;
    return FIELD((uint8_t *)ADDR_MAP_GRID + x * MAP_ROW_SIZE + y * MAP_CELL_SIZE, off, int);
}

#define CELL_THUNK(name, ofs)                                              \
    static __declspec(naked) void name(void)                                 \
    {                                                                        \
        __asm cmp  eax, MAP_SIZE - 1                                         \
        __asm ja   bad                                                       \
        __asm cmp  edx, MAP_SIZE - 1                                         \
        __asm ja   bad                                                       \
        __asm imul eax, eax, MAP_ROW_SIZE                                    \
        __asm imul edx, edx, MAP_CELL_SIZE                                   \
        __asm add  eax, edx                                                  \
        __asm mov  eax, dword ptr [eax + ADDR_MAP_GRID + ofs]             \
        __asm ret                                                            \
        __asm bad:                                                           \
        __asm push dword ptr [esp]                                           \
        __asm push edx                                                       \
        __asm push eax                                                       \
        __asm push ofs                                                       \
        __asm call CellOutOfRange                                            \
        __asm add  esp, 16                                                   \
        __asm ret                                                            \
    }

CELL_THUNK(Thunk_CellScreenX, CELL_SCREEN_X)
CELL_THUNK(Thunk_CellScreenY, CELL_SCREEN_Y)

/* ---- 2) team window clicks ---------------------------------------------------------------- */

static int __cdecl TeamExists(const uint8_t *window)
{
    int id = FIELD(window, GUIPGROUP_GROUP_ID, int);
    void *groups = *(void **)ADDR_GROUPLIST_VAR;
    if (groups)
        for (int i = 0; i < ListCount(groups); i++)
            if (FIELD(ListItem(groups, i), 4, int) == id) return 1;
    Log("Original bug: team window refers to team %d which does not exist - click ignored", id);
    return 0;
}

/* procedure(Self {eax}; Sender {edx}): checks the team, then runs the original handler */
#define CLICK_THUNK(name, target)                                            \
    static __declspec(naked) void name(void)                                 \
    {                                                                        \
        __asm push eax                                                       \
        __asm push edx                                                       \
        __asm push eax                                                       \
        __asm call TeamExists                                                \
        __asm add  esp, 4                                                    \
        __asm test eax, eax                                                  \
        __asm pop  edx                                                       \
        __asm pop  eax                                                       \
        __asm jz   skip                                                      \
        __asm push ebp                                                       \
        __asm mov  ebp, esp                                                  \
        __asm add  esp, -8                                                   \
        __asm push target + 6                                                \
        __asm ret                                                            \
        __asm skip:                                                          \
        __asm ret                                                            \
    }

CLICK_THUNK(Thunk_GroupAdd, ADDR_GroupAddClick)
CLICK_THUNK(Thunk_GroupRemove, ADDR_GroupRemoveClick)

/* ------------------------------------------------------------------------------------------ */
/* Black ground after the graphics memory was lost                                            */
/*                                                                                            */
/* The ground, buildings and some overlays are rendered into render target textures only when */
/* the camera moves (flag at 0x00A9E698). When Windows takes the graphics memory away (Alt+Tab,*/
/* UAC prompt, lock screen, display change, ...), Asphyre recreates these textures empty and  */
/* the ground stays black until the camera is moved. Now the layers are redrawn right away.   */
/* ------------------------------------------------------------------------------------------ */

#define CALL_RT_RESET          0x00596330   /* TDX7RenderTargetTexture.HandleDeviceReset    */
#define ADDR_RT_Recreate       0x0059623C
#define ADDR_LAYERS_DIRTY      0x00A9E698

static __declspec(naked) void Thunk_RtReset(void)
{
    __asm {
        mov  ecx, ADDR_RT_Recreate
        call ecx
        mov  byte ptr ds:[ADDR_LAYERS_DIRTY], 1
        ret
    }
}

static void LayerRestore_Install(void)
{
    if (RedirectCall(CALL_RT_RESET, ADDR_RT_Recreate, (void *)Thunk_RtReset))
        Log("Layer restore fix installed");
}

void Robustness_Install(void)
{
    LayerRestore_Install();
    static const uint8_t cellX[] = { 0x85, 0xC0, 0x7C, 0x29, 0x85, 0xD2, 0x7C, 0x25 };
    if (CheckBytes(ADDR_CellScreenX, cellX, sizeof(cellX)) &&
        CheckBytes(ADDR_CellScreenY, cellX, sizeof(cellX))) {
        PatchJmp(ADDR_CellScreenX, (void *)Thunk_CellScreenX, 5);
        PatchJmp(ADDR_CellScreenY, (void *)Thunk_CellScreenY, 5);
    }
    static const uint8_t prologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xF8, 0x53, 0x56 };
    if (CheckBytes(ADDR_GroupAddClick, prologue, sizeof(prologue)) &&
        CheckBytes(ADDR_GroupRemoveClick, prologue, sizeof(prologue))) {
        PatchJmp(ADDR_GroupAddClick, (void *)Thunk_GroupAdd, 6);
        PatchJmp(ADDR_GroupRemoveClick, (void *)Thunk_GroupRemove, 6);
    }
    Log("Robustness fixes installed");
}

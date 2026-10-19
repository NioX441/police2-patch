/*
 * main.c - Entry point of p2patch.dll, the Police 2 community patch.
 *
 * The DLL is copied into the game folder as "msimg32.dll" (see the proxy at the end of this
 * file), so Windows loads it together with the game. police2.exe is compressed with UPX: when DllMain runs,
 * the game code is not unpacked yet. We therefore hook VirtualProtect in the import table of the
 * UPX loader stub; the stub calls it right after unpacking, just before it jumps to the game's
 * entry point. At that moment all fixes are applied. An already unpacked police2.exe is
 * supported as well (fixes are applied directly).
 */
#include "patch.h"
#include "game.h"

typedef BOOL (WINAPI *VirtualProtect_t)(LPVOID, SIZE_T, DWORD, PDWORD);
static VirtualProtect_t g_realVirtualProtect;
static volatile LONG g_applied;

static const uint8_t SIG1[] = { 0x53, 0x56, 0x57, 0x8B, 0xF0, 0x33, 0xD2, 0x8B, 0x46, 0x04, 0xE8, 0x09 };
static const uint8_t SIG2[] = { 0xDB, 0x43, 0x18, 0xD8, 0x35, 0x24, 0xEE, 0x55, 0x00, 0x8B, 0x53, 0x04 };
static const uint8_t SIG3[] = { 0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xA0, 0x53, 0x56, 0x57, 0x89, 0x4D, 0xF4 };

static BOOL IsGameCodePresent(void)
{
    __try {
        return CheckBytes(SIG_ADDR_1, SIG1, sizeof(SIG1)) &&
               CheckBytes(SIG_ADDR_2, SIG2, sizeof(SIG2)) &&
               CheckBytes(SIG_ADDR_3, SIG3, sizeof(SIG3));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static void ApplyAll(void)
{
    if (InterlockedExchange(&g_applied, 1)) return;
    if (UserData_Relaunch()) {
        Log("Started police2_niox.exe instead");
        TerminateProcess(GetCurrentProcess(), 0);
    }
    Log("Game code found, applying fixes");
    UserData_Install();
    Audio_Install();
    Display_EarlyInit();
    Display_Install();
    Presenter_Install();
    Sprites_Install();
    Paths_Install();
    Vehicles_Install();
    Groups_Install();
    Errors_Install();
    Gameplay_Install();
    Perf_Install();
    Robustness_Install();
    TileSeams_Install();
    Fuel_Install();
    WalkerType_Install();
    CameraLimits_Install();
    ShadowEdge_Install();
    Log("All fixes applied");
}

static BOOL WINAPI Hook_VirtualProtect(LPVOID addr, SIZE_T size, DWORD prot, PDWORD old)
{
    BOOL r = g_realVirtualProtect(addr, size, prot, old);
    if (!g_applied && IsGameCodePresent()) {
        /* restore the original import first, our own patch code uses VirtualProtect too */
        void *real = (void *)g_realVirtualProtect;
        DWORD o;
        g_realVirtualProtect((void *)IAT_PACKED_VirtualProtect, 4, PAGE_EXECUTE_READWRITE, &o);
        *(void **)IAT_PACKED_VirtualProtect = real;
        g_realVirtualProtect((void *)IAT_PACKED_VirtualProtect, 4, o, &o);
        ApplyAll();
    }
    return r;
}

static void Startup(HMODULE self)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    lstrcpynA(g_gameDir, path, MAX_PATH);

    LogInit();
    LoadConfig();
    Log("Police 2 Community Patch %s loaded (%s)", P2PATCH_VERSION, g_gameDir);
    (void)self;

    if (IsGameCodePresent()) {          /* unpacked executable */
        ApplyAll();
        return;
    }

    /* packed executable: wait for the UPX stub */
    void *vp = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "VirtualProtect");
    __try {
        if (*(void **)IAT_PACKED_VirtualProtect == vp) {
            g_realVirtualProtect = (VirtualProtect_t)vp;
            void *hook = (void *)Hook_VirtualProtect;
            DWORD o;
            VirtualProtect((void *)IAT_PACKED_VirtualProtect, 4, PAGE_EXECUTE_READWRITE, &o);
            *(void **)IAT_PACKED_VirtualProtect = hook;
            VirtualProtect((void *)IAT_PACKED_VirtualProtect, 4, o, &o);
            Log("Waiting for the UPX loader");
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    /* No message box here: DllMain runs under the loader lock. The README describes the supported version. */
    Log("Unsupported police2.exe version - no fixes applied. Expected build 488 (patch 4.8.8).");
}

static void LoadSystemMsimg32(void);

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        LoadSystemMsimg32();
        Startup(inst);
    }
    return TRUE;
}

/* ---------------------------------------------------------------------------------------- */
/* msimg32.dll proxy                                                                           */
/*                                                                                             */
/* This DLL is installed as "msimg32.dll" in the game folder. Windows loads DLLs from the       */
/* application folder first, so the game loads it instead of the system msimg32.dll - no game  */
/* file has to be modified. The real functions are forwarded to the system DLL.                */
/* ---------------------------------------------------------------------------------------- */

static FARPROC g_realAlphaBlend, g_realGradientFill, g_realTransparentBlt;

static void LoadSystemMsimg32(void)
{
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    lstrcpyW(path + n, L"\\msimg32.dll");
    HMODULE m = LoadLibraryW(path);
    if (!m) return;
    g_realAlphaBlend     = GetProcAddress(m, "AlphaBlend");
    g_realGradientFill   = GetProcAddress(m, "GradientFill");
    g_realTransparentBlt = GetProcAddress(m, "TransparentBlt");
}

__declspec(naked) void Proxy_AlphaBlend(void)     { __asm jmp dword ptr [g_realAlphaBlend] }
__declspec(naked) void Proxy_GradientFill(void)   { __asm jmp dword ptr [g_realGradientFill] }
__declspec(naked) void Proxy_TransparentBlt(void) { __asm jmp dword ptr [g_realTransparentBlt] }

#pragma comment(linker, "/export:AlphaBlend=_Proxy_AlphaBlend")
#pragma comment(linker, "/export:GradientFill=_Proxy_GradientFill")
#pragma comment(linker, "/export:TransparentBlt=_Proxy_TransparentBlt")

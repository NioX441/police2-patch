/*
 * patch.c - Logging, configuration, memory patching and Delphi interop helpers.
 */
#include "patch.h"
#include "game.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

Config g_cfg;
char   g_gameDir[MAX_PATH];

static CRITICAL_SECTION g_logLock;
static char g_logPath[MAX_PATH];

/* ---------------------------------------------------------------------------------------- */
/* Logging                                                                                     */
/* ---------------------------------------------------------------------------------------- */

void LogInit(void)
{
    InitializeCriticalSection(&g_logLock);
    wsprintfA(g_logPath, "%sp2patch.log", g_gameDir);
    HANDLE h = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        /* game folder not writable (no administrator rights): log into the user data folder */
        const wchar_t *dir = UserData_Dir();
        g_logPath[0] = 0;
        if (dir[0] && WideCharToMultiByte(CP_ACP, 0, dir, -1, g_logPath, MAX_PATH - 16, NULL, NULL))
            lstrcatA(g_logPath, "p2patch.log");
        h = g_logPath[0] ? CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
        if (h == INVALID_HANDLE_VALUE) g_logPath[0] = 0;   /* logging disabled */
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void Log(const char *fmt, ...)
{
    if (!g_logPath[0]) return;
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(line, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    n += _vsnprintf_s(line + n, sizeof(line) - n - 3, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(line) - 3) n = (int)strlen(line);
    line[n++] = '\r';
    line[n++] = '\n';

    EnterCriticalSection(&g_logLock);
    HANDLE h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, line, n, &w, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_logLock);
}

/* ---------------------------------------------------------------------------------------- */
/* Configuration                                                                               */
/* ---------------------------------------------------------------------------------------- */

static const char *g_defaultIni =
    "; Police 2 Community Patch - configuration\r\n"
    "; Delete this file to restore the defaults.\r\n"
    "\r\n"
    "[Display]\r\n"
    "; Internal render resolution in fullscreen mode: auto or WIDTHxHEIGHT (e.g. 1920x1080).\r\n"
    "; Direct3D 7 cannot render wider/taller than 2048 pixels, larger values are clamped.\r\n"
    "; 'auto' picks the largest resolution with the aspect ratio of your monitor.\r\n"
    "FullscreenResolution=auto\r\n"
    "; 1 = keep the aspect ratio (black bars if needed), 0 = stretch to the full screen.\r\n"
    "KeepAspectRatio=1\r\n"
    "; Size of the game window in windowed mode: auto or a factor such as 1, 1.5, 2.\r\n"
    "WindowScale=auto\r\n"
    "; Scaling filter when the image is enlarged: bicubic, sharp, bilinear, nearest or\r\n"
    "; directdraw (original output without Direct3D 11).\r\n"
    "Filter=bicubic\r\n"
    "; 1 = synchronise with the monitor refresh (no tearing).\r\n"
    "VSync=1\r\n"
    "; 1 = sharp image on high-DPI screens (recommended).\r\n"
    "DpiAware=1\r\n"
    "\r\n"
    "[Fixes]\r\n"
    "; Sprites (e.g. shadows) no longer disappear near the edges of the screen.\r\n"
    "SpriteCulling=1\r\n"
    "; Robust and fast path finding for police officers on foot.\r\n"
    "WalkerPathfinding=1\r\n"
    "; Police cars always get a route, even if the original route search fails.\r\n"
    "CarPathfinding=1\r\n"
    "; Officers can no longer be members of two teams at the same time.\r\n"
    "GroupMembership=1\r\n"
    "; Vehicles that are blocked for this many simulation ticks may pass (0 = off).\r\n"
    "TrafficDeadlockTicks=600\r\n"
    "; Write swallowed game errors to p2patch.log.\r\n"
    "LogExceptions=1\r\n"
    "\r\n"
    "[System]\r\n"
    "; 1 = save games and options are stored in %LOCALAPPDATA%\\Police 2 Community Patch\r\n"
    ";     (save games in the game folder are still found). Needed to play without\r\n"
    ";     administrator rights.\r\n"
    "UserDataFolder=1\r\n"
    "; 1 = police2.exe creates and starts police2_niox.exe: a copy that can use 4 GB of memory\r\n"
    ";     instead of 2 GB and needs no administrator rights. police2.exe is not changed.\r\n"
    "LargeMemory=1\r\n"
    "\r\n"
    "[Performance]\r\n"
    "; Game frames per second. The game logic runs once per frame and assumes 30 frames per\r\n"
    "; second (original value). A precise frame limiter keeps the game speed constant.\r\n"
    "; 0 = original (imprecise) timer.\r\n"
    "GameFPS=30\r\n"
    "; Frames drawn per second: auto (refresh rate of the monitor), a number such as 60 or\r\n"
    "; 144, or 0 (one frame per game frame, original). The game logic keeps running at\r\n"
    "; GameFPS; moving cars and people are interpolated between the game frames.\r\n"
    "RenderFPS=auto\r\n"
    "\r\n"
    "[Gameplay]\r\n"
    "; Hard mode: criminals are stronger, you need more/better officers per mission.\r\n"
    "HardMode=0\r\n"
    "; Strength multiplier for criminals in hard mode (1.0 - 5.0).\r\n"
    "CriminalStrength=2.5\r\n"
    "; Number of crimes per day in hard mode, relative to the chosen difficulty (1.0 - 8.0).\r\n"
    "; 4.0 = 40 instead of 10 crimes per day on the highest difficulty. More crimes per day\r\n"
    "; also means more cases at the same time.\r\n"
    "CrimeFrequency=4.0\r\n"
    "; 1 = car theft crimes (prepared in the game but switched off by the developers).\r\n"
    "CarTheft=1\r\n"
    "; 1 = cheat keys: Ctrl+Shift+M adds 1,000 to your budget,\r\n"
    ";     Ctrl+Shift+L raises the public safety level by 10 percentage points,\r\n"
    ";     Ctrl+Shift+1..8 starts a crime (1 robbery, 2 stabbing, 3 homicide, 4 burglary,\r\n"
    ";     5 smuggling, 6 drug abuse, 7 terrorism, 8 riot, 9 car theft), Ctrl+Shift+R a\r\n"
    ";     random crime.\r\n"
    "Cheats=1\r\n";

static double IniDouble(const char *ini, const char *sec, const char *key, double def)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini);
    if (!buf[0] || _stricmp(buf, "auto") == 0) return def;
    for (char *p = buf; *p; p++) if (*p == ',') *p = '.';
    return atof(buf);
}

void LoadConfig(void)
{
    char ini[MAX_PATH], buf[64];
    wsprintfA(ini, "%sp2patch.ini", g_gameDir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileA(ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            /* game folder not writable: use p2patch.ini in the user data folder */
            const wchar_t *dir = UserData_Dir();
            if (dir[0] && WideCharToMultiByte(CP_ACP, 0, dir, -1, ini, MAX_PATH - 16, NULL, NULL)) {
                lstrcatA(ini, "p2patch.ini");
                h = CreateFileA(ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            }
        }
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, g_defaultIni, (DWORD)strlen(g_defaultIni), &w, NULL);
            CloseHandle(h);
        }
    }

    GetPrivateProfileStringA("Display", "FullscreenResolution", "auto", buf, sizeof(buf), ini);
    g_cfg.fullscreenWidth = g_cfg.fullscreenHeight = 0;
    if (_stricmp(buf, "auto") != 0) {
        int w = 0, h = 0;
        if (sscanf_s(buf, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 480) {
            g_cfg.fullscreenWidth = w;
            g_cfg.fullscreenHeight = h;
        }
    }
    g_cfg.keepAspect  = GetPrivateProfileIntA("Display", "KeepAspectRatio", 1, ini);
    g_cfg.windowScale = IniDouble(ini, "Display", "WindowScale", 0.0);
    g_cfg.dpiAware    = GetPrivateProfileIntA("Display", "DpiAware", 1, ini);
    g_cfg.vsync       = GetPrivateProfileIntA("Display", "VSync", 1, ini);
    GetPrivateProfileStringA("Display", "Filter", "bicubic", buf, sizeof(buf), ini);
    g_cfg.filter = !_stricmp(buf, "nearest") ? 0 : !_stricmp(buf, "bilinear") ? 1 :
                   !_stricmp(buf, "sharp") ? 3 : !_stricmp(buf, "directdraw") ? -1 : 2;

    g_cfg.fixSpriteCulling     = GetPrivateProfileIntA("Fixes", "SpriteCulling", 1, ini);
    g_cfg.fixWalkerPathfinding = GetPrivateProfileIntA("Fixes", "WalkerPathfinding", 1, ini);
    g_cfg.fixCarPathfinding    = GetPrivateProfileIntA("Fixes", "CarPathfinding", 1, ini);
    g_cfg.fixGroupMembership   = GetPrivateProfileIntA("Fixes", "GroupMembership", 1, ini);
    g_cfg.trafficDeadlockTicks = GetPrivateProfileIntA("Fixes", "TrafficDeadlockTicks", 600, ini);
    g_cfg.logExceptions        = GetPrivateProfileIntA("Fixes", "LogExceptions", 1, ini);
    g_cfg.debugKeys            = GetPrivateProfileIntA("Debug", "Keys", 0, ini);

    g_cfg.hardMode         = GetPrivateProfileIntA("Gameplay", "HardMode", 0, ini);
    g_cfg.cheats           = GetPrivateProfileIntA("Gameplay", "Cheats", 1, ini);
    g_cfg.carTheft         = GetPrivateProfileIntA("Gameplay", "CarTheft", 1, ini);
    g_cfg.userDataFolder   = GetPrivateProfileIntA("System", "UserDataFolder", 1, ini);
    g_cfg.largeMemory      = GetPrivateProfileIntA("System", "LargeMemory", 1, ini);
    g_cfg.gameFps          = GetPrivateProfileIntA("Performance", "GameFPS", 30, ini);
    if (g_cfg.gameFps > 240) g_cfg.gameFps = 240;
    GetPrivateProfileStringA("Performance", "RenderFPS", "auto", buf, sizeof(buf), ini);
    g_cfg.renderFps = _stricmp(buf, "auto") == 0 ? -1 : atoi(buf);
    g_cfg.criminalStrength = IniDouble(ini, "Gameplay", "CriminalStrength", 2.5);
    if (g_cfg.criminalStrength < 1.0) g_cfg.criminalStrength = 1.0;
    if (g_cfg.criminalStrength > 5.0) g_cfg.criminalStrength = 5.0;
    g_cfg.crimeFrequency   = IniDouble(ini, "Gameplay", "CrimeFrequency", 4.0);
    if (g_cfg.crimeFrequency < 1.0) g_cfg.crimeFrequency = 1.0;
    if (g_cfg.crimeFrequency > 8.0) g_cfg.crimeFrequency = 8.0;
}

/* ---------------------------------------------------------------------------------------- */
/* Memory patching                                                                             */
/* ---------------------------------------------------------------------------------------- */

BOOL WriteMem(uintptr_t addr, const void *data, size_t len)
{
    DWORD old;
    if (!VirtualProtect((void *)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        Log("VirtualProtect failed at %08X (%u)", (unsigned)addr, GetLastError());
        return FALSE;
    }
    memcpy((void *)addr, data, len);
    VirtualProtect((void *)addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, len);
    return TRUE;
}

BOOL CheckBytes(uintptr_t addr, const void *expected, size_t len)
{
    return memcmp((const void *)addr, expected, len) == 0;
}

static BOOL PatchRel(uintptr_t addr, uint8_t op, void *target, size_t len)
{
    uint8_t buf[32];
    if (len < 5 || len > sizeof(buf)) return FALSE;
    memset(buf, 0x90, len);
    buf[0] = op;
    *(int32_t *)(buf + 1) = (int32_t)((uintptr_t)target - (addr + 5));
    return WriteMem(addr, buf, len);
}

BOOL PatchCall(uintptr_t addr, void *target, size_t len) { return PatchRel(addr, 0xE8, target, len); }
BOOL PatchJmp(uintptr_t addr, void *target, size_t len)  { return PatchRel(addr, 0xE9, target, len); }

BOOL RedirectCall(uintptr_t addr, uintptr_t expectedTarget, void *newTarget)
{
    const uint8_t *p = (const uint8_t *)addr;
    if (p[0] != 0xE8 || addr + 5 + *(const int32_t *)(p + 1) != expectedTarget) {
        Log("RedirectCall: unexpected code at %08X, patch skipped", (unsigned)addr);
        return FALSE;
    }
    return PatchCall(addr, newTarget, 5);
}

void *HookIat(uintptr_t slot, const char *module, const char *name, void *hook)
{
    void *expected = (void *)GetProcAddress(GetModuleHandleA(module), name);
    void *current = *(void **)slot;
    if (!expected || current != expected) {
        Log("IAT slot %08X does not contain %s!%s (%p != %p), hook skipped",
            (unsigned)slot, module, name, current, expected);
        return NULL;
    }
    WriteMem(slot, &hook, sizeof(hook));
    return current;
}

/* ---------------------------------------------------------------------------------------- */
/* Delphi interop                                                                              */
/* ---------------------------------------------------------------------------------------- */

void *DelphiCreate(uintptr_t vmt)
{
    void *r;
    __asm {
        mov  eax, vmt
        mov  dl, 1
        mov  ecx, ADDR_TObject_Create
        call ecx
        mov  r, eax
    }
    return r;
}

void DelphiFree(void *obj)
{
    __asm {
        mov  eax, obj
        mov  ecx, ADDR_TObject_Free
        call ecx
    }
}

int DelphiListAdd(void *list, void *item)
{
    int r;
    __asm {
        mov  eax, list
        mov  edx, item
        mov  ecx, ADDR_TList_Add
        call ecx
        mov  r, eax
    }
    return r;
}

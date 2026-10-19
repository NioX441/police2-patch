/*
 * patch.h - Shared helpers: logging, configuration, code patching and Delphi interop.
 */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define P2PATCH_VERSION "1.2.1"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Configuration (p2patch.ini next to police2.exe) -------------------------------- */
typedef struct Config {
    /* [Display] */
    int  fullscreenWidth;       /* 0 = auto */
    int  fullscreenHeight;      /* 0 = auto */
    int  keepAspect;
    double windowScale;         /* 0 = auto */
    int  dpiAware;
    int  filter;                /* -1 = DirectDraw, 0 nearest, 1 bilinear, 2 bicubic, 3 sharp */
    int  vsync;
    /* [Fixes] */
    int  fixSpriteCulling;
    int  fixWalkerPathfinding;
    int  fixCarPathfinding;
    int  fixGroupMembership;
    int  trafficDeadlockTicks;  /* 0 = disabled */
    int  logExceptions;
    int  debugKeys;             /* [Debug] Keys: developer hotkeys */
    /* [Gameplay] */
    int  hardMode;
    int  cheats;                /* Ctrl+Shift+M money, Ctrl+Shift+L safety */
    int  gameFps;               /* 0 = original timer */
    int  userDataFolder;        /* saves in %LOCALAPPDATA% */
    int  largeMemory;           /* start police2_niox.exe (4 GB, no admin) */
    double criminalStrength;
    double crimeFrequency;      /* hard mode: more crimes per day */
} Config;

extern Config g_cfg;
extern char   g_gameDir[MAX_PATH];

void LoadConfig(void);
void LogInit(void);
void Log(const char *fmt, ...);

/* ---- Code patching ------------------------------------------------------------------------ */
BOOL WriteMem(uintptr_t addr, const void *data, size_t len);
BOOL CheckBytes(uintptr_t addr, const void *expected, size_t len);
/* Writes "E8 rel32" (call) or "E9 rel32" (jmp) at addr, padding with NOPs up to len bytes. */
BOOL PatchCall(uintptr_t addr, void *target, size_t len);
BOOL PatchJmp(uintptr_t addr, void *target, size_t len);
/* Re-targets an existing "call rel32" at addr, verifying it currently calls expectedTarget. */
BOOL RedirectCall(uintptr_t addr, uintptr_t expectedTarget, void *newTarget);
/* Replaces a pointer in an import address table slot after verifying it. Returns the old value. */
void *HookIat(uintptr_t slot, const char *module, const char *name, void *hook);

/* ---- Delphi interop (register calling convention: eax, edx, ecx, then stack) ------------ */
void *DelphiCreate(uintptr_t vmt);           /* TObject.Create for a class without own constructor */
void  DelphiFree(void *obj);                 /* TObject.Free */
int   DelphiListAdd(void *list, void *item); /* TList.Add */

static __inline int   ListCount(const void *list)        { return *(const int *)((const uint8_t *)list + 8); }
static __inline void *ListItem(const void *list, int i)  { return (*(void ***)((const uint8_t *)list + 4))[i]; }

#define FIELD(obj, off, type) (*(type *)((uint8_t *)(obj) + (off)))

/* ---- Modules ------------------------------------------------------------------------------ */
void Display_EarlyInit(void);   /* before any window exists                     */
void Display_Install(void);     /* after unpacking                              */
void Sprites_Install(void);
void Paths_Install(void);
void Vehicles_Install(void);
void Groups_Install(void);
void Errors_Install(void);
void Gameplay_Install(void);
void Presenter_Install(void);
void Perf_Install(void);
void Robustness_Install(void);
void TileSeams_Install(void);
void Fuel_Install(void);
void WalkerType_Install(void);
void CameraLimits_Install(void);
void ShadowEdge_Install(void);
void UserData_Install(void);
BOOL UserData_Relaunch(void);       /* TRUE: police2_niox.exe was started instead */
const wchar_t *UserData_Dir(void);  /* %LOCALAPPDATA%\Police 2 Community Patch\ or "" */
void Gameplay_Tick(void);   /* called once per game frame */

#ifdef __cplusplus
}
#endif

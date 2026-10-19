/*
 * audio.c - Sound output follows the Windows default device.
 *
 * The game initialises BASS once at start with the default output device. If the speakers
 * were switched off at that moment (or another device was the default), the game stayed on
 * that device - or without sound - for the whole session.
 *
 * 1. BASS_CONFIG_DEV_DEFAULT (BASS 2.4.7+, the game ships 2.4.8) makes the "Default" device
 *    follow changes of the Windows default device.
 * 2. If no output device exists at start, BASS is initialised without sound so the game runs
 *    normally; as soon as a device appears, it is initialised and used for new sounds.
 */
#include "patch.h"
#include "game.h"

#define IAT_BASS_Init              0x00AA0440
#define IAT_BASS_StreamCreateFile  0x00AA043C

#define BASS_CONFIG_DEV_DEFAULT    36
#define BASS_DEVICE_ENABLED        1
#define BASS_DEVICE_INIT           4
#define BASS_ERROR_ALREADY         14

typedef struct { const char *name, *driver; DWORD flags; } BASS_DEVICEINFO;

typedef BOOL  (WINAPI *Init_t)(int device, DWORD freq, DWORD flags, HWND win, const void *clsid);
typedef DWORD (WINAPI *StreamCreateFile_t)(BOOL mem, const void *file, uint64_t offset, uint64_t length, DWORD flags);
typedef BOOL  (WINAPI *SetConfig_t)(DWORD option, DWORD value);
typedef BOOL  (WINAPI *GetDeviceInfo_t)(DWORD device, BASS_DEVICEINFO *info);
typedef BOOL  (WINAPI *SetDevice_t)(DWORD device);
typedef int   (WINAPI *ErrorGetCode_t)(void);

static Init_t             g_init;
static StreamCreateFile_t g_streamCreateFile;
static SetConfig_t        g_setConfig;
static GetDeviceInfo_t    g_getDeviceInfo;
static SetDevice_t        g_setDevice;
static ErrorGetCode_t     g_errorGetCode;

static BOOL  g_waitingForDevice;
static DWORD g_freq, g_flags, g_lastCheck;
static HWND  g_win;

static BOOL WINAPI Hook_BASS_Init(int device, DWORD freq, DWORD flags, HWND win, const void *clsid)
{
    if (g_setConfig) g_setConfig(BASS_CONFIG_DEV_DEFAULT, 1);
    if (g_init(device, freq, flags, win, clsid)) {
        Log("Audio: output on the Windows default device (follows device changes)");
        return TRUE;
    }
    Log("Audio: no output device (BASS error %d) - waiting for one", g_errorGetCode ? g_errorGetCode() : -1);
    g_freq = freq;
    g_flags = flags;
    g_win = win;
    g_waitingForDevice = TRUE;
    return g_init(0, freq, flags, win, clsid);         /* "no sound" device */
}

/* Called before a sound is played: switch to a real device once one exists. */
static void CheckForDevice(void)
{
    DWORD now = GetTickCount();
    if (now - g_lastCheck < 2000) return;
    g_lastCheck = now;
    for (DWORD d = 1; d < 16; d++) {
        BASS_DEVICEINFO info;
        if (!g_getDeviceInfo(d, &info)) break;
        if (!(info.flags & BASS_DEVICE_ENABLED)) continue;
        if (g_init((int)d, g_freq, g_flags, g_win, NULL) || g_errorGetCode() == BASS_ERROR_ALREADY) {
            g_setDevice(d);
            g_waitingForDevice = FALSE;
            Log("Audio: output device %u (%s) available, sound enabled", d, info.name ? info.name : "?");
            return;
        }
    }
}

static DWORD WINAPI Hook_BASS_StreamCreateFile(BOOL mem, const void *file, uint64_t offset, uint64_t length, DWORD flags)
{
    if (g_waitingForDevice) CheckForDevice();
    return g_streamCreateFile(mem, file, offset, length, flags);
}

void Audio_Install(void)
{
    HMODULE bass = GetModuleHandleA("bass.dll");
    if (!bass) return;
    g_setConfig     = (SetConfig_t)GetProcAddress(bass, "BASS_SetConfig");
    g_getDeviceInfo = (GetDeviceInfo_t)GetProcAddress(bass, "BASS_GetDeviceInfo");
    g_setDevice     = (SetDevice_t)GetProcAddress(bass, "BASS_SetDevice");
    g_errorGetCode  = (ErrorGetCode_t)GetProcAddress(bass, "BASS_ErrorGetCode");
    if (!g_setConfig || !g_getDeviceInfo || !g_setDevice || !g_errorGetCode) return;
    g_init = (Init_t)HookIat(IAT_BASS_Init, "bass.dll", "BASS_Init", (void *)Hook_BASS_Init);
    g_streamCreateFile = (StreamCreateFile_t)HookIat(IAT_BASS_StreamCreateFile, "bass.dll",
                                                     "BASS_StreamCreateFile", (void *)Hook_BASS_StreamCreateFile);
    if (g_init && g_streamCreateFile) Log("Audio device fix installed");
}

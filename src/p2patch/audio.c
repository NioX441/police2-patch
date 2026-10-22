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

/* ---- Two music tracks at the same time -------------------------------------------------- */
/* The game cross-fades between two music channels (0x0062F49C / 0x0062F4A0). When the music  */
/* changes while both channels are still active - a faded-out track that was never stopped,   */
/* or a cross-fade still running (calm music fading in, then a crime starts the action        */
/* music) - it stops only the quieter channel and starts the new track there: the louder      */
/* track keeps playing, so two tracks run at once. In that case the quieter channel is now    */
/* stopped first, and the game takes its normal path: fade out the other track, start the new */
/* one.                                                                                       */

#define ADDR_MusicSwitch     0x0059F9B8
#define ADDR_MUSIC_CHANNEL1  0x0062F49C   /* channel handles, syncs at +8 and +16 */
#define ADDR_MUSIC_CHANNEL2  0x0062F4A0
#define ADDR_MUSIC_TYPE      0x0062F4B8   /* type of the last started track */
#define BASS_ATTRIB_VOL      2
#define BASS_ACTIVE_PLAYING  1

typedef DWORD (WINAPI *ChannelIsActive_t)(DWORD h);
typedef BOOL  (WINAPI *ChannelIsSliding_t)(DWORD h, DWORD attrib);
typedef BOOL  (WINAPI *ChannelGetAttribute_t)(DWORD h, DWORD attrib, float *value);
typedef BOOL  (WINAPI *ChannelStop_t)(DWORD h);
typedef BOOL  (WINAPI *ChannelRemoveSync_t)(DWORD h, DWORD sync);

static ChannelIsActive_t     g_isActive;
static ChannelIsSliding_t    g_isSliding;
static ChannelGetAttribute_t g_getAttribute;
static ChannelStop_t         g_stop;
static ChannelRemoveSync_t   g_removeSync;

static void StopMusicChannel(DWORD *slot)
{
    g_removeSync(*slot, slot[2]);          /* end-of-track and slide syncs of this channel */
    g_removeSync(*slot, slot[4]);
    g_stop(*slot);
}

static void __cdecl BeforeMusicSwitch(DWORD type)
{
    DWORD *c1 = (DWORD *)ADDR_MUSIC_CHANNEL1, *c2 = (DWORD *)ADDR_MUSIC_CHANNEL2;
    if (!*c1 || !*c2) return;
    if (g_isActive(*c1) != BASS_ACTIVE_PLAYING || g_isActive(*c2) != BASS_ACTIVE_PLAYING) return;
    /* back to the track that is fading out: the game reverses the cross-fade itself */
    if ((uint8_t)type == *(uint8_t *)ADDR_MUSIC_TYPE &&
        (g_isSliding(*c1, BASS_ATTRIB_VOL) || g_isSliding(*c2, BASS_ATTRIB_VOL))) return;
    float v1 = 0, v2 = 0;
    g_getAttribute(*c1, BASS_ATTRIB_VOL, &v1);
    g_getAttribute(*c2, BASS_ATTRIB_VOL, &v2);
    StopMusicChannel(v1 <= v2 ? c1 : c2);
}

/* original first instructions: push ebp / mov ebp, esp / mov ecx, 0Bh (al = music type) */
static __declspec(naked) void Thunk_MusicSwitch(void)
{
    __asm {
        push eax                        /* al = music type, also the argument */
        call BeforeMusicSwitch
        pop  eax
        push ebp
        mov  ebp, esp
        mov  ecx, 0x0B
        push ADDR_MusicSwitch + 8
        ret
    }
}

static void InstallMusicFix(HMODULE bass)
{
    static const uint8_t prologue[] = { 0x55, 0x8B, 0xEC, 0xB9, 0x0B, 0x00, 0x00, 0x00 };
    g_isActive     = (ChannelIsActive_t)GetProcAddress(bass, "BASS_ChannelIsActive");
    g_isSliding    = (ChannelIsSliding_t)GetProcAddress(bass, "BASS_ChannelIsSliding");
    g_getAttribute = (ChannelGetAttribute_t)GetProcAddress(bass, "BASS_ChannelGetAttribute");
    g_stop         = (ChannelStop_t)GetProcAddress(bass, "BASS_ChannelStop");
    g_removeSync   = (ChannelRemoveSync_t)GetProcAddress(bass, "BASS_ChannelRemoveSync");
    if (g_isActive && g_isSliding && g_getAttribute && g_stop && g_removeSync &&
        CheckBytes(ADDR_MusicSwitch, prologue, sizeof(prologue)) &&
        PatchJmp(ADDR_MusicSwitch, (void *)Thunk_MusicSwitch, sizeof(prologue)))
        Log("Music overlap fix installed");
}

void Audio_Install(void)
{
    HMODULE bass = GetModuleHandleA("bass.dll");
    if (!bass) return;
    g_setConfig     = (SetConfig_t)GetProcAddress(bass, "BASS_SetConfig");
    g_getDeviceInfo = (GetDeviceInfo_t)GetProcAddress(bass, "BASS_GetDeviceInfo");
    g_setDevice     = (SetDevice_t)GetProcAddress(bass, "BASS_SetDevice");
    g_errorGetCode  = (ErrorGetCode_t)GetProcAddress(bass, "BASS_ErrorGetCode");
    InstallMusicFix(bass);
    if (!g_setConfig || !g_getDeviceInfo || !g_setDevice || !g_errorGetCode) return;
    g_init = (Init_t)HookIat(IAT_BASS_Init, "bass.dll", "BASS_Init", (void *)Hook_BASS_Init);
    g_streamCreateFile = (StreamCreateFile_t)HookIat(IAT_BASS_StreamCreateFile, "bass.dll",
                                                     "BASS_StreamCreateFile", (void *)Hook_BASS_StreamCreateFile);
    if (g_init && g_streamCreateFile) Log("Audio device fix installed");
}

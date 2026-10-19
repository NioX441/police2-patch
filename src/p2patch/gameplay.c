/*
 * gameplay.c - Optional hard mode.
 *
 * Every case has a "skillPoint" value (strength of the criminals, TCase+0x3C). During a
 * mission the game adds up the skill of all officers at the scene ("PoliceSkill", TCase+0xAC)
 * and the criminals win/escape while PoliceSkill < skillPoint. In hard mode the strength of
 * every newly generated crime is multiplied by CriminalStrength (default 2.5), so missions
 * need considerably more or better trained officers. Money is not affected.
 */
#include "patch.h"
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PATCH_GENERATECRIME_END 0x00602D0C   /* TCaseList.GenerateCrime epilogue, eax = TCase */
#define CASE_SKILLPOINT         0x3C

static void __cdecl HardenCrime(uint8_t *cs)
{
    int s = FIELD(cs, CASE_SKILLPOINT, int);
    if (s <= 0) return;
    double v = floor(s * g_cfg.criminalStrength + 0.5);
    FIELD(cs, CASE_SKILLPOINT, int) = v > 1e6 ? 1000000 : (int)v;
    static int logged;
    if (logged++ < 5) Log("Hard mode: crime strength %d -> %d", s, FIELD(cs, CASE_SKILLPOINT, int));
}

static __declspec(naked) void Thunk_GenerateCrimeEnd(void)
{
    __asm {
        push eax
        call HardenCrime
        add  esp, 4
        /* original epilogue */
        mov  eax, esi
        pop  edi
        pop  esi
        pop  ebx
        mov  esp, ebp
        pop  ebp
        ret  0x1C
    }
}

/* ---- Cheats ------------------------------------------------------------------------------ */
/* Ctrl+Shift+M: +1,000 budget.  Ctrl+Shift+L: public safety level +10 percentage points.        */

#define ADDR_MONEY            0x00A9E6DC   /* game state record (0x00A9E6C8) + 0x14: budget       */
#define ADDR_SAFETY_LEVEL     0x00A4C510   /* byte: public safety level in percent                */
#define ADDR_CalcSafetyLevel  0x005AD1E4   /* recomputes the level from the map                   */
#define CELL_AREA_ACTIVE      0x11         /* byte: cell belongs to the playable city area        */
#define CELL_SAFE_HOURS       0x12         /* 24 bytes: cell was safe in hour 0..23               */
#define MONEY_AMOUNT          1000

static void AddMoney(void)
{
    int *money = (int *)ADDR_MONEY;
    if (*money < 2000000000 - MONEY_AMOUNT) *money += MONEY_AMOUNT;
    Log("Cheat: budget is now %d", *money);
}

static void RaiseSafety(void)
{
    uint8_t *map = (uint8_t *)ADDR_MAP_GRID;
    int cells = 0, safe = 0;
    for (int i = 0; i < MAP_SIZE * MAP_SIZE; i++) {
        uint8_t *c = map + i * MAP_CELL_SIZE;
        if (!c[CELL_AREA_ACTIVE]) continue;
        cells++;
        for (int h = 0; h < 24; h++) safe += c[CELL_SAFE_HOURS + h] != 0;
    }
    if (!cells) return;
    int total = cells * 24;
    int target = min(total, safe + total / 10);
    /* spread the new safe hours evenly: hour by hour over all cells */
    for (int h = 0; h < 24 && safe < target; h++)
        for (int i = 0; i < MAP_SIZE * MAP_SIZE && safe < target; i++) {
            uint8_t *c = map + i * MAP_CELL_SIZE;
            if (c[CELL_AREA_ACTIVE] && !c[CELL_SAFE_HOURS + h]) {
                c[CELL_SAFE_HOURS + h] = 1;
                safe++;
            }
        }
    __asm {
        mov  ecx, ADDR_CalcSafetyLevel
        call ecx
    }
    Log("Cheat: public safety level is now %d%%", *(uint8_t *)ADDR_SAFETY_LEVEL);
}

/* Keyboard hook of the game thread: sees the keys regardless of which control has focus. */
static LRESULT CALLBACK KeyboardProc(int code, WPARAM vk, LPARAM lp)
{
    if (code == HC_ACTION && !(lp & 0x80000000) && !(lp & 0x40000000) &&
        (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
        if (vk == 'M' && g_cfg.cheats) AddMoney();
        if (vk == 'L' && g_cfg.cheats) RaiseSafety();
        if (vk == VK_F11 && g_cfg.debugKeys) {   /* debug: toggle sprite culling (1 <-> 2) */
            g_cfg.fixSpriteCulling = g_cfg.fixSpriteCulling == 2 ? 1 : 2;
            Log("Debug: SpriteCulling=%d", g_cfg.fixSpriteCulling);
        }
    }
    return CallNextHookEx(NULL, code, vk, lp);
}

static void RefillCrimeSlots(void);

void Gameplay_Tick(void)
{
    static BOOL hooked;
    if (g_cfg.hardMode) RefillCrimeSlots();
    if (!hooked && (g_cfg.cheats || g_cfg.debugKeys)) {
        hooked = TRUE;   /* installed from the game thread on the first frame */
        SetWindowsHookExA(WH_KEYBOARD, KeyboardProc, NULL, GetCurrentThreadId());
    }
}


/* ---- Hard mode: more crimes per day --------------------------------------------------------- */
/* TCaseList.CrimeGenerator (0x00602064) plans the crimes of the next day once per day: 5, 8 or  */
/* 10 random times (depending on the difficulty) in 19 two-byte slots (hour, minute) of the game */
/* state at +0x58. TCaseList.InitCrime checks the slots every game minute and clears a slot when */
/* its crime starts. In hard mode more times are planned: the earliest 19 go into the slots, the */
/* others wait in a queue and are put into slots that became free (no save game format change). */

#define ADDR_CrimeGenerator  0x00602064
#define ADDR_GAME_STATE      0x00A9E6C8
#define ADDR_GAME_TIME       (ADDR_GAME_STATE + 0x08)   /* TDateTime */
#define ADDR_CRIME_SLOTS     (ADDR_GAME_STATE + 0x58)
#define CRIME_SLOTS          19
#define MAX_CRIMES_PER_DAY   (CRIME_SLOTS + 64)

static uint32_t g_rand = 0x2545F491;
static int RandInt(int n) { g_rand = g_rand * 1103515245u + 12345u; return (int)((g_rand >> 16) % (uint32_t)n); }

static uint16_t g_crimeQueue[MAX_CRIMES_PER_DAY];   /* minute of the day (h * 60 + m), sorted */
static int      g_queueLen, g_queuePos;
static double   g_lastGameTime;

static int CompareU16(const void *a, const void *b) { return *(const uint16_t *)a - *(const uint16_t *)b; }

static void __cdecl AfterCrimeGenerator(const uint8_t *before)
{
    uint8_t *slots = (uint8_t *)ADDR_CRIME_SLOTS;
    if (memcmp(before, slots, CRIME_SLOTS * 2) == 0) return;      /* no new planning */
    uint16_t times[MAX_CRIMES_PER_DAY];
    int n = 0;
    for (int i = 0; i < CRIME_SLOTS; i++)
        if (slots[2 * i] || slots[2 * i + 1]) times[n++] = (uint16_t)(slots[2 * i] * 60 + slots[2 * i + 1]);
    int planned = n;
    int target = (int)(n * g_cfg.crimeFrequency + 0.5);
    if (target > MAX_CRIMES_PER_DAY) target = MAX_CRIMES_PER_DAY;
    g_rand ^= GetTickCount();
    for (int tries = 0; n < target && tries < 10000; tries++) {
        uint16_t t = (uint16_t)(RandInt(24) * 60 + RandInt(59) + 1);   /* minute 1..59 as the game */
        BOOL clash = FALSE;
        for (int j = 0; j < n && !clash; j++) clash = times[j] == t;
        if (!clash) times[n++] = t;
    }
    qsort(times, n, sizeof(times[0]), CompareU16);
    memset(slots, 0, CRIME_SLOTS * 2);
    int k = 0;
    for (; k < n && k < CRIME_SLOTS; k++) {
        slots[2 * k] = (uint8_t)(times[k] / 60);
        slots[2 * k + 1] = (uint8_t)(times[k] % 60);
    }
    g_queueLen = 0;
    for (; k < n; k++) g_crimeQueue[g_queueLen++] = times[k];
    g_queuePos = 0;
    Log("Hard mode: %d crimes planned for the next day (original %d)", n, planned);
}

/* Puts queued crime times into slots that became free. Called once per frame. */
static void RefillCrimeSlots(void)
{
    double now = *(const double *)ADDR_GAME_TIME;
    /* another game was loaded (time jumped back or by more than a day): drop the queue */
    if (now < g_lastGameTime || now > g_lastGameTime + 1.0) g_queuePos = g_queueLen;
    g_lastGameTime = now;
    if (g_queuePos >= g_queueLen) return;
    uint8_t *slots = (uint8_t *)ADDR_CRIME_SLOTS;
    int minute = (int)((now - floor(now)) * 1440.0 + 0.001);
    for (int i = 0; i < CRIME_SLOTS && g_queuePos < g_queueLen; i++) {
        if (slots[2 * i] || slots[2 * i + 1]) continue;
        int t = g_crimeQueue[g_queuePos++];
        if (t <= minute) {                       /* the slot became free too late: start soon */
            t = minute + 1 + RandInt(5);
            if (t % 60 == 0) t++;                /* minute 0 is never used by the game */
            if (t >= 24 * 60) continue;          /* day is over */
        }
        slots[2 * i] = (uint8_t)(t / 60);
        slots[2 * i + 1] = (uint8_t)(t % 60);
    }
}

static __declspec(naked) void Orig_CrimeGenerator(void)
{
    __asm {
        push ebx
        push esi
        push edi
        add  esp, -0x18
        push ADDR_CrimeGenerator + 6
        ret
    }
}

static uint8_t g_slotsBefore[CRIME_SLOTS * 2];

static __declspec(naked) void Thunk_CrimeGenerator(void)
{
    __asm {
        push eax
        push esi
        push edi
        push ecx
        mov  esi, ADDR_CRIME_SLOTS
        lea  edi, g_slotsBefore
        mov  ecx, CRIME_SLOTS * 2
        rep  movsb
        pop  ecx
        pop  edi
        pop  esi
        pop  eax
        call Orig_CrimeGenerator
        push eax
        push offset g_slotsBefore
        call AfterCrimeGenerator
        add  esp, 4
        pop  eax
        ret
    }
}

/* ---- "build 488 - Niox Patch" in the main menu ------------------------------------------- */

#define MENU_BUILD_WIDE_1  0x005B510F   /* mov edx, offset WideString "build 488"    (+1 = imm) */
#define MENU_BUILD_WIDE_2  0x005B514A
#define MENU_BUILD_USTR    0x005B517A   /* push offset UnicodeString "build 488"      (+1 = imm) */
#define BUILD_TEXT         L"build 488 - Niox Patch"

/* WideString constant: 4-byte length (bytes) followed by the characters */
static struct { uint32_t len; wchar_t s[32]; } g_wideBuild = { sizeof(BUILD_TEXT) - 2, BUILD_TEXT };
/* UnicodeString constant: code page, element size, ref count -1 (constant), length */
static struct { uint16_t cp, elem; int32_t ref, len; wchar_t s[32]; } g_ustrBuild =
    { 1200, 2, -1, sizeof(BUILD_TEXT) / 2 - 1, BUILD_TEXT };

static void PatchBuildLabel(void)
{
    uint32_t oldWide = 0x005B53F0, oldUstr = 0x005B5410;
    uint32_t w = (uint32_t)(uintptr_t)g_wideBuild.s, u = (uint32_t)(uintptr_t)g_ustrBuild.s;
    if (CheckBytes(MENU_BUILD_WIDE_1 + 1, &oldWide, 4) && CheckBytes(MENU_BUILD_WIDE_2 + 1, &oldWide, 4) &&
        CheckBytes(MENU_BUILD_USTR + 1, &oldUstr, 4)) {
        WriteMem(MENU_BUILD_WIDE_1 + 1, &w, 4);
        WriteMem(MENU_BUILD_WIDE_2 + 1, &w, 4);
        WriteMem(MENU_BUILD_USTR + 1, &u, 4);
    }
}

void Gameplay_Install(void)
{
    PatchBuildLabel();
    if (!g_cfg.hardMode) return;
    static const uint8_t epilogue[] = { 0x8B, 0xC6, 0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x1C, 0x00 };
    if (CheckBytes(PATCH_GENERATECRIME_END, epilogue, sizeof(epilogue)) &&
        PatchJmp(PATCH_GENERATECRIME_END, (void *)Thunk_GenerateCrimeEnd, 5))
        Log("Hard mode enabled: criminal strength x%.2f", g_cfg.criminalStrength);
    static const uint8_t genPrologue[] = { 0x53, 0x56, 0x57, 0x83, 0xC4, 0xE8 };
    if (g_cfg.crimeFrequency > 1.0 && CheckBytes(ADDR_CrimeGenerator, genPrologue, sizeof(genPrologue)) &&
        PatchJmp(ADDR_CrimeGenerator, (void *)Thunk_CrimeGenerator, sizeof(genPrologue)))
        Log("Hard mode: crime frequency x%.2f", g_cfg.crimeFrequency);
}

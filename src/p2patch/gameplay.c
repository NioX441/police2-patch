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

#define ADDR_GAME_STATE      0x00A9E6C8
#define ADDR_GAME_TIME       (ADDR_GAME_STATE + 0x08)   /* TDateTime */
#define ADDR_CRIME_SLOTS     (ADDR_GAME_STATE + 0x58)
#define CRIME_SLOTS          19
#define MAX_CRIMES_PER_DAY   (CRIME_SLOTS + 64)

static uint16_t g_crimeQueue[MAX_CRIMES_PER_DAY];   /* minute of the day (h * 60 + m), sorted */
static int      g_queueLen, g_queuePos;
static int CompareU16(const void *a, const void *b) { return *(const uint16_t *)a - *(const uint16_t *)b; }

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

/* ---- Cheat: start a crime of a chosen type ---------------------------------------------- */
/* A free crime slot is set to the next game minute; TCaseList.InitCrime then starts the crime */
/* and the type it picked is replaced right before its "type 6 (car theft, unused) -> 2" check. */
/* Types: 0 robbery, 1 stabbing, 2 homicide, 3 burglary, 4 smuggling, 5 drug abuse,           */
/* 7 terrorism, 8 riot; -1 = random type chosen by the game.                                 */

#define PATCH_CRIME_TYPE     0x0060227F   /* InitCrime: cmp bl, 6 / jne 00602286              */
#define CRIME_TYPE_NOT6      0x00602286
#define CRIME_TYPE_IS6       0x00602284

static volatile int g_forcedCrime = -2;   /* -2 = none requested */
static int g_carTheft;                    /* car theft (type 6) enabled */

static __declspec(naked) void Thunk_CrimeType(void)
{
    __asm {
        mov  eax, g_forcedCrime
        cmp  eax, -2
        je   original
        mov  g_forcedCrime, -2
        cmp  eax, -1
        je   original
        mov  bl, al
    original:
        cmp  bl, 6
        jne  not6
        cmp  g_carTheft, 0
        jne  not6
        push CRIME_TYPE_IS6
        ret
    not6:
        push CRIME_TYPE_NOT6
        ret
    }
}

static const char *const g_crimeNames[] = { "robbery", "stabbing", "homicide", "burglary",
    "smuggling", "drug abuse", "car theft", "terrorism", "riot" };

static void SpawnCrime(int type)
{
    double now = *(const double *)ADDR_GAME_TIME;
    int t = (int)((now - floor(now)) * 1440.0 + 0.001) + 1;
    if (t % 60 == 0) t++;                   /* minute 0 is never used by the game */
    if (t >= 24 * 60) t = 1;                /* shortly after midnight */
    uint8_t *slots = (uint8_t *)ADDR_CRIME_SLOTS;
    int latest = -1, latestTime = -1;
    for (int i = 0; i < CRIME_SLOTS; i++) {
        int st = slots[2 * i] * 60 + slots[2 * i + 1];
        if (st > latestTime) { latestTime = st; latest = i; }
    }
    /* all slots in use (hard mode): the latest planned crime waits in the queue instead */
    if (latestTime > 0 && g_queueLen < MAX_CRIMES_PER_DAY) {
        BOOL full = TRUE;
        for (int i = 0; i < CRIME_SLOTS; i++) full &= slots[2 * i] || slots[2 * i + 1];
        if (full) {
            if (g_queuePos >= g_queueLen) g_queuePos = g_queueLen = 0;
            g_crimeQueue[g_queueLen++] = (uint16_t)latestTime;
            qsort(g_crimeQueue + g_queuePos, g_queueLen - g_queuePos, sizeof(g_crimeQueue[0]), CompareU16);
            slots[2 * latest] = slots[2 * latest + 1] = 0;
        }
    }
    for (int i = 0; i < CRIME_SLOTS; i++) {
        if (slots[2 * i] || slots[2 * i + 1]) continue;
        slots[2 * i] = (uint8_t)(t / 60);
        slots[2 * i + 1] = (uint8_t)(t % 60);
        g_forcedCrime = type;
        Log("Cheat: %s at %02d:%02d", type < 0 ? "random crime" : g_crimeNames[type], t / 60, t % 60);
        return;
    }
    Log("Cheat: no free crime slot");
}

/* ---- Car theft ------------------------------------------------------------------------- */
/* Crime type 6 (car theft) has a name, a dispatcher message and is handled like robbery,      */
/* stabbing, homicide and burglary when patrols look for criminals - but InitCrime replaces it */
/* with homicide, and ProcessCrime creates no criminal for it (and does not give him the       */
/* criminal look). With these two checks extended it plays like the other street crimes.      */

#define PATCH_CRIMINAL_CREATE  0x00603D31   /* sub al,6 / jb create / dec al / jne skip        */
#define PATCH_CRIMINAL_LOOK    0x00604E7E   /* sub al,6 / jb look / sub al,2 / jne skip        */

static void EnableCarTheft(void)
{
    static const uint8_t createOld[] = { 0x2C, 0x06, 0x72, 0x08, 0xFE, 0xC8, 0x0F, 0x85, 0xBC, 0x01, 0x00, 0x00 };
    /* cmp al,7 / jbe 00603D3D / jmp 00603EF9 / nop x3 : types 0..7 get a criminal */
    static const uint8_t createNew[] = { 0x3C, 0x07, 0x76, 0x08, 0xE9, 0xBF, 0x01, 0x00, 0x00, 0x90, 0x90, 0x90 };
    static const uint8_t lookOld[] = { 0x2C, 0x06, 0x72, 0x08, 0x2C, 0x02, 0x0F, 0x85, 0xFD, 0x04, 0x00, 0x00 };
    /* cmp al,6 / jbe 00604E8A / cmp al,8 / jne 00605387 : types 0..6 and 8 */
    static const uint8_t lookNew[] = { 0x3C, 0x06, 0x76, 0x08, 0x3C, 0x08, 0x0F, 0x85, 0xFD, 0x04, 0x00, 0x00 };
    if (!CheckBytes(PATCH_CRIMINAL_CREATE, createOld, sizeof(createOld)) ||
        !CheckBytes(PATCH_CRIMINAL_LOOK, lookOld, sizeof(lookOld)))
        return;
    WriteMem(PATCH_CRIMINAL_CREATE, createNew, sizeof(createNew));
    WriteMem(PATCH_CRIMINAL_LOOK, lookNew, sizeof(lookNew));
    g_carTheft = 1;
    Log("Car theft enabled");
}

/* Keyboard hook of the game thread: sees the keys regardless of which control has focus. */
static LRESULT CALLBACK KeyboardProc(int code, WPARAM vk, LPARAM lp)
{
    if (code == HC_ACTION && !(lp & 0x80000000) && !(lp & 0x40000000) &&
        (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
        if (vk == 'M' && g_cfg.cheats) AddMoney();
        if (vk == 'L' && g_cfg.cheats) RaiseSafety();
        if (g_cfg.cheats && vk >= '0' && vk <= '9' && (vk != '9' || g_carTheft)) {
            static const int types[] = { -1, 0, 1, 2, 3, 4, 5, 7, 8, 6 };   /* key 0 = random */
            SpawnCrime(types[vk - '0']);
        }
        if (vk == 'R' && g_cfg.cheats) SpawnCrime(-1);   /* Ctrl+Shift+0 is often taken by Windows */
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

static uint32_t g_rand = 0x2545F491;
static int RandInt(int n) { g_rand = g_rand * 1103515245u + 12345u; return (int)((g_rand >> 16) % (uint32_t)n); }

static double   g_lastGameTime;


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

/* ---- New crimes away from running cases ------------------------------------------------ */
/* The crime area (0x0060E590) is chosen at random among the 20 least safe map cells, so new  */
/* crimes often started exactly where another case was still running. The search is now     */
/* repeated (up to 8 times) while its result lies next to a running case.                    */

#define ADDR_ChooseCrimeLocation  0x0060E590
#define ADDR_CASELIST_VAR         0x00A9D820
#define CASE_X                    0x0C
#define CASE_STATE                0x39         /* 7 = escaped, 8 = closed */
#define CRIME_DISTANCE            8            /* cells */
#define LOCATION_TRIES            8

/* a case stores three map points: crime location (+0x0C), and the two points of the search */
static BOOL NearPoint(const uint8_t *c, int off, int x, int y)
{
    return abs(FIELD(c, off, int) - x) < CRIME_DISTANCE && abs(FIELD(c, off + 4, int) - y) < CRIME_DISTANCE;
}

static BOOL NearRunningCase(const int *p1, const int *p2)
{
    void *cases = *(void **)ADDR_CASELIST_VAR;
    if (!cases) return FALSE;
    for (int i = 0; i < ListCount(cases); i++) {
        const uint8_t *c = ListItem(cases, i);
        if (!c || c[CASE_STATE] >= 7) continue;
        for (int off = CASE_X; off <= 0x1C; off += 8)
            if (NearPoint(c, off, p1[0], p1[1]) || NearPoint(c, off, p2[0], p2[1])) return TRUE;
    }
    return FALSE;
}

/* original first instructions: push ebp / mov ebp, esp / push ecx / mov ecx, 10h */
static __declspec(naked) void Orig_ChooseCrimeLocation(void)
{
    __asm {
        push ebp
        mov  ebp, esp
        push ecx
        mov  ecx, 0x10
        push ADDR_ChooseCrimeLocation + 9
        ret
    }
}

/* params: the 6 stack parameters of the original call; params[4], params[5] = results */
static int __cdecl ChooseCrimeLocation(const uint32_t *params, uint32_t ra, uint32_t rd, uint32_t rc)
{
    int found = 0;
    for (int t = 0; t < LOCATION_TRIES; t++) {
        __asm {
            mov  esi, params
            push dword ptr [esi + 20]
            push dword ptr [esi + 16]
            push dword ptr [esi + 12]
            push dword ptr [esi + 8]
            push dword ptr [esi + 4]
            push dword ptr [esi]
            mov  eax, ra
            mov  edx, rd
            mov  ecx, rc
            call Orig_ChooseCrimeLocation
            movzx eax, al
            mov  found, eax
        }
        if (!found || !NearRunningCase((const int *)params[5], (const int *)params[4])) break;
    }
    return found;
}

/* register call: eax, edx, ecx + 6 stack parameters (callee removes them, ret 18h) */
static __declspec(naked) void Thunk_ChooseCrimeLocation(void)
{
    __asm {
        push ecx
        push edx
        push eax
        push esp
        add  dword ptr [esp], 16        /* -> first stack parameter */
        call ChooseCrimeLocation
        add  esp, 16
        ret  0x18
    }
}

/* ---- Crime window title ------------------------------------------------------------------- */
/* TGUIpCrime.ResetData looks up the title with the key "GUI CRIME"; the text file has it as  */
/* "[GUI CRIME]" (Tatort), so the window showed the raw key.                                  */

#define CRIME_TITLE_KEY  0x005D230A   /* mov edx, offset UnicodeString "GUI CRIME" (+1 = imm) */
#define CRIME_TITLE_TEXT L"[GUI CRIME]"

static struct { uint16_t cp, elem; int32_t ref, len; wchar_t s[16]; } g_ustrCrimeTitle =
    { 1200, 2, -1, sizeof(CRIME_TITLE_TEXT) / 2 - 1, CRIME_TITLE_TEXT };

static void PatchCrimeTitle(void)
{
    uint32_t oldKey = 0x005D2C70, key = (uint32_t)(uintptr_t)g_ustrCrimeTitle.s;
    if (*(const uint8_t *)CRIME_TITLE_KEY == 0xBA && CheckBytes(CRIME_TITLE_KEY + 1, &oldKey, 4))
        WriteMem(CRIME_TITLE_KEY + 1, &key, 4);
}

void Gameplay_Install(void)
{
    PatchCrimeTitle();
    static const uint8_t locPrologue[] = { 0x55, 0x8B, 0xEC, 0x51, 0xB9, 0x10, 0x00, 0x00, 0x00 };
    if (CheckBytes(ADDR_ChooseCrimeLocation, locPrologue, sizeof(locPrologue)) &&
        PatchJmp(ADDR_ChooseCrimeLocation, (void *)Thunk_ChooseCrimeLocation, sizeof(locPrologue)))
        Log("Crime location fix installed");
    PatchBuildLabel();
    if (g_cfg.carTheft) EnableCarTheft();
    static const uint8_t cmp6[] = { 0x80, 0xFB, 0x06, 0x75, 0x02 };
    if ((g_cfg.cheats || g_carTheft) && CheckBytes(PATCH_CRIME_TYPE, cmp6, sizeof(cmp6)))
        PatchJmp(PATCH_CRIME_TYPE, (void *)Thunk_CrimeType, sizeof(cmp6));
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

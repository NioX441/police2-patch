/*
 * userdata.c - Run without administrator rights, more memory.
 *
 * police2.exe requests administrator rights (manifest) only because it writes its save games
 * and options into its own folder below "Program Files". It is also a 32-bit program without
 * the "large address aware" flag, so it can only use 2 GB of memory. With all images of a big
 * city loaded, less than 300 MB of address space are left (largest free block often < 50 MB);
 * failing texture allocations show up as black or missing ground for a moment.
 *
 * 1. Saved data: every file the game writes into its folder is stored in
 *    %LOCALAPPDATA%\Police 2 Community Patch instead (copy on write). Files there take
 *    precedence when the game reads; saves still in the game folder are found as before. This
 *    works the same with and without administrator rights.
 * 2. police2_niox.exe: when the game runs with write access to its folder, the patch creates a
 *    copy of police2.exe in the game folder with two header changes: "large address aware"
 *    (4 GB of address space on 64-bit Windows) and execution level "asInvoker" instead of
 *    "requireAdministrator". police2.exe then starts this copy automatically; it can also be
 *    started directly without administrator rights. The original police2.exe is not changed.
 */
#include "patch.h"
#include "game.h"
#include <stdio.h>

#define COPY_NAME L"police2_niox.exe"

static wchar_t g_gameDirW[MAX_PATH];
static size_t  g_gameDirLen;
static wchar_t g_userDirW[MAX_PATH];

/* ---- paths ------------------------------------------------------------------------------- */

/* creates a directory and its parents */
static void CreateDirs(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    wcscpy_s(p, MAX_PATH, dir);
    for (wchar_t *c = p + 3; *c; c++)
        if (*c == L'\\') { *c = 0; CreateDirectoryW(p, NULL); *c = L'\\'; }
    CreateDirectoryW(p, NULL);
}

static void InitDirs(void)
{
    if (g_gameDirLen) return;
    GetModuleFileNameW(NULL, g_gameDirW, MAX_PATH);
    wchar_t *slash = wcsrchr(g_gameDirW, L'\\');
    if (slash) slash[1] = 0;
    g_gameDirLen = wcslen(g_gameDirW);
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", g_userDirW, MAX_PATH - 40);
    if (n > 0 && n < MAX_PATH - 40) {
        wcscat_s(g_userDirW, MAX_PATH, L"\\Police 2 Community Patch\\");
        CreateDirs(g_userDirW);
    } else {
        g_userDirW[0] = 0;
    }
}

const wchar_t *UserData_Dir(void) { InitDirs(); return g_userDirW; }

/* Maps a path inside the game folder to the user folder. FALSE for all other paths. */
static BOOL MapPath(LPCWSTR in, wchar_t *game, wchar_t *user)
{
    if (!in || !g_userDirW[0]) return FALSE;
    DWORD n = GetFullPathNameW(in, MAX_PATH, game, NULL);
    if (n == 0 || n >= MAX_PATH) return FALSE;
    if (_wcsnicmp(game, g_gameDirW, g_gameDirLen) != 0 || !game[g_gameDirLen]) return FALSE;
    if (wcslen(g_userDirW) + wcslen(game + g_gameDirLen) >= MAX_PATH) return FALSE;
    wcscpy_s(user, MAX_PATH, g_userDirW);
    wcscat_s(user, MAX_PATH, game + g_gameDirLen);
    return TRUE;
}

static BOOL Exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static void CreateParentDir(const wchar_t *file)
{
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, file);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) { *slash = 0; CreateDirs(dir); }
}

/* ---- file API hooks (import table of police2.exe) --------------------------------------- */

#define IAT_CreateFileW_1         0x00AA0250
#define IAT_CreateFileW_2         0x00A9FC14
#define IAT_GetFileAttributesW    0x00AA01D4
#define IAT_FindFirstFileW_1      0x00AA0224
#define IAT_FindFirstFileW_2      0x00A9FBF4
#define IAT_DeleteFileW           0x00AA0244
#define IAT_MoveFileW             0x00AA015C

static HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                      DWORD disp, DWORD flags, HANDLE tmpl)
{
    wchar_t game[MAX_PATH], user[MAX_PATH];
    if (!MapPath(name, game, user))
        return CreateFileW(name, access, share, sa, disp, flags, tmpl);
    if (Exists(user))
        return CreateFileW(user, access, share, sa, disp, flags, tmpl);
    BOOL inGame = Exists(game);
    BOOL write = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) ||
                 disp == CREATE_ALWAYS || disp == CREATE_NEW || disp == TRUNCATE_EXISTING ||
                 (disp == OPEN_ALWAYS && !inGame);
    if (!write)
        return CreateFileW(game, access, share, sa, disp, flags, tmpl);
    if (inGame && disp == CREATE_NEW) {
        SetLastError(ERROR_FILE_EXISTS);
        return INVALID_HANDLE_VALUE;
    }
    CreateParentDir(user);
    if (inGame && disp != CREATE_ALWAYS && disp != TRUNCATE_EXISTING)
        CopyFileW(game, user, TRUE);                   /* modify a copy */
    return CreateFileW(user, access, share, sa, disp, flags, tmpl);
}

static DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR name)
{
    wchar_t game[MAX_PATH], user[MAX_PATH];
    if (MapPath(name, game, user)) {
        DWORD a = GetFileAttributesW(user);
        if (a != INVALID_FILE_ATTRIBUTES) return a;
    }
    return GetFileAttributesW(name);
}

static HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
    wchar_t game[MAX_PATH], user[MAX_PATH];
    if (!wcspbrk(name, L"*?") && MapPath(name, game, user) && Exists(user))
        return FindFirstFileW(user, data);
    return FindFirstFileW(name, data);
}

static BOOL WINAPI Hook_DeleteFileW(LPCWSTR name)
{
    wchar_t game[MAX_PATH], user[MAX_PATH];
    if (MapPath(name, game, user) && Exists(user)) {
        BOOL r = DeleteFileW(user);
        if (Exists(game)) DeleteFileW(game);            /* only possible with write access */
        return r;
    }
    return DeleteFileW(name);
}

static BOOL WINAPI Hook_MoveFileW(LPCWSTR from, LPCWSTR to)
{
    wchar_t game1[MAX_PATH], user1[MAX_PATH], game2[MAX_PATH], user2[MAX_PATH];
    if (MapPath(from, game1, user1) && MapPath(to, game2, user2)) {
        if (!Exists(user1)) {
            if (!Exists(game1)) return MoveFileW(from, to);
            CreateParentDir(user1);
            if (!CopyFileW(game1, user1, TRUE)) return FALSE;
        }
        CreateParentDir(user2);
        return MoveFileW(user1, user2);
    }
    return MoveFileW(from, to);
}

void UserData_Install(void)
{
    if (!g_cfg.userDataFolder) return;
    InitDirs();
    if (!g_userDirW[0]) {
        Log("User data folder not available, saving in the game folder");
        return;
    }
    BOOL ok = HookIat(IAT_CreateFileW_1, "kernel32.dll", "CreateFileW", (void *)Hook_CreateFileW) != NULL;
    ok &= HookIat(IAT_CreateFileW_2, "kernel32.dll", "CreateFileW", (void *)Hook_CreateFileW) != NULL;
    ok &= HookIat(IAT_GetFileAttributesW, "kernel32.dll", "GetFileAttributesW", (void *)Hook_GetFileAttributesW) != NULL;
    ok &= HookIat(IAT_FindFirstFileW_1, "kernel32.dll", "FindFirstFileW", (void *)Hook_FindFirstFileW) != NULL;
    ok &= HookIat(IAT_FindFirstFileW_2, "kernel32.dll", "FindFirstFileW", (void *)Hook_FindFirstFileW) != NULL;
    ok &= HookIat(IAT_DeleteFileW, "kernel32.dll", "DeleteFileW", (void *)Hook_DeleteFileW) != NULL;
    ok &= HookIat(IAT_MoveFileW, "kernel32.dll", "MoveFileW", (void *)Hook_MoveFileW) != NULL;
    if (ok) Log("Save games and options are stored in %ls", g_userDirW);
}

/* ---- police2_niox.exe ------------------------------------------------------------------- */

static BOOL IsLargeAddressAware(const uint8_t *image)
{
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

static uint8_t *ReadWholeFile(const wchar_t *path, DWORD *size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(h, NULL), got = 0;
    uint8_t *buf = (n != INVALID_FILE_SIZE && n > 0x1000 && n < 64u << 20) ? (uint8_t *)HeapAlloc(GetProcessHeap(), 0, n) : NULL;
    if (buf && (!ReadFile(h, buf, n, &got, NULL) || got != n)) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    CloseHandle(h);
    *size = n;
    return buf;
}

/* Builds police2_niox.exe from police2.exe. TRUE if the copy is up to date. */
static BOOL EnsureCopy(const wchar_t *exe, const wchar_t *copy)
{
    WIN32_FILE_ATTRIBUTE_DATA a, b;
    if (GetFileAttributesExW(exe, GetFileExInfoStandard, &a) &&
        GetFileAttributesExW(copy, GetFileExInfoStandard, &b) &&
        a.nFileSizeLow == b.nFileSizeLow && CompareFileTime(&b.ftLastWriteTime, &a.ftLastWriteTime) >= 0)
        return TRUE;

    DWORD size;
    uint8_t *data = ReadWholeFile(exe, &size);
    if (!data) return FALSE;
    BOOL ok = FALSE;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)data;
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && (DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) < size) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(data + dos->e_lfanew);
        nt->FileHeader.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        static const char from[] = "level=\"requireAdministrator\"";
        static const char to[]   = "level=\"asInvoker\"           ";   /* same length */
        for (DWORD i = 0; i + sizeof(from) - 1 <= size; i++)
            if (memcmp(data + i, from, sizeof(from) - 1) == 0) { memcpy(data + i, to, sizeof(to) - 1); break; }
        wchar_t tmp[MAX_PATH];
        wcscpy_s(tmp, MAX_PATH, copy);
        wcscat_s(tmp, MAX_PATH, L".tmp");
        HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            ok = WriteFile(h, data, size, &w, NULL) && w == size;
            CloseHandle(h);
            ok = ok && MoveFileExW(tmp, copy, MOVEFILE_REPLACE_EXISTING);
            if (!ok) DeleteFileW(tmp);
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    if (ok) Log("Created %ls (4 GB memory, no administrator rights needed)", copy);
    return ok;
}

/* Called before the game starts. TRUE when police2_niox.exe was started instead. */
BOOL UserData_Relaunch(void)
{
    if (!g_cfg.largeMemory) return FALSE;
    if (IsLargeAddressAware((const uint8_t *)GetModuleHandleW(NULL))) return FALSE;
    InitDirs();
    wchar_t exe[MAX_PATH], copy[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wcscpy_s(copy, MAX_PATH, g_gameDirW);
    wcscat_s(copy, MAX_PATH, COPY_NAME);
    if (_wcsicmp(exe, copy) == 0) return FALSE;
    if (!EnsureCopy(exe, copy)) {
        Log("%ls could not be created - running with 2 GB memory", copy);
        return FALSE;
    }

    /* same arguments: skip the program name of the command line */
    const wchar_t *args = GetCommandLineW();
    BOOL quoted = FALSE;
    for (; *args && (quoted || (*args != L' ' && *args != L'\t')); args++)
        if (*args == L'"') quoted = !quoted;
    static wchar_t cmd[2 * MAX_PATH + 1024];
    swprintf_s(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%s\"%s", copy, args);

    STARTUPINFOW parent, si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    GetStartupInfoW(&parent);
    si.dwFlags = parent.dwFlags & STARTF_USESHOWWINDOW;
    si.wShowWindow = parent.wShowWindow;
    if (!CreateProcessW(copy, cmd, NULL, NULL, FALSE, 0, NULL, g_gameDirW, &si, &pi)) {
        Log("Starting %ls failed (%u) - running with 2 GB memory", copy, GetLastError());
        return FALSE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

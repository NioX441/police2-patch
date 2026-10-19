/*
 * errors.c - Make swallowed game errors visible.
 *
 * TMainForm.ApplicationEvents1Exception, the global exception handler of the game, is empty:
 * every runtime error is silently ignored and the affected unit/case just stops working.
 * We point the published method of the handler to a logger that writes the error to
 * p2patch.log (the game still continues as before).
 */
#include "patch.h"
#include "game.h"

#define METHOD_ENTRY_LEGACY   0x005E7159   /* published method table entry (code pointer) */
#define METHOD_ENTRY_EXTENDED 0x005E7931   /* extended RTTI method entry  (code pointer) */

typedef struct { uint32_t hash; int count; } Seen;
static Seen g_seen[256];

static void __cdecl LogException(const uint8_t *exc)
{
    char cls[64] = "?", msg[512] = "";
    __try {
        const uint8_t *vmt = *(const uint8_t **)exc;
        const uint8_t *name = *(const uint8_t **)(vmt - 56);
        int n = name[0] < 63 ? name[0] : 63;
        memcpy(cls, name + 1, n);
        cls[n] = 0;
        const wchar_t *m = *(const wchar_t **)(exc + 4);
        if (m) WideCharToMultiByte(CP_ACP, 0, m, -1, msg, sizeof(msg), NULL, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    uint32_t h = 2166136261u;
    for (const char *p = cls; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    for (const char *p = msg; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    Seen *s = &g_seen[h & 255];
    if (s->hash != h) { s->hash = h; s->count = 0; }
    s->count++;
    if (s->count <= 5 || s->count % 1000 == 0)
        Log("Game error (#%d): %s: %s", s->count, cls, msg);
}

/* procedure TMainForm.ApplicationEvents1Exception(Sender: TObject; E: Exception) - E in ecx */
static __declspec(naked) void Handler(void)
{
    __asm {
        push ecx
        call LogException
        add  esp, 4
        ret
    }
}

void Errors_Install(void)
{
    if (!g_cfg.logExceptions) return;
    uint32_t expected = ADDR_AppException;
    void *h = (void *)Handler;
    BOOL ok = TRUE;
    if (CheckBytes(METHOD_ENTRY_LEGACY, &expected, 4)) ok &= WriteMem(METHOD_ENTRY_LEGACY, &h, 4);
    else ok = FALSE;
    if (CheckBytes(METHOD_ENTRY_EXTENDED, &expected, 4)) ok &= WriteMem(METHOD_ENTRY_EXTENDED, &h, 4);
    if (ok) Log("Exception logging installed");
}

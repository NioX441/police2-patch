/*
 * groups.c - Keep team (group) membership of officers consistent.
 *
 * When an officer returns from the hospital or from training (0x005AB514, called by the game
 * timer), the game respawns him at his station and marks him as "not in a team"
 * (TPoliceman+0x230 = -1) - but it does not remove him from the member list of his old team.
 * He is shown as available, the player assigns him to another team, and from then on the same
 * officer belongs to two teams and receives contradicting orders: he walks back and forth,
 * blocks missions or never returns to the station. Save games created with the original game
 * often already contain such officers.
 *
 * Fix: after each run of that routine the member lists are checked: officers marked as
 * "not in a team" are removed from team lists, and officers listed in several teams are kept
 * only in the team they belong to.
 */
#include "patch.h"
#include "game.h"

#define ADDR_GROUPLIST_VAR        0x0062F50C   /* TGroupList instance         */
#define ADDR_TimerRoutine         0x005AB514   /* returning officers           */
#define CALL_TimerRoutine         0x005F48CB   /* in TMainForm.TimerEvent      */
#define ADDR_TPolicemanList_Remove 0x005A1DC8

#define GRP_ID       0x04
#define GRP_WOMEN    0x48
#define GRP_MEMBERS  0x4C
#define PM_GROUP     0x230
#define PM_FEMALE    0x269

#define MAX_PAIRS 4096

static void RemoveMember(uint8_t *grp, uint8_t *pm)
{
    void *list = FIELD(grp, GRP_MEMBERS, void *);
    __asm {
        mov  eax, list
        mov  edx, pm
        mov  ecx, ADDR_TPolicemanList_Remove
        call ecx
    }
    if (pm[PM_FEMALE] && FIELD(grp, GRP_WOMEN, int) > 0) FIELD(grp, GRP_WOMEN, int)--;
}

typedef struct { uint8_t *pm; uint8_t *grp; int gi; } Pair;
static Pair g_pairs[MAX_PAIRS];

static void __cdecl SanitizeGroups(void)
{
    void *groups = *(void **)ADDR_GROUPLIST_VAR;
    if (!groups) return;
    int ng = ListCount(groups);

    /* 1) officers flagged as "not in a team" must not be listed in a team */
    for (int gi = 0; gi < ng; gi++) {
        uint8_t *g = ListItem(groups, gi);
        void *members = FIELD(g, GRP_MEMBERS, void *);
        if (!members) continue;
        for (int i = ListCount(members) - 1; i >= 0; i--) {
            uint8_t *pm = ListItem(members, i);
            if (FIELD(pm, PM_GROUP, int) == -1) {
                Log("Groups: officer %p released but still listed in team %d - removed",
                    pm, FIELD(g, GRP_ID, int));
                RemoveMember(g, pm);
            }
        }
    }

    /* 2) officers listed in more than one team */
    int n = 0;
    for (int gi = 0; gi < ng && n < MAX_PAIRS; gi++) {
        uint8_t *g = ListItem(groups, gi);
        void *members = FIELD(g, GRP_MEMBERS, void *);
        if (!members) continue;
        for (int i = 0; i < ListCount(members) && n < MAX_PAIRS; i++) {
            g_pairs[n].pm = ListItem(members, i);
            g_pairs[n].grp = g;
            g_pairs[n].gi = gi;
            n++;
        }
    }
    for (int a = 0; a < n; a++) {
        uint8_t *pm = g_pairs[a].pm;
        if (!pm) continue;
        int count = 0, keep = -1, keepScore = -1;
        for (int b = a; b < n; b++) {
            if (g_pairs[b].pm != pm) continue;
            count++;
            int ref = FIELD(pm, PM_GROUP, int);
            int score = ref == FIELD(g_pairs[b].grp, GRP_ID, int) ? 2 : ref == g_pairs[b].gi ? 1 : 0;
            if (score >= keepScore) { keepScore = score; keep = b; }
        }
        if (count > 1) {
            for (int b = a; b < n; b++) {
                if (g_pairs[b].pm != pm) continue;
                if (b != keep) {
                    Log("Groups: officer %p was in teams %d and %d - removed from team %d", pm,
                        FIELD(g_pairs[keep].grp, GRP_ID, int), FIELD(g_pairs[b].grp, GRP_ID, int),
                        FIELD(g_pairs[b].grp, GRP_ID, int));
                    RemoveMember(g_pairs[b].grp, pm);
                }
                g_pairs[b].pm = NULL;
            }
        }
    }
}

static __declspec(naked) void Thunk_TimerRoutine(void)
{
    __asm {
        mov  ecx, ADDR_TimerRoutine
        call ecx
        call SanitizeGroups
        ret
    }
}

void Groups_Install(void)
{
    if (!g_cfg.fixGroupMembership) return;
    if (RedirectCall(CALL_TimerRoutine, ADDR_TimerRoutine, (void *)Thunk_TimerRoutine))
        Log("Team membership fix installed");
}

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

/* Injured officers of patrols and checkpoints stayed on the street.
 *
 * An officer who loses a fight gets the state "in hospital" (+0x22C = 0x10), the lying
 * animation and a return time. He is only removed from the street when his team finishes a
 * case; teams on patrol or at a checkpoint never do, so the officer lay there (with the duty
 * icon over his head) until he came back from hospital days later. Now injured officers are
 * taken away after INJURED_TICKS game frames, like officers of other teams.
 */
#define PM_STATE        0x22C
#define PM_IN_HOSPITAL  0x10
#define SPR_VISIBLE     0x38
#define INJURED_TICKS   300          /* 10 seconds at 30 game frames per second */
#define INJURED_SLOTS   256

typedef struct { uint8_t *pm; int ticks; unsigned seen; } Injured;
static Injured g_injured[INJURED_SLOTS];
static unsigned g_pass;

static void RemoveInjured(void)
{
    void *groups = *(void **)ADDR_GROUPLIST_VAR;
    if (!groups) return;
    g_pass++;
    int ng = ListCount(groups);
    for (int gi = 0; gi < ng; gi++) {
        uint8_t *g = ListItem(groups, gi);
        void *members = FIELD(g, GRP_MEMBERS, void *);
        if (!members) continue;
        for (int i = 0; i < ListCount(members); i++) {
            uint8_t *pm = ListItem(members, i);
            if (pm[PM_STATE] != PM_IN_HOSPITAL || !pm[SPR_VISIBLE]) continue;
            Injured *e = NULL, *free = NULL;
            for (int k = 0; k < INJURED_SLOTS && !e; k++) {
                if (g_injured[k].pm == pm) e = &g_injured[k];
                else if (!g_injured[k].pm && !free) free = &g_injured[k];
            }
            if (!e) {
                if (!free) continue;
                e = free;
                e->pm = pm;
                e->ticks = 0;
            }
            e->seen = g_pass;
            if (++e->ticks >= INJURED_TICKS) {
                pm[SPR_VISIBLE] = 0;
                e->pm = NULL;
                Log("Groups: injured officer %p of team %d taken to hospital", pm, FIELD(g, GRP_ID, int));
            }
        }
    }
    for (int i = 0; i < INJURED_SLOTS; i++)        /* forget officers no longer lying there */
        if (g_injured[i].pm && g_injured[i].seen != g_pass) g_injured[i].pm = NULL;
}

static void __cdecl AfterTimerRoutine(void)
{
    SanitizeGroups();
    RemoveInjured();
}

static __declspec(naked) void Thunk_TimerRoutine(void)
{
    __asm {
        mov  ecx, ADDR_TimerRoutine
        call ecx
        call AfterTimerRoutine
        ret
    }
}

/* Teams with an injured officer could still be sent to a crime (TGUIpCrime.CollectUnits).
   They are no longer listed until everybody is back from hospital. */
#define CALL_CRIME_LIST_INSERT  0x005D2270   /* TGroupList.Insert(list, index, team)        */
#define ADDR_TGroupList_Insert  0x005A8638

static int __cdecl TeamInjured(const uint8_t *g)
{
    void *members = FIELD(g, GRP_MEMBERS, void *);
    if (members)
        for (int i = 0; i < ListCount(members); i++)
            if (((const uint8_t *)ListItem(members, i))[PM_STATE] == PM_IN_HOSPITAL) return 1;
    return 0;
}

static __declspec(naked) void Thunk_CrimeListInsert(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push ecx
        call TeamInjured
        add  esp, 4
        test eax, eax
        pop  ecx
        pop  edx
        pop  eax
        jnz  skip
        push ADDR_TGroupList_Insert
    skip:
        ret
    }
}

void Groups_Install(void)
{
    RedirectCall(CALL_CRIME_LIST_INSERT, ADDR_TGroupList_Insert, (void *)Thunk_CrimeListInsert);
    if (!g_cfg.fixGroupMembership) return;
    if (RedirectCall(CALL_TimerRoutine, ADDR_TimerRoutine, (void *)Thunk_TimerRoutine))
        Log("Team membership fix installed");
}

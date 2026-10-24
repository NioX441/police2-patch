/*
 * paths.c - Path finding fixes.
 *
 * 1) Officers on foot (TPoliceman) use an A* search over the 288x288 map grid
 *    (0x0059CF98). Problems of the original:
 *      - If the destination cannot be reached (e.g. the target cell is not walkable), the
 *        search floods the whole map using linear list scans (freezes the game for a moment)
 *        and returns a path that only contains the start cell. The officer then stands still
 *        forever because his destination is never reached - "units stuck at a mission" or
 *        "never find home".
 *    The replacement is a fast binary-heap A* with the same walkability rules. If the
 *    destination is unreachable, it walks to the reachable cell closest to the destination
 *    and then straight to the destination, so the officer always arrives.
 *
 * 2) TPoliceman.DoMove: when the officer reached the last waypoint but this is not his
 *    destination (destination changed in the meantime), nothing happened and the officer
 *    froze. Now a new route is requested.
 *
 * 3) Police cars (TPolicecar) use an A* on the crossroad graph (0x0059D8D8).
 *      - The graph builder (0x00598560) overwrites the global crossroad entry/exit markers
 *        of the map with temporary start/destination nodes of the current search. They are
 *        restored after each search.
 *      - When no route is found, an empty route is returned. The car then stands still and
 *        repeats the (expensive) search every frame. We retry with the other start
 *        directions and finally drive to the nearest reachable road cell at the destination.
 *      - A car standing off the road (sidewalk, lawn) never finds a route; it first drives
 *        back onto the nearest road cell.
 */
#include "patch.h"
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define N_CELLS (MAP_SIZE * MAP_SIZE)

static __inline uint8_t *Cell(int x, int y)
{
    return (uint8_t *)ADDR_MAP_GRID + x * MAP_ROW_SIZE + y * MAP_CELL_SIZE;
}

static __inline BOOL Walkable(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_SIZE || y >= MAP_SIZE) return FALSE;
    uint8_t t = Cell(x, y)[CELL_TYPE];
    return (t != 0 && t < 3) || t == 4;
}

static __inline float ScreenDist(int x1, int y1, int x2, int y2)
{
    float dx = (float)(FIELD(Cell(x1, y1), CELL_SCREEN_X, int) - FIELD(Cell(x2, y2), CELL_SCREEN_X, int));
    float dy = (float)(FIELD(Cell(x1, y1), CELL_SCREEN_Y, int) - FIELD(Cell(x2, y2), CELL_SCREEN_Y, int));
    return sqrtf(dx * dx + dy * dy);
}

static void *NewWayPoint(void *list, int x, int y)
{
    void *wp = DelphiCreate(VMT_TWayPoint);
    FIELD(wp, 4, int) = x;
    FIELD(wp, 8, int) = y;
    DelphiListAdd(list, wp);
    return wp;
}

/* ---------------------------------------------------------------------------------------- */
/* 1) Pedestrian A*                                                                            */
/* ---------------------------------------------------------------------------------------- */

static float    g_g[N_CELLS];
static int      g_parent[N_CELLS];
static uint32_t g_stamp[N_CELLS];   /* search id in which the node was opened */
static uint8_t  g_closed[N_CELLS];
static uint32_t g_search;
static int      g_heap[N_CELLS];
static float    g_heapF[N_CELLS];
static int      g_heapN;
static int      g_path[N_CELLS];
static CRITICAL_SECTION g_pathLock;

static void HeapPush(int node, float f)
{
    int i = g_heapN++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (g_heapF[p] <= f) break;
        g_heap[i] = g_heap[p];
        g_heapF[i] = g_heapF[p];
        i = p;
    }
    g_heap[i] = node;
    g_heapF[i] = f;
}

static int HeapPop(void)
{
    int top = g_heap[0];
    int node = g_heap[--g_heapN];
    float f = g_heapF[g_heapN];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= g_heapN) break;
        if (c + 1 < g_heapN && g_heapF[c + 1] < g_heapF[c]) c++;
        if (g_heapF[c] >= f) break;
        g_heap[i] = g_heap[c];
        g_heapF[i] = g_heapF[c];
        i = c;
    }
    g_heap[i] = node;
    g_heapF[i] = f;
    return top;
}

static void *__cdecl WalkerAStar(int fx, int fy, int tx, int ty)
{
    void *list = DelphiCreate(VMT_TWayPointList);
    if (fx < 0 || fy < 0 || fx >= MAP_SIZE || fy >= MAP_SIZE ||
        tx < 0 || ty < 0 || tx >= MAP_SIZE || ty >= MAP_SIZE) {
        NewWayPoint(list, fx, fy);
        if (tx != fx || ty != fy) NewWayPoint(list, tx, ty);
        return list;
    }

    EnterCriticalSection(&g_pathLock);
    if (++g_search == 0) {                /* wrap-around: reset stamps */
        memset(g_stamp, 0, sizeof(g_stamp));
        g_search = 1;
    }
    memset(g_closed, 0, sizeof(g_closed));
    g_heapN = 0;

    int start = fx * MAP_SIZE + fy, goal = tx * MAP_SIZE + ty;
    int best = start;
    float bestH = ScreenDist(fx, fy, tx, ty);
    g_g[start] = 0.0f;
    g_parent[start] = -1;
    g_stamp[start] = g_search;
    HeapPush(start, bestH);

    static const int DX[4] = { -1, 1, 0, 0 }, DY[4] = { 0, 0, -1, 1 };
    BOOL found = FALSE;
    while (g_heapN > 0) {
        int n = HeapPop();
        if (g_closed[n]) continue;
        g_closed[n] = 1;
        if (n == goal) { found = TRUE; best = n; break; }
        int x = n / MAP_SIZE, y = n % MAP_SIZE;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!Walkable(nx, ny)) continue;
            int m = nx * MAP_SIZE + ny;
            if (g_closed[m]) continue;
            float g = g_g[n] + ScreenDist(x, y, nx, ny);
            if (g_stamp[m] == g_search && g >= g_g[m]) continue;
            g_stamp[m] = g_search;
            g_g[m] = g;
            g_parent[m] = n;
            float h = ScreenDist(nx, ny, tx, ty);
            if (h < bestH) { bestH = h; best = m; }
            HeapPush(m, g + h);
        }
    }

    int len = 0;
    for (int n = best; n >= 0 && len < N_CELLS; n = g_parent[n]) g_path[len++] = n;
    for (int i = len - 1; i >= 0; i--)
        NewWayPoint(list, g_path[i] / MAP_SIZE, g_path[i] % MAP_SIZE);
    LeaveCriticalSection(&g_pathLock);

    if (!found) {
        /* unreachable: finish the way in a straight line */
        if (best != goal) NewWayPoint(list, tx, ty);
        Log("Walker path %d,%d -> %d,%d unreachable, using closest cell %d,%d",
            fx, fy, tx, ty, best / MAP_SIZE, best % MAP_SIZE);
    }
    return list;
}

/* Delphi: function(fx, fy, tx: Integer {eax, edx, ecx}; ty: Integer {stack}): TWayPointList */
static __declspec(naked) void Thunk_WalkerAStar(void)
{
    __asm {
        push dword ptr [esp + 4]     /* ty */
        push ecx                     /* tx */
        push edx                     /* fy */
        push eax                     /* fx */
        call WalkerAStar
        add  esp, 16
        ret  4
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 2) Officer reached the last waypoint but not the destination                                */
/* ---------------------------------------------------------------------------------------- */

static void __cdecl WalkerPathEnd(uint8_t *pm)
{
    if (FIELD(pm, PM_TARGET_X, int) == FIELD(pm, PM_DEST_X, int) &&
        FIELD(pm, PM_TARGET_Y, int) == FIELD(pm, PM_DEST_Y, int))
        return;
    __asm {
        mov  eax, pm
        xor  edx, edx
        mov  ecx, ADDR_GetPolicemanDest
        call ecx
    }
}

/* replaces at 0x005A184F: dec eax / cmp eax,[esi+244h] / jle 005A18B2   (eax = waypoint count) */
static __declspec(naked) void Thunk_WalkerPathEnd(void)
{
    __asm {
        dec  eax
        cmp  eax, dword ptr [esi + 0x244]
        jle  at_end
        ret                          /* more waypoints: continue original code */
    at_end:
        add  esp, 4                  /* drop return address */
        push esi
        call WalkerPathEnd
        add  esp, 4
        push PATCH_WALKER_PATHEND_RET
        ret
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 3) Police car route search                                                                  */
/* ---------------------------------------------------------------------------------------- */

#define CAR_DEST_RADIUS 10      /* cells around an unreachable destination */
#define CAR_DEST_TRIES  16      /* route searches at most */
#define CAR_ROAD_RADIUS 3       /* cells around a car that stands off the road */
#define CAR_ROAD_TRIES  3       /* road cells tried from there */

static uint8_t g_inout[N_CELLS];

static void SaveInOut(void)
{
    for (int i = 0; i < N_CELLS; i++)
        g_inout[i] = Cell(i / MAP_SIZE, i % MAP_SIZE)[CELL_INOUT];
}

static void RestoreInOut(void)
{
    for (int i = 0; i < N_CELLS; i++)
        Cell(i / MAP_SIZE, i % MAP_SIZE)[CELL_INOUT] = g_inout[i];
}

static void *CallCarAStar(int startX, int startY, int destX, int *len, int dir, int destY)
{
    void *r;
    __asm {
        push destY
        push dir
        push len
        mov  eax, startX
        mov  edx, startY
        mov  ecx, destX
        mov  ebx, ADDR_CarAStar
        call ebx
        mov  r, eax
    }
    return r;
}

/* cars drive on road (3) and crossing (2) cells */
static __inline BOOL CarCell(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_SIZE || y >= MAP_SIZE) return FALSE;
    uint8_t t = Cell(x, y)[CELL_TYPE];
    return t == 2 || t == 3;
}

/* the original search, retried with the other start directions; NULL if there is no route */
static void *TryCarRoute(int startX, int startY, int destX, int destY, int *len, int dir)
{
    for (int d = dir, i = 0; i <= 4; d = ++i) {
        if (i > 0 && d == dir) continue;
        void *list = CallCarAStar(startX, startY, destX, len, d, destY);
        RestoreInOut();
        if (list && ListCount(list) >= 2) {
            if (d != dir)
                Log("Car route %d,%d -> %d,%d found with start direction %d instead of %d",
                    startX, startY, destX, destY, d, dir);
            return list;
        }
        if (list) DelphiFree(list);
    }
    return NULL;
}

/* No route: the destination is usually not on a road (e.g. a crime on a sidewalk or in a
   courtyard). Route to the nearest road cell around it instead. (The patch used to fall back
   to a straight line here, which let cars slide over buildings.) */
static void *CarRouteNearDest(int startX, int startY, int destX, int destY, int *len, int dir)
{
    void *list = TryCarRoute(startX, startY, destX, destY, len, dir);
    if (list) return list;
    int tries = 0;
    for (int r = 1; r <= CAR_DEST_RADIUS && tries < CAR_DEST_TRIES; r++) {
        for (int dx = -r; dx <= r && tries < CAR_DEST_TRIES; dx++) {
            for (int dy = -r; dy <= r && tries < CAR_DEST_TRIES; dy++) {
                if (max(abs(dx), abs(dy)) != r) continue;           /* ring of radius r */
                int x = destX + dx, y = destY + dy;
                if (!CarCell(x, y)) continue;
                tries++;
                list = CallCarAStar(startX, startY, x, len, dir, y);
                RestoreInOut();
                if (list && ListCount(list) >= 2) {
                    Log("Car route %d,%d -> %d,%d not found, driving to %d,%d instead",
                        startX, startY, destX, destY, x, y);
                    return list;
                }
                if (list) DelphiFree(list);
            }
        }
    }
    return NULL;
}

/* puts a waypoint at the front of a route */
static void PrependWayPoint(void *list, int x, int y)
{
    NewWayPoint(list, x, y);
    void **items = *(void ***)((uint8_t *)list + 4);
    int n = ListCount(list);
    void *wp = items[n - 1];
    memmove(items + 1, items, (n - 1) * sizeof(void *));
    items[0] = wp;
}

static void *__cdecl CarAStar(int startX, int startY, int destX, int *len, int dir, int destY)
{
    SaveInOut();
    void *list = CarRouteNearDest(startX, startY, destX, destY, len, dir);
    if (list) return list;

    /* A car standing next to the road (e.g. pushed onto a sidewalk or a lawn) never finds a
       route from there: drive back onto the nearest road cell first. */
    if (!CarCell(startX, startY)) {
        int tries = 0;
        for (int r = 1; r <= CAR_ROAD_RADIUS && tries < CAR_ROAD_TRIES; r++) {
            for (int dx = -r; dx <= r && tries < CAR_ROAD_TRIES; dx++) {
                for (int dy = -r; dy <= r && tries < CAR_ROAD_TRIES; dy++) {
                    if (max(abs(dx), abs(dy)) != r) continue;
                    int x = startX + dx, y = startY + dy;
                    if (!CarCell(x, y)) continue;
                    tries++;
                    list = CarRouteNearDest(x, y, destX, destY, len, dir);
                    if (list) {
                        PrependWayPoint(list, startX, startY);
                        Log("Car at %d,%d is off the road, drives back onto the road at %d,%d",
                            startX, startY, x, y);
                        return list;
                    }
                }
            }
        }
    }

    /* nothing reachable: stay here (the officers walk the rest) instead of freezing forever */
    list = DelphiCreate(VMT_TWayPointList);
    NewWayPoint(list, startX, startY);
    NewWayPoint(list, startX, startY);
    if (len) *len = 0;
    Log("Car route %d,%d -> %d,%d not found, car stops here", startX, startY, destX, destY);
    return list;
}

/* Delphi: (sx, sy, dx {eax, edx, ecx}; [esp+4] = var len, [esp+8] = dir, [esp+12] = dy) */
static __declspec(naked) void Thunk_CarAStar(void)
{
    __asm {
        push ebp
        mov  ebp, esp
        push dword ptr [ebp + 16]    /* dy   */
        push dword ptr [ebp + 12]    /* dir  */
        push dword ptr [ebp + 8]     /* len  */
        push ecx                     /* dx   */
        push edx                     /* sy   */
        push eax                     /* sx   */
        call CarAStar
        add  esp, 24
        pop  ebp
        ret  12
    }
}

/* ---------------------------------------------------------------------------------------- */

void Paths_Install(void)
{
    InitializeCriticalSection(&g_pathLock);
    if (g_cfg.fixWalkerPathfinding) {
        RedirectCall(CALL_WALKER_ASTAR, ADDR_WalkerAStar, (void *)Thunk_WalkerAStar);
        static const uint8_t pathEnd[] = { 0x48, 0x3B, 0x86, 0x44, 0x02, 0x00, 0x00, 0x7E, 0x5A };
        if (CheckBytes(PATCH_WALKER_PATHEND, pathEnd, sizeof(pathEnd)))
            PatchCall(PATCH_WALKER_PATHEND, (void *)Thunk_WalkerPathEnd, sizeof(pathEnd));
        Log("Walker path finding fix installed");
    }
    if (g_cfg.fixCarPathfinding) {
        if (RedirectCall(CALL_CAR_ASTAR, ADDR_CarAStar, (void *)Thunk_CarAStar))
            Log("Car path finding fix installed");
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 4) SWAT officers look like patrol officers for a moment after leaving the helicopter      */
/*                                                                                           */
/* The sprite image of an officer depends on his walker type (+0x1C1). TPoliceman.DoMove     */
/* derives the type from his rank (+0x224: 3 = undercover, 4 = riot police, 5 = SWAT) at the  */
/* start of every move, but officers leaving a vehicle get the default type first and their  */
/* image is chosen by TWalker.SetAnimType before that correction - so the wrong image stays  */
/* until the next waypoint. The rank mapping is now applied before every image selection.    */
/* ---------------------------------------------------------------------------------------- */

#define ADDR_TWalker_SetAnimType 0x00599E8C
#define VMT_TPoliceman           0x005A0A3C
#define PM_RANK                  0x224
#define WALKER_TYPE              0x1C1

static void __cdecl FixWalkerType(uint8_t *walker)
{
    if (*(uintptr_t *)walker != VMT_TPoliceman) return;
    switch (FIELD(walker, PM_RANK, int)) {
    case 3: walker[WALKER_TYPE] = 6; break;
    case 4: walker[WALKER_TYPE] = 7; break;
    case 5: walker[WALKER_TYPE] = 8; break;
    }
}

static __declspec(naked) void Thunk_SetAnimType(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push eax
        call FixWalkerType
        add  esp, 4
        pop  ecx
        pop  edx
        pop  eax
        /* original first instructions */
        push ebp
        mov  ebp, esp
        add  esp, 0xFFFFFEF4
        push ADDR_TWalker_SetAnimType + 9
        ret
    }
}

void WalkerType_Install(void)
{
    static const uint8_t prologue[] = { 0x55, 0x8B, 0xEC, 0x81, 0xC4, 0xF4, 0xFE, 0xFF, 0xFF };
    if (CheckBytes(ADDR_TWalker_SetAnimType, prologue, sizeof(prologue)) &&
        PatchJmp(ADDR_TWalker_SetAnimType, (void *)Thunk_SetAnimType, sizeof(prologue)))
        Log("Officer appearance fix installed");
}

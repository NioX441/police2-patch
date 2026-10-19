/*
 * vehicles.c - Traffic deadlock breaker.
 *
 * Every vehicle (civil cars and police cars) reserves the lane cells it occupies in a global
 * 288x288 grid. TVehicle.SetCollosion marks a vehicle as "blocked" when the next cell is
 * owned by another vehicle. Two vehicles that wait for each other (typical at crossroads, or
 * a car waiting behind a police car that stopped on the road) never move again; police units
 * stuck in such a jam never reach the scene or never return to their station.
 *
 * Fix: a vehicle that stayed blocked for TrafficDeadlockTicks simulation ticks may ignore the
 * blockage for a short time, which dissolves the deadlock.
 */
#include "patch.h"
#include "game.h"

#define SLOTS        4096
#define GHOST_TICKS  90

typedef struct { const void *veh; int blocked; int ghost; } Entry;
static Entry g_tab[SLOTS];

static void __cdecl AfterSetCollision(uint8_t *veh)
{
    Entry *e = &g_tab[((uintptr_t)veh >> 4) & (SLOTS - 1)];
    if (e->veh != veh) {
        e->veh = veh;
        e->blocked = 0;
        e->ghost = 0;
    }
    if (e->ghost > 0) {
        e->ghost--;
        veh[VEH_BLOCKED] = 0;
        return;
    }
    if (!veh[VEH_BLOCKED]) {
        e->blocked = 0;
        return;
    }
    if (++e->blocked >= g_cfg.trafficDeadlockTicks) {
        e->blocked = 0;
        e->ghost = GHOST_TICKS;
        veh[VEH_BLOCKED] = 0;
    }
}

/* replaces "call TVehicle.SetCollosion" (eax = vehicle) */
static __declspec(naked) void Thunk_SetCollision(void)
{
    __asm {
        push eax
        mov  ecx, ADDR_Vehicle_SetCollision
        call ecx
        call AfterSetCollision      /* argument: saved vehicle on the stack */
        add  esp, 4
        ret
    }
}

void Vehicles_Install(void)
{
    if (g_cfg.trafficDeadlockTicks <= 0) return;
    BOOL a = RedirectCall(CALL_SETCOLLISION_POLICE, ADDR_Vehicle_SetCollision, (void *)Thunk_SetCollision);
    BOOL b = RedirectCall(CALL_SETCOLLISION_CIVIL, ADDR_Vehicle_SetCollision, (void *)Thunk_SetCollision);
    if (a && b) Log("Traffic deadlock breaker installed (%d ticks)", g_cfg.trafficDeadlockTicks);
}

/* ------------------------------------------------------------------------------------------ */
/* Police cars refuel twice in a row                                                           */
/*                                                                                             */
/* TPolicecar.DoMove charges fuel (40 per stop, fuel icon) when "MinuteOf(game time) mod 29 =  */
/* 0", i.e. at :00, :29 and :58 - so :58 and the next :00 are only two minutes apart. The      */
/* replacement keeps the rate (three times per hour) but spaces it evenly every 20 minutes.    */
/* ------------------------------------------------------------------------------------------ */

#define CALL_FUEL_MINUTE   0x005A5AE0   /* call MinuteOf(TDateTime) in TPolicecar.DoMove */
#define ADDR_MinuteOf      0x0045BFD8

/* function MinuteOf(const AValue: TDateTime): Word; - the date is passed on the stack */
static int __stdcall FuelMinute(double gameTime)
{
    long long minutes = (long long)(gameTime * 1440.0 + 0.5);
    return minutes % 20 == 0 ? 0 : 1;   /* the caller tests "result mod 29 = 0" */
}

void Fuel_Install(void)
{
    if (RedirectCall(CALL_FUEL_MINUTE, ADDR_MinuteOf, (void *)FuelMinute))
        Log("Refuel timing fix installed");
}

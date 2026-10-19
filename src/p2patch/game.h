/*
 * game.h - Addresses and structure offsets inside police2.exe (build 488, patch 4.8.8).
 *
 * All addresses are virtual addresses of the UPX-unpacked image (image base 0x400000).
 * They were recovered by decompiling the game (Delphi XE2 + Asphyre Sphinx, DirectX 7).
 * Names in comments follow the Delphi RTTI found in the executable.
 */
#pragma once
#include <stdint.h>

/* ---- Delphi runtime ------------------------------------------------------------ */
#define ADDR_TObject_Create        0x004066BC  /* class function TObject.Create (eax=VMT, dl=1) */
#define ADDR_TObject_Free          0x004066EC  /* procedure TObject.Free (eax=Self)            */
#define ADDR_TList_Add             0x004468A0  /* function TList.Add(eax=Self, edx=Item)        */
#define ADDR_TList_Clear_Virtual   0           /* not used                                       */

/* ---- Classes (VMT addresses) ---------------------------------------------------- */
#define VMT_TWayPoint              0x0059C5DC  /* fields: +4 X (cell), +8 Y (cell)              */
#define VMT_TWayPointList          0x0059C690  /* TList descendant                              */

/* ---- TList layout ------------------------------------------------------------- */
#define TLIST_ITEMS                0x04
#define TLIST_COUNT                0x08

/* ---- Global data ---------------------------------------------------------------- */
#define ADDR_MAP_GRID              0x0062F510  /* TMapCell[288][288], 0x2C bytes per cell        */
#define MAP_SIZE                   288
#define MAP_CELL_SIZE              0x2C
#define MAP_ROW_SIZE               0x3180      /* MAP_SIZE * MAP_CELL_SIZE                       */
#define CELL_TYPE                  0x00        /* byte: 1,2,4 = walkable for pedestrians         */
#define CELL_SCREEN_X              0x04        /* int: isometric screen X of the cell centre     */
#define CELL_SCREEN_Y              0x08        /* int: isometric screen Y of the cell centre     */
#define CELL_CROSSROAD             0x0C        /* int: crossroad index or -1                     */
#define CELL_INOUT                 0x2B        /* byte: 1 = crossroad entry, 2 = crossroad exit  */

#define ADDR_VEHICLE_GRID          0x00A4C814  /* TVehicle*[288][288] lane occupancy              */
#define ADDR_SETTINGS_PTR          0x00626B70  /* -> settings record; +2 = fullscreen flag      */
#define ADDR_MAINFORM              0x00A4C80C  /* TMainForm instance                             */

/* ---- Functions ---------------------------------------------------------------------- */
#define ADDR_WalkerAStar           0x0059CF98  /* pedestrian A* (eax=fx, edx=fy, ecx=tx, [esp+4]=ty) */
#define ADDR_CarAStar              0x0059D8D8  /* car A* on the crossroad graph                  */
#define ADDR_GetPolicemanDest      0x005A1AC8  /* TPoliceman.GetPolicemanDestination(eax, dl)    */
#define ADDR_Vehicle_SetCollision  0x005FF804  /* TVehicle.SetCollosion (sic)                    */
#define ADDR_AppException          0x005E9210  /* TMainForm.ApplicationEvents1Exception (empty)  */

/* ---- Call sites / patch locations ------------------------------------------------------ */
#define PATCH_CULL_BEGIN           0x0055ED00  /* TSprite.Draw: start of the visibility test     */
#define PATCH_CULL_DRAW            0x0055EDDE  /* TSprite.Draw: "visible" path (DoDraw)          */
#define PATCH_CULL_SKIP            0x0055EDF0  /* TSprite.Draw: "invisible" path (DoNotDraw)     */

#define CALL_WALKER_ASTAR          0x005A1AF7  /* in TPoliceman.GetPolicemanDestination          */
#define CALL_CAR_ASTAR             0x005A5C5F  /* in TPolicecar.DoMove                           */
#define CALL_SETCOLLISION_POLICE   0x005A5A03  /* in TPolicecar.DoMove                           */
#define CALL_SETCOLLISION_CIVIL    0x005FFE4E  /* in TVehicle.DoMove                             */

#define PATCH_WALKER_PATHEND       0x005A184F  /* TPoliceman.DoMove: "dec eax; cmp; jle" (9 bytes) */
#define PATCH_WALKER_PATHEND_RET   0x005A18B2

#define CALL_GAMEINIT_SCREENW      0x005F2FDC  /* TMainForm.GameInit: call TScreen.GetWidth     */
#define CALL_GAMEINIT_SCREENH      0x005F2FF4  /* TMainForm.GameInit: call TScreen.GetHeight    */
#define PATCH_GAMEINIT_WINW        0x005F3013  /* TMainForm.GameInit: mov edx, 1024             */
#define PATCH_GAMEINIT_WINH        0x005F3022  /* TMainForm.GameInit: mov edx, 768              */

/* ---- Import address table slots (user32) of the unpacked image ------------------------- */
#define IAT_CreateWindowExW        0x00A9FC60
#define IAT_ScreenToClient         0x00A9FCF8
#define IAT_GetWindowRect          0x00A9FDA8
#define IAT_GetWindowInfo          0x00A9FDB0
#define IAT_GetClientRect          0x00A9FE54
#define IAT_ClientToScreen         0x00A9FF04

/* ---- Import address table slot of the UPX loader stub (packed image) ------------------- */
#define IAT_PACKED_VirtualProtect  0x00B11938

/* ---- Object field offsets ----------------------------------------------------------- */
/* TSprite (Asphyre sprite engine) */
#define SPR_ENGINE                 0x04
#define SPR_WIDTH                  0x18        /* int   */
#define SPR_HEIGHT                 0x1C        /* int   */
#define SPR_X                      0x24        /* float */
#define SPR_Y                      0x28        /* float */
#define SPR_OFFSET_X               0x30        /* float */
#define SPR_OFFSET_Y               0x34        /* float */
#define SPR_SCALE_X                0x110       /* float */
#define SPR_DRAW_OFS_X             0x118       /* float: additional draw offset (anchor) */
#define SPR_DRAW_OFS_Y             0x11C       /* float */
#define SPR_SCALE_Y                0x114       /* float */
/* TSpriteEngine */
#define ENG_WORLD_X                0xBC        /* float: world X of the left screen edge */
#define ENG_WORLD_Y                0xC0        /* float: world Y of the top screen edge  */
#define ENG_VISIBLE_W              0xD4        /* int  : client width + margin           */
#define ENG_VISIBLE_H              0xD8        /* int  : client height + margin          */
#define ENG_ZOOM                   0xF4        /* float */

/* TPoliceman (TWalker descendant) */
#define PM_TARGET_X                0x1B0       /* cell the walker currently walks to     */
#define PM_TARGET_Y                0x1B4
#define PM_DEST_X                  0x24C       /* final destination cell                 */
#define PM_DEST_Y                  0x250

/* TVehicle / TPolicecar */
#define VEH_BLOCKED                0x1E9       /* byte: next lane cell is occupied        */

/* Signature bytes used to verify that the expected (unpacked) build is in memory */
#define SIG_ADDR_1                 0x00593D78  /* TDX7Device.InitDevice prologue            */
#define SIG_ADDR_2                 0x0055ED00  /* TSprite.Draw visibility test              */
#define SIG_ADDR_3                 0x0059CF98  /* walker A* prologue                        */

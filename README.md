# Police 2 Community Patch

An unofficial fan patch for **Police 2 – Recht und Ordnung** (rondomedia / CyberFlix, 2012),
build 488 (official update 4.8.8). It makes the game run properly on Windows 10 and 11,
improves the image quality and fixes several long-standing bugs (stuck units, disappearing
shadows, ...).

The patch consists of a single DLL that is copied into the game folder. **No game file is
modified or shipped** – the game executable stays untouched.

## Installation

1. Make sure the official update 4.8.8 (build 488) is installed. The game folder of the
   retail version contains `police2patch488.exe` for this; the start menu shows "build 488"
   in the lower right corner.
2. Copy `msimg32.dll` and `p2patch.ini` into the game folder
   (default: `C:\Program Files (x86)\Police 2 - Recht und Ordnung`).
3. Start the game as usual (police2.exe, administrator rights are requested as before).
   On this first start the patch creates `police2_niox.exe` in the game folder and starts it
   instead. From then on you can start `police2_niox.exe` directly - without administrator
   rights and with 4 GB of memory instead of 2 GB (create a shortcut to it if you like).
   police2.exe keeps working and starts `police2_niox.exe` automatically.

Save games and options are stored in `%LOCALAPPDATA%\Police 2 Community Patch`. Save games
that are still in the game folder are found as before.

Uninstall: delete `msimg32.dll`, `p2patch.ini`, `p2patch.log` and `police2_niox.exe` from the
game folder. Save games made with the patch stay in `%LOCALAPPDATA%\Police 2 Community Patch`
(copy them into the game folder to use them without the patch).

Windows loads DLLs from the application folder before the system folder, so the game picks up
this `msimg32.dll`, which forwards the real `msimg32` functions to Windows and applies the
fixes in memory while the game starts.

## What is fixed / improved

| Area | Problem | Fix |
|------|---------|-----|
| Fullscreen | On screens wider or taller than 2048 pixels (1440p, 4K, ultrawide) the game aborts with *"Failed initializing Asphyre device"*: Direct3D 7 on current Windows refuses back buffers larger than 2048 pixels. | Borderless fullscreen at the native monitor resolution. The game renders at up to 2048×2048 (keeping the monitor's aspect ratio) and the image is scaled to the screen; mouse input is translated. |
| Image quality | Enlarged images were scaled with nearest-neighbour sampling (blocky). | New Direct3D 11 output with a bicubic (Catmull-Rom) filter, flip-model swap chain and V-Sync. Selectable filters: bicubic, sharp, bilinear, nearest. |
| Windowed mode | Fixed 1024×768 window placed at the top-left corner, tiny on high-resolution screens. | The window is scaled (auto: largest size that fits) and centred; DPI aware. |
| Shadows | Shadows and other large sprites disappeared near the edges of the screen (the visibility test of the sprite engine was wrong). Along the bottom edge the shadows were always missing: the game draws the shadow as a darkened copy of the building layer moved up by 25 pixels, so the bottom 25 pixels never got one. | Correct visibility test that only skips sprites that are certainly off-screen; the shadow is rendered from an own copy of the layer. |
| Memory | The game is a 32-bit program limited to 2 GB of memory. With a big city loaded the largest free block was often below 50 MB: loading or restoring textures could fail (black ground for a moment, possible crashes). | `police2_niox.exe` can use 4 GB; about 2 GB stay free. |
| Administrator rights | The game requests administrator rights only because it writes save games into its own folder below "Program Files". | Save games and options in the user folder, `police2_niox.exe` runs without administrator rights. |
| Black ground | After Windows had taken away the graphics memory (Alt+Tab, UAC prompt, lock screen) the ground stayed black until the camera was moved. | Layers are redrawn immediately. |
| Officers stuck in two teams | When an officer returned from hospital/training, the game marked him as free but kept him in his old team. After assigning him again he belonged to two teams and received contradicting orders – he got stuck at missions or never returned to the station. Existing save games often contain such officers. | Team lists are kept consistent; affected save games are repaired automatically when loaded. |
| Officers on foot | When the destination could not be reached, the route search flooded the whole map (short freezes) and the officer stood still forever. Officers also froze when they reached the end of their route but not their destination. | Fast A* search; unreachable destinations are approached as closely as possible and then walked to directly; new route when the destination changed. |
| Police cars | A failed route search returned an empty route: the car stood still and repeated the expensive search every frame. Every search also overwrote global crossroad markers of the map. | Retries with all start directions, direct route as last resort; crossroad markers are restored. |
| Traffic jams | Vehicles waiting for each other (typical at crossroads) never moved again and blocked police units. | Deadlock breaker: a vehicle blocked for a long time may pass. |
| Performance | Moving the camera redraws all static layers, and the sprite engines drew up to 1024 pixels beyond the visible area. The HUD converted the mouse position to a map cell by testing all 82,944 cells, several times per frame (~70 % of the frame time). Because the game logic runs once per frame, this slowed down the whole game, especially while dragging the camera. | Exact, constant-time replacement; sprites are only drawn in the visible area (identical image). Frame time on a big city dropped from ~18 ms to ~6 ms, while dragging the camera from ~36 ms to ~20 ms. |
| Game speed | The original timer used the coarse 15.6 ms Windows timer: frames came every 17–35 ms, so the game speed varied all the time (cars slowed down while scrolling). | Precise frame limiter at the game's intended 30 frames per second (constant game speed). |
| Injured officers | Officers injured on patrol or at a checkpoint lay on the street until they were back from hospital, and their team could still be sent to crimes. | Taken away after 10 seconds; teams with injured officers are not offered. |
| Sound device | Speakers switched on after the start stayed silent (sound only on the device present at start). | Output follows the Windows default device. |
| Selection square | Missing at the farthest zoom level and near the right/bottom part of the map when zoomed out; map clicks were ignored there. | Drawn and clickable at every zoom level, correct mouse-to-map conversion. |
| Crime locations | New crimes could start exactly where another case was still running. | The location search is repeated while it lands next to a running case. |
| Music | Two music tracks could play at the same time. | The old track is always faded out. |
| Camera when zoomed out | Near the right and bottom edge of the city the view showed a black area; buildings and shadows were cut off at a straight line (camera limits ignored the zoom). | Zoom-aware camera limits. |
| Tile seams | Thin lines between map tiles when zoomed (texture addressing left at WRAP). | CLAMP texture addressing. |
| Refuelling | Police cars refuelled twice in a row (fuel charged at :00, :29 and :58), all at the same moment. | Evenly every 20 game minutes, same rate, each car at its own time. |
| Special units | SWAT, riot police and undercover officers looked like patrol officers for a moment after leaving a vehicle. | Correct appearance from the start. |
| Errors | The global error handler of the game was empty – errors were silently ignored. Two latent bugs (undefined positions for invalid map cells, team buttons acting on a dissolved team) could misplace markers or affect the wrong team. | Errors are written to `p2patch.log`; the latent bugs are handled safely and logged. |

## Extras

* **Hard mode** (`HardMode=1` in `p2patch.ini`): criminals are 2.5× stronger
  (`CriminalStrength`, 1.0–5.0) and there are 4× more crimes per day (`CrimeFrequency`,
  1.0–8.0; 40 instead of 10 on the highest difficulty), so more cases run at the same time. Missions need more or
  better trained officers; money is not affected. Applies to crimes planned after the game
  was started (the crime plan is made once per game day).
* **Cheats** (`Cheats=0` disables them):
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>M</kbd> adds 1,000 to the budget,
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd> raises the public safety level by
  10 percentage points, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>1</kbd> ... <kbd>9</kbd> starts a
  crime at the next game minute (1 robbery, 2 stabbing, 3 homicide, 4 burglary, 5 smuggling,
  6 drug abuse, 7 terrorism, 8 riot, 9 car theft), <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd>
  a random one.
* **Car theft** (`CarTheft=1`): a crime type the developers prepared but switched off.
* The main menu shows "build 488 - Niox Patch".

## Configuration

All options are documented in `p2patch.ini`. If the file is missing, the patch creates it with
the default values on the next start. Switching between fullscreen and windowed mode is done
in the game's own settings menu (restart required, as before).

## Requirements and limitations

* Police 2 build 488 (official update 4.8.8). Other versions are detected and left alone
  (see `p2patch.log`).
* Windows 10 or 11 with a Direct3D 11 capable graphics card (falls back to the original
  DirectDraw output otherwise, `Filter=directdraw` forces it).
* The internal render resolution is limited to 2048 pixels by Direct3D 7; on bigger screens
  the image is scaled up.
* police2.exe still asks for administrator rights (its manifest cannot be changed without
  modifying the file); `police2_niox.exe` does not. `police2_niox.exe` is created on your
  computer from your own police2.exe - two header fields differ, nothing else.

## Building from source

Requirements: Visual Studio 2019 or newer with the C++ x86 tools and the Windows SDK
(`fxc` shader compiler). Run `build.bat`; the result is `build\msimg32.dll`.

| File | Content |
|------|---------|
| `src/p2patch/main.c` | Entry point, waits for the UPX unpacker, `msimg32` forwarding |
| `src/p2patch/display.c` | Fullscreen/window scaling, coordinate translation |
| `src/p2patch/presenter.cpp`, `upscale.hlsl` | Direct3D 11 output and scaling filters |
| `src/p2patch/sprites.c` | Sprite visibility test (shadows), tile seams |
| `src/p2patch/paths.c` | Path finding for officers and police cars, officer appearance |
| `src/p2patch/vehicles.c` | Traffic deadlock breaker, refuel timing |
| `src/p2patch/groups.c` | Team membership repair |
| `src/p2patch/perf.c` | Performance fixes, frame limiter, camera limits |
| `src/p2patch/gameplay.c` | Hard mode, cheats, menu text |
| `src/p2patch/errors.c` | Error logging |
| `src/p2patch/robustness.c` | Safe handling of latent bugs of the original code, layer restore |
| `src/p2patch/audio.c` | Sound output follows the default device |
| `src/p2patch/userdata.c` | Save games in the user folder, `police2_niox.exe` (4 GB, no administrator rights) |
| `src/p2patch/game.h` | Addresses and structure offsets of build 488 |
| `tools/` | Analysis helpers (Delphi RTTI name recovery, Ghidra export, Frida probes) |

## Legal

This is an unofficial, non-commercial fan project and not affiliated with rondomedia or
CyberFlix. It contains no code, graphics, sound or other files of the game; you need your own
copy of Police 2. The patch was created by analysing the behaviour of the game in order to
make it work on current Windows versions. All trademarks belong to their respective owners.

The source code of the patch is released under the MIT license (see `LICENSE`).

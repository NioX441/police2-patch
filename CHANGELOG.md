# Changelog

All notable changes of the Police 2 Community Patch.

## [1.2.1] - 2026-10-19

### Added
- Cheat Ctrl+Shift+1 ... 8: start a crime of a chosen type at the next game minute
  (1 robbery, 2 stabbing, 3 homicide, 4 burglary, 5 smuggling, 6 drug abuse, 7 terrorism,
  8 riot), Ctrl+Shift+0: random crime.

### Fixed
- The selection square was missing at the farthest zoom level (the game skipped it below
  zoom 0.75) and near the right and bottom part of the map at the second zoom level (the
  mouse-to-map conversion only worked for part of the map when zoomed out).

## [1.2.0] - 2026-10-19

### Added
- `police2_niox.exe`: police2.exe creates a copy of itself in the game folder that can use
  4 GB of memory instead of 2 GB and needs no administrator rights, and starts it
  automatically. It can also be started directly (no UAC prompt). police2.exe itself is not
  changed (`LargeMemory`).
- Save games and options are stored in `%LOCALAPPDATA%\Police 2 Community Patch`; save games
  in the game folder are still found. Works the same with and without administrator rights
  (`UserDataFolder`).

### Changed
- Hard mode: no limit of 19 crimes per day any more. Additional crimes wait in a queue and
  are planned as soon as a crime slot of the game becomes free. `CrimeFrequency` now ranges
  from 1.0 to 8.0, default 4.0 (40 instead of 10 crimes per day on the highest difficulty).

### Fixed
- Shadows were missing along the bottom edge of the screen. The game draws the shadow by
  drawing the layer with buildings and trees a second time, darkened and moved up by 25
  pixels; the bottom 25 pixels never got a shadow. The shadow is now rendered from a copy of
  the layer with the camera moved accordingly.
- The game ran at the 2 GB memory limit of 32-bit programs: with a big city loaded, the
  largest free block of address space was often below 50 MB, so loading or restoring textures
  could fail (black ground for a moment, possible crashes). With `police2_niox.exe` about
  2 GB stay free.
- Ground, buildings and overlays stayed black after Windows had taken away the graphics
  memory (Alt+Tab, UAC prompt, lock screen, display change) until the camera was moved.
  They are now redrawn immediately.

## [1.1.0] - 2026-10-18

### Added
- "build 488 - Niox Patch" in the main menu.
- Cheat Ctrl+Shift+L: public safety level +10 percentage points.
- Hard mode: considerably more crimes per day and therefore more cases at the same time
  (`CrimeFrequency`, default 2.5, at most 19 crimes per day).
- The two latent bugs found in 1.0.0 are now handled safely and logged (see Fixed).
- `[Debug] Keys=1` enables developer hotkeys (Ctrl+Shift+F11 toggles the sprite visibility
  test, for comparisons).

### Changed
- Money cheat (Ctrl+Shift+M) adds 1,000 instead of 100,000. The option is now called `Cheats`.

### Fixed
- Zoomed out near the right or bottom edge of the city the view showed a black area with
  buildings and shadows cut off at a straight line: the camera limits ignored the zoom factor.
- Thin lines between map tiles when zoomed: texture addressing was left at WRAP, so bilinear
  filtering mixed in texels of the opposite texture border. CLAMP addressing is used now.
- Police cars refuelled twice in a row: fuel was charged at minute :00, :29 and :58 of every
  hour (only two minutes between :58 and :00). Now evenly every 20 game minutes (same rate).
- SWAT, riot police and undercover officers looked like patrol officers for a moment after
  leaving a vehicle/the helicopter (their image was chosen before the type was set).
- Dragging the camera slowed the game down: when the camera moves, the game redraws all
  static layers, and the sprite engines drew up to 1024 pixels beyond the visible area.
  Sprites are now limited to the visible area (identical image), which almost halves the cost.
- The HUD requested the screen position of map cell -1,-1 ("nothing selected") every frame and
  got an undefined value; such requests now return a position outside the screen, other
  invalid cells are clamped. Logged once per call site.
- The "add/remove officer" buttons of a team window no longer act on a random team when the
  team does not exist any more (click ignored and logged).

## [1.0.0] - 2026-10-18

First release. Requires Police 2 build 488 (official update 4.8.8).

### Added
- Drop-in installation: `msimg32.dll` + `p2patch.ini` in the game folder, no game file is
  modified.
- Configuration file `p2patch.ini` (created automatically with defaults).
- Direct3D 11 output with selectable scaling filter (bicubic, sharp, bilinear, nearest),
  flip-model swap chain and V-Sync.
- Scalable, centred window in windowed mode; DPI awareness.
- Optional hard mode: criminals 2.5× stronger (`HardMode`, `CriminalStrength`).
- Money cheat: Ctrl+Shift+M adds 100,000 (`MoneyCheat`).
- Log file `p2patch.log` with the applied fixes and previously swallowed game errors.

### Fixed
- Fullscreen failed with "Failed initializing Asphyre device" on screens wider or taller than
  2048 pixels (Direct3D 7 render target limit on current Windows). Fullscreen is now a
  borderless window at native resolution with internal rendering up to 2048 pixels.
- Shadows and other large sprites disappeared near the left, right and bottom edges of the
  screen (wrong visibility test in the sprite engine).
- Officers returning from hospital or training stayed in their old team while being marked as
  available; assigning them again put them into two teams, which made units hang at missions
  or never return to the station. Broken save games are repaired when loaded.
- Officers on foot stood still forever when their destination was unreachable, and froze when
  they reached the end of their route while their destination had changed.
- Pedestrian route search flooded the entire map for unreachable destinations (short freezes).
- Police cars without a route stood still and repeated the route search every frame.
- The car route search overwrote global crossroad markers of the map.
- Traffic deadlocks (vehicles waiting for each other) blocked civil and police vehicles forever.
- The HUD tested all 82,944 map cells several times per frame to find the cell under the mouse,
  which slowed down the whole game (especially while dragging the camera). Frame time dropped
  from about 18 ms to about 6 ms.
- Uneven game speed: the original timer produced frame intervals between 17 and 35 ms; a
  precise frame limiter now keeps the intended 30 frames per second.
- Game errors were silently ignored by an empty exception handler; they are logged now.

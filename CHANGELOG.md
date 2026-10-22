# Changelog

Everything the Police 2 Community Patch changes compared to the original game (build 488).

## Running on current Windows

- Fullscreen works on big screens again. On anything wider or taller than 2048 pixels the
  game used to quit with "Failed initializing Asphyre device". Fullscreen is now a borderless
  window at your monitor's resolution, and the picture is scaled up from up to 2048 pixels.
- The picture goes through Direct3D 11 and is scaled with a proper filter instead of blocky
  pixel doubling. You can pick bicubic, sharp, bilinear or nearest in `p2patch.ini`.
- In windowed mode the window is scaled to a sensible size and centred, instead of a tiny
  1024×768 window in the top-left corner.
- The game no longer needs administrator rights. On the first start the patch makes a copy
  of `police2.exe` called `police2_niox.exe`, which runs without the UAC prompt and can use
  4 GB of memory instead of 2 GB. Your original exe is not touched.
- Save games and options go to `%LOCALAPPDATA%\Police 2 Community Patch`. Old save games in
  the game folder are still found.
- Sound now follows the Windows default output device. If your speakers were off when the
  game started, you used to get no sound at all.

## Graphics

- Frames are drawn at your monitor's refresh rate. The game logic still runs at 30 steps per
  second as intended; cars and people are smoothly interpolated in between. Blinking
  markers, siren lights, the moving arrows to a crime scene and the day/night change keep
  their original speed. Set `RenderFPS=0` for the original behaviour.
- Shadows no longer vanish near the screen edges, including the strip along the bottom
  where they were always missing.
- The ground and buildings no longer go black after Alt+Tab, the lock screen or a UAC
  prompt, and the black flashes caused by running out of memory are gone.
- No more thin lines between map tiles when zoomed.
- When zoomed out, the camera can no longer show a black area beyond the right or bottom
  edge of the city.
- The selection square shows up at every zoom level, and clicks on the map (patrol routes,
  checkpoints) work when zoomed out.
- SWAT, riot police and undercover officers no longer look like normal patrol officers for
  a moment after getting out of a vehicle.

## Performance

- The game runs at an even speed. The original timer jittered between 17 and 35 ms per
  frame, so cars sped up and slowed down, especially while scrolling.
- Finding the map cell under the mouse used to check all 82,944 cells several times per
  frame. That alone ate most of the frame time. Together with drawing only what is on
  screen, a frame on a big city now takes about 6 ms instead of 18 ms.

## Units and traffic

- Officers no longer end up in two teams at once after coming back from hospital or
  training. That was the reason units got stuck at missions or never came back to the
  station. Save games with such officers are repaired when you load them.
- Officers on foot no longer stand still forever when they can't reach their destination,
  and the route search no longer freezes the game for a moment.
- Police cars without a route no longer stand around recalculating every frame, and they no
  longer slide across buildings. They drive to the nearest road at the destination, or stay
  put if there is none.
- Traffic jams where vehicles wait for each other forever now clear up after a while.
- Officers injured on patrol or at a checkpoint are taken away after a few seconds instead
  of lying in the street until they're back from hospital. Teams with an injured officer no
  longer show up in the list when you send units to a crime.
- Police cars refuel every 20 game minutes, each car at its own time. Before, they all
  refuelled at once and sometimes twice in a row.

## Crimes and music

- New crimes no longer start right where another case is still running.
- The crime window of an unreported riot shows its title instead of "GUI CRIME".
- Car theft is back. The developers had prepared it but switched it off.
- Only one music track plays at a time. Before, the calm and the action music could end up
  playing on top of each other.

## Extras

- Hard mode (`HardMode=1`): criminals are stronger and there are many more crimes per day,
  with no limit of 19 any more. Both can be tuned in `p2patch.ini`.
- Cheats: Ctrl+Shift+M for money, Ctrl+Shift+L for public safety, Ctrl+Shift+1 to 9 to
  start a crime of a chosen type and Ctrl+Shift+R for a random one. `Cheats=0` turns them
  off.
- The main menu shows "build 488 - Niox Patch".
- `p2patch.log` lists what the patch did and any game errors, which the original silently
  swallowed. A few hidden bugs in the original code (team buttons acting on a dissolved
  team, positions of invalid map cells) are caught and logged instead of misbehaving.

# Quest 64 Tale: Recompiled v1.1.1

## Mods and saves

Place the separate mod packs in `mods/` beside the executable. The launcher
scans this folder regardless of its launch directory. Open Mods Folder and
Install Mods use this same folder. Restart after adding packs.

Controller Pak reads prefer `%LOCALAPPDATA%/Quest64Recompiled` on Windows,
or `~/.config/Quest64Recompiled` on Linux, then fall back to the game folder.
Successful Controller Pak writes update both the profile and game folders.
This includes `controllerPak_header.sav` and `controllerPak_file_*.sav`.
If a destination cannot be written, the game reports the mirror failure in
its log. Existing saves are not overwritten merely by loading them.
Portable mode still controls launcher settings; save priority remains the same.
Separate Auto Save checkpoints retain their existing profile location.

## Gameplay updates

- Optional Game Speed mod: Configure 2x, 4x or 8x. Walking, attacks, enemies,
  spell effects and combat timers accelerate. Menus, dialogue and transitions
  stay at normal speed. Camera, audio and FMV playback retain normal timing.
- Spell Preview displays element multipliers (50%, 100%, 125%), estimated
  damage ranges and damage as a percentage of enemy maximum HP. Staff damage
  appears when the locked enemy is in reach. Predictions assume a hit.
- Debug arrivals use the opposite side of the selected door/stair entrance.
- Includes the latest world-sprite, battle orientation, camera-relative
  movement, widescreen compass and bounded 3D-arrow rendering fixes.
- Includes all existing gameplay mods and the three-clip Solvaring FMV support.

The game ZIPs contain no ROM, extracted game assets, movies, saves or personal
settings. Supply your own supported USA ROM. The optional mods ZIP contains
thirteen installable packs only, including the Brian texture and FMV packs.

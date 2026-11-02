# Quest 64 Tale: Recompiled v1.1.3

Battle pause-menu Escape now releases enemies, battle effects and the movement
boundary, restores area music, and clears combat state without rewards.
Quit performs battle cleanup and stops music before opening the menu; it also
clears the active boss encounter so loading a save starts outside combat.
Escape remains unavailable during boss fights.

The optional Solvaring FMV pack contains only the three updated intro, battle
and defeat movies. All retain 1280x720 resolution, 30 fps and original durations.
High-fidelity MPEG-1 encoding was selected using full-length SSIM comparisons
above 0.99. MPEG is lossy; the source MP4s remain unchanged. The new pack is
approximately 103.5 MB, down from 138.6 MB. It requires game v1.1.3 or later.

Game-to-FMV playback now fades the game and audio to black before showing the
initialized movie window. Movies fade in and out, including on skip; the game
fades back in afterward. The video window follows the game's display mode.

Includes all v1.1.2 save-loading, debug travel, speed, mod-priority and preview
fixes. Debug travel uses normal grounded arrival coordinates for all locations
and bosses. Ordinary door animations remain in use during normal travel.

Mods load from the profile's Quest64Recompiled/mods folder first, then the mods
folder beside the executable. A profile copy overrides a duplicate game-folder
pack. On Windows the profile is %LOCALAPPDATA%/Quest64Recompiled; on Linux it is
~/.config/Quest64Recompiled. Install the updated FMV pack there and restart.

Controller Pak reads prefer the profile, then fall back to the game folder.
Writes mirror to both locations. Auto Save checkpoints remain separate.

Game ZIPs contain no ROM, extracted game assets, saves, movies or personal
settings. Supply your own supported USA ROM. The separate mods ZIP contains
exactly thirteen installable packs, including the optional FMV and texture packs.

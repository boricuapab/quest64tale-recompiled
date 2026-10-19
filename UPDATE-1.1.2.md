# Quest 64 Tale: Recompiled v1.1.2

Fixes an access violation when loading a saved game and entering gameplay.
The gameplay and accelerated door-check hooks now sign-extend guest addresses
before accessing game memory on a 64-bit host. Existing saves need no conversion.

Includes the latest readable vertical attack preview, spell/staff damage
percentages, and accelerated character/camera rendering changes. Game Speed
offers Default (1x), 2x, 4x and 8x in its Configure menu.

Debug travel uses normal grounded arrival coordinates for every selectable
location and all eight bosses, clearing leftover door input locks. This
avoids replaying a door animation that places Brian behind level geometry.
Ordinary door travel retains its native animations.

Mods load from the user profile's Quest64Recompiled/mods folder first, then
the mods folder beside the executable. The profile copy wins duplicate IDs.
On Windows the profile is %LOCALAPPDATA%/Quest64Recompiled; on Linux it is
~/.config/Quest64Recompiled. Restart the launcher after installing mods.

Controller Pak reads prefer the profile, then fall back to the game folder.
Successful writes mirror to both locations. Keep the header and numbered
Controller Pak save files together. Auto Save checkpoints remain separate.

The game archives contain no ROM, extracted game assets, saves, movies or
personal settings. Supply your own supported USA ROM. Optional mods are
distributed separately.

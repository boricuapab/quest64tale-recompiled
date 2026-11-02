# Quest 64 Tale: Recompiled v1.1.0

Solvaring Backstory FMV now includes three fullscreen movies with sound:

- Backstory: after exiting the monastery's main front door.
- Battle introduction: when Solvaring's encounter starts, before his dialogue.
- Defeat: after defeating Solvaring and finishing Brian's victory celebration.

Enable the updated Solvaring Backstory FMV pack in the launcher. It requires
game v1.1.0 or later. Each movie plays once per game session. Escape, Enter,
Space, controller B or Start skips playback. Gameplay and its audio pause
until playback finishes or is skipped, then native dialogue/rewards resume.
Other bosses, escapes and loading an already completed save do not trigger
the Solvaring victory movie. The existing monastery-exit trigger is unchanged.

The mod's version-2 quest64_fmv.json supports multiple clips per pack and
boss_encounter/boss_victory events with a zero-based boss index. Existing
version-1 door-triggered movie packs remain supported.

The game ZIPs contain no ROM, extracted game assets, movie media, saves or
personal settings. Movie media is included only in the optional mods ZIP.
Supply your own USA ROM. Install the twelve mod packs in your profile's Mods
folder and enable the ones you want. Windows normally uses
%LOCALAPPDATA%/Quest64Recompiled/mods; Linux uses ~/.config/Quest64Recompiled/mods.

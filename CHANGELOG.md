## 1.1.2

- Fix the crash when loading a save and entering gameplay.
- Apply the confirmed Dondoran debug-arrival fix to all selectable destinations and eight bosses; regular door animations remain in use during ordinary travel.
- Use a readable vertical attack-preview panel with bold text, effectiveness multipliers and spell/staff damage as a percentage of enemy HP.
- Offer Default (1x), 2x, 4x and 8x in Game Speed and include the latest accelerated-rendering changes.
- Load mods from the profile first, then the game folder. Controller Pak saves retain profile-first reads and mirrored writes.

See [v1.1.2 release notes](UPDATE-1.1.2.md). Windows and Linux contain the same fixes; gameplay validation on each platform remains useful.

## 1.1.1

- Scan mods beside the executable.
- Prefer profile Controller Pak saves and mirror writes to profile and game folders.
- Add optional 2x/4x/8x gameplay speed and damage percentage previews.
- Include current rendering, camera, targeting and debug-arrival fixes on both platforms.

## 1.0.7

- Add separate transition autosaves with recovery from the Mods footer.
- Add five selectable random encounter rates without disabling bosses.
- Add overhead enemy and boss health bars.

## 1.0.6

- Restore Brian's moving ground shadow from joint matrix attachment coordinates.
- Fix running hair interpolation at higher rendering framerates.
- Restore native blob-shadow depth behavior.
- Keep all four existing mods supported on Windows and Linux.

# Changelog

## 1.0.5

- Windows/Linux feature parity, including all four optional mod handlers.
- Correct Quest 64 filtering in the Mods menu.
- Maximum Stats and All Spells can be toggled independently.
- Native map-entrance travel and eight boss selections in Settings → Debug.
- Supplied launcher artwork and open launcher fonts.
- UI files resolve relative to the executable on Windows and Linux.
- Shadow depth-mode and post-blend-noise adjustments for further visual testing.
- Public source preparation excludes ROMs, generated game code, extracted media, saves, caches and personal paths.

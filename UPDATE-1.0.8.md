# Quest 64 Tale Recompiled 1.0.8

Right stick: orbit left/right, raise toward an overhead view, or lower toward
the ground. Camera collision moves the view inward near loaded map surfaces.
Works during exploration and battles; scripted camera sequences keep control.
Walking follows the visible camera. Z cycles medium, far, furthest and near
distances. The camera searches for a clearer orbit when severely wall-blocked.
In battle, A acquires an enemy and aims attacks toward it; L cycles living
targets. Battle starts with a living target selected and retains the final
enemy. Yellow brackets encompass the target's body. Z is reserved for camera zoom in gameplay;
use A for attacks. Button names refer to the configured N64 controls.
Controls use Quest 64 action labels. Choose Overworld or Battle in the Controls
tab to edit independent gameplay bindings. The game switches profiles when
battle starts or ends. D-pad Up selects Fire, Down Water, Left Earth and Right
Wind; A casts the selected spell. Right-stick orbit directions can also be
rebound separately for each profile. Launcher menu bindings are shared.
In battle, Start opens Return / Escape / Quit. A selects an option; B or Start
returns to battle. Left stick or D-pad moves between options. Escape leaves
ordinary battles and is unavailable during boss fights. Quit returns to the title menu.
Graphics > Aspect Ratio > 16:9 applies globally, including native menus and
cutscenes. HUD Placement controls whether the Quest HUD stays centered, anchors
within 16:9, or follows the full screen width. XP and route panels are smaller
and adapt to the chosen aspect ratio with readable text and bar borders. Arrows
are clipped inside the cleared viewport. Door fades cover the widescreen view;
native HUD groups and element-choice numbers retain their alignment.

Enable All Stats Experience and Story Direction Arrow in the launcher's Mods
tab. These optional packs require version 1.0.8. The vertical XP panel shows HP, MP,
speed, defense and element progress. Successful magic attacks grant additional
HP XP; successful staff and magic attacks grant speed and defense XP. Existing
movement and damage XP remain active. Native stat caps and growth rules apply.

The horizontal, extruded purple 3D story arrow guides toward doors/zones along the route to the next
undefeated story boss. A locked route means its required item is missing; this
is a boss-route guide rather than a tracker for every NPC or optional quest.
Spirit Tracker is a separate optional mod: it counts uncollected spirits in the
current level, shows collected/total and remaining, and draws a smaller orange
3D arrow beside the purple arrow toward the nearest spirit,
using connecting doors when the spirit is in another reachable room.

Spell Preview shows the selected spell's native hit radius and target distance
in game world units, plus its element effectiveness against the locked enemy.
Hit radius describes the collision area; it does not predict projectile travel,
obstructions, random spread or guarantees that an attack will hit.

Debug travel restores the complete native entrance record, including door and
stair walk-in state. Boss travel starts at the normal area entrance; approach
the boss to trigger the reopened encounter. The boss selector matches the
location selectors' styling.

Mods are distributed separately. Place the .qsmod and .rtz files in the game's
Mods folder. Windows defaults to %LOCALAPPDATA%/Quest64Recompiled/mods;
Linux defaults to ~/.config/Quest64Recompiled/mods. Portable mode uses mods/
beside the executable. This package contains no ROM or extracted game assets.

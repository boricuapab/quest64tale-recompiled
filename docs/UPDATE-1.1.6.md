# Quest 64 Tale: Recompiled v1.1.6

Fixes Mr. McCloud drifting away from Brian during door transitions. Door poses
are centered on the character feet; the matching native Brian XYZ foot path is
applied separately in game units, independent of model size and facing.
The Maya door clips were corrected using the original Brian scene as reference.
Door placement now follows the root matrix actually submitted by the native
renderer, including its translation masks and scripted door movement. This
prevents the unmasked Maya path from being added again during a transition.
The tail is animated throughout the clips; staff grip and intersecting arm poses
were adjusted in the rig. The original Maya files remain untouched.
The native actor, camera, collision, shadow calculations and smooth vertex
interpolation remain active. The updated character pack requires v1.1.6.

Adds CustomChrMr.McCloudMod, a custom Brian replacement with all 30 animation
clips, the animated staff, original vertex colors and original-resolution PNGs.
The character faces Brian's forward direction, including the opening dialogue.
Eyes hold open for about three seconds, followed by a half-second blink at 12 fps:
001, 002, 003, 004, 003, 002 (the requested frames 17 through 22).
Texture UVs use Maya-style repeat wrapping and no low-resolution half-texel
shift, preserving small atlas details such as the nose and arm textures.
The GLB also animates eyelids through standard skinned geometry; no proprietary
texture-animation extension is required.

Custom-character support now accepts this more detailed model (3,725 triangles),
vertex colors and animated texture sequences. The native collision, attachment
and moving-shadow calculations and isolated rendering from v1.1.4 are preserved.
Both custom characters now use stable RT64 vertex-interpolation groups so
their baked animation poses blend together at 60 fps and higher. Body, head
and staff share one interpolated pose; UVs and eyelid textures remain discrete.
The existing high-speed interpolation guard is retained.
Kid Mage remains supported. Enable one custom character at a time in Mods.

Install the separate CustomChrMr.McCloudMod.qsmod and enable it in the launcher.
Requires v1.1.6. Profile mods load before executable-folder mods. Windows profile:
%LOCALAPPDATA%/Quest64Recompiled/mods. Linux: ~/.config/Quest64Recompiled/mods.

Includes all v1.1.4 and earlier game, camera, battle, save, travel, FMV and HUD fixes.
Game ZIPs contain no ROMs, extracted game assets, custom models, saves or personal
settings. Supply your own supported USA ROM. The separate mods ZIP has fifteen
installable packs. Character packs include their model, PNGs and pose cache.

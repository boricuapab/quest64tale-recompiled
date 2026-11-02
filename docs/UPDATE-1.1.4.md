# Quest 64 Tale: Recompiled v1.1.4

Adds custom animated character support and the optional CustomChrKidMageMod.
Kid Mage replaces Brian's model and staff with the user's Maya character,
including all 30 animation clips and nine original PNG maps. Character packs
load from the profile mods directory first, then the executable's mods folder.

The native actor, animation state, collision, gameplay, attachments and shadow
calculations remain active. Only the emitted body drawing is replaced. Custom
draws preserve the original rendering modes, lighting, combiner and model matrix
so NPCs, sprites and later draws retain their native rendering state.

Includes the v1.1.3 battle Escape/Quit cleanup, updated Solvaring FMVs and smooth
game/video fades, plus earlier save, travel, camera, speed and widescreen fixes.

Install CustomChrKidMageMod.qsmod from the separate mods download and enable it
in the launcher. Requires this v1.1.4 executable. Windows profile mods are in
%LOCALAPPDATA%/Quest64Recompiled/mods; Linux uses ~/.config/Quest64Recompiled/mods.
The mods folder beside the executable is also supported. Duplicate IDs prefer
the profile copy. Restart after installing or replacing a pack.

The separate CustomChrKidMageMod_v1.1.4.zip contains the installable pack,
KidMage.glb with all 30 clips and embedded textures, and nine standalone PNGs.
The GLB is skinned, UV mapped and includes vertex colors. Native 30 fps animation
samples preserve the evaluated Maya poses and nonuniform affine transforms.

Game ZIPs contain no ROM, extracted game assets, character models, save files or
personal settings. Supply your own supported USA ROM. The separate mods ZIP
contains fourteen installable packs, including Kid Mage and the optional FMVs.

# Quest 64 Tale: Recompiled v1.0.7

Adds support for three independently toggleable gameplay packs: Auto Save, Random Encounter Rate and Enemy Health Bars. Existing four packs remain compatible on Windows and Linux.

Auto Save records a separate checkpoint after door/map/submap transitions finish, keeping two generations. Enable it in Mods. To recover a checkpoint, select Restore Auto Save in the Mods footer, load a game if necessary, then close settings. Ordinary Controller Pak saves are untouched. Initial game loading does not overwrite the previous checkpoint. Active battles and dead-player states are not saved or restored.

Random Encounter Rate offers Off, 10%, 25%, 50% and Default under Mods > Configure. Rates are relative to the original encounters per distance travelled; scripted boss encounters remain available.

Enemy Health Bars shows overhead bars for all visible combat enemies and bosses, with green/yellow/red health thresholds.

Retains the confirmed v1.0.6 shadow and hair fixes. Supply your own USA ROM. Game ZIPs contain no ROMs, extracted gameplay assets, saves, personal settings or mod packs.

Both builds pass compilation and automated behavior checks. The three new features still need an in-game visual check.

Autosave doorway correction: new checkpoints wait until scripted door movement releases control, then wait 15 native ticks. Restoring uses the native free-position spawn instead of entrance 0, and clears stale doorway animation state. Older checkpoints restore at the closest native entrance rather than forcing potentially unsafe doorway coordinates.

Enemy health-bar correction: each battle frame projects every living enemy independently of damage/status effects. Bars remain at the screen edge when close-range head anchors move outside the view, rather than being discarded. Dead and behind-camera enemies are excluded.

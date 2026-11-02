# Game Speed

Enable **Game Speed** in the launcher's **Mods** tab, then open **Configure** and select **Default (1x)**, **2x**, **4x**, or **8x**.

The mod runs complete native gameplay updates multiple times per displayed frame. Brian's walking and attacks, enemy actions, spell effects and combat timers all advance faster. Camera rotation, audio and FMV playback keep their normal timing. Menus, dialogue, door transitions and battle pause keep their normal update rate.

Each additional update uses native movement and collision handling. Door triggers stop the additional updates so normal travel can begin. Button presses are handled once per frame; held movement continues throughout all updates. The existing camera-relative walking and targeting controls remain in use.

Install `qs64_game_speed.qsmod` with the updated game executable. The pack contains configuration only and needs the new runtime handler; an older executable will not accelerate gameplay.

"""Package the optional native simulation-speed mod (no game assets)."""
from pathlib import Path
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
feature = 'qs64_game_speed'
manifest = dict(game_id='qs64', id=feature, display_name='Game Speed',
    short_description='2x, 4x or 8x gameplay speed', version='1.0.0',
    minimum_recomp_version='1.1.1', authors=['Quest 64 Tale'], enabled_by_default=False,
    description='Accelerates walking, attacks, enemies and combat timers using complete native simulation steps. Menus, dialogue and transitions stay at normal speed. Requires the updated game executable.',
    config_schema={'options':[dict(id='speed', name='Gameplay speed',
        description='All active gameplay advances at this rate; disable the mod for normal speed.',
        type='Enum', options=['2x','4x','8x'], default='2x')]})
files = {'mod.json':manifest, 'qs64_gameplay.json':dict(version=1, feature=feature)}
archive = root/'mods'/(feature+'.qsmod')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as out:
    for name, data in files.items():out.writestr(name,json.dumps(data,indent=2))
for folder in ('playable-windows/mods','render-test-windows/mods','publication/mods'):
    destination=root/folder
    destination.mkdir(parents=True,exist_ok=True)
    shutil.copy2(archive,destination/archive.name)
source=root/'publication/mods/packs'/feature
source.mkdir(parents=True,exist_ok=True)
for name,data in files.items():(source/name).write_text(json.dumps(data,indent=2)+'\n')
print(archive)

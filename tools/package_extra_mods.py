"""Build the three additional native gameplay mod packs (code/configuration only)."""
from pathlib import Path
import json,zipfile
root=Path(__file__).resolve().parents[1]
mods=[
 ('qs64_autosave','Auto Save','Saves a separate checkpoint after door and area transitions. Restore from the Mods footer after loading a game; normal Controller Pak saves are untouched.'),
 ('qs64_encounter_rate','Random Encounter Rate','Choose Off, 10%, 25%, 50%, or Default in Configure. Rates scale encounters per distance travelled; scripted boss fights are unchanged.'),
 ('qs64_enemy_health','Enemy Health Bars','Shows an overhead health bar for every visible enemy during battles, including bosses. Bars turn yellow and red as health falls.'),
]
for id,name,description in mods:
 manifest=dict(game_id='qs64',id=id,display_name=name,description=description,short_description=name,
  version='1.0.0',minimum_recomp_version='1.0.7',authors=['Quest 64 Tale'],enabled_by_default=False)
 if id=='qs64_encounter_rate':
  manifest['config_schema']={'options':[dict(id='rate',name='Random encounter rate',description='Relative to the original encounters per distance travelled.',type='Enum',options=['Off','10%','25%','50%','Default'],default='Default')]}
 target=root/'mods'/(id+'.qsmod')
 with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
  z.writestr('mod.json',json.dumps(manifest,indent=2));z.writestr('qs64_gameplay.json',json.dumps(dict(version=1,feature=id)))
 print(target.name)

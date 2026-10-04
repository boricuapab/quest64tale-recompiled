"""Generate valid Quest 64 maps, entrances and boss locations from the ROM."""
import json,struct
from pathlib import Path
root=Path(__file__).resolve().parents[1];rom=(root/'quest64.us.z64').read_bytes()
names=['Melrode','Dondoran','Holy Plain','Dondoran Flats','Larapool','West Carmagh','Normoon','East Limelin','Limelin Castle','Dindom Dries','Shamwood','Brannoch','Isle of Skye','Monastery','Dondoran Castle','Melrode Houses','Dondoran Houses','Larapool Houses','Carmagh Houses','Windmills','Normoon Houses','Limelin Houses','Regional Houses','Brannoch Houses','Shamwood Interior','Skye Houses','Blue Cave','Cull Hazard','Baragoon Tunnel','Boil Hole','Brannoch Castle','Connor Fortress','Glencoe Forest','Windward Forest','Mammon','Nepty']
areas=[];destinations=[]
for map_id,name in enumerate(names):
    offset=0x55b10+map_id*68
    start,end,base=struct.unpack_from('>III',rom,offset+4)
    ptr=struct.unpack_from('>I',rom,offset+24)[0];count=struct.unpack_from('>I',rom,offset+64)[0]
    scenes=[]
    for submap in range(count):
        sub=start+ptr-base+submap*24
        entrances,spawn_ptr=struct.unpack_from('>II',rom,sub+8)
        assert 0<=entrances<256,(name,submap,entrances)
        if entrances==0:continue
        labels=[]
        for entrance in range(entrances):
            spawn=start+spawn_ptr-base+entrance*20
            assert start<=spawn<end
            x,z,heading=struct.unpack_from('>fff',rom,spawn)
            flags,door=struct.unpack_from('>HH',rom,spawn+12)
            destinations.append(dict(map=map_id,submap=submap,entrance=entrance,x=x,z=z,heading=heading,flags=flags,door=door))
            labels.append('Entrance '+str(entrance+1))
        scene_name='Submap '+str(submap+1)
        if map_id==13 and submap==17:scene_name="Abbot's Chamber"
        scenes.append(dict(index=submap,name=scene_name,entrances=labels))
    areas.append(dict(name=name,scenes=scenes))
boss_names=['Solvaring','Zelse','Nepty','Shilf','Fargo','Guilty','King Beigis','Mammon']
bosses=[]
for i,name in enumerate(boss_names):
    map_id,submap,model,pad,ptr,x,z,heading=struct.unpack_from('>HHHHIfff',rom,0xd873a0+i*24)
    assert any(s['index']==submap for s in areas[map_id]['scenes'])
    bosses.append(dict(id=i,name=name,map=map_id,submap=submap,mask=rom[0x4e340+i]))
out=['#include "zelda_debug.h"','std::vector<zelda64::AreaWarps> zelda64::game_warps {']
for area in areas:
    out.append('{'+json.dumps(area['name'])+', {')
    for scene in area['scenes']:out.append('{'+str(scene['index'])+','+json.dumps(scene['name'])+',{'+','.join(json.dumps(x) for x in scene['entrances'])+'}},')
    out.append('}},')
out.append('};');(root/'src/game/scene_table.cpp').write_text('\n'.join(out))
data=['// Generated from the verified Quest 64 US ROM.','static constexpr Destination destinations[] = {']
for d in destinations:
    def f(v):return str(float(v))+'f'
    data.append('{'+','.join(str(d[k]) for k in ['map','submap','entrance'])+','+','.join(f(d[k]) for k in ['x','z','heading'])+','+str(d['flags'])+','+str(d['door'])+'},')
data.append('};');data.append('static constexpr BossDestination bosses[] = {')
for b in bosses:data.append('{'+json.dumps(b['name'])+','+','.join(str(b[k]) for k in ['map','submap','mask'])+'},')
data.append('};');(root/'src/game/quest64_destinations.inc').write_text('\n'.join(data))
(root/'mods/debug-destinations.json').write_text(json.dumps(dict(maps=areas,bosses=bosses),indent=2))
print('Maps',len(areas),'submaps',sum(len(a['scenes']) for a in areas),'entrances',len(destinations),'bosses',len(bosses))

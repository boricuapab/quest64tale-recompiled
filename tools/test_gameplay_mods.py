import ctypes as c, json, struct
from pathlib import Path
root = Path(__file__).resolve().parents[1]
dll = c.CDLL(str(root/'tools/quest64_mod_test.dll'))
ram = (c.c_uint8 * 0x800000)()
def offset(a, fmt):
    size = struct.calcsize(fmt)
    return (a & 0x7fffff) ^ ({1:3,2:2}.get(size,0))
def put(a, value, fmt='I'): struct.pack_into('<'+fmt,ram,offset(a,fmt),value)
def get(a, fmt='I'): return struct.unpack_from('<'+fmt,ram,offset(a,fmt))[0]
def tick(): dll.qs64_mod_tick(ram)
def enable(stats=0, spells=0, debug=1): dll.qs64_test_enable(stats,spells,debug)
P=0x8007BA80; M=0x8007B2E0
dll.qs64_mod_reset(ram)
put(M,1,'H')
for off,val in [(4,33),(6,60),(8,12),(10,40),(12,7),(14,8)]: put(P+off,val,'H')
for i in range(4): put(P+0x24+i,i+1,'B'); put(P+0x30+i,i+2,'B')
put(P+0x34,10,'B')
enable(1);tick()
assert [get(P+i,'H') for i in (6,10,12,14)]==[500,500,255,255]
assert [get(P+0x24+i,'B') for i in range(4)]==[50]*4
put(P+4,123,'H');tick();assert get(P+4,'H')==123, 'must not provide infinite health'
enable(0,1);tick()
assert [get(P+i,'H') for i in (6,10,12,14)]==[60,40,7,8]
assert get(P+4,'H')==60
assert [get(P+0x24+i,'B') for i in range(4)]==[50]*4
enable();tick()
assert [get(P+0x24+i,'B') for i in range(4)]==[1,2,3,4]
assert get(P+0x34,'B')==10
put(M,3,'H');enable(1);tick();assert get(P+6,'H')==60
enable();put(M,1,'H')
assert not dll.qs64_test_warp(-1,0,0)
assert not dll.qs64_test_boss(8)
data=json.loads((root/'mods/debug-destinations.json').read_text())
count=0
for map_id, area in enumerate(data['maps']):
    for scene in area['scenes']:
        for entrance in range(len(scene['entrances'])):
            put(M,1,'H');put(0x80084EEC,999)
            assert dll.qs64_test_warp(map_id,scene['index'],entrance)
            tick();assert get(0x80084EEC)==999 and get(M,'H')==0
            dll.qs64_mod_finish_warp(ram)
            assert (get(0x80084EEC),get(0x80084EF0),get(0x80085370))==(map_id,scene['index'],entrance)
            assert get(0x8007B2E4)==0x140
            count+=1
put(M,1,'H');put(0x8008C592,1,'H');put(0x80084EEC,777)
assert dll.qs64_test_warp(0,0,0);tick();dll.qs64_mod_finish_warp(ram)
assert get(0x80084EEC)==777 and get(M,'H')==1
put(0x8008C592,0,'H')
for boss in data['bosses']:
    put(M,1,'H');put(0x8007D19C,255,'B')
    assert dll.qs64_test_boss(boss['id']);tick();dll.qs64_mod_finish_warp(ram)
    assert get(0x8007D19C,'B')==255 & ~boss['mask']
    put(M,1,'H');put(0x8007D1A0,0);put(0x8007BACC,-999,'f')
    dll.qs64_mod_player_tick(ram);assert get(0x8007BACC,'f')==-999
    for i in range(3):put(0x8007D1CC+4*i,10+i,'f')
    put(0x8007D1A0,boss['id']+1);dll.qs64_mod_player_tick(ram)
    assert abs(get(0x8007BACC,'f')-10.01)<1e-5
    assert get(0x8007BAD0,'f')==11 and get(0x8007BAD4,'f')==12
enable(debug=0);assert not dll.qs64_test_warp(0,0,0)
print(f'PASS: toggle composition/restoration, legal stat caps, no infinite HP, title protection, {count} entrances, battle guard, eight boss requests and deferred positioning.')

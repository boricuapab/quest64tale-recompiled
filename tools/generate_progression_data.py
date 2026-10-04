"""Derive route connections and boss targets locally from the user's US ROM.

The generated include is build data; never distribute it in the source repo.
"""
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
rom = (root / 'quest64.us.z64').read_bytes()
edges = []
nodes = set()
for m in range(36):
    h = 0x55b10 + m * 68
    start, end, base = struct.unpack_from('>III', rom, h + 4)
    ptr = struct.unpack_from('>I', rom, h + 24)[0]
    count = struct.unpack_from('>I', rom, h + 64)[0]
    for s in range(count):
        nodes.add((m, s))
        sub = start + ptr - base + s * 24
        exits, n = struct.unpack_from('>II', rom, sub + 4)
        assert n < 256
        for i in range(n):
            p = start + exits - base + i * 36
            assert start <= p and p + 36 <= end
            x, z = struct.unpack_from('>ff', rom, p)
            flags, item = struct.unpack_from('>HH', rom, p + 20)
            dest, scene, entrance = struct.unpack_from('>HHH', rom, p + 30)
            edges.append((m, s, dest, scene, x, z, flags, item))
assert all((e[2], e[3]) in nodes for e in edges)
lines = ['// Generated locally from the user-supplied ROM.', 'static constexpr RouteEdge route_edges[] = {']
for m, s, d, t, x, z, flags, item in edges:
    lines.append(f'{{{m},{s},{d},{t},{x}f,{z}f,{flags},{item}}},')
lines += ['};', 'static constexpr StoryBoss story_bosses[] = {']
for i, name in enumerate(('Solvaring', 'Zelse', 'Nepty', 'Shilf', 'Fargo', 'Guilty', 'King Beigis', 'Mammon')):
    m, s = struct.unpack_from('>HH', rom, 0xd873a0 + i * 24)
    x, z, heading = struct.unpack_from('>fff', rom, 0xd873a0 + i * 24 + 12)
    y = 0.0
    lines.append(f'{{"{name}",{m},{s},{x}f,{y}f,{z}f,{rom[0x4e340+i]}}},')
lines.append('};')
lines.append('static constexpr SpiritSpot spirit_spots[] = {')
spirits=[]
for i in range(43):
    m,s,n,_,ptr=struct.unpack_from('>HHHHI',rom,0x4d110+i*12)
    start,end,base=struct.unpack_from('>III',rom,0x55b10+m*68+4)
    for j in range(n):
        p=start+ptr-base+j*12
        assert start<=p and p+12<=end
        x,z,flag=struct.unpack_from('>ffB',rom,p)
        spirits.append((m,s,x,-z,flag))
        lines.append(f'{{{m},{s},{x}f,{-z}f,{flag}}},')
assert len(spirits)==98 and len({s[4] for s in spirits})==98
lines.append('};')
(root / 'src/game/quest64_progression_data.inc').write_text('\n'.join(lines) + '\n')
print(f'Generated {len(edges)} route exits across {len(nodes)} areas and 8 story targets.')

from pathlib import Path
import struct,json
root=Path(__file__).resolve().parents[1];rom=(root/'quest64.us.z64').read_bytes()
ranges={}
for off in range(0x4b000,0x73a90,4):
    a,b=struct.unpack_from('>II',rom,off)
    if 0x86b830<=a<b<=0xd3e240 and a%16==0 and b%16==0 and 128<b-a<0x100000:
        ranges.setdefault((a,b),[]).append(off)
for off in range(0xd77380,0xda68f0,4):
    a,b=struct.unpack_from('>II',rom,off)
    if 0xcc4570<=a<b<=0xd305e0 and a%16==0 and b%16==0 and 128<b-a<0x40000:
        ranges.setdefault((a,b),[]).append(off)
inventory=[]
for (start,end),refs in sorted(ranges.items()):
    desc=[]
    for off in range(start,end-12,4):
        n,frames,p,dls=struct.unpack_from('>hhII',rom,off)
        if 1<=n<=64 and 1<=frames<=2000 and 0x80100000<=p<0x803b5000 and 0x80100000<=dls<0x803b5000 and p%4==dls%4==0:
            desc.append((off,n,frames,p,dls))
    if desc:inventory.append(dict(start=start,end=end,table_refs=refs,descriptors=desc))
(root/'tools/actor-bank-inventory.json').write_text(json.dumps(inventory,indent=2))
for b in inventory:print(hex(b['start']),hex(b['end']),len(b['descriptors']),hex(b['descriptors'][0][3]),hex(b['descriptors'][0][4]),[hex(x) for x in b['table_refs']])

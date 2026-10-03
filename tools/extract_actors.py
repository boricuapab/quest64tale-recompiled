"""Export all actor banks referenced by the native NPC/enemy/boss tables."""
import ctypes, io, json, struct, hashlib, sys
from pathlib import Path
import numpy as np
from PIL import Image
from extract_glb import ROOT,Memory,GLB,DisplayList,sample_function,quaternion,EXPECTED_SHA1
from combine_brian import decompose
from verify_glb import transform

def records(mem, n, ptr, dls):
    out=[]
    for j in range(n):
        addr=ptr+j*32
        track,keys,cycle=mem.unpack('>IHH',addr)
        if not 1<=keys<=2000:raise ValueError('invalid track length')
        mem.read(track,36 if keys==1 else keys*52)
        pivot=mem.unpack('>fff',addr+8)
        if not np.isfinite(pivot).all():raise ValueError('invalid pivot')
        parent,mesh=mem.unpack('>bb',addr+20)
        if not 0<=parent<=n or parent==j+1:raise ValueError('invalid parent')
        seq,length=mem.unpack('>IH',addr+24)
        if length>2000:raise ValueError('invalid mesh sequence')
        ids=mem.unpack('>'+str(length)+'I',seq) if seq and length else [mesh]
        dlids=[]
        for i in ids:
            if i<0 or i==0xffffffff:dlids.append(0);continue
            if i>255:raise ValueError('invalid mesh index')
            dl=mem.unpack('>I',dls+4*i)[0]
            if dl:mem.read(dl,8)
            dlids.append(dl)
        out.append((addr,pivot,parent,dlids,cycle))
    # Detect cycles before invoking the original evaluator.
    for j in range(n):
        seen=set();k=j
        while k>=0:
            if k in seen:raise ValueError('cyclic hierarchy')
            seen.add(k);k=out[k][2]-1
    return out

def export(mem, clips, path, name):
    glb=GLB(name);root=glb.node(name=name,scale=[.01]*3,children=[])
    glb.doc['scenes'][0]['nodes']=[root]
    count=max(c[1] for c in clips)
    joints=[glb.node(name=f'joint_{j:02d}',children=[]) for j in range(count)]
    bases=[glb.node(name=f'joint_{j:02d}_basis',children=[]) for j in range(count)]
    glb.doc['nodes'][root]['children']=joints
    for j in range(count):glb.doc['nodes'][joints[j]]['children']=[bases[j]]
    variants={}; parsed=[]
    for off,n,duration,ptr,dls in clips:
        rec=records(mem,n,ptr,dls);parsed.append(rec)
        for j,(_,pivot,parent,ids,cycle) in enumerate(rec):
            for dl in set(ids):
                key=(j,dl)
                if not dl or key in variants:continue
                parser=DisplayList(mem,glb);parser.run(dl);mesh=parser.mesh(f'{name}_part_{j:02d}_{dl:08x}')
                if mesh is not None:
                    node=glb.node(name=f'part_{j:02d}_{dl:08x}',mesh=mesh,scale=[0]*3)
                    glb.doc['nodes'][bases[j]]['children'].append(node);variants[key]=node
    if not variants:raise ValueError('actor has no drawable mesh')
    sample=sample_function();buffer=(ctypes.c_ubyte*len(mem.rdram)).from_buffer(mem.rdram)
    for aid,((off,n,duration,ptr,dls),rec) in enumerate(zip(clips,parsed)):
        frames=[]
        for f in range(duration+1):
            local=[]
            for addr,pivot,parent,ids,cycle in rec:
                result=(ctypes.c_float*9)();sample(buffer,addr,f%cycle if cycle else 0,result)
                mat=transform(dict(translation=list(result[:3]),rotation=quaternion(result[3:6]),scale=list(result[6:])))
                p=np.eye(4);p[:3,3]=-np.array(pivot);local.append(mat@p)
            world={}
            def resolve(j):
                if j not in world:world[j]=(resolve(rec[j][2]-1) if rec[j][2] else np.eye(4))@local[j]
                return world[j]
            values=[]
            for j in range(count):
                mat=resolve(j) if j<n else np.eye(4);v=decompose(mat)
                rebuilt=transform(dict(translation=v[0],rotation=v[1],scale=v[2]))@transform(dict(rotation=v[3]))
                if not np.allclose(mat,rebuilt,atol=1e-5):raise ValueError('matrix reconstruction failed')
                values.append(v)
            frames.append(values)
        animation=dict(name=f'animation_{aid:03d}',channels=[],samplers=[],extras=dict(descriptor_rom=f'{off:08x}',reference_fps=30,n64_duration=duration))
        times=glb.accessor(np.arange(duration+1)/30,'SCALAR')
        def channel(node,prop,values,kind):
            animation['samplers'].append(dict(input=times,output=glb.accessor(values,kind),interpolation='STEP'))
            animation['channels'].append(dict(sampler=len(animation['samplers'])-1,target=dict(node=node,path=prop)))
            if aid==0:glb.doc['nodes'][node][prop]=values[0]
        for j in range(count):
            for k,prop,kind in [(0,'translation','VEC3'),(1,'rotation','VEC4'),(2,'scale','VEC3')]:channel(joints[j],prop,[f[j][k] for f in frames],kind)
            channel(bases[j],'rotation',[f[j][3] for f in frames],'VEC4')
        for (j,dl),node in variants.items():
            ids=rec[j][3] if j<n else [0]
            channel(node,'scale',[[1]*3 if ids[f%len(ids)]==dl else [0]*3 for f in range(duration+1)],'VEC3')
        glb.doc['animations'].append(animation)
    glb.doc['extras']=dict(source_sha1=EXPECTED_SHA1,rig='Rigid parts, native frame STEP transforms, mesh variants',material_limitations='N64 lighting/combiner approximated by glTF materials')
    path.mkdir(parents=True,exist_ok=True);glb.save(path/(name+'.glb'))
    tex=path/'textures';tex.mkdir(exist_ok=True);manifest=[]
    for i,img in enumerate(glb.doc['images']):
        view=glb.doc['bufferViews'][img['bufferView']];raw=glb.data[view['byteOffset']:view['byteOffset']+view['byteLength']]
        filename=f'{i:03d}_{img["name"]}.png';(tex/filename).write_bytes(raw)
        size=Image.open(io.BytesIO(raw)).size;manifest.append(dict(file=filename,glb_image=i,width=size[0],height=size[1]))
    (tex/'manifest.json').write_text(json.dumps(manifest,indent=2))
    return dict(name=name,path=str(path/(name+'.glb')),animations=len(clips),textures=len(manifest),joints=count,mesh_variants=len(variants))

def main():
    rom=(ROOT/'quest64.us.z64').read_bytes();assert hashlib.sha1(rom).hexdigest()==EXPECTED_SHA1
    inv=json.loads((ROOT/'tools/actor-bank-inventory.json').read_text())
    # Only native table entries; omit accidental overlapping start/end matches.
    inv=[b for b in inv if any(r in list(range(0x553f0,0x55428,8))+list(range(0x54d60,0x54dd8,20))+list(range(0xd87360,0xd873a0,8)) for r in b['table_refs'])]
    boss={0xccf460:'solvaring',0xcdb730:'zelse',0xcc4570:'nepty',0xce53e0:'fargo',0xcf2a10:'guilty',0xd006b0:'fale',0xd237f0:'king_beigis',0xd0fee0:'mammon'}
    report_path=ROOT/'extracted-assets/actors-extraction-report.json'
    retry='--retry' in sys.argv
    report=json.loads(report_path.read_text()) if retry else dict(models=[],rejected_descriptors=[],failed_models=[])
    requested={f['name'] for f in report['failed_models']} if retry else None
    if retry:report['failed_models']=[]
    for bank in inv:
        start,end=bank['start'],bank['end'];base=0x802a7bc0 if start in boss else 0x8020e6f0
        mem=Memory(rom);mem.map(0x80000400,0x1000,0x73a90);mem.map(0x80100000,0x77560,0x87360)
        mem.map(0x80206000,0x86b830,0x871900)
        mem.map(0x8020c0d0,0x871900,0x873f20);mem.map(0x802a0000,0xa725d0,0xa7a190);mem.map(base,start,end)
        groups={}
        for desc in bank['descriptors']:
            try:records(mem,desc[1],desc[3],desc[4]);groups.setdefault(desc[4],[]).append(desc)
            except (ValueError,struct.error) as e:
                if not retry:report['rejected_descriptors'].append(dict(rom=hex(desc[0]),reason=str(e)))
        category='bosses' if start in boss else 'characters' if start>=0xa7a190 else 'enemies'
        for group_id,(dls,clips) in enumerate(sorted(groups.items())):
            singular={'characters':'character','enemies':'enemy','bosses':'boss'}[category]
            name=boss[start]+(f'_variant_{group_id:02d}' if len(groups)>1 else '') if start in boss else f'{singular}_{start:08x}_{group_id:03d}'
            if retry and name not in requested:continue
            try:
                result=export(mem,clips,ROOT/'extracted-assets'/category/name,name)
                result.update(bank_start=f'{start:08x}',bank_end=f'{end:08x}',display_list_table=f'{dls:08x}');report['models'].append(result)
                print('EXPORTED',name,len(clips),'clips',result['textures'],'textures',flush=True)
            except Exception as e:
                report['failed_models'].append(dict(name=name,reason=str(e)));print('FAILED',name,str(e),flush=True)
        (ROOT/'extracted-assets/actors-extraction-report.json').write_text(json.dumps(report,indent=2))
    print('TOTAL',len(report['models']),'models;',len(report['failed_models']),'failed',flush=True)

if __name__=='__main__':main()

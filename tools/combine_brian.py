"""Bake Brian's changing joint attachments into one shared 24-node rig."""
import ctypes, json, math, struct
import numpy as np
from extract_glb import ROOT, Memory, GLB, DisplayList, sample_function, quaternion, EXPECTED_SHA1
from verify_glb import transform

def decompose(m):
    r,scale,right=np.linalg.svd(m[:3,:3])
    if np.linalg.det(r)<0:r[:,-1]*=-1;scale[-1]*=-1
    if np.linalg.det(right)<0:right[-1,:]*=-1;scale[-1]*=-1
    return m[:3,3].tolist(),rotation_quaternion(r),scale.tolist(),rotation_quaternion(right)

def rotation_quaternion(r):
    trace=np.trace(r)
    if trace>0:
        s=math.sqrt(trace+1)*2
        q=[(r[2,1]-r[1,2])/s,(r[0,2]-r[2,0])/s,(r[1,0]-r[0,1])/s,s/4]
    else:
        i=int(np.argmax(np.diag(r))); j=(i+1)%3; k=(i+2)%3
        s=math.sqrt(1+r[i,i]-r[j,j]-r[k,k])*2
        q=[0.,0.,0.,0.];q[i]=s/4;q[j]=(r[j,i]+r[i,j])/s;q[k]=(r[k,i]+r[i,k])/s;q[3]=(r[k,j]-r[j,k])/s
    return q

def main():
    rom=(ROOT/'quest64.us.z64').read_bytes();mem=Memory(rom)
    mem.map(0x80000400,0x1000,0x73A90);mem.map(0x80206000,0x86B830,0x871900)
    glb=GLB('Brian_all_30_animations');root=glb.node(name='Brian',scale=[.01]*3,children=[])
    glb.doc['scenes'][0]['nodes']=[root]
    joints=[glb.node(name=f'joint_{i:02d}',children=[]) for i in range(24)]
    corrections=[glb.node(name=f'joint_{i:02d}_basis',children=[]) for i in range(24)]
    for j,node in enumerate(joints):glb.doc['nodes'][node]['children']=[corrections[j]]
    glb.doc['nodes'][root]['children']=joints
    sample=sample_function();clips=[];variants={};references={}
    for aid in range(30):
        start,end=struct.unpack_from('>II',rom,0x54B58+aid*8)
        count,duration,ptr,dlptr=mem.unpack('>hhII',0x80206064+aid*12)
        mem.map(0x80200000,start,end)
        records=[]
        for j in range(count):
            addr=ptr+j*32;pivot=mem.unpack('>fff',addr+8);parent,mesh=mem.unpack('>bb',addr+20)
            seq,length=mem.unpack('>IH',addr+24)
            ids=list(mem.unpack('>'+str(length)+'I',seq)) if seq and length else [mesh]
            mapped=[]
            for meshid in ids:
                dl=mem.unpack('>I',dlptr+meshid*4)[0] if meshid>=0 else 0
                key=(j,dl)
                if dl and key not in variants:
                    parser=DisplayList(mem,glb);parser.run(dl);meshidx=parser.mesh(f'Brian_part_{j:02d}_{dl:08x}')
                    if meshidx is not None:
                        node=glb.node(name=f'part_{j:02d}_{dl:08x}',mesh=meshidx,scale=[0]*3)
                        glb.doc['nodes'][corrections[j]]['children'].append(node);variants[key]=node
                mapped.append(key if key in variants else None)
            records.append((addr,pivot,parent,mapped))
        buffer=(ctypes.c_ubyte*len(mem.rdram)).from_buffer(mem.rdram)
        frames=[];native_matrices=[]
        for frame in range(duration+1):
            locals=[]
            for addr,pivot,parent,ids in records:
                cycle=mem.unpack('>H',addr+6)[0];result=(ctypes.c_float*9)()
                sample(buffer,addr,frame%cycle if cycle else 0,result)
                m=transform(dict(translation=list(result[:3]),rotation=quaternion(result[3:6]),scale=list(result[6:9])))
                p=np.eye(4);p[:3,3]=-np.array(pivot);locals.append(m@p)
            world={}
            def resolve(j):
                if j not in world:
                    parent=records[j][2];world[j]=(resolve(parent-1) if parent else np.eye(4))@locals[j]
                return world[j]
            matrices=[resolve(j) if j<count else np.eye(4) for j in range(24)]
            native_matrices.append(matrices)
            values=[decompose(m) for m in matrices]
            for m,(t,q,s,basis) in zip(matrices,values):
                rebuilt=transform(dict(translation=t,rotation=q,scale=s))@transform(dict(rotation=basis))
                assert np.allclose(m,rebuilt,atol=1e-5), 'Animation matrix reconstruction failed'
            frames.append(values)
        references[f'animation_{aid:02d}']=np.asarray(native_matrices)
        clips.append((aid,duration,records,frames))
    for aid,duration,records,frames in clips:
        animation=dict(name=f'animation_{aid:02d}',channels=[],samplers=[],extras=dict(reference_fps=30,n64_duration=duration))
        times=glb.accessor(np.arange(duration+1)/30,'SCALAR')
        # SVD bases can swap axes between frames. Interpolating their factors
        # independently distorts otherwise correct matrices. Native-frame STEP
        # baking preserves each exact matrix, including nonuniform-scale shear.
        def channel(node,path,values,kind,interpolation='STEP'):
            animation['samplers'].append(dict(input=times,output=glb.accessor(values,kind),interpolation=interpolation))
            animation['channels'].append(dict(sampler=len(animation['samplers'])-1,target=dict(node=node,path=path)))
        for j,node in enumerate(joints):
            values=[[frame[j][k] for frame in frames] for k in range(3)]
            for f in range(1,len(frames)):
                if np.dot(values[1][f-1],values[1][f])<0: values[1][f]=[-x for x in values[1][f]]
            for k,(path,kind) in enumerate([('translation','VEC3'),('rotation','VEC4'),('scale','VEC3')]):
                channel(node,path,values[k],kind)
                if aid==0:glb.doc['nodes'][node][path]=values[k][0]
            basis=[frame[j][3] for frame in frames]
            for f in range(1,len(basis)):
                if np.dot(basis[f-1],basis[f])<0:basis[f]=[-x for x in basis[f]]
            channel(corrections[j],'rotation',basis,'VEC4')
            if aid==0:glb.doc['nodes'][corrections[j]]['rotation']=basis[0]
        for (j,dl),node in variants.items():
            ids=records[j][3] if j<len(records) else [None]
            scales=[[1]*3 if ids[f%len(ids)]==(j,dl) else [0]*3 for f in range(duration+1)]
            channel(node,'scale',scales,'VEC3','STEP')
            if aid==0:glb.doc['nodes'][node]['scale']=scales[0]
        glb.doc['animations'].append(animation)
    glb.doc['extras']=dict(source_sha1=EXPECTED_SHA1,rig='Shared rigid-part nodes; changing attachments baked to world transforms',material_limitations='N64 combiner and lighting approximated')
    path=ROOT/'extracted-assets/characters/brian/brian.glb';glb.save(path)
    np.savez_compressed(ROOT/'extracted-assets/characters/brian/animation-reference.npz',**references)
    import io
    from PIL import Image
    texture_dir=ROOT/'extracted-assets/characters/brian/textures'
    texture_dir.mkdir(parents=True,exist_ok=True)
    manifest=[]
    for index,image in enumerate(glb.doc['images']):
        view=glb.doc['bufferViews'][image['bufferView']];offset=view['byteOffset']
        filename=f'{index:02d}_{image["name"]}.png'
        raw=glb.data[offset:offset+view['byteLength']]
        (texture_dir/filename).write_bytes(raw)
        dimensions=Image.open(io.BytesIO(raw)).size
        manifest.append(dict(file=filename,glb_image=index,width=dimensions[0],height=dimensions[1]))
    (texture_dir/'manifest.json').write_text(json.dumps(manifest,indent=2))
    report_path=ROOT/'extracted-assets/extraction-report.json'
    if report_path.exists():
        report=json.loads(report_path.read_text())
        report.update(joints=24,meshes=len(variants),textures=len(glb.doc['images']),combined_animations=30)
        for clip in report['animations']:
            clip['status']='exported';clip.pop('reason',None)
        report_path.write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(path=str(path),animations=len(clips),joint_nodes=len(joints),mesh_variants=len(variants))))

if __name__=='__main__':main()

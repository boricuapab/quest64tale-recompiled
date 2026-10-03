"""Structural validation plus a CPU-rendered preview of extracted GLBs."""
from pathlib import Path
import argparse, io, json, struct
import numpy as np
from PIL import Image

def load(path):
    raw=path.read_bytes()
    magic,version,length=struct.unpack_from('<4sII',raw)
    assert magic==b'glTF' and version==2 and length==len(raw)
    size,kind=struct.unpack_from('<I4s',raw,12)
    assert kind==b'JSON'
    doc=json.loads(raw[20:20+size])
    pos=20+size
    binsize,kind=struct.unpack_from('<I4s',raw,pos)
    assert kind==b'BIN\0'
    data=raw[pos+8:pos+8+binsize]
    assert len(data)>=doc['buffers'][0]['byteLength']
    for view in doc['bufferViews']:
        assert view.get('byteOffset',0)+view['byteLength']<=len(data)
    def accessor(index):
        acc=doc['accessors'][index];view=doc['bufferViews'][acc['bufferView']]
        n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[acc['type']]
        dtype={5126:'<f4',5125:'<u4'}[acc['componentType']]
        start=view.get('byteOffset',0)+acc.get('byteOffset',0)
        values=np.frombuffer(data,dtype=dtype,count=acc['count']*n,offset=start).reshape(-1,n)
        assert values.nbytes+acc.get('byteOffset',0)<=view['byteLength']
        assert np.isfinite(values).all()
        return values
    for i in range(len(doc['accessors'])): accessor(i)
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            attrs=primitive['attributes']
            assert 'POSITION' in attrs and 'COLOR_0' in attrs
            count=len(accessor(attrs['POSITION']))
            assert count%3==0 and all(len(accessor(index))==count for index in attrs.values())
    for animation in doc['animations']:
        for channel in animation['channels']:
            sampler=animation['samplers'][channel['sampler']]
            times=accessor(sampler['input']).ravel();values=accessor(sampler['output'])
            assert len(times)==len(values) and np.all(np.diff(times)>0)
            assert channel['target']['node']<len(doc['nodes'])
            if channel['target']['path']=='rotation':
                assert np.allclose(np.linalg.norm(values,axis=1),1,atol=1e-5)
    return doc,data,accessor

def transform(node):
    x,y,z,w=node.get('rotation',[0,0,0,1])
    rotation=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                       [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                       [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    matrix=np.eye(4);matrix[:3,:3]=rotation @ np.diag(node.get('scale',[1,1,1]))
    matrix[:3,3]=node.get('translation',[0,0,0])
    return matrix

def preview(path, output):
    doc,data,accessor=load(path)
    textures=[]
    for image in doc['images']:
        view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0)
        textures.append(np.asarray(Image.open(io.BytesIO(data[start:start+view['byteLength']])).convert('RGBA'))/255)
    draws=[];allpos=[];visited=set()
    def walk(index,parent,ancestors):
        assert index not in ancestors, 'Joint hierarchy cycle'
        assert index not in visited, 'Node has multiple parents'
        visited.add(index)
        node=doc['nodes'][index];world=parent @ transform(node)
        if abs(np.linalg.det(world[:3,:3]))<1e-12: return
        if 'mesh' in node:
            for primitive in doc['meshes'][node['mesh']]['primitives']:
                attrs=primitive['attributes'];positions=accessor(attrs['POSITION'])
                positions=positions @ world[:3,:3].T+world[:3,3]
                colors=accessor(attrs['COLOR_0']).copy()
                material=doc['materials'][primitive['material']]
                pbr=material['pbrMetallicRoughness'];colors*=pbr['baseColorFactor']
                uv=accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else None
                texture=None
                if 'baseColorTexture' in pbr:
                    tex=doc['textures'][pbr['baseColorTexture']['index']]
                    texture=(textures[tex['source']],doc['samplers'][tex.get('sampler',0)])
                if 'NORMAL' in attrs:
                    normals=accessor(attrs['NORMAL']) @ np.linalg.inv(world[:3,:3])
                    normals/=np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-9)
                    light=np.array([.25,.6,.75]);light/=np.linalg.norm(light)
                    shade=.55+.45*np.maximum(normals @ light,0)
                    colors[:,:3]*=shade[:,None]
                draws.append((positions,colors,uv,texture));allpos.extend(positions)
        for child in node.get('children',[]): walk(child,world,ancestors|{index})
    for root in doc['scenes'][doc['scene']]['nodes']: walk(root,np.eye(4),set())
    points=np.asarray(allpos);minimum=points.min(axis=0);maximum=points.max(axis=0)
    print(path.name,'meshes',len(doc['meshes']),'textures',len(textures),'animations',len(doc['animations']),'bounds',minimum.round(3),maximum.round(3))
    yaw=.3
    view=np.array([[np.cos(yaw),0,np.sin(yaw)],[0,1,0],[-np.sin(yaw),0,np.cos(yaw)]])
    projected=points @ view.T; low=projected.min(axis=0);high=projected.max(axis=0)
    width,height=600,800
    scale=min((width-100)/max(high[0]-low[0],.01),(height-100)/max(high[1]-low[1],.01))
    center=(low+high)/2
    colorbuffer=np.full((height,width,3),.12);depth=np.full((height,width),-np.inf)
    for positions,colors,uv,texture in draws:
        proj=positions @ view.T
        proj[:,0]=(proj[:,0]-center[0])*scale+width/2
        proj[:,1]=-(proj[:,1]-center[1])*scale+height/2
        for i in range(0,len(proj),3):
            triangle=proj[i:i+3]; tc=colors[i:i+3]
            x0,y0=np.floor(triangle[:,:2].min(axis=0)).astype(int)
            x1,y1=np.ceil(triangle[:,:2].max(axis=0)).astype(int)
            x0=max(x0,0);y0=max(y0,0);x1=min(x1,width-1);y1=min(y1,height-1)
            if x1<x0 or y1<y0: continue
            px,py=np.meshgrid(np.arange(x0,x1+1)+.5,np.arange(y0,y1+1)+.5)
            a,b,c=triangle[:,:2]
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-8: continue
            u=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/denominator
            v=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/denominator
            weights=np.stack((u,v,1-u-v),axis=-1)
            z=weights @ triangle[:,2]
            rgba=weights @ tc
            if texture is not None:
                image,sampling=texture
                texuv=weights @ uv[i:i+3]
                for axis,key in enumerate(['wrapS','wrapT']):
                    mode=sampling.get(key,10497)
                    v=texuv[...,axis]
                    texuv[...,axis]=np.clip(v,0,1) if mode==33071 else 1-np.abs(v%2-1) if mode==33648 else v%1
                texture_pixels=image
                tx=np.minimum((texuv[...,0]*image.shape[1]).astype(int),image.shape[1]-1)
                ty=np.minimum((texuv[...,1]*image.shape[0]).astype(int),image.shape[0]-1)
                rgba*=image[ty,tx]
            localdepth=depth[y0:y1+1,x0:x1+1]
            mask=(weights>=-1e-6).all(axis=-1)&(z>localdepth)&(rgba[...,3]>.2)
            localdepth[mask]=z[mask]
            colorbuffer[y0:y1+1,x0:x1+1][mask]=rgba[...,:3][mask]
    output.parent.mkdir(parents=True,exist_ok=True)
    Image.fromarray((np.clip(colorbuffer,0,1)*255).astype(np.uint8)).save(output)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args()
    files=list(args.folder.rglob('*.glb'))
    for path in files: load(path)
    preview(args.folder/'characters/brian/animations/animation_00.glb',args.folder/'previews/brian.png')
    print(f'Validated {len(files)} GLBs, including geometry, vertex color, texture, quaternion, and animation accessor checks.')

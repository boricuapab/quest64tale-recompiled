"""Extract verified Quest 64 model/animation data to self-contained glTF 2 GLB."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA1 = '91b96e938c6d91699057fad91d726ee5a23ce33a'

class Memory:
    def __init__(self, rom):
        self.rom = rom
        self.maps = []
        self.rdram = bytearray(8 * 1024 * 1024)

    def map(self, cpu, start, end):
        self.maps.insert(0, (cpu, start, end))
        block = self.rom[start:end]
        # Recompiled N64 functions use word-swapped RDRAM.
        padded = block + bytes((-len(block)) % 4)
        swapped = np.frombuffer(padded, dtype=np.uint8).reshape(-1, 4)[:, ::-1].tobytes()
        pos = cpu & 0x7fffff
        self.rdram[pos:pos+len(swapped)] = swapped

    def offset(self, address):
        for cpu, start, end in self.maps:
            if cpu <= address < cpu + end - start:
                return start + address - cpu
        raise ValueError(f'Unmapped pointer {address:08x}')

    def read(self, address, size):
        pos = self.offset(address)
        for cpu, start, end in self.maps:
            if cpu <= address and address + size <= cpu + end - start:
                return self.rom[pos:pos+size]
        raise ValueError(f'Read exceeds bank at {address:08x}')

    def unpack(self, fmt, address):
        return struct.unpack(fmt, self.read(address, struct.calcsize(fmt)))

class GLB:
    def __init__(self, name):
        self.data = bytearray()
        self.doc = dict(asset=dict(version='2.0', generator='Quest64 ROM extractor'),
                        scene=0, scenes=[dict(name=name, nodes=[])], nodes=[], meshes=[],
                        materials=[], textures=[], images=[], samplers=[dict(magFilter=9728, minFilter=9728, wrapS=10497, wrapT=10497)],
                        bufferViews=[], accessors=[], animations=[],
                        extensionsUsed=['KHR_materials_unlit'])
        self.texture_cache = {}

    def blob(self, data, target=None):
        self.data.extend(bytes((-len(self.data)) % 4))
        view = dict(buffer=0, byteOffset=len(self.data), byteLength=len(data))
        if target:
            view['target'] = target
        self.data.extend(data)
        self.doc['bufferViews'].append(view)
        return len(self.doc['bufferViews']) - 1

    def accessor(self, values, kind, component=5126, target=None):
        dtype = {5126:'<f4', 5125:'<u4'}[component]
        values = np.asarray(values, dtype=dtype)
        if not np.isfinite(values).all():
            raise ValueError('Non-finite GLB accessor')
        count = len(values)
        view = self.blob(values.tobytes(), target)
        record = dict(bufferView=view, componentType=component, count=count, type=kind)
        if count:
            record['min'] = np.min(values.reshape(count, -1), axis=0).tolist()
            record['max'] = np.max(values.reshape(count, -1), axis=0).tolist()
        self.doc['accessors'].append(record)
        return len(self.doc['accessors'])-1

    def texture(self, pixels, name, wrap_s=10497, wrap_t=10497):
        key = (pixels.shape, pixels.tobytes(), wrap_s, wrap_t)
        if key in self.texture_cache:
            return self.texture_cache[key]
        stream = io.BytesIO()
        Image.fromarray(pixels).save(stream, format='PNG')
        self.doc['images'].append(dict(name=name, bufferView=self.blob(stream.getvalue()), mimeType='image/png'))
        self.doc['samplers'].append(dict(magFilter=9728,minFilter=9728,wrapS=wrap_s,wrapT=wrap_t))
        self.doc['textures'].append(dict(source=len(self.doc['images'])-1, sampler=len(self.doc['samplers'])-1))
        index = len(self.doc['textures'])-1
        self.texture_cache[key] = index
        return index

    def node(self, **kwargs):
        self.doc['nodes'].append(kwargs)
        return len(self.doc['nodes'])-1

    def save(self, path):
        self.doc['buffers'] = [dict(byteLength=len(self.data))]
        encoded = json.dumps(self.doc, separators=(',', ':')).encode()
        encoded += b' ' * ((-len(encoded)) % 4)
        self.data.extend(bytes((-len(self.data)) % 4))
        length = 12 + 8 + len(encoded) + 8 + len(self.data)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(struct.pack('<4sII', b'glTF', 2, length) + struct.pack('<I4s', len(encoded), b'JSON') + encoded + struct.pack('<I4s', len(self.data), b'BIN\0') + self.data)

def rgba5551(words):
    words = np.asarray(words, dtype=np.uint16)
    rgba = np.empty((*words.shape, 4), dtype=np.uint8)
    for channel, shift in enumerate((11, 6, 1)):
        value = (words >> shift) & 31
        rgba[..., channel] = (value << 3) | (value >> 2)
    rgba[..., 3] = (words & 1) * 255
    return rgba

class DisplayList:
    def __init__(self, memory, glb):
        self.mem, self.glb = memory, glb
        self.vertices = [None] * 64
        self.image = None
        self.tile = dict(fmt=0, siz=2, line=0, cms=0,cmt=0,shifts=0,shiftt=0)
        self.origin = (0,0)
        self.texture_scale=(1.,1.)
        self.palette = None
        self.width, self.height = 1, 1
        self.prim = [1., 1., 1., 1.]
        self.lighting = False
        self.textured = False
        self.groups = {}
        self.command_count = 0

    def decode_texture(self):
        if not self.image:
            raise ValueError('Textured draw has no image pointer')
        address, fmt, siz, source_width = self.image
        fmt, siz = self.tile['fmt'], self.tile['siz']
        w, h = self.width, self.height
        if not 0 < w <= 1024 or not 0 < h <= 1024:
            raise ValueError(f'Invalid texture dimensions {w}x{h}')
        bits = 4 << siz
        # LoadTile source row stride comes from SetTextureImage.
        stride = max((source_width * bits + 7)//8, (w * bits + 7)//8)
        raw = self.mem.read(address, stride*h)
        if siz == 0:
            rows = np.frombuffer(raw, dtype=np.uint8).reshape(h, stride)
            values = np.stack((rows >> 4, rows & 15), axis=-1).reshape(h, -1)[:, :w]
        elif siz == 1:
            values = np.frombuffer(raw, dtype=np.uint8).reshape(h, stride)[:, :w]
        elif siz == 2:
            values = np.frombuffer(raw, dtype='>u2').reshape(h, stride//2)[:, :w]
        elif siz == 3 and fmt == 0:
            return np.frombuffer(raw, dtype=np.uint8).reshape(h, stride//4, 4)[:, :w].copy()
        else:
            raise ValueError(f'Unsupported texture format {fmt}/{siz}')
        if fmt == 0 and siz == 2:
            return rgba5551(values)
        if fmt == 2:
            if self.palette is None or values.max() >= len(self.palette):
                raise ValueError('CI texture has no valid palette')
            return self.palette[values]
        result = np.empty((h, w, 4), dtype=np.uint8)
        if fmt == 3:
            if siz == 0:
                # Promote before multiplying: uint8 arithmetic wrapped bright
                # IA4 texels to near-black (notably Brian's red cape).
                intensity = ((values.astype(np.uint16) >> 1) * 255 // 7).astype(np.uint8)
                alpha = (values & 1) * 255
            elif siz == 1:
                intensity, alpha = (values >> 4) * 17, (values & 15) * 17
            else:
                intensity, alpha = values >> 8, values & 255
        elif fmt == 4:
            intensity = values * 17 if siz == 0 else values
            alpha = intensity
        else:
            raise ValueError(f'Unsupported texture format {fmt}/{siz}')
        result[..., :3] = intensity[..., None]
        result[..., 3] = alpha
        return result

    def triangle(self, indices):
        vertices = [self.vertices[index] for index in indices]
        if any(vertex is None for vertex in vertices):
            raise ValueError(f'Triangle references unloaded vertices {indices}')
        tex = None
        if self.textured:
            def wrap(mode): return 33071 if mode&2 else 33648 if mode&1 else 10497
            tex = self.glb.texture(self.decode_texture(), f'rom_{self.mem.offset(self.image[0]):08x}',wrap(self.tile['cms']),wrap(self.tile['cmt']))
        key = (tex, tuple(self.prim), self.lighting, self.width, self.height,self.tile['shifts'],self.tile['shiftt'],self.origin,self.texture_scale)
        self.groups.setdefault(key, []).extend(vertices)

    def run(self, address, stack=()):
        if address in stack or len(stack) > 64:
            raise ValueError('Recursive/cyclic display list')
        stack = stack + (address,)
        for _ in range(10000):
            w0, w1 = self.mem.unpack('>II', address)
            address += 8
            self.command_count += 1
            opcode = w0 >> 24
            if opcode == 0xB8:
                return
            if opcode == 0x06:
                self.run(w1, stack)
                if (w0 >> 16) & 255:
                    return
            elif opcode == 0x04:
                n, start = (w0 >> 10) & 63, (w0 >> 17) & 127
                if not 0 < n <= 32 or start+n > 64:
                    raise ValueError(f'Unsupported vertex command {w0:08x}')
                for i in range(n):
                    self.vertices[start+i] = self.mem.unpack('>hhhHhhBBBB', w1+i*16)
            elif opcode == 0xBF:
                self.triangle([(w1 >> shift & 255)//2 for shift in (16, 8, 0)])
            elif opcode == 0xB1:
                for word in (w0, w1):
                    self.triangle([(word >> shift & 255)//2 for shift in (16, 8, 0)])
            elif opcode == 0xFD:
                self.image = (w1, (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 4095)+1)
            elif opcode == 0xF5:
                self.tile = dict(fmt=(w0 >> 21)&7, siz=(w0 >> 19)&3, line=(w0 >> 9)&511,cms=(w1>>8)&3,cmt=(w1>>18)&3,shifts=w1&15,shiftt=(w1>>10)&15)
            elif opcode == 0xF2:
                self.origin=(((w0>>12)&4095)/4,(w0&4095)/4)
                self.width = (((w1 >> 12)&4095)-((w0 >> 12)&4095))//4+1
                self.height = ((w1&4095)-(w0&4095))//4+1
            elif opcode == 0xF0:
                count = ((w1 >> 14)&1023)+1
                self.palette = rgba5551(np.frombuffer(self.mem.read(self.image[0], count*2), dtype='>u2'))
            elif opcode == 0xFA:
                self.prim = [(w1 >> shift & 255)/255 for shift in (24,16,8,0)]
            elif opcode == 0xBB:
                self.textured = bool(w0 & 255)
                self.texture_scale=((w1>>16)/65536,(w1&65535)/65536)
            elif opcode == 0xB6 and w1 & 0x20000:
                self.lighting = False
            elif opcode == 0xB7 and w1 & 0x20000:
                self.lighting = True
        raise ValueError('Unterminated display list')

    def mesh(self, name):
        primitives = []
        for (tex, prim, lit, w, h, shifts, shiftt, origin, texture_scale), vertices in self.groups.items():
            positions, uv, colors, normals = [], [], [], []
            for vertex in vertices:
                x,y,z,flag,s,t,r,g,b,a = vertex
                positions.append((x,y,z))
                def shift(value, amount): return value/(2**amount) if amount<=10 else value*(2**(16-amount))
                uv.append(((shift(s/32*texture_scale[0],shifts)-origin[0])/w,(shift(t/32*texture_scale[1],shiftt)-origin[1])/h))
                colors.append((1,1,1,a/255) if lit else (r/255,g/255,b/255,a/255))
                normals.append(tuple((v if v < 128 else v-256)/127 for v in (r,g,b)))
            pbr = dict(baseColorFactor=list(prim), metallicFactor=0, roughnessFactor=1)
            if tex is not None:
                pbr['baseColorTexture'] = dict(index=tex)
            # N64 cutout texels require an alpha test, not sorted transparency.
            image_alpha=False
            if tex is not None:
                source=self.glb.doc['textures'][tex]['source']
                view=self.glb.doc['bufferViews'][self.glb.doc['images'][source]['bufferView']]
                pixels=np.asarray(Image.open(io.BytesIO(self.glb.data[view['byteOffset']:view['byteOffset']+view['byteLength']])))
                image_alpha=bool(np.any(pixels[...,3]<255))
            material = dict(name=f'{name}_material_{len(primitives)}', pbrMetallicRoughness=pbr, doubleSided=True, alphaMode='MASK' if image_alpha else 'OPAQUE')
            if image_alpha:material['alphaCutoff']=0.5
            if not lit:
                material['extensions'] = {'KHR_materials_unlit':{}}
            self.glb.doc['materials'].append(material)
            attrs = dict(POSITION=self.glb.accessor(positions,'VEC3', target=34962),
                         COLOR_0=self.glb.accessor(colors,'VEC4', target=34962))
            if tex is not None:
                attrs['TEXCOORD_0'] = self.glb.accessor(uv,'VEC2',target=34962)
            if lit:
                attrs['NORMAL'] = self.glb.accessor(normals,'VEC3',target=34962)
            primitives.append(dict(attributes=attrs, material=len(self.glb.doc['materials'])-1, mode=4))
        if not primitives:
            return None
        self.glb.doc['meshes'].append(dict(name=name, primitives=primitives))
        return len(self.glb.doc['meshes'])-1

def quaternion(angles):
    # Verified from func_80023C1C: column-vector Ry * Rx * Rz, degrees.
    y,x,z = [math.radians(value)/2 for value in angles]
    def product(a,b):
        ax,ay,az,aw=a; bx,by,bz,bw=b
        return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx,
                aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
    return product(product((0,math.sin(y),0,math.cos(y)),(math.sin(x),0,0,math.cos(x))), (0,0,math.sin(z),math.cos(z)))

def sample_function():
    dll = ctypes.CDLL(str(ROOT/'tools/animation_sampler.dll'))
    func = dll.sample_animation
    func.argtypes = [ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_float)]
    return func

def export_brian(rom, output):
    sampler = sample_function()
    model_start, model_end = 0x86B830, 0x871900
    glb = GLB('Brian')
    mem = Memory(rom)
    mem.map(0x80000400,0x1000,0x73A90)
    mem.map(0x80206000,model_start,model_end)
    # The model owns a 12-byte descriptor per animation. The game loads each
    # animation bank into 0x80200000 through func_80006720.
    descriptors = []
    for animation_id in range(30):
        start,end = struct.unpack_from('>II',rom,0x54B58+animation_id*8)
        if not (0 < start < end <= len(rom)):
            break
        joints, duration, joint_ptr, dl_ptr = mem.unpack('>hhII',0x80206064+animation_id*12)
        if not 0 < joints < 128 or duration <= 0:
            break
        descriptors.append((animation_id,start,end,joints,duration,joint_ptr,dl_ptr))
    aid,start,end,joints,duration,joint_ptr,dl_ptr=descriptors[0]
    mem.map(0x80200000,start,end)
    bone_nodes=[]; pivot_nodes=[]; mesh_count=0
    root=glb.node(name='Brian', children=[], scale=[0.01,0.01,0.01])
    glb.doc['scenes'][0]['nodes']=[root]
    for joint in range(joints):
        ptr=joint_ptr+joint*32
        pivot=mem.unpack('>fff',ptr+8)
        parent, mesh_id=mem.unpack('>bb',ptr+20)
        bone=glb.node(name=f'joint_{joint:02d}',children=[],extras=dict(n64_joint=joint,parent_record=parent))
        pivot_node=glb.node(name=f'pivot_{joint:02d}',translation=[-v for v in pivot],children=[])
        glb.doc['nodes'][bone]['children'].append(pivot_node)
        bone_nodes.append(bone); pivot_nodes.append(pivot_node)
        mesh_ptr=0
        animation_dl_ptr=mem.unpack('>I',ptr+24)[0]
        if animation_dl_ptr:
            mesh_id=mem.unpack('>I',animation_dl_ptr)[0]
        if mesh_id >= 0:
            mesh_ptr=mem.unpack('>I',dl_ptr+mesh_id*4)[0]
        if mesh_ptr:
            parser=DisplayList(mem,glb)
            parser.run(mesh_ptr)
            mesh=parser.mesh(f'Brian_part_{joint:02d}')
            if mesh is not None:
                mesh_node=glb.node(name=f'mesh_{joint:02d}',mesh=mesh,extras=dict(display_list=f'{mesh_ptr:08x}'))
                glb.doc['nodes'][pivot_node]['children'].append(mesh_node)
                mesh_count+=1
    # Children use the parent's pivot-adjusted matrix, exactly as the game's
    # recursive renderer does. Parent 0 means a root; otherwise one-based.
    for joint,bone in enumerate(bone_nodes):
        parent=mem.unpack('>b',joint_ptr+joint*32+20)[0]
        destination=root if parent==0 else pivot_nodes[parent-1]
        glb.doc['nodes'][destination]['children'].append(bone)
    clips=[]
    for aid,start,end,count,duration,ptr,dl_ptr in descriptors:
        if count != joints:
            clips.append(dict(animation_id=aid,status='unsupported',reason='different joint count'))
            continue
        mem.map(0x80200000,start,end)
        memory_buffer=(ctypes.c_ubyte*len(mem.rdram)).from_buffer(mem.rdram)
        animation=dict(name=f'animation_{aid:02d}',channels=[],samplers=[],extras=dict(rom_start=f'{start:08x}',n64_duration=duration,reference_fps=30))
        # Sample every native frame using the original TCB/linear evaluator.
        times=glb.accessor([frame/30 for frame in range(duration+1)],'SCALAR')
        for joint,bone in enumerate(bone_nodes):
            translations=[]; rotations=[]; scales=[]
            joint_address=ptr+joint*32
            track_ptr,track_count,cycle=mem.unpack('>IHH',joint_address)
            mem.read(track_ptr,36 if track_count==1 else track_count*52)
            result=(ctypes.c_float*9)()
            for frame in range(duration+1):
                sampler(memory_buffer,joint_address,frame%cycle if cycle else 0,result)
                values=list(result)
                translations.append(values[:3]); rotations.append(quaternion(values[3:6])); scales.append(values[6:9])
            # Maintain quaternion continuity for glTF interpolation.
            for i in range(1,len(rotations)):
                if sum(a*b for a,b in zip(rotations[i-1],rotations[i])) < 0:
                    rotations[i]=tuple(-v for v in rotations[i])
            if aid==0:
                glb.doc['nodes'][bone].update(translation=translations[0],rotation=list(rotations[0]),scale=scales[0])
            for path,values,kind in [('translation',translations,'VEC3'),('rotation',rotations,'VEC4'),('scale',scales,'VEC3')]:
                animation['samplers'].append(dict(input=times,output=glb.accessor(values,kind),interpolation='LINEAR'))
                animation['channels'].append(dict(sampler=len(animation['samplers'])-1,target=dict(node=bone,path=path)))
        glb.doc['animations'].append(animation)
        clips.append(dict(animation_id=aid,status='exported',frames=duration+1))
    glb.doc['extras']=dict(source_sha1=EXPECTED_SHA1,source_model_rom=f'{model_start:08x}',
                          limitations=['N64 combiner, lighting and fog are approximated by glTF materials.',
                                       'Display-list mesh animation is represented by its initial mesh.'])
    glb.save(output/'characters/brian/brian.glb')
    for descriptor in descriptors:
        aid,start,end,count,duration,ptr,dl_ptr=descriptor
        # Per-clip files retain the exact hierarchy, including animations that
        # reparent the staff or add attachment nodes.
        clip_glb=GLB(f'Brian_animation_{aid:02d}')
        export_clip(mem, clip_glb, sampler, descriptor, f'Brian_animation_{aid:02d}')
        clip_path=output/f'characters/brian/animations/animation_{aid:02d}.glb'
        clip_glb.save(clip_path)
        clips[aid]['per_clip_output']=str(clip_path.relative_to(output)).replace('\\','/')
        clips[aid]['per_clip_status']='exported'
    report=dict(model='Brian',meshes=mesh_count,joints=joints,textures=len(glb.doc['images']),animations=clips,
                output='characters/brian/brian.glb',scope='Brian prototype; remaining banks pending')
    (output/'extraction-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

def export_clip(mem, glb, sampler, descriptor, name):
    aid,start,end,count,duration,ptr,dl_ptr=descriptor
    mem.map(0x80200000,start,end)
    root=glb.node(name=name,children=[],scale=[0.01,0.01,0.01])
    glb.doc['scenes'][0]['nodes']=[root]
    bones=[]; pivots=[]; variants=[]
    for joint in range(count):
        address=ptr+joint*32
        pivot=mem.unpack('>fff',address+8)
        parent,mesh_id=mem.unpack('>bb',address+20)
        seq,seqlen=mem.unpack('>IH',address+24)
        ids=list(mem.unpack('>'+str(seqlen)+'I',seq)) if seq and seqlen else [mesh_id]
        bone=glb.node(name=f'joint_{joint:02d}',children=[])
        pivot_node=glb.node(name=f'pivot_{joint:02d}',children=[],translation=[-v for v in pivot])
        glb.doc['nodes'][bone]['children']=[pivot_node]
        bones.append(bone); pivots.append(pivot_node)
        mesh_nodes={}
        for variant in sorted(set(ids)):
            if variant<0:
                continue
            display_list=mem.unpack('>I',dl_ptr+variant*4)[0]
            if not display_list:
                continue
            parser=DisplayList(mem,glb)
            parser.run(display_list)
            mesh=parser.mesh(f'{name}_part_{joint:02d}_variant_{variant}')
            if mesh is not None:
                node=glb.node(name=f'mesh_{joint:02d}_{variant}',mesh=mesh,scale=[1,1,1] if variant==ids[0] else [0,0,0])
                glb.doc['nodes'][pivot_node]['children'].append(node)
                mesh_nodes[variant]=node
        variants.append((ids,mesh_nodes))
    for joint,bone in enumerate(bones):
        parent=mem.unpack('>b',ptr+joint*32+20)[0]
        if not 0<=parent<=count:
            raise ValueError('Invalid joint parent')
        destination=root if parent==0 else pivots[parent-1]
        glb.doc['nodes'][destination]['children'].append(bone)
    animation=dict(name=f'animation_{aid:02d}',channels=[],samplers=[],extras=dict(rom_start=f'{start:08x}',n64_duration=duration,reference_fps=30))
    times=glb.accessor([frame/30 for frame in range(duration+1)],'SCALAR')
    memory_buffer=(ctypes.c_ubyte*len(mem.rdram)).from_buffer(mem.rdram)
    def channel(node,path,values,kind,interpolation='LINEAR'):
        animation['samplers'].append(dict(input=times,output=glb.accessor(values,kind),interpolation=interpolation))
        animation['channels'].append(dict(sampler=len(animation['samplers'])-1,target=dict(node=node,path=path)))
    for joint,bone in enumerate(bones):
        address=ptr+joint*32
        track,track_count,cycle=mem.unpack('>IHH',address)
        mem.read(track,36 if track_count==1 else track_count*52)
        translations=[];rotations=[];scales=[]
        result=(ctypes.c_float*9)()
        for frame in range(duration+1):
            sampler(memory_buffer,address,frame%cycle if cycle else 0,result)
            values=list(result)
            translations.append(values[:3]);rotations.append(quaternion(values[3:6]));scales.append(values[6:9])
        for i in range(1,len(rotations)):
            if sum(a*b for a,b in zip(rotations[i-1],rotations[i]))<0:
                rotations[i]=tuple(-v for v in rotations[i])
        glb.doc['nodes'][bone].update(translation=translations[0],rotation=list(rotations[0]),scale=scales[0])
        channel(bone,'translation',translations,'VEC3')
        channel(bone,'rotation',rotations,'VEC4')
        channel(bone,'scale',scales,'VEC3')
        ids,mesh_nodes=variants[joint]
        if len(ids)>1:
            for variant,node in mesh_nodes.items():
                values=[[1,1,1] if ids[frame%len(ids)]==variant else [0,0,0] for frame in range(duration+1)]
                channel(node,'scale',values,'VEC3','STEP')
    glb.doc['animations']=[animation]
    glb.doc['extras']=dict(source_sha1=EXPECTED_SHA1,original_interpolation='func_80022B40 and func_80022F60',
                          material_limitations='N64 combiner, lighting and fog use glTF approximations')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--rom',type=Path,default=ROOT/'quest64.us.z64')
    parser.add_argument('--output',type=Path,default=ROOT/'extracted-assets')
    args=parser.parse_args()
    rom=args.rom.read_bytes()
    if hashlib.sha1(rom).hexdigest()!=EXPECTED_SHA1:
        raise SystemExit('ROM does not match the supported original Quest 64 US release')
    export_brian(rom,args.output)
    if args.output.resolve()==(ROOT/'extracted-assets').resolve():
        from combine_brian import main as combine
        combine()

if __name__=='__main__':
    main()

"""Build a native RT64 texture pack from Brian's named ROM textures."""
import ctypes, hashlib, io, json, struct, zipfile
from pathlib import Path
import numpy as np
from PIL import Image
from extract_glb import ROOT,Memory,EXPECTED_SHA1

class TextureScanner:
    def __init__(self,mem,dll):
        self.mem=mem;self.dll=dll;self.ram=(ctypes.c_ubyte*len(mem.rdram)).from_buffer(mem.rdram)
        self.tmem=(ctypes.c_ubyte*4096)();self.tiles=[dict(fmt=0,siz=0,line=0,tmem=0,palette=0,cms=0,cmt=0,masks=0,maskt=0,uls=0,ult=0,lrs=0,lrt=0) for _ in range(8)]
        self.image=None;self.loaded={};self.texture_tile=0;self.enabled=False;self.other=0;self.matches={}
    def draw(self):
        if not self.enabled:return
        tile=self.tiles[self.texture_tile]
        if not tile['line']:return
        dims=[]
        for axis in ('s','t'):
            mask=tile['mask'+axis];clamp=mask==0 or tile['cm'+axis]&2
            size=max((tile['lr'+axis]-tile['ul'+axis]+4)//4,1) if clamp else 65535
            dims.append(min(size,1<<mask if mask else 65535))
        width,height=dims;tlut=(self.other>>14)&3
        assert width<4096 and height<4096,(tile,width,height)
        key=self.dll.tmem_hash(self.tmem,tile['fmt'],tile['siz'],tile['line'],tile['tmem'],width,height,tlut,tile['palette'])
        offset=self.loaded[tile['tmem']]
        info=dict(hash=f'{key:016x}',rom_offset=f'{offset:08x}',width=width,height=height,fmt=tile['fmt'],siz=tile['siz'],line=tile['line'],tlut=tlut)
        previous=self.matches.get(info['hash'])
        if previous:assert previous['rom_offset']==info['rom_offset'] or previous['width']==width
        self.matches[info['hash']]=info
    def run(self,address,stack=()):
        assert address not in stack
        for _ in range(10000):
            a,b=self.mem.unpack('>II',address);address+=8;op=a>>24
            if op==0xb8:return
            if op==0x06:
                self.run(b,stack+(address-8,))
                if (a>>16)&255:return
            elif op==0xfd:self.image=(b,(a>>21)&7,(a>>19)&3,(a&4095)+1)
            elif op==0xf5:
                tile=self.tiles[(b>>24)&7]
                tile.update(fmt=(a>>21)&7,siz=(a>>19)&3,line=(a>>9)&511,tmem=a&511,palette=(b>>20)&15,cmt=(b>>18)&3,maskt=(b>>14)&15,cms=(b>>8)&3,masks=(b>>4)&15)
            elif op in (0xf2,0xf4,0xf3,0xf0):
                tile=self.tiles[(b>>24)&7]
                tile.update(uls=(a>>12)&4095,ult=a&4095,lrs=(b>>12)&4095,lrt=b&4095)
                if op==0xf2:continue
                src,fmt,siz,source_width=self.image
                stride=(source_width<<siz)>>1
                if op==0xf3:
                    start=src+((tile['uls']<<siz)>>1)+stride*tile['ult'];rows=1;words=((tile['lrs']-tile['uls'])>>(4-tile['siz']))+1;mode=1;line=tile['line']*8
                else:
                    start=src+(((tile['uls']>>2)<<siz)>>1)+stride*(tile['ult']>>2)
                    rows=1+(tile['lrt']>>2)-(tile['ult']>>2)
                    words=((tile['lrs']>>2)-(tile['uls']>>2))
                    if op==0xf0:words+=1;mode=2;line=tile['line']*32
                    else:words=(words>>(4-tile['siz']))+1;mode=3 if fmt==0 and tile['siz']==3 else 0;line=tile['line']*8
                assert 0<rows<=1024 and 0<words<=512
                self.dll.tmem_load(self.tmem,self.ram,start&0x7fffff,stride,tile['tmem']*8,line,words,rows,tile['lrt'],mode)
                self.loaded[tile['tmem']]=self.mem.offset(src)
            elif op==0xbb:self.enabled=bool(a&255);self.texture_tile=(a>>8)&7
            elif op==0xba:
                shift=(a>>8)&255;length=a&255;mask=((1<<length)-1)<<shift
                self.other=(self.other&~mask)|(b&mask)
            elif op in (0xbf,0xb1):self.draw()
        raise ValueError('Unterminated display list')

def main():
    rom=(ROOT/'quest64.us.z64').read_bytes();assert hashlib.sha1(rom).hexdigest()==EXPECTED_SHA1
    mem=Memory(rom);mem.map(0x80000400,0x1000,0x73a90);mem.map(0x80206000,0x86b830,0x871900)
    dll=ctypes.CDLL(str(ROOT/'tools/texture_hasher.dll'))
    dll.tmem_load.argtypes=[ctypes.c_void_p,ctypes.c_void_p]+[ctypes.c_uint32]*8
    dll.tmem_hash.argtypes=[ctypes.c_void_p]+[ctypes.c_uint32]*8;dll.tmem_hash.restype=ctypes.c_uint64
    lists=set()
    for aid in range(30):
        start,end=struct.unpack_from('>II',rom,0x54b58+aid*8);mem.map(0x80200000,start,end)
        count,duration,ptr,dls=mem.unpack('>hhII',0x80206064+aid*12)
        for j in range(count):
            mesh=mem.unpack('>b',ptr+j*32+21)[0];seq,length=mem.unpack('>IH',ptr+j*32+24)
            ids=mem.unpack('>'+str(length)+'I',seq) if seq and length else [mesh]
            for i in set(ids):
                if i>=0:
                    dl=mem.unpack('>I',dls+i*4)[0]
                    if dl:lists.add(dl)
    matches={}
    for dl in sorted(lists):
        scanner=TextureScanner(mem,dll);scanner.run(dl);matches.update(scanner.matches)
    source=ROOT/'hires_tex/characters/brian/textures';original=ROOT/'extracted-assets/characters/brian/textures'
    sources={}
    for path in sorted(source.glob('*.png')):sources.setdefault(path.stem.split('rom_')[1],path)
    output=ROOT/'mods/brian-hires';output.mkdir(parents=True,exist_ok=True)
    archive_files={};entries=[];report=[]
    for key,info in sorted(matches.items()):
        path=sources.get(info['rom_offset'])
        if not path:raise ValueError(f'No source image for {info}')
        replacement=Image.open(path).convert('RGBA');base=Image.open(original/path.name).convert('RGBA')
        changed=replacement.size!=base.size or replacement.tobytes()!=base.tobytes()
        # Preserve original cutouts for RGB-only high-resolution input images.
        alpha=np.asarray(base)[...,3]
        if Image.open(path).mode=='RGB' and alpha.min()<255:
            replacement.putalpha(base.getchannel('A').resize(replacement.size,Image.Resampling.NEAREST))
        if changed:
            assert info['width']==base.width and info['height']==base.height,(path.name,info,base.size)
            relative='textures/'+path.name
            stream=io.BytesIO();replacement.save(stream,format='PNG');archive_files[relative]=stream.getvalue()
            entries.append(dict(path=relative,hashes=dict(rt64=key),operation='preload',shift='half'))
        report.append(dict(**info,source=path.name,replacement_size=replacement.size,changed=changed))
    assert entries,'No changed textures found'
    database=dict(configuration=dict(configurationVersion=3,hashVersion=5,autoPath='rt64',defaultOperation='preload',defaultShift='half'),textures=entries)
    manifest=dict(game_id='qs64',id='brian_hires',display_name='Brian High Resolution Textures',description='Brian texture replacements from the supplied hires_tex folder. Original cutout alpha is preserved.',short_description='High-resolution Brian character textures',version='1.0.0',minimum_recomp_version='1.0.2',authors=['Local texture collection'],enabled_by_default=True)
    archive_files['rt64.json']=json.dumps(database,indent=2).encode();archive_files['mod.json']=json.dumps(manifest,indent=2).encode()
    archive=output/'brian-hires.rtz'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as pack:
        for name,data in archive_files.items():pack.writestr(name,data)
    (output/'mapping-report.json').write_text(json.dumps(report,indent=2))
    (output/'rt64.json').write_text(json.dumps(database,indent=2))
    with zipfile.ZipFile(archive) as pack:
        assert pack.testzip() is None
        for entry in entries:Image.open(io.BytesIO(pack.read(entry['path']))).verify()
    print(json.dumps(dict(archive=str(archive),original_hashes=len(matches),replacement_hashes=len(entries),replacement_images=len(archive_files)-2,report=str(output/'mapping-report.json'))))

if __name__=='__main__':main()

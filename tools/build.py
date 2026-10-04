"""Rebuild from a recursive checkout and a privately supplied USA ROM."""
import argparse,hashlib,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--rom',required=True,type=Path);p.add_argument('--jobs',default='8');p.add_argument('--font-source',type=Path);a=p.parse_args()
def run(args):subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
raw=a.rom.read_bytes()
if raw[:4]==bytes.fromhex('37804012'):raw=b''.join(raw[i:i+2][::-1] for i in range(0,len(raw),2))
elif raw[:4]==bytes.fromhex('40123780'):raw=b''.join(raw[i:i+4][::-1] for i in range(0,len(raw),4))
if hashlib.sha1(raw).hexdigest()!='91b96e938c6d91699057fad91d726ee5a23ce33a':raise SystemExit('Unsupported ROM: expected original USA Quest 64')
(ROOT/'quest64.us.z64').write_bytes(raw)
for directory in ['rsp','RecompiledFuncs','RecompiledPatches','mods']:(ROOT/directory).mkdir(exist_ok=True)
run([sys.executable,ROOT/'tools/prepare_ui.py']+(['--font-source',a.font_source] if a.font_source else []))
run([sys.executable,ROOT/'tools/generate_debug_destinations.py'])
exe='.exe' if os.name=='nt' else ''
win=os.name=='nt'; cc='clang-cl' if win else os.environ.get('CC','clang');cxx='clang-cl' if win else os.environ.get('CXX','clang++')
prefix=ROOT.as_posix()
privacy=f'-ffile-prefix-map={prefix}=. -fmacro-prefix-map={prefix}=.'
flags=f'/EHsc -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH /clang:-ffile-prefix-map={prefix}=. /clang:-fmacro-prefix-map={prefix}=.' if win else privacy
cflags=f'/clang:-ffile-prefix-map={prefix}=. /clang:-fmacro-prefix-map={prefix}=.' if win else privacy
common=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DCMAKE_C_COMPILER={cc}',f'-DCMAKE_CXX_COMPILER={cxx}',f'-DCMAKE_CXX_FLAGS={flags}',f'-DCMAKE_C_FLAGS={cflags}']
run(['cmake','-S','lib/N64ModernRuntime/N64Recomp','-B','build/tools',*common])
run(['cmake','--build','build/tools','--target','N64RecompCLI','RSPRecomp','--parallel',a.jobs])
for name in ['N64Recomp','RSPRecomp']:shutil.copy2(ROOT/'build/tools'/(name+exe),ROOT/(name+exe))
run([ROOT/('N64Recomp'+exe),'us.rev0.toml']);run([ROOT/('RSPRecomp'+exe),'aspMain.toml'])
run(['cmake','-S','.','-B','build/game',*common])
run(['cmake','--build','build/game','--target','Quest64Recompiled','--parallel',a.jobs])
print('Built version 1.0.7 in build/game. Generated code and ROM remain private local files.')

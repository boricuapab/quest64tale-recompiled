"""Reuse the pinned RT64 TMEM loader and version-5 hasher verbatim."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
rdp=(root/'lib/rt64/src/hle/rt64_rdp.cpp').read_text()
loader=rdp[rdp.index('    template<bool RGBA32 = false, bool TLUT = false>'):rdp.index('    void RDP::loadTileOperation')]
header='''#include <cstdint>
#include <cassert>
#include <algorithm>
#include <intrin.h>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "../lib/rt64/src/contrib/xxHash/xxhash.h"
constexpr uint32_t RDP_TMEM_BYTES=4096,RDP_TMEM_MASK16=2047,RDP_TMEM_MASK8=4095;
constexpr uint8_t G_IM_SIZ_32b=3,G_IM_SIZ_16b=2,G_IM_SIZ_4b=0,G_IM_FMT_RGBA=0;
namespace RT64 { struct LoadTile {uint16_t line,tmem;uint8_t siz,fmt,palette;}; }
#include "../lib/rt64/src/common/rt64_tmem_hasher.h"
namespace RT64 {
'''
exports='''}
extern "C" __declspec(dllexport) void tmem_load(uint8_t* tmem,const uint8_t* ram,uint32_t source,uint32_t stride,uint32_t dest,uint32_t line,uint32_t words,uint32_t rows,uint32_t dxt,uint32_t mode) {
    if(mode==0) RT64::loadToTMEMCommon<false>(tmem,ram,source,stride,dest,line,words,rows);
    else if(mode==1) RT64::loadToTMEMCommon<false,true>(tmem,ram,source,stride,dest,line,words,rows,dxt);
    else if(mode==2) RT64::loadToTMEMCommon<false,false,true>(tmem,ram,source,stride,dest,line,words,rows);
    else if(mode==3) RT64::loadToTMEMCommon<true>(tmem,ram,source,stride,dest,line,words,rows);
}
extern "C" __declspec(dllexport) uint64_t tmem_hash(const uint8_t* tmem,uint32_t fmt,uint32_t siz,uint32_t line,uint32_t dest,uint32_t width,uint32_t height,uint32_t tlut,uint32_t palette) {
    RT64::LoadTile tile{uint16_t(line),uint16_t(dest),uint8_t(siz),uint8_t(fmt),uint8_t(palette)};
    return RT64::TMEMHasher::hash(tmem,tile,uint16_t(width),uint16_t(height),tlut,5);
}
'''
(root/'tools/texture_hasher.cpp').write_text(header+loader+exports)

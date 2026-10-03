"""Build the original game's animation evaluator as a small extraction DLL."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parts = []
for name in ('func_80022B40', 'func_80022F60'):
    for path in (root / 'RecompiledFuncs').glob('funcs_*.c'):
        source = path.read_text()
        match = re.search(r'RECOMP_FUNC void ' + name + r'\(.*?(?=RECOMP_FUNC void |\Z)', source, re.S)
        if match:
            parts.append(match.group())
            break
    else:
        raise RuntimeError(f'Missing original function {name}')
source = '#include "recomp.h"\n#include <string.h>\n'
source += 'void func_80022F60(uint8_t*, recomp_context*);\n'
source += '\n'.join(parts)
source += '''
__declspec(dllexport) void sample_animation(uint8_t* rdram, uint32_t joint, uint32_t frame, float* output) {
    recomp_context context = {0};
    context.f_odd = &context.f0.u32h;
    context.r4 = (int32_t)joint;
    context.r5 = (int32_t)0x807FE000;
    context.r6 = frame;
    context.r29 = (int32_t)0x807FF000;
    func_80022B40(rdram, &context);
    memcpy(output, rdram + 0x7FE000, 9 * sizeof(float));
}
'''
(root / 'tools' / 'animation_sampler.c').write_text(source)
print('Extracted original animation evaluator and interpolation routine.')

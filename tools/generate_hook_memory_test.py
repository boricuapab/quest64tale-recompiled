"""Run the exact production hooks against an ordinary 8 MB guest allocation."""
from pathlib import Path
import tomllib

root = Path(__file__).resolve().parents[1]
hooks = tomllib.loads((root/'us.rev0.toml').read_text())['patches']['hook']
def hook(address):
    return next(h['text'] for h in hooks if h.get('before_vram') == address)
source = r'''
#include "recomp.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static int action, repeat, door_hit, cancel_count;
static int qs64_battle_pause_input(uint8_t* ram) {return action;}
static void qs64_speed_begin(uint8_t* ram) {}
static int qs64_speed_repeat(uint8_t* ram) {return repeat;}
static void qs64_speed_cancel(uint8_t* ram) {cancel_count++;}
static void func_80019CA4(uint8_t* r,recomp_context* c){}
static void func_80008F6C(uint8_t* r,recomp_context* c){}
static void func_800143C0(uint8_t* r,recomp_context* c){}
static void func_8002684C(uint8_t* r,recomp_context* c){}
static void func_800253F0(uint8_t* r,recomp_context* c){}
static void qs64_battle_exit_finish(uint8_t* r,int action){}
static void func_8001CFE8(uint8_t* ram,recomp_context* ctx) {}
static void func_8000FDE0(uint8_t* ram,recomp_context* ctx) {
    assert(ctx->f12.u32l==0x11223344);
    assert(ctx->f14.u32l==0x22334455);
    assert(ctx->r6==0x33445566);
    ctx->r2=door_hit;
}
static int begin(uint8_t* rdram,recomp_context* ctx) {
BEGIN_HOOK
    return 1;
qs64_pause_render_frame:return 2;
L_80001FF4:return 3;
}
static int next(uint8_t* rdram,recomp_context* ctx) {
NEXT_HOOK
    return 0;
qs64_speed_simulation:return 1;
}
int main(void) {
    uint8_t* rdram=calloc(1,8*1024*1024);
    assert(rdram);recomp_context ctx={0};
    MEM_W(0,S32(0x8007B2F8))=123;
    MEM_W(0,S32(0x80087200))=456;
    assert(begin(rdram,&ctx)==1);
    assert(MEM_W(0,S32(0x8007B2F8))==0);
    assert(MEM_W(0,S32(0x80087200))==0);
    action=1;assert(begin(rdram,&ctx)==2);
    action=3;assert(begin(rdram,&ctx)==3);action=0;
    MEM_H(0x74,S32(0x8007BAB8))=0;
    MEM_W(0x14,S32(0x8007BAB8))=0x11223344;
    MEM_W(0x1C,S32(0x8007BAB8))=0x22334455;
    MEM_W(0x24,S32(0x8007BAB8))=0x33445566;
    repeat=1;assert(next(rdram,&ctx)==1);
    door_hit=1;assert(next(rdram,&ctx)==0&&cancel_count==1);
    MEM_H(0x74,S32(0x8007BAB8))=1;
    assert(next(rdram,&ctx)==1);
    repeat=0;assert(next(rdram,&ctx)==0);
    free(rdram);puts("PASS: production load/update and speed-door hooks stay inside 8 MB guest memory.");
}
'''.replace('BEGIN_HOOK', hook(0x80001B14)).replace('NEXT_HOOK', hook(0x80001BB0))
(root/'tools/hook_memory_test.c').write_text(source)

#pragma once
#include <cstdint>
namespace recomp::mods { class ModContext; class ModHandle; }
namespace quest64 {
void fmv_enable(recomp::mods::ModContext&,const recomp::mods::ModHandle&);
void fmv_disable(const char* id);
void fmv_poll();
bool fmv_active();
bool fmv_blocks_input();
float fmv_screen_fade();
float fmv_game_audio_gain();
int fmv_preview(const char* path,double maximum_seconds=0);
}
extern "C" void qs64_fmv_tick(uint8_t*);
extern "C" void qs64_fmv_reset();


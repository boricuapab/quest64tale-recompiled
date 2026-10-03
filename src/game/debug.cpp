#include <atomic>
#include "zelda_debug.h"
#include "quest64_mods.h"
#include "librecomp/helpers.hpp"
// #include "../patches/input.h"

std::atomic<uint16_t> pending_warp = 0xFFFF;
std::atomic<uint32_t> pending_set_time = 0xFFFF;

void zelda64::do_warp(int area, int scene, int entrance) {
    if (area < 0 || area >= int(zelda64::game_warps.size())) return;
    const auto& scenes = zelda64::game_warps[area].scenes;
    if (scene < 0 || scene >= int(scenes.size())) return;
    quest64::request_warp(area, scenes[scene].index, entrance);
}

extern "C" void recomp_get_pending_warp(uint8_t* rdram, recomp_context* ctx) {
    // Return the current warp value and reset it.
    _return(ctx, pending_warp.exchange(0xFFFF));
}

void zelda64::set_time(uint8_t day, uint8_t hour, uint8_t minute) {
    pending_set_time.store((day << 16) | (uint16_t(hour) << 8) | minute);
}

extern "C" void recomp_get_pending_set_time(uint8_t* rdram, recomp_context* ctx) {
    // Return the current set time value and reset it.
    _return(ctx, pending_set_time.exchange(0xFFFF));
}

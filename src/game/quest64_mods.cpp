#include "quest64_mods.h"
extern "C" void qs64_extra_set(const char*,bool);
extern "C" void qs64_extra_tick(uint8_t*);
extern "C" void qs64_extra_reset();
extern "C" void qs64_extra_finish(uint8_t*);
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#ifndef QUEST64_MOD_TEST
#include "librecomp/mods.hpp"
#include "zelda_config.h"
#endif

namespace {
constexpr uint32_t Player=0x8007BA80,GameMode=0x8007B2E0,GameState=0x8007B2E4;
struct Destination {int map,submap,entrance;float x,z,heading;};
struct BossDestination {const char* name;int map,submap;uint8_t mask;};
#include "quest64_destinations.inc"
std::atomic<bool> max_stats=false,all_spells=false,debug_menu=false;
std::atomic<int> status=0;
std::mutex request_mutex;
struct Warp {Destination target;int boss=-1;};
std::optional<Warp> pending,transition;
int pending_boss=-1;
bool stats_applied=false,elements_applied=false;
std::array<uint16_t,4> old_stats;
std::array<uint8_t,4> old_levels,old_elements;
uint8_t old_spirits=0;
template<class T> T read(uint8_t* ram,uint32_t address) {
    T value;uint32_t offset=address&0x7fffff;
    if constexpr(sizeof(T)==1)offset^=3;
    if constexpr(sizeof(T)==2)offset^=2;
    std::memcpy(&value,ram+offset,sizeof(T));return value;
}
template<class T> void write(uint8_t* ram,uint32_t address,T value) {
    uint32_t offset=address&0x7fffff;
    if constexpr(sizeof(T)==1)offset^=3;
    if constexpr(sizeof(T)==2)offset^=2;
    std::memcpy(ram+offset,&value,sizeof(T));
}
constexpr uint32_t stat_offsets[]={6,10,12,14};
constexpr uint16_t caps[]={500,500,255,255};
void apply_stats(uint8_t* ram) {
    if (read<uint16_t>(ram,Player+6)==0)return;
    const bool stats=max_stats.load();
    if(stats && !stats_applied) {
        for(int i=0;i<4;i++){old_stats[i]=read<uint16_t>(ram,Player+stat_offsets[i]);old_levels[i]=read<uint8_t>(ram,Player+0x30+i);}
        write<uint16_t>(ram,Player+4,500);write<uint16_t>(ram,Player+8,500);
        stats_applied=true;
    }
    if(stats) {
        for(int i=0;i<4;i++){write<uint16_t>(ram,Player+stat_offsets[i],caps[i]);write<uint8_t>(ram,Player+0x30+i,54);}
    } else if(stats_applied) {
        for(int i=0;i<4;i++){write<uint16_t>(ram,Player+stat_offsets[i],old_stats[i]);write<uint8_t>(ram,Player+0x30+i,old_levels[i]);}
        write<uint16_t>(ram,Player+4,std::min(read<uint16_t>(ram,Player+4),old_stats[0]));
        write<uint16_t>(ram,Player+8,std::min(read<uint16_t>(ram,Player+8),old_stats[1]));
        stats_applied=false;
    }
    const bool elements=stats||all_spells.load();
    if(elements && !elements_applied) {
        for(int i=0;i<4;i++)old_elements[i]=read<uint8_t>(ram,Player+0x24+i);
        old_spirits=read<uint8_t>(ram,Player+0x34);elements_applied=true;
    }
    if(elements) {
        for(int i=0;i<4;i++)write<uint8_t>(ram,Player+0x24+i,50);
        write<uint8_t>(ram,Player+0x34,98);
    } else if(elements_applied) {
        for(int i=0;i<4;i++)write<uint8_t>(ram,Player+0x24+i,old_elements[i]);
        write<uint8_t>(ram,Player+0x34,old_spirits);elements_applied=false;
    }
}
const Destination* destination(int map,int submap,int entrance) {
    for(const auto& d:destinations)if(d.map==map&&d.submap==submap&&d.entrance==entrance)return &d;
    return nullptr;
}
#ifndef QUEST64_MOD_TEST
void set_mod(const recomp::mods::ModHandle& mod,bool enabled) {
    qs64_extra_set(mod.manifest.mod_id.c_str(),enabled);
    if(mod.manifest.mod_id=="qs64_max_stats")max_stats=enabled;
    else if(mod.manifest.mod_id=="qs64_all_spells")all_spells=enabled;
    else if(mod.manifest.mod_id=="qs64_debug_menu") {
        debug_menu=enabled;zelda64::set_debug_mode_enabled(enabled);
        if(!enabled){std::lock_guard lock(request_mutex);pending.reset();}
    }
}
#endif
}

void quest64::register_gameplay_mods() {
#ifndef QUEST64_MOD_TEST
    recomp::mods::ModContentType type{
        .content_filename="qs64_gameplay.json",.allow_runtime_toggle=true,
        .on_enabled=[](recomp::mods::ModContext&,const recomp::mods::ModHandle& mod){set_mod(mod,true);},
        .on_disabled=[](recomp::mods::ModContext&,const recomp::mods::ModHandle& mod){set_mod(mod,false);},
        .on_reordered=nullptr
    };
    auto id=recomp::mods::register_mod_content_type(type);
    recomp::mods::register_mod_container_type("qsmod",std::vector{id},true);
#endif
}
bool quest64::request_warp(int map,int submap,int entrance) {
    const auto* d=destination(map,submap,entrance);
    if(!debug_menu||!d){status=4;return false;}
    std::lock_guard lock(request_mutex);pending=Warp{*d,-1};status=1;return true;
}
bool quest64::valid_area(int map,int submap) {return destination(map,submap,0)!=nullptr;}
int quest64::nearest_entrance(int map,int submap,float x,float z) {
    int entrance=-1;float best=1.0e30f;
    for(const auto& d:destinations)if(d.map==map&&d.submap==submap){
        const float dx=d.x-x,dz=d.z-z,dist=dx*dx+dz*dz;
        if(dist<best){best=dist;entrance=d.entrance;}
    }
    return entrance;
}
bool quest64::request_boss(int boss) {
    if(boss<0||boss>=8||!debug_menu){status=4;return false;}
    const auto* d=destination(bosses[boss].map,bosses[boss].submap,0);
    if(!d){status=4;return false;}
    std::lock_guard lock(request_mutex);pending=Warp{*d,boss};status=1;return true;
}
std::vector<std::string> quest64::boss_names() {
    std::vector<std::string> names;for(const auto& b:bosses)names.emplace_back(b.name);return names;
}
std::string quest64::debug_status() {
    switch(status.load()) {
    case 1:return "Travel queued. Close settings to continue.";
    case 2:return "Destination loaded.";
    case 3:return "Finish the current battle, then try again.";
    case 4:return "Enable the Debug Menu mod and choose a valid destination.";
    default:return "Load a game, then choose a destination or boss.";
    }
}
extern "C" void qs64_mod_reset(uint8_t*) {
    qs64_extra_reset();
    stats_applied=false;elements_applied=false;pending_boss=-1;transition.reset();
    std::lock_guard lock(request_mutex);pending.reset();status=0;
}
extern "C" void qs64_mod_tick(uint8_t* ram) {
    if(read<uint16_t>(ram,GameMode)!=1)return;
    qs64_extra_tick(ram);
    if(read<uint16_t>(ram,GameMode)!=1)return;
    apply_stats(ram);
    std::lock_guard lock(request_mutex);
    if(pending) {
        if(read<uint16_t>(ram,0x8008C592)&1){status=3;pending.reset();return;}
        transition=pending;pending.reset();
        // Finish the native gameplay loop before changing any map pointers.
        write<uint16_t>(ram,GameMode,0);
    }
}
extern "C" void qs64_mod_finish_warp(uint8_t* ram) {
    qs64_extra_finish(ram);
    if(!transition)return;
    const auto warp=*transition;transition.reset();const auto& d=warp.target;
    write<uint32_t>(ram,0x80084EEC,d.map);write<uint32_t>(ram,0x80084EF0,d.submap);
    write<int32_t>(ram,0x80084EE4,-1);write<int32_t>(ram,0x80084EE8,-1);
    write<int32_t>(ram,0x80084EF8,-1);write<int32_t>(ram,0x80084F04,-1);
    write<int32_t>(ram,0x80085370,d.entrance);
    write<float>(ram,0x8007BA40,d.x);write<float>(ram,0x8007BA44,d.z);write<float>(ram,0x8007BA48,d.heading);
    write<uint16_t>(ram,Player+0x3E,0);
    write<uint16_t>(ram,0x8007B2E8,0);
    write<uint32_t>(ram,GameState,0x140);
    write<uint16_t>(ram,0x8008C592,read<uint16_t>(ram,0x8008C592)&0x8000);
    if(warp.boss>=0) {
        write<uint8_t>(ram,0x8007D19C,read<uint8_t>(ram,0x8007D19C)&~bosses[warp.boss].mask);
        pending_boss=warp.boss;
    }
    status=2;
}
extern "C" void qs64_mod_player_tick(uint8_t* ram) {
    if(pending_boss<0 || read<uint16_t>(ram,GameMode)!=1)return;
    if(read<uint32_t>(ram,0x8007D1A0)!=uint32_t(pending_boss+1))return;
    // Place Brian next to the loaded boss; the original proximity routine
    // starts its dialogue, arena, combat, rewards and story progression.
    for(int i=0;i<3;i++)write<float>(ram,0x8007BACC+i*4,read<float>(ram,0x8007D1CC+i*4)+(i==0 ? 0.01f:0.0f));
    pending_boss=-1;
}
#ifdef QUEST64_MOD_TEST
extern "C" __declspec(dllexport) void qs64_test_enable(int stats,int spells,int debug){max_stats=stats!=0;all_spells=spells!=0;debug_menu=debug!=0;}
extern "C" __declspec(dllexport) bool qs64_test_warp(int map,int submap,int entrance){return quest64::request_warp(map,submap,entrance);}
extern "C" __declspec(dllexport) bool qs64_test_boss(int boss){return quest64::request_boss(boss);}
#endif

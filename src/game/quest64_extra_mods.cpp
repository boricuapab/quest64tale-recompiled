#include "quest64_mods.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>
#ifndef QUEST64_MOD_TEST
#include "librecomp/mods.hpp"
#else
#define EXTRA_EXPORT __declspec(dllexport)
#endif
#ifndef EXTRA_EXPORT
#define EXTRA_EXPORT
#endif

namespace {
constexpr uint32_t Player=0x8007BA80, Position=0x8007BACC;
std::atomic<bool> autosave_on=false,encounters_on=false,bars_on=false,restore_requested=false;
std::atomic<int> save_status=0;
bool zone_pending=false,session_started=false;int stable_ticks=0;
#ifdef QUEST64_MOD_TEST
std::filesystem::path test_folder="tools/autosave-test";
std::atomic<int> test_rate=4;
#endif
std::mutex checkpoint_mutex;
template<class T> T rd(uint8_t* ram,uint32_t a) {
    T value;uint32_t o=a&0x7fffff;
    if constexpr(sizeof(T)==1)o^=3;
    if constexpr(sizeof(T)==2)o^=2;
    std::memcpy(&value,ram+o,sizeof(T));return value;
}
template<class T> void wr(uint8_t* ram,uint32_t a,T v) {
    uint32_t o=a&0x7fffff;
    if constexpr(sizeof(T)==1)o^=3;
    if constexpr(sizeof(T)==2)o^=2;
    std::memcpy(ram+o,&v,sizeof(T));
}
// Exactly the pointer-free persistent ranges used by native save/load.
constexpr std::array<std::pair<uint32_t,uint32_t>,9> ranges={{{Player,0x38},{0x800869D8,32},{0x80086AE8,16},
    {0x8008CF78,151},{0x8007D19C,1},{0x800859E0,4},{0x80084EF8,24},{0x8007BA60,6},{0x8005FA00,1}}};
constexpr size_t payload_size=292; // ranges above plus native music/state byte
struct Checkpoint {
    bool legacy=false;
    uint64_t sequence=0;
    uint32_t map=0,submap=0;
    std::array<float,4> location{}; // world XYZ and heading
    std::array<uint8_t,payload_size> payload{};
};
std::optional<Checkpoint> restoring;
std::filesystem::path folder() {
#ifdef QUEST64_MOD_TEST
    return test_folder;
#else
    return recomp::mods::get_mods_directory().parent_path()/"autosaves";
#endif
}
uint32_t hash(const std::vector<uint8_t>& bytes) {
    uint32_t result=2166136261u;for(auto b:bytes){result^=b;result*=16777619u;}return result;
}
void append(std::vector<uint8_t>& v,uint64_t x,int count){for(int i=0;i<count;i++)v.push_back(uint8_t(x>>(8*i)));}
std::vector<uint8_t> encode(const Checkpoint& c) {
    std::vector<uint8_t> v={'Q','6','4','A','U','T','O',2};
    append(v,c.sequence,8);append(v,c.map,4);append(v,c.submap,4);
    for(float f:c.location){uint32_t u;std::memcpy(&u,&f,4);append(v,u,4);}
    v.insert(v.end(),c.payload.begin(),c.payload.end());append(v,hash(v),4);return v;
}
std::optional<Checkpoint> decode(const std::vector<uint8_t>& bytes) {
    if(bytes.size()!=44+payload_size)return {};
    const uint8_t header[]={'Q','6','4','A','U','T','O',1};
    if(std::memcmp(bytes.data(),header,7)||(bytes[7]!=1&&bytes[7]!=2))return {};
    auto get=[&](int p,int n){uint64_t r=0;for(int i=0;i<n;i++)r|=uint64_t(bytes[p+i])<<(8*i);return r;};
    std::vector<uint8_t> body(bytes.begin(),bytes.end()-4);
    if(hash(body)!=get(int(bytes.size()-4),4))return {};
    Checkpoint c;c.legacy=bytes[7]==1;c.sequence=get(8,8);c.map=uint32_t(get(16,4));c.submap=uint32_t(get(20,4));
    if(!quest64::valid_area(int(c.map),int(c.submap)))return {};
    for(int i=0;i<4;i++){uint32_t u=uint32_t(get(24+i*4,4));std::memcpy(&c.location[i],&u,4);
        if(!std::isfinite(c.location[i])||std::abs(c.location[i])>1000000)return {};}
    std::copy_n(bytes.begin()+40,payload_size,c.payload.begin());
    // Canonical big-endian stats; reject dead/invalid checkpoint data.
    auto hp=[&](int p){return (unsigned(c.payload[p])<<8)|c.payload[p+1];};
    if(hp(4)==0||hp(6)==0||hp(4)>hp(6))return {};
    return c;
}
std::optional<Checkpoint> latest() {
    std::optional<Checkpoint> best;
    for(int slot=0;slot<2;slot++) {
        std::ifstream in(folder()/("checkpoint-"+std::to_string(slot)+".q64auto"),std::ios::binary);
        if(!in)continue;
        std::vector<uint8_t> bytes(44+payload_size);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!in||in.peek()!=EOF)continue;
        auto candidate=decode(bytes);
        if(candidate&&(!best||candidate->sequence>best->sequence))best=candidate;
    }
    return best;
}
void capture(uint8_t* ram) {
    Checkpoint c;c.map=rd<uint32_t>(ram,0x80084EE4);c.submap=rd<uint32_t>(ram,0x80084EE8);
    for(int i=0;i<3;i++)c.location[i]=rd<float>(ram,Position+4*i);
    c.location[3]=rd<float>(ram,Position+16);
    size_t p=0;for(auto [a,n]:ranges)for(uint32_t i=0;i<n;i++)c.payload[p++]=rd<uint8_t>(ram,a+i);
    c.payload[p++]=rd<uint8_t>(ram,0x8005F010);
    if(p!=payload_size){save_status=4;return;}
    std::lock_guard lock(checkpoint_mutex);
    try {
        auto previous=latest();c.sequence=previous?previous->sequence+1:1;
        auto bytes=encode(c);if(!decode(bytes)){save_status=4;return;}
        std::filesystem::create_directories(folder());
        const auto target=folder()/("checkpoint-"+std::to_string(c.sequence%2)+".q64auto");
        // Write the alternate generation, keeping the other valid checkpoint
        // available if an interrupted write leaves this generation incomplete.
        std::ofstream out(target,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.flush();
        save_status=out?1:4;
    } catch(...){save_status=4;}
}
void apply_checkpoint(uint8_t* ram,const Checkpoint& c) {
    size_t p=0;for(auto [a,n]:ranges)for(uint32_t i=0;i<n;i++)wr<uint8_t>(ram,a+i,c.payload[p++]);
    wr<uint8_t>(ram,0x8005F010,c.payload[p]);
}
int rate_index() {
#ifdef QUEST64_MOD_TEST
    return test_rate;
#else
    auto value=recomp::mods::get_mod_config_value("qs64_encounter_rate","rate");
    if(auto index=std::get_if<uint32_t>(&value))return int(*index);
    return 4;
#endif
}
struct Bar {int x,y;uint16_t hp,max_hp;};
thread_local std::vector<Bar> bars;
}
namespace quest64 {
bool autosave_enabled(){return autosave_on;}
bool request_autosave_restore(){if(!autosave_on){save_status=5;return false;}restore_requested=true;save_status=2;return true;}
std::string autosave_status(){switch(save_status.load()){
    case 1:return "Area checkpoint saved.";
    case 2:return "Restore queued. Load a game and close settings.";
    case 3:return "Autosave restored.";
    case 4:return "Could not save or read a valid checkpoint.";
    case 5:return "Enable Auto Save first.";
    case 6:return "Finish the battle before restoring.";
    default:return "Auto Save keeps two separate area checkpoints.";
}}
}
extern "C" EXTRA_EXPORT void qs64_extra_set(const char* id,bool enabled) {
    if(!std::strcmp(id,"qs64_autosave")){autosave_on=enabled;if(!enabled)restore_requested=false;}
    else if(!std::strcmp(id,"qs64_encounter_rate"))encounters_on=enabled;
    else if(!std::strcmp(id,"qs64_enemy_health"))bars_on=enabled;
}
extern "C" EXTRA_EXPORT float qs64_encounter_step(float movement) {
    if(!encounters_on)return movement;
    constexpr float rates[]={0.0f,0.10f,0.25f,0.50f,1.0f};
    return movement*rates[std::clamp(rate_index(),0,4)];
}
extern "C" EXTRA_EXPORT void qs64_zone_begin(uint8_t* ram) {
    if(session_started && (rd<uint32_t>(ram,0x8007B2E4)&0x40)){zone_pending=true;stable_ticks=0;}
}
extern "C" void qs64_extra_reset(){zone_pending=false;session_started=false;stable_ticks=0;restoring.reset();bars.clear();}
extern "C" EXTRA_EXPORT void qs64_extra_tick(uint8_t* ram) {
    if(rd<uint16_t>(ram,0x8007B2E0)!=1)return;
    session_started=true;
    if(restore_requested.exchange(false)) {
        if(!autosave_on)return;
        if(rd<uint16_t>(ram,0x8008C592)&1){save_status=6;return;}
        std::lock_guard lock(checkpoint_mutex);
        restoring=latest();if(!restoring){save_status=4;return;}
        wr<uint16_t>(ram,0x8007B2E0,0);zone_pending=false;return;
    }
    if(!zone_pending)return;
    if(!autosave_on){zone_pending=false;return;}
    if(rd<uint16_t>(ram,Player+4)==0||rd<uint16_t>(ram,Player+6)==0)return;
    if((rd<uint32_t>(ram,0x8007B2E4)&0x4083)||(rd<uint16_t>(ram,0x8008C592)&1)||
       (rd<uint16_t>(ram,0x8007BB2C)&1)){stable_ticks=0;return;}
    // Wait half a native second after scripted door movement releases control.
    if(++stable_ticks>=15){capture(ram);zone_pending=false;}
}
extern "C" EXTRA_EXPORT void qs64_extra_finish(uint8_t* ram) {
    if(!restoring)return;
    const auto c=*restoring;
    apply_checkpoint(ram,c);
    wr<uint32_t>(ram,0x80084EEC,c.map);wr<uint32_t>(ram,0x80084EF0,c.submap);
    wr<int32_t>(ram,0x80084EE4,-1);wr<int32_t>(ram,0x80084EE8,-1);
    wr<int32_t>(ram,0x80084EF8,-1);wr<int32_t>(ram,0x80084F04,-1);
    const int entrance=c.legacy?quest64::nearest_entrance(int(c.map),int(c.submap),c.location[0],c.location[2]):-1;
    wr<int32_t>(ram,0x80085370,entrance);
    wr<float>(ram,0x8007BA40,c.location[0]);wr<float>(ram,0x8007BA44,c.location[2]);wr<float>(ram,0x8007BA48,c.location[3]);
    wr<uint16_t>(ram,Player+0x3E,0);wr<uint16_t>(ram,0x8007B2E8,0);
    wr<uint16_t>(ram,0x8008C592,rd<uint16_t>(ram,0x8008C592)&0x8000);
    wr<uint32_t>(ram,0x8007B2E4,0x140);save_status=3;
}
extern "C" EXTRA_EXPORT void qs64_restore_spawn(uint8_t* ram) {
    if(!restoring)return;
    const auto c=*restoring;restoring.reset();
    // Legacy checkpoints may lie inside a door animation. Let the native
    // entrance initializer place them and finish its walk-in instead.
    if(c.legacy)return;
    wr<float>(ram,0x8007BA40,c.location[0]);wr<float>(ram,0x8007BA44,c.location[2]);wr<float>(ram,0x8007BA48,c.location[3]);
    wr<uint32_t>(ram,0x8007BA4C,0); // ordinary spawn, no door/stair animation
    wr<uint32_t>(ram,0x8007BA50,0); // discard the previous map's door index
}
extern "C" EXTRA_EXPORT void qs64_health_begin(){bars.clear();}
extern "C" EXTRA_EXPORT void qs64_health_position(uint8_t* ram,uint32_t actor,int x,int y) {
    if(!bars_on || !(rd<uint16_t>(ram,0x8008C592)&1))return;
    const uint32_t base=0x8007C998;
    const uint32_t count=rd<uint32_t>(ram,0x8007C990);
    if(count>6 || actor<base+0x24 || (actor-base-0x24)%0x128)return;
    const uint32_t index=(actor-base-0x24)/0x128;
    if(index>=count)return;
    const uint32_t enemy=actor-0x24;
    auto hp=rd<uint16_t>(ram,enemy+0xA),maximum=rd<uint16_t>(ram,enemy+0xC);
    if(maximum==0||hp>maximum)return;
    bars.push_back({std::clamp(x-20,8,272),std::clamp(y-12,8,225),hp,maximum});
}
extern "C" EXTRA_EXPORT void qs64_health_collect(uint8_t* ram) {
    bars.clear();
    if(!bars_on||!(rd<uint16_t>(ram,0x8008C592)&1))return;
    const auto count=rd<uint32_t>(ram,0x8007C990);
    if(count>6)return;
    const float scale=rd<float>(ram,0x80086ED4);
    if(!std::isfinite(scale)||scale<=0.00001f)return;
    // Same camera matrix and screen conversion as native func_8002413C,
    // sampled directly for every living enemy rather than through transient
    // damage/status rendering. Keep near-camera anchors instead of clipping
    // them at the native damage-number near plane.
    auto camera=[&](float x,float y,float z){
        std::array<float,3> out;
        for(int i=0;i<3;i++)out[i]=rd<float>(ram,0x80086E88+4*i)*x+
            rd<float>(ram,0x80086E98+4*i)*y+rd<float>(ram,0x80086EA8+4*i)*z+
            rd<float>(ram,0x80086EB8+4*i);
        return out;
    };
    for(uint32_t i=0;i<count;i++) {
        const uint32_t actor=0x8007C9BC+i*0x128;
        if(rd<int16_t>(ram,actor+0x50)<0||rd<uint16_t>(ram,actor-0x1A)==0)continue;
        const uint32_t model=rd<uint32_t>(ram,actor+0x64);
        if(model<0x80000000||model>0x807FFF00)continue;
        const float x=rd<float>(ram,actor),y=rd<float>(ram,actor+4),z=rd<float>(ram,actor+8);
        const float height=rd<float>(ram,model+0x1C)*rd<float>(ram,actor+0x24);
        if(!std::isfinite(height)||height<0||height>100000)continue;
        auto anchor=camera(x,y+height,z);
        if(anchor[2]>=-0.01f)anchor=camera(x,y+height*0.5f,z);
        if(anchor[2]>=-0.01f)anchor=camera(x,y,z);
        if(!std::isfinite(anchor[0])||!std::isfinite(anchor[1])||!std::isfinite(anchor[2])||anchor[2]>=-0.01f)continue;
        const float denominator=anchor[2]*scale;
        const float sx=160.0f-150.0f*anchor[0]/denominator;
        const float sy=120.0f+120.0f*anchor[1]/denominator;
        if(!std::isfinite(sx)||!std::isfinite(sy))continue;
        qs64_health_position(ram,actor,int(std::clamp(sx,28.0f,292.0f)),int(std::clamp(sy,20.0f,237.0f)));
    }
}
extern "C" EXTRA_EXPORT void qs64_health_draw(uint8_t* ram) {
    if(!bars_on||bars.empty())return;
    uint32_t p=rd<uint32_t>(ram,0x8007B2FC);
    // Reject malformed pointers rather than writing outside guest memory.
    if(p<0x80000000||p>0x807FF000)return;
    auto emit=[&](uint32_t a,uint32_t b){wr<uint32_t>(ram,p,a);wr<uint32_t>(ram,p+4,b);p+=8;};
    auto rect=[&](int x,int y,int w,int h,uint16_t color){if(w<=0)return;
        emit(0xE7000000,0);emit(0xF7000000,uint32_t(color)*0x10001);
        emit(0xF6000000|uint32_t((x+w-1)*4)<<12|uint32_t((y+h-1)*4),uint32_t(x*4)<<12|uint32_t(y*4));};
    emit(0xE7000000,0);emit(0xBA001402,0x00300000); // G_CYC_FILL
    for(const auto& b:bars) {
        rect(b.x,b.y,40,6,0xFFFF);rect(b.x+1,b.y+1,38,4,0x1085);
        const int width=int((uint32_t(b.hp)*38+b.max_hp-1)/b.max_hp);
        const uint16_t color=uint32_t(b.hp)*4<=b.max_hp?0xF801:(uint32_t(b.hp)*2<=b.max_hp?0xFFC1:0x07C1);
        rect(b.x+1,b.y+1,width,4,color);
    }
    emit(0xE7000000,0);emit(0xBA001402,0x00100000); // restore native HUD two-cycle mode
    wr<uint32_t>(ram,0x8007B2FC,p);bars.clear();
}
#ifdef QUEST64_MOD_TEST
extern "C" EXTRA_EXPORT void qs64_test_rate(int index){test_rate=index;}
extern "C" EXTRA_EXPORT bool qs64_test_restore(){return quest64::request_autosave_restore();}
extern "C" EXTRA_EXPORT int qs64_test_save_status(){return save_status;}
#endif

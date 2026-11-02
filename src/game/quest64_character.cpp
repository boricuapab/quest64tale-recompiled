// User-supplied animated geometry only. Brian's game logic and shadow stay native.
#include "quest64_character.h"
#include "librecomp/addresses.hpp"
#include "zelda_render.h"
#include "recomp.h"
#include "json/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

extern "C" void func_80023360(uint8_t*,recomp_context*);
extern "C" void func_8001DB38(uint8_t*,recomp_context*);
namespace {
using Json=nlohmann::json;
template<class T>T rd(uint8_t* r,uint32_t a){T v;uint32_t o=a&0x1fffffff;if constexpr(sizeof(T)==2)o^=2;if constexpr(sizeof(T)==1)o^=3;std::memcpy(&v,r+o,sizeof(v));return v;}
template<class T>void wr(uint8_t* r,uint32_t a,T v){uint32_t o=a&0x1fffffff;if constexpr(sizeof(T)==2)o^=2;if constexpr(sizeof(T)==1)o^=3;std::memcpy(r+o,&v,sizeof(v));}
struct Corner{uint32_t vertex;float u,v;uint32_t color=0xffffffff;};
struct Triangle{uint32_t material;Corner c[3];};
struct Clip{uint32_t start,count;};
struct TextureCycle{uint32_t material;double fps;std::vector<uint32_t> frames;};
struct Model{uint32_t vertices,frames,materials;float height;bool repeat_textures=false;std::vector<float> poses;std::vector<uint16_t> tiles;std::vector<Triangle> triangles;std::vector<Clip> clips;std::vector<TextureCycle> cycles;std::vector<std::array<float,3>> native_root_motion,native_root_anchors;std::array<bool,30> native_anchor_clips{};std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();};
std::atomic<std::shared_ptr<const Model>> current;
// The native draw owns root-motion suppression and scripted door placement.
// Preserve its evaluated root matrix before replacing the model display list.
thread_local uint32_t native_brian_start=0,native_brian_actor=0,native_brian_matrix=0;
thread_local bool native_root_valid=false;
thread_local std::array<float,16> native_root_matrix{};
std::mutex characters_mutex;
std::vector<std::pair<std::string,std::shared_ptr<const Model>>> enabled_characters;
uint32_t texture_material(const Model& m,uint32_t material,double seconds){
    for(const auto& cycle:m.cycles)if(cycle.material==material)return cycle.frames[size_t(seconds*cycle.fps)%cycle.frames.size()];
    return material;
}
std::shared_ptr<Model> load_model(recomp::mods::ZipModFileHandle& archive){
        auto read=[&](const char* name){bool exists=false;auto b=archive.read_file(name,exists);if(!exists)throw std::runtime_error(name);return b;};
        const auto config=Json::parse(read("quest64_character.json"));
        if(config.at("version")!=1)throw std::runtime_error("Unsupported character format");
        // Validate the editable GLB before enabling its corresponding native pose cache.
        auto glb=read(config.at("model").get<std::string>().c_str());
        if(glb.size()<20||std::memcmp(glb.data(),"glTF",4)!=0)throw std::runtime_error("Invalid character GLB");
        auto json_bytes=read(config.at("runtime").get<std::string>().c_str());const auto j=Json::parse(json_bytes);
        auto m=std::make_shared<Model>();m->vertices=j.at("vertices");m->frames=j.at("frames");m->materials=j.at("materials");m->height=j.at("height");
        if(j.at("version")!=1||m->vertices==0||m->vertices>16384||m->frames==0||m->frames>20000||m->materials==0||m->materials>32||!std::isfinite(m->height)||m->height<=0)throw std::runtime_error("Invalid character bounds");
        auto poses=read(config.at("poses").get<std::string>().c_str());auto tiles=read(config.at("tiles").get<std::string>().c_str());
        if(poses.size()>256*1024*1024||poses.size()!=size_t(m->vertices)*m->frames*3*sizeof(float)||tiles.size()!=size_t(m->materials)*2048)throw std::runtime_error("Invalid character buffers");
        m->poses.resize(poses.size()/4);std::memcpy(m->poses.data(),poses.data(),poses.size());
        for(float f:m->poses)if(!std::isfinite(f)||std::abs(f)>30000)throw std::runtime_error("Invalid vertex position");
        for(size_t i=0;i<tiles.size();i+=2)m->tiles.push_back(uint16_t((uint8_t(tiles[i])<<8)|uint8_t(tiles[i+1])));
        for(const auto& c:j.at("clips")){Clip clip{c.at("start"),c.at("count")};if(!clip.count||uint64_t(clip.start)+clip.count>m->frames)throw std::runtime_error("Invalid animation range");m->clips.push_back(clip);}
        if(m->clips.size()!=30)throw std::runtime_error("Character needs all 30 Brian animations");
        for(const auto& t:j.at("triangles")){
            Triangle tri{};tri.material=t.at(0);if(tri.material>=m->materials||m->triangles.size()>=8192)throw std::runtime_error("Invalid character triangle");
            for(int i=0;i<3;i++){const auto& c=t.at(i+1);tri.c[i]={c.at(0),c.at(1),c.at(2),c.size()>3?c.at(3).get<uint32_t>():0xffffffff};if(tri.c[i].vertex>=m->vertices||!std::isfinite(tri.c[i].u)||!std::isfinite(tri.c[i].v)||std::abs(tri.c[i].u)>16||std::abs(tri.c[i].v)>16)throw std::runtime_error("Invalid character UV");}
            m->triangles.push_back(tri);
        }
        if(j.contains("texture_cycles"))for(const auto& c:j.at("texture_cycles")){
            TextureCycle cycle{c.at("material"),c.at("fps"),c.at("frames").get<std::vector<uint32_t>>()};
            if(cycle.material>=m->materials||!std::isfinite(cycle.fps)||cycle.fps<=0||cycle.fps>120||cycle.frames.empty()||cycle.frames.size()>120)throw std::runtime_error("Invalid texture animation");
            for(auto frame:cycle.frames)if(frame>=m->materials)throw std::runtime_error("Invalid texture animation frame");m->cycles.push_back(cycle);
        }
        m->repeat_textures=j.value("repeat_textures",false);
        if(j.contains("native_root_motion")){
            for(const auto& p:j.at("native_root_motion")){
                if(!p.is_array()||(p.size()!=2&&p.size()!=3))throw std::runtime_error("Invalid root-motion vector");
                m->native_root_motion.push_back(p.size()==3?std::array<float,3>{p[0],p[1],p[2]}:std::array<float,3>{p[0],0,p[1]});
            }
            if(m->native_root_motion.size()!=m->frames)throw std::runtime_error("Invalid root-motion frame count");
            for(const auto& p:m->native_root_motion)for(float f:p)if(!std::isfinite(f)||std::abs(f)>10000)throw std::runtime_error("Invalid native root motion");
        }
        if(j.contains("native_root_anchors")){
            m->native_root_anchors=j.at("native_root_anchors").get<std::vector<std::array<float,3>>>();
            if(m->native_root_anchors.size()!=m->frames)throw std::runtime_error("Invalid native anchor frame count");
            for(const auto& p:m->native_root_anchors)for(float f:p)if(!std::isfinite(f)||std::abs(f)>10000)throw std::runtime_error("Invalid native anchor");
            for(int clip:j.at("native_root_anchor_clips").get<std::vector<int>>()){
                if(clip<0||clip>=30)throw std::runtime_error("Invalid native anchor clip");m->native_anchor_clips[clip]=true;
            }
        }
        return m;
}
void enable(recomp::mods::ModContext& context,const recomp::mods::ModHandle& mod){
    try {
        recomp::mods::ModOpenError error;recomp::mods::ZipModFileHandle archive(context.get_mod_filename(mod.manifest.mod_id),error);
        auto m=load_model(archive);
        zelda64::renderer::enable_texture_pack(context,mod);
        {std::lock_guard lock(characters_mutex);std::erase_if(enabled_characters,[&](const auto& item){return item.first==mod.manifest.mod_id;});enabled_characters.emplace_back(mod.manifest.mod_id,m);current.store(m);}
        std::fprintf(stdout,"Custom character enabled: %s (%zu triangles, 30 clips)\n",mod.manifest.mod_id.c_str(),m->triangles.size());
    }catch(const std::exception& e){std::fprintf(stderr,"Custom character failed: %s\n",e.what());}
}
struct Buffer{uint32_t address=0;};
Buffer buffers[3];unsigned next_buffer=0;
constexpr uint32_t BufferSize=1024*1024;
}
recomp::mods::ModContentTypeId quest64::register_character_content(){
    recomp::mods::ModContentType type{
        .content_filename="quest64_character.json",.allow_runtime_toggle=true,
        .on_enabled=enable,
        .on_disabled=[](recomp::mods::ModContext&,const recomp::mods::ModHandle& mod){
            {std::lock_guard lock(characters_mutex);std::erase_if(enabled_characters,[&](const auto& item){return item.first==mod.manifest.mod_id;});current.store(enabled_characters.empty()?nullptr:enabled_characters.back().second);}
            zelda64::renderer::disable_texture_pack(mod);
        },
        .on_reordered=nullptr
    };
    return recomp::mods::register_mod_content_type(type);
}

extern "C" int qs64_custom_character_draw(uint8_t* ram,recomp_context* ctx){
    // D1CC is the render copy of Brian; BACC is his simulation actor.
    const uint32_t actor=uint32_t(ctx->r5);
    if(actor!=0x8007D1CC&&actor!=0x8007BACC)return 0;
    auto m=current.load();if(!m)return 0;
    const int animation=rd<int16_t>(ram,actor+0x52);
    if(animation<0||animation>=30)return 0;
    const auto clip=m->clips[animation];const uint32_t frame=std::min<uint32_t>(rd<uint16_t>(ram,actor+0x54),clip.count-1);
    const float* pose=m->poses.data()+size_t(clip.start+frame)*m->vertices*3;
    const uint32_t original=rd<uint32_t>(ram,0x8007B2FC);
    if(original<0x80000000||original>0x807ffff0)return 0;
    auto& buffer=buffers[next_buffer++%3];
    if(!buffer.address){auto* p=static_cast<uint8_t*>(recomp::alloc(ram,BufferSize));if(!p)return 0;buffer.address=uint32_t(p-ram)+0x80000000u;}
    const uint32_t base=buffer.address;uint32_t matrix=base+64,vertices=base+256,texture=vertices+uint32_t(m->triangles.size())*48,commands=texture+m->materials*2048;
    uint32_t material_changes=0,previous=~0u;
    for(const auto& tri:m->triangles)if(tri.material!=previous){++material_changes;previous=tri.material;}
    if(commands+uint32_t(m->triangles.size())*16+material_changes*64+256>base+BufferSize)return 0;
    // Use the game's exact actor transform (including attack/fall rotations).
    // The native projection already contains the camera; do not multiply it twice.
    recomp_context scratch=*ctx;scratch.r29=S32(uint32_t(ctx->r29)-0x180);scratch.r4=S32(matrix);
    scratch.f_odd=scratch.mips3_float_mode?&scratch.f1.u32l:&scratch.f0.u32h;
    scratch.r5=rd<uint32_t>(ram,actor+12);scratch.r6=rd<uint32_t>(ram,actor+16);scratch.r7=rd<uint32_t>(ram,actor+20);
    const float native_height=rd<float>(ram,0x8007BA9C);
    const float fit=std::isfinite(native_height)&&native_height>0?native_height/m->height:1.0f;
    wr<float>(ram,uint32_t(scratch.r29)+0x10,rd<float>(ram,actor+0x24)*fit);
    for(int i=0;i<3;i++)wr<float>(ram,uint32_t(scratch.r29)+0x14+4*i,rd<float>(ram,actor+4*i));
    func_80023360(ram,&scratch);
    if(native_root_valid&&native_brian_actor==actor&&m->native_anchor_clips[animation]){
        const auto& anchor=m->native_root_anchors[clip.start+frame];
        for(int i=0;i<3;i++){
            const float target=native_root_matrix[12+i]+native_root_matrix[i]*anchor[0]+native_root_matrix[4+i]*anchor[1]+native_root_matrix[8+i]*anchor[2];
            wr<float>(ram,matrix+48+4*i,target);
        }
    }else if(!m->native_root_motion.empty()){
        const auto& motion=m->native_root_motion[clip.start+frame];
        for(int i=0;i<3;i++){
            const float delta=(rd<float>(ram,matrix+4*i)*motion[0]+rd<float>(ram,matrix+16+4*i)*motion[1]+rd<float>(ram,matrix+32+4*i)*motion[2])/fit;
            wr<float>(ram,matrix+48+4*i,rd<float>(ram,matrix+48+4*i)+delta);
        }
    }
    for(int i=0;i<16;i++){
        const auto fixed=int32_t(std::clamp(double(rd<float>(ram,matrix+4*i))*65536.0,-2147483648.0,2147483647.0));
        wr<uint16_t>(ram,base+2*i,uint16_t(uint32_t(fixed)>>16));wr<uint16_t>(ram,base+32+2*i,uint16_t(fixed));
    }
    uint32_t vp=vertices;
    for(const auto& tri:m->triangles)for(const auto& c:tri.c){
        for(int i=0;i<3;i++)wr<int16_t>(ram,vp+2*i,int16_t(std::lround(pose[3*c.vertex+i])));
        wr<uint16_t>(ram,vp+6,0);wr<int16_t>(ram,vp+8,int16_t(std::lround(c.u*1024)));wr<int16_t>(ram,vp+10,int16_t(std::lround(c.v*1024)));wr<uint32_t>(ram,vp+12,c.color);vp+=16;
    }
    for(size_t i=0;i<m->tiles.size();i++)wr<uint16_t>(ram,texture+uint32_t(i)*2,m->tiles[i]);
    uint32_t p=commands;auto emit=[&](uint32_t a,uint32_t b){wr<uint32_t>(ram,p,a);wr<uint32_t>(ram,p+4,b);p+=8;};
    emit(m->vertices&0xffffu,0x4B4D4247); // Stable model identity for vertex interpolation.
    emit(0xE7000000,0);emit(0x01060040,base);
    emit(0xB6000000,0xffffffff);emit(0xB7000000,0x205); // Unlit, opaque, depth-tested, double-sided.
    emit(0xBA001402,0);emit(0xBA000E02,0);emit(0xBA000C02,0x2000); // One cycle, RGBA, bilinear.
    emit(0xBA001301,0x00080000); // Explicit perspective-correct texture coordinates.
    emit(0xB900031D,0x00552078);emit(0xBB000001,0xffffffff);
    // (TEXEL0 - 0) * SHADE + 0; alpha is opaque TEXEL0.
    emit(0xFC000000|(1u<<20)|(4u<<15)|(7u<<12)|(7u<<9)|(1u<<5)|4u,
         (15u<<28)|(15u<<24)|(7u<<21)|(7u<<18)|(7u<<15)|(7u<<12)|(1u<<9)|(7u<<6)|(7u<<3)|1u);
    int material=-1;
    const double texture_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-m->started).count();
    for(size_t t=0;t<m->triangles.size();t++){
        const int selected=int(texture_material(*m,m->triangles[t].material,texture_seconds));
        if(material!=selected){
            material=selected;
            emit(0xE7000000,0);emit(0xFD10001F,texture+uint32_t(material)*2048);
            emit(0xF5100000,0x07000000);emit(0xE6000000,0);emit(0xF3000000,0x073FF100);emit(0xE7000000,0);
            emit(0xF5101000,m->repeat_textures?0x00014050:0x00094250);emit(0xF2000000,0x0007c07c);
        }
        emit(0x04000000|(3u<<10),vertices+uint32_t(t)*48);emit(0xBF000000,(1u<<9)|(2u<<1));
    }
    emit(0xE7000000,0);emit(0xBD000000,0);emit(0,0x4B4D454E);emit(0xB8000000,0);
    wr<uint32_t>(ram,original,0);wr<uint32_t>(ram,original+4,0x51455854);
    wr<uint32_t>(ram,original+8,0x06000000);wr<uint32_t>(ram,original+12,commands);
    wr<uint32_t>(ram,0x8007B2FC,original+16);
    return 1;
}

extern "C" void qs64_custom_character_begin(uint8_t* ram,recomp_context* ctx){
    const uint32_t actor=uint32_t(ctx->r5);native_brian_start=0;native_root_valid=false;
    if(current.load()&&(actor==0x8007D1CC||actor==0x8007BACC)){
        native_brian_start=rd<uint32_t>(ram,0x8007B2FC);native_brian_actor=actor;
        native_brian_matrix=uint32_t(ctx->r7)+0xc0+rd<uint32_t>(ram,0x8007B2F8)*64;
    }
}
extern "C" void qs64_custom_character_end(uint8_t* ram,recomp_context* ctx){
    if(!native_brian_start)return;
    const uint32_t fallback=rd<uint32_t>(ram,0x8007B2FC);
    // Read the matrix actually submitted for Brian's single root. This also
    // works when the native bone renderer is replaced by an interpolation
    // patch, so an original-ROM function hook cannot see its float matrix.
    if(fallback>native_brian_start&&(rd<uint32_t>(ram,native_brian_start)>>24)==1&&native_brian_matrix>=0x80000000&&native_brian_matrix<=0x807fffc0){
        for(int i=0;i<16;i++)native_root_matrix[i]=float(int32_t((uint32_t(rd<uint16_t>(ram,native_brian_matrix+2*i))<<16)|rd<uint16_t>(ram,native_brian_matrix+32+2*i)))/65536.f;
        native_root_valid=true;
    }
    wr<uint32_t>(ram,0x8007B2FC,native_brian_start);native_brian_start=0;
    // Keep all native bone/attachment/shadow calculations, replacing only the
    // emitted model display list. The original function has restored its registers.
    auto render=*ctx;render.r5=S32(native_brian_actor);
    if(!qs64_custom_character_draw(ram,&render))wr<uint32_t>(ram,0x8007B2FC,fallback);
    native_root_valid=false;
}

// Headless integration check: real archive loading and native actor-matrix code,
// without starting a game, touching saves, or needing a ROM or graphics device.
int quest64::validate_character_file(const char* path){
    try {
        recomp::mods::ModOpenError error;recomp::mods::ZipModFileHandle archive(path,error);auto m=load_model(archive);
        std::unique_ptr<uint8_t,decltype(&std::free)> memory(static_cast<uint8_t*>(std::calloc(1,32*1024*1024)),&std::free);
        if(!memory)throw std::runtime_error("Test allocation failed");auto* ram=memory.get();
        for(int i=0;i<3;i++)buffers[i].address=0x81000000+i*BufferSize;
        current.store(m);unsigned tested=0;
        for(const auto& cycle:m->cycles)for(size_t i=0;i<cycle.frames.size()*3;i++)if(texture_material(*m,cycle.material,(i+.25)/cycle.fps)!=cycle.frames[i%cycle.frames.size()])throw std::runtime_error("Texture cycle does not loop continuously");
        for(int clip=0;clip<30;clip++)for(uint32_t f:{0u,m->clips[clip].count/2,m->clips[clip].count-1}){
            recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;ctx.r29=S32(0x807ff000);ctx.r5=S32(0x8007D1CC);
            wr<float>(ram,0x8007BA9C,50);wr<float>(ram,0x8007D1F0,1);
            wr<float>(ram,0x8007D1CC,123);wr<float>(ram,0x8007D1D0,30);wr<float>(ram,0x8007D1D4,-456);
            wr<float>(ram,0x8007D1D8,.2f);wr<float>(ram,0x8007D1DC,1.1f);wr<float>(ram,0x8007D1E0,-.1f);
            wr<int16_t>(ram,0x8007D21E,int16_t(clip));wr<uint16_t>(ram,0x8007D220,uint16_t(f));
            wr<uint32_t>(ram,0x8007B2FC,0x80010000);auto before=ctx;
            if(!qs64_custom_character_draw(ram,&ctx)||std::memcmp(&ctx,&before,sizeof(ctx))||rd<uint32_t>(ram,0x8007B2FC)!=0x80010010)throw std::runtime_error("Character hook/context check failed");
            uint32_t p=rd<uint32_t>(ram,0x8001000C),triangles=0;
            const uint32_t model_matrix=rd<uint32_t>(ram,p+20);
            auto fixed=[&](unsigned i){return float(int32_t((uint32_t(rd<uint16_t>(ram,model_matrix+2*i))<<16)|rd<uint16_t>(ram,model_matrix+32+2*i)))/65536.0f;};
            std::array<float,3> motion{};if(!m->native_root_motion.empty())motion=m->native_root_motion[m->clips[clip].start+f];
            const float origin[3]={123,30,-456};
            for(int i=0;i<3;i++){
                const float delta=(rd<float>(ram,model_matrix+64+4*i)*motion[0]+rd<float>(ram,model_matrix+80+4*i)*motion[1]+rd<float>(ram,model_matrix+96+4*i)*motion[2])/(50/m->height);
                if(std::abs(fixed(12+i)-origin[i]-delta)>.001)throw std::runtime_error("Native root motion was scaled or rotated incorrectly");
            }
            if(std::abs(fixed(15)-1)>.001)throw std::runtime_error("Native model matrix address is invalid");
            for(unsigned i=0;i<20000;i++,p+=8){
                uint32_t a=rd<uint32_t>(ram,p),b=rd<uint32_t>(ram,p+4);
                if((a>>24)==0x04){
                    if(((a>>10)&63)!=3)throw std::runtime_error("Invalid generated vertex command");
                    const auto& tri=m->triangles.at(triangles);
                    for(int k=0;k<3;k++)for(int axis=0;axis<3;axis++){
                        float v=m->poses[(size_t(m->clips[clip].start+f)*m->vertices+tri.c[k].vertex)*3+axis];
                        if(rd<int16_t>(ram,b+16*k+axis*2)!=int16_t(std::lround(v)))throw std::runtime_error("Mixed animation frames in generated geometry");
                    }
                }
                if((a>>24)==0xBF)++triangles;
                if((a>>24)==0xB8)break;
            }
            if(triangles!=m->triangles.size())throw std::runtime_error("Incomplete character display list");++tested;
        }
        unsigned door_tested=0;
        if(!m->native_root_anchors.empty()){
            // Exercise the actual native animation renderer and both hooks.
            // One constant-pose root is enough to test the door motion masks;
            // no ROM or extracted game geometry is needed for this fixture.
            constexpr uint32_t descriptor=0x80250000,record=descriptor+0x40,values=descriptor+0x80,arena=0x80280000;
            wr<int16_t>(ram,descriptor,1);wr<uint32_t>(ram,descriptor+4,record);
            wr<uint32_t>(ram,record,values);wr<uint16_t>(ram,record+4,1);wr<uint16_t>(ram,record+6,1);
            wr<float>(ram,record+8,0);wr<float>(ram,record+12,-50);wr<float>(ram,record+16,-10);
            wr<int8_t>(ram,record+20,0);wr<int8_t>(ram,record+21,-1);
            const float sample[9]={130,17,-95,.1f,.3f,-.2f,1,1,1};
            for(int i=0;i<9;i++)wr<float>(ram,values+4*i,sample[i]);
            auto fixed_at=[&](uint32_t p,unsigned i){return float(int32_t((uint32_t(rd<uint16_t>(ram,p+2*i))<<16)|rd<uint16_t>(ram,p+32+2*i)))/65536.0f;};
            for(int clip=0;clip<30;clip++)if(m->native_anchor_clips[clip])for(uint32_t f:{0u,m->clips[clip].count/2,m->clips[clip].count-1})for(int flags=0;flags<4;flags++)for(float scale:{.75f,1.f,1.5f}){
                recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;ctx.r29=S32(0x807ff000);ctx.r4=flags;ctx.r5=S32(0x8007D1CC);ctx.r6=S32(descriptor);ctx.r7=S32(arena);
                wr<float>(ram,0x8007D1F0,scale);wr<int16_t>(ram,0x8007D21E,int16_t(clip));wr<uint16_t>(ram,0x8007D220,uint16_t(f));wr<uint16_t>(ram,0x8007D222,uint16_t(flags));
                wr<uint32_t>(ram,0x8007B2F8,0);wr<uint32_t>(ram,0x8007B2FC,0x80010000);
                func_8001DB38(ram,&ctx);
                if(rd<uint32_t>(ram,0x8007B2FC)!=0x80010010)throw std::runtime_error("Native character wrapper failed");
                const uint32_t commands=rd<uint32_t>(ram,0x8001000C),custom=rd<uint32_t>(ram,commands+20);
                const auto& anchor=m->native_root_anchors[m->clips[clip].start+f];
                for(int axis=0;axis<3;axis++){
                    float expected=fixed_at(arena+0xc0,12+axis);
                    for(int k=0;k<3;k++)expected+=fixed_at(arena+0xc0,k*4+axis)*anchor[k];
                    if(std::abs(fixed_at(custom,12+axis)-expected)>.02f){
                        std::fprintf(stderr,"Door fixture clip=%d frame=%u flags=%d scale=%g axis=%d custom=%g native=%g capturedOrigin=%g nativeOrigin=%g\n",clip,f,flags,scale,axis,fixed_at(custom,12+axis),expected,native_root_matrix[12+axis],fixed_at(arena+0xc0,12+axis));
                        throw std::runtime_error("Door anchor differs from the native rendered root with movement masking");
                    }
                }
                ++door_tested;
            }
        }
        recomp_context npc{};npc.r5=0x8007C998;if(qs64_custom_character_draw(ram,&npc))throw std::runtime_error("NPC was replaced");
        if(door_tested)std::fprintf(stdout,"PASS: %u actual native door renders, all four movement masks, three actor scales, native-root anchor agreement.\n",door_tested);
        current.store({});std::fprintf(stdout,"PASS: 30 clips, %u native render cases, %zu textured triangles, unchanged caller context, NPC isolation.\n",tested,m->triangles.size());return 0;
    }catch(const std::exception& e){current.store({});std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

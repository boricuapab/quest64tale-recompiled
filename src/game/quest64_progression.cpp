#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <vector>
#include <unordered_map>
#ifndef QUEST64_MOD_TEST
#include "recomp_input.h"
#include "recomp_ui.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"
#include "librecomp/addresses.hpp"
#else
#define PROGRESS_EXPORT __declspec(dllexport)
#endif
#ifndef PROGRESS_EXPORT
#define PROGRESS_EXPORT
#endif

namespace {
constexpr uint32_t Player=0x8007BA80, Actor=0x8007BACC;
std::atomic<bool> experience_on=false,arrow_on=false,spirits_on=false,spell_preview_on=false;
template<class T> T rd(uint8_t* ram,uint32_t a) {
    T v;uint32_t o=a&0x1fffffff;
    if constexpr(sizeof(T)==1)o^=3;
    if constexpr(sizeof(T)==2)o^=2;
    std::memcpy(&v,ram+o,sizeof(T));return v;
}
template<class T> void wr(uint8_t* ram,uint32_t a,T v) {
    uint32_t o=a&0x1fffffff;
    if constexpr(sizeof(T)==1)o^=3;
    if constexpr(sizeof(T)==2)o^=2;
    std::memcpy(ram+o,&v,sizeof(T));
}
bool playing(uint8_t* ram){return rd<uint16_t>(ram,0x8007B2E0)==1&&rd<uint16_t>(ram,Player+4)>0;}
void add_xp(uint8_t* ram,int index,unsigned amount) {
    constexpr unsigned stats[]={6,10,12,14},caps[]={500,500,255,255};
    if(rd<uint16_t>(ram,Player+stats[index])>=caps[index])return;
    const uint32_t a=Player+0x28+index*2;
    wr<uint16_t>(ram,a,uint16_t(std::min(65535u,unsigned(rd<uint16_t>(ram,a))+amount)));
}
int percent(uint8_t* ram,int index) {
    if(index==4){
        const auto level=rd<uint8_t>(ram,Player+0x34);
        if(level>=98)return 100;
        const auto needed=rd<uint32_t>(ram,0x80053D3C+std::min(int(level),97)*4);
        return needed?int(std::min(uint64_t(100),uint64_t(rd<uint32_t>(ram,Player+0x10))*100/needed)):0;
    }
    constexpr unsigned stats[]={6,10,12,14},caps[]={500,500,255,255},multipliers[]={1,4,1,2};
    if(rd<uint16_t>(ram,Player+stats[index])>=caps[index])return 100;
    const auto level=std::min(int(rd<uint8_t>(ram,Player+0x30+index)),54);
    const unsigned needed=rd<uint16_t>(ram,0x80053ECC+level*2)*multipliers[index];
    return needed?int(std::min(100u,unsigned(rd<uint16_t>(ram,Player+0x28+index*2))*100/needed)):0;
}
struct RouteEdge {int map,scene,to_map,to_scene;float x,z;uint16_t flags,item;};
struct StoryBoss {const char* name;int map,scene;float x,y,z;uint8_t mask;};
struct SpiritSpot {int map,scene;float x,z;uint8_t flag;};
#include "quest64_progression_data.inc"
bool available(uint8_t* ram,const RouteEdge& edge) {
    if(!(edge.flags&0x30))return true;
    bool found=false;
    for(unsigned i=0;i<150;i++)if(rd<uint8_t>(ram,0x8008CF78+i)==edge.item){found=true;break;}
    return (edge.flags&0x10)?found:!found;
}
struct Target {float x,z;const char* name;bool locked=false;};
struct SpiritGuide {int remaining=0,total=0;std::optional<std::array<float,2>> direction;};
SpiritGuide spirit_guide(uint8_t* ram){
    SpiritGuide result;
    int map=rd<int32_t>(ram,0x80084EE4),scene=rd<int32_t>(ram,0x80084EE8);
    if(map<0||map>=36||scene<0||scene>=256)return result;
    float px=rd<float>(ram,0x8007BACC),pz=rd<float>(ram,0x8007BAD4);
    float nearest=std::numeric_limits<float>::max();
    std::array<int,256> distance;distance.fill(-1);distance[scene]=0;
    std::array<const RouteEdge*,256> first{};std::queue<int> queue;queue.push(scene);
    while(!queue.empty()){
        int from=queue.front();queue.pop();
        for(const auto& edge:route_edges)if(edge.map==map&&edge.scene==from&&edge.to_map==map&&
            edge.to_scene>=0&&edge.to_scene<256&&distance[edge.to_scene]<0&&available(ram,edge)){
            distance[edge.to_scene]=distance[from]+1;
            first[edge.to_scene]=from==scene?&edge:first[from];queue.push(edge.to_scene);
        }
    }
    for(const auto& spirit:spirit_spots){
        if(spirit.map!=map)continue;
        result.total++;
        auto mask=rd<uint8_t>(ram,0x8004D740+(spirit.flag&7));
        if(rd<uint8_t>(ram,0x80086AE8+(spirit.flag>>3))&mask)continue;
        result.remaining++;
        if(distance[spirit.scene]<0)continue;
        float x=spirit.x,z=spirit.z;
        if(auto door=first[spirit.scene]){x=door->x;z=door->z;}
        float score=distance[spirit.scene]*100000000.f+(x-px)*(x-px)+(z-pz)*(z-pz);
        if(score<nearest){nearest=score;result.direction=std::array<float,2>{x-px,z-pz};}
    }
    return result;
}
std::optional<Target> target(uint8_t* ram) {
    const int map=rd<int32_t>(ram,0x80084EE4),scene=rd<int32_t>(ram,0x80084EE8);
    if(map<0||map>=36||scene<0||scene>=256)return {};
    const auto defeated=rd<uint8_t>(ram,0x8007D19C);
    const StoryBoss* boss=nullptr;
    for(const auto& b:story_bosses)if(!(defeated&b.mask)){boss=&b;break;}
    if(!boss)return {};
    if(boss->map==map&&boss->scene==scene)return Target{boss->x,boss->z,boss->name};
    // Reverse breadth-first search over native door/zone connections. Respect
    // native inventory gates; if blocked, point at the gate on the story route.
    constexpr int N=36*256;
    std::vector<int> distance(N,-1);
    auto search=[&](bool gates){
        std::fill(distance.begin(),distance.end(),-1);
        std::queue<int> q;int end=boss->map*256+boss->scene;distance[end]=0;q.push(end);
        while(!q.empty()){
            int node=q.front();q.pop();
            for(const auto& e:route_edges)if(e.to_map*256+e.to_scene==node&&(!gates||available(ram,e))){
                int from=e.map*256+e.scene;
                if(distance[from]<0){distance[from]=distance[node]+1;q.push(from);}
            }
        }
    };
    search(true);bool blocked=distance[map*256+scene]<0;
    if(blocked)search(false);
    const int here=distance[map*256+scene];if(here<=0)return {};
    const RouteEdge* best=nullptr;float nearest=std::numeric_limits<float>::max();
    const float x=rd<float>(ram,Actor),z=rd<float>(ram,Actor+8);
    for(const auto& e:route_edges)if(e.map==map&&e.scene==scene&&distance[e.to_map*256+e.to_scene]==here-1){
        if(!blocked&&!available(ram,e))continue;
        float d=(e.x-x)*(e.x-x)+(e.z-z)*(e.z-z);
        if(d<nearest){nearest=d;best=&e;}
    }
    if(!best)return {};
    return Target{best->x,best->z,boss->name,!available(ram,*best)};
}

// Original 3x5 bitmap alphabet for a compact HUD. Render runs rather than
// individual pixels to keep the guest display list bounded.
constexpr const char* alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ%:- /";
constexpr uint16_t glyphs[]={
    0x7B6F,0x2492,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF,
    0x2BED,0x6BAE,0x7927,0x6B6E,0x79E7,0x79E4,0x796F,0x5BED,0x7497,0x124F,
    0x5BAD,0x4927,0x5FED,0x5F6D,0x7B6F,0x7BE4,0x7B7B,0x7BAD,0x79CF,0x7492,
    0x5B6F,0x5B6A,0x5BFD,0x5AAD,0x5A92,0x73A7,0x52A5,0x0410,0x01C0,0,0x12A4};
struct Hud {
    uint8_t* ram;uint32_t p;int rectangles=0;
    float scale=1.0f;
    void emit(uint32_t a,uint32_t b){wr<uint32_t>(ram,p,a);wr<uint32_t>(ram,p+4,b);p+=8;}
    void rect(int x,int y,int w,int h,uint16_t color){
        if(scale!=1.0f){int right=int(std::lround(8+(x+w-8)*scale)),bottom=int(std::lround(76+(y+h-76)*scale));
            x=int(std::lround(8+(x-8)*scale));y=int(std::lround(76+(y-76)*scale));w=right-x;h=bottom-y;}
        if(rectangles>=2000||w<=0||h<=0)return;
        // Quest's world viewport excludes overscan rows. Those rows are not
        // cleared every frame, so writing there leaves persistent arrow trails.
        int x2=std::min(312,x+w),y2=std::min(228,y+h);x=std::max(8,x);y=std::max(12,y);
        if(x>=x2||y>=y2)return;++rectangles;
        emit(0xE7000000,0);emit(0xF7000000,uint32_t(color)*0x10001);
        emit(0xF6000000|uint32_t((x2-1)*4)<<12|uint32_t((y2-1)*4),uint32_t(x*4)<<12|uint32_t(y*4));
    }
    void text(int x,int y,const std::string& value,uint16_t color=0xFFFF){
        for(char c:value){const char* g=std::strchr(alphabet,c);uint16_t bits=g?glyphs[g-alphabet]:0;
            for(int row=0;row<5;row++)for(int col=0;col<3;){
                if(!(bits&(1u<<(14-row*3-col)))){col++;continue;}
                int start=col;while(col<3&&(bits&(1u<<(14-row*3-col))))col++;
                rect(x+start,y+row,col-start,1,color);
            }x+=4;
        }
    }
    void polygon(const std::array<std::array<float,2>,7>& points,uint16_t color){
        for(int y=0;y<240;y++){
            float left=1000,right=-1000;
            for(int i=0;i<7;i++){
                auto a=points[i],b=points[(i+1)%7];
                if((a[1]<=y+0.5f&&b[1]>y+0.5f)||(b[1]<=y+0.5f&&a[1]>y+0.5f)){
                    float x=a[0]+(y+0.5f-a[1])*(b[0]-a[0])/(b[1]-a[1]);
                    left=std::min(left,x);right=std::max(right,x);
                }
            }
            if(left<=right)rect(int(std::ceil(left)),y,int(std::floor(right)-std::ceil(left)+1),1,color);
        }
    }
};
std::array<float,3> camera(uint8_t* ram,float x,float y,float z){
    std::array<float,3> out;
    for(int i=0;i<3;i++)out[i]=rd<float>(ram,0x80086E88+4*i)*x+
        rd<float>(ram,0x80086E98+4*i)*y+rd<float>(ram,0x80086EA8+4*i)*z+rd<float>(ram,0x80086EB8+4*i);
    return out;
}
bool orbit=false,restore_camera=false;
float yaw=0,pitch=0,radius=100,actual_radius=100;
float original_yaw=0;
std::array<float,6> original_camera{};
int camera_map=-1,camera_scene=-1;
int locked_enemy=-1,zoom_step=1;
bool zoom_requested=false,lock_reframe=false,movement_restore=false;
float movement_angle=0,view_heading=0,zoom_radius=0;
bool battle_paused=false;
int pause_selection=0,pause_stick=0;
using Vec=std::array<float,3>;
struct Triangle {Vec a,b,c;};
std::vector<Triangle> collision;
uint32_t collision_scene=0;
Vec sub(Vec a,Vec b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vec cross(Vec a,Vec b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float dot(Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool pointer(uint32_t a,size_t size){return a>=0x80000000&&uint64_t(a)+size<=0x80800000;}
std::vector<Triangle> visual_triangles(uint8_t* ram,uint32_t address){
    std::vector<Triangle> result;
    std::array<Vec,64> vertices{};std::array<bool,64> valid{};
    unsigned commands=0;
    auto triangle=[&](uint32_t word){
        unsigned a=((word>>16)&255)/2,b=((word>>8)&255)/2,c=(word&255)/2;
        if(a<64&&b<64&&c<64&&valid[a]&&valid[b]&&valid[c]&&result.size()<20000)
            result.push_back({vertices[a],vertices[b],vertices[c]});
    };
    std::function<void(uint32_t,int)> walk=[&](uint32_t p,int depth){
        if(depth>16)return;
        while(pointer(p,8)&&commands++<50000){
            auto a=rd<uint32_t>(ram,p),b=rd<uint32_t>(ram,p+4);p+=8;
            const auto op=a>>24;if(op==0xB8)return;
            if(op==0x06){walk(b,depth+1);if((a>>16)&255)return;}
            else if(op==0x04){
                unsigned n=(a>>10)&63,start=(a>>17)&127;
                if(n==0||n>32||start+n>64||!pointer(b,n*16))return;
                for(unsigned i=0;i<n;i++){
                    vertices[start+i]={float(rd<int16_t>(ram,b+i*16)),float(rd<int16_t>(ram,b+i*16+2)),float(rd<int16_t>(ram,b+i*16+4))};
                    valid[start+i]=true;
                }
            }else if(op==0xBF)triangle(b);
            else if(op==0xB1){triangle(a);triangle(b);}
            // Static map lists do not require an additional model matrix.
            else if(op==0x01)return;
        }
    };
    if(pointer(address,8))walk(address,0);
    return result;
}
void load_collision(uint8_t* ram){
    collision.clear();collision_scene=rd<uint32_t>(ram,0x80084F20);
    const auto instances=rd<uint32_t>(ram,0x80084F24),meshes=rd<uint32_t>(ram,0x80084F28);
    if(!pointer(collision_scene,44)||!pointer(instances,24)||!pointer(meshes,32))return;
    const auto count=rd<uint16_t>(ram,collision_scene);
    if(count>4096||!pointer(instances,count*24))return;
    std::unordered_map<int,std::vector<Triangle>> visual_cache;
    for(unsigned i=0;i<count;i++){
        uint32_t instance=instances+i*24;
        // Match the native instance selector; these are the loaded collidable
        // surfaces, not an independent copy of the game's meshes.
        int model=rd<int16_t>(ram,instance+20);if(model<0||model>4096)continue;
        uint32_t mesh=meshes+model*32;if(!pointer(mesh,32))continue;
        uint32_t polygons=rd<uint32_t>(ram,mesh+20),vertices=rd<uint32_t>(ram,mesh+28);
        unsigned n=rd<uint16_t>(ram,mesh+24);
        float scale=rd<float>(ram,instance+16),angle=rd<float>(ram,instance+12)*0.01745329252f;
        if(!std::isfinite(scale)||!std::isfinite(angle)||scale<=0||scale>1000)continue;
        const float co=std::cos(angle),si=std::sin(angle);
        Vec origin={rd<float>(ram,instance),rd<float>(ram,instance+4),rd<float>(ram,instance+8)};
        auto transform=[&](Vec v){return Vec{origin[0]+scale*(v[0]*co+v[2]*si),origin[1]+scale*v[1],origin[2]+scale*(v[2]*co-v[0]*si)};};
        // Ground collision alone omits many walls and ceilings. Also trace
        // the loaded static display-list geometry, including those surfaces.
        const auto display_list=rd<uint32_t>(ram,mesh+16);
        if(pointer(display_list,8)){
            if(!visual_cache.contains(model))visual_cache[model]=visual_triangles(ram,display_list);
            for(const auto& t:visual_cache[model])if(collision.size()<200000)
                collision.push_back({transform(t.a),transform(t.b),transform(t.c)});
        }
        if((rd<uint16_t>(ram,instance+22)&255)>=16||!pointer(polygons,n*20)||!pointer(vertices,8))continue;
        for(unsigned j=0;j<n&&collision.size()<200000;j++){
            uint32_t poly=polygons+j*20;std::array<Vec,3> v;bool valid=true;
            for(int k=0;k<3;k++){
                int index=rd<int16_t>(ram,poly+k*2);uint32_t vp=vertices+index*8;
                if(index<0||!pointer(vp,8)){valid=false;break;}
                float x=rd<int16_t>(ram,vp)*scale,y=rd<int16_t>(ram,vp+2)*scale,z=rd<int16_t>(ram,vp+4)*scale;
                v[k]={origin[0]+x*co+z*si,origin[1]+y,origin[2]+z*co-x*si};
            }
            if(valid)collision.push_back({v[0],v[1],v[2]});
        }
    }
}
float clearance(Vec origin,Vec direction,float maximum){
    float closest=maximum;
    for(const auto& t:collision){
        Vec e1=sub(t.b,t.a),e2=sub(t.c,t.a),p=cross(direction,e2);
        float determinant=dot(e1,p);if(std::abs(determinant)<0.00001f)continue;
        Vec v=sub(origin,t.a);float u=dot(v,p)/determinant;if(u<0||u>1)continue;
        Vec q=cross(v,e1);float w=dot(direction,q)/determinant;if(w<0||u+w>1)continue;
        float d=dot(e2,q)/determinant;
        if(d>=0.05f&&d<closest)closest=d;
    }
    return closest;
}
bool living_enemy(uint8_t* ram,int index){
    const auto count=rd<uint32_t>(ram,0x8007C990);
    if(count>6||index<0||unsigned(index)>=count)return false;
    const uint32_t actor=0x8007C9BC+index*0x128;
    return rd<int16_t>(ram,actor+0x50)>=0&&rd<uint16_t>(ram,actor-0x1A)>0;
}
std::array<uint32_t,3> overlay_buffers{};
unsigned overlay_frame=0;
#ifdef QUEST64_MOD_TEST
bool test_external_overlay=false;
#endif
Vec enemy_position(uint8_t* ram,int index){
    const uint32_t actor=0x8007C9BC+index*0x128;
    return {rd<float>(ram,actor),rd<float>(ram,actor+4)+8,rd<float>(ram,actor+8)};
}
int choose_enemy(uint8_t* ram,bool cycle){
    if(cycle&&locked_enemy>=0){
        for(int j=1;j<=6;j++){int i=(locked_enemy+j)%6;if(living_enemy(ram,i))return i;}
    }
    int best=-1;float score=1e30f;
    const float facing=rd<float>(ram,Actor+16);
    for(int i=0;i<6;i++)if(living_enemy(ram,i)){
        auto e=enemy_position(ram,i);
        float dx=e[0]-rd<float>(ram,Actor),dz=e[2]-rd<float>(ram,Actor+8);
        float angle=std::abs(std::remainder(std::atan2(dx,dz)-facing,6.283185307f));
        float value=std::hypot(dx,dz)*(1+angle*0.4f);
        if(std::isfinite(value)&&value<score){best=i;score=value;}
    }
    return best;
}
int elemental_percent(uint8_t* ram,uint32_t spell,uint32_t enemy){
    if(rd<uint16_t>(ram,spell+2)&0x8000)return 100;
    const auto model=rd<uint32_t>(ram,enemy-4); // EnemyAction.monBaseData
    if(!pointer(model,0x38))return 100;
    unsigned a=rd<uint8_t>(ram,spell+6),b=rd<uint16_t>(ram,model+0x26);
    if(a>=4||b>=4)return 100;
    unsigned pair=(1u<<a)|(1u<<b);
    if(pair==1||pair==2||pair==4||pair==8)return 50;
    return pair==5||pair==10?125:100;
}
unsigned spell_power(uint8_t* ram,uint32_t spell,uint32_t status){
    unsigned element=rd<uint8_t>(ram,spell+6);if(element>=4)return 0;
    // Match the native elemental-growth weights and floating-point rounding.
    unsigned level=rd<uint8_t>(ram,Player+0x24+element);
    for(int i=0;i<3;i++){
        unsigned other=rd<uint16_t>(ram,0x8004CDD0+element*4+i*2);
        if(other>=4)return 0;
        level+=rd<uint8_t>(ram,Player+0x24+other)>>(i==0?4:3);
    }
    float power=float(rd<uint16_t>(ram,spell+12)*.02);
    float increment=float(double(power)*.2);
    for(unsigned i=0;i<level;i++){power+=increment;increment=float(double(increment)*1.03);}
    unsigned value=unsigned(std::max(0.f,power))&65535;
    if(rd<uint16_t>(ram,status)&1)value>>=1;
    return value;
}
unsigned staff_power(uint8_t* ram,uint32_t status){
    unsigned base=rd<uint16_t>(ram,status+0x104),quarter=base>>2,total=base;
    for(int i=0;i<4;i++){unsigned level=rd<uint8_t>(ram,Player+0x24+i);if(level>quarter)total=(total-level+quarter)&65535;}
    unsigned value=(((total+(base>>1))&65535)*rd<uint16_t>(ram,status+0x10A)>>4)&65535;
    if(rd<uint16_t>(ram,status+0x80)&1)value>>=1;
    return value;
}
std::array<unsigned,2> damage_bounds(uint8_t* ram,uint32_t status,uint32_t enemy,unsigned power,int percent){
    if(percent==50)power>>=1;else if(percent==125)power=(power+(power>>2))&65535;
    unsigned attack=rd<uint16_t>(ram,status+0x84),defense=rd<uint16_t>(ram,enemy-0x24+0x118);
    unsigned low=attack+defense?unsigned(float(power)*float(attack)/float(attack+defense)):0;
    unsigned high=low+unsigned(std::sqrt(float(low)));low=std::max(1u,low);high=std::max(1u,high);
    return {low,high};
}
std::string damage_preview(uint8_t* ram,uint32_t status,uint32_t enemy,unsigned power,int percent){
    const auto bounds=damage_bounds(ram,status,enemy,power,percent);
    const unsigned low=bounds[0],high=bounds[1];
    unsigned hp=rd<uint16_t>(ram,enemy-0x18);
    if(!hp)return "DMG UNKNOWN";
    auto pct=[&](unsigned value){return std::min(100u,(value*100+hp/2)/hp);};
    return "DMG "+std::to_string(low)+"-"+std::to_string(high)+" / "+std::to_string(pct(low))+"-"+std::to_string(pct(high))+"% HP";
}
// An extruded arrow mesh in world space. Perspective-project its 14 vertices
// and depth-sort its shaded surfaces into the HUD overlay. This keeps a route
// marker readable through scenery, while its orientation and thickness follow
// the real camera instead of rotating a flat screen-space icon.
void arrow_mesh(Hud& hud,uint8_t* ram,float x,float y,float z,float dx,float dz,float size_factor=1.0f,bool orange=false){
    float length=std::hypot(dx,dz);if(!std::isfinite(length)||length<0.001f)return;
    dx/=length;dz/=length;
    const float projection=rd<float>(ram,0x80086ED4);
    auto center=camera(ram,x,y,z);
    if(!std::isfinite(projection)||projection<0.00001f||!std::isfinite(center[2])||center[2]>-2)return;
    // Limit extreme close-up size, without distorting any mesh axis.
    float size=std::min(0.65f,-center[2]*projection/65.0f)*size_factor;
    constexpr std::array<std::array<float,2>,7> outline={{{0,12},{7,2},{3,2},{3,-10},{-3,-10},{-3,2},{-7,2}}};
    struct Point{float x,y,depth;};std::array<Point,14> vertices;
    for(int i=0;i<14;i++){
        auto p=outline[i%7];float side=p[0]*size,along=p[1]*size;
        auto v=camera(ram,x+dz*side+dx*along,y+(i<7?1.5f:-1.5f)*size,z-dx*side+dz*along);
        if(!std::isfinite(v[2])||v[2]>-0.1f)return;
        vertices[i]={160-150*v[0]/(v[2]*projection),120+120*v[1]/(v[2]*projection),-1/v[2]};
        if(!std::isfinite(vertices[i].x)||!std::isfinite(vertices[i].y))return;
    }
    // Keep the complete marker inside the cleared viewport, including close
    // camera positions where its world-space height projects above the screen.
    float left=320,right=0,top=240,bottom=0;
    for(auto v:vertices){left=std::min(left,v.x);right=std::max(right,v.x);top=std::min(top,v.y);bottom=std::max(bottom,v.y);}
    const float fit=std::min({1.f,288.f/std::max(1.f,right-left),200.f/std::max(1.f,bottom-top)});
    float cx=(left+right)*.5f,cy=(top+bottom)*.5f;
    float safe_x=std::clamp(cx,16+(right-left)*fit*.5f,304-(right-left)*fit*.5f);
    float safe_y=std::clamp(cy,20+(bottom-top)*fit*.5f,220-(bottom-top)*fit*.5f);
    for(auto& v:vertices){v.x=safe_x+(v.x-cx)*fit;v.y=safe_y+(v.y-cy)*fit;}
    std::vector<float> depth(320*240,0);std::vector<uint16_t> pixels(320*240,0);
    auto triangle=[&](int ia,int ib,int ic,uint16_t color){
        auto a=vertices[ia],b=vertices[ib],c=vertices[ic];
        float determinant=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
        if(std::abs(determinant)<0.001f)return;
        int x0=int(std::clamp(std::floor(std::min({a.x,b.x,c.x})),0.0f,319.0f)),x1=int(std::clamp(std::ceil(std::max({a.x,b.x,c.x})),0.0f,319.0f));
        int y0=int(std::clamp(std::floor(std::min({a.y,b.y,c.y})),0.0f,239.0f)),y1=int(std::clamp(std::ceil(std::max({a.y,b.y,c.y})),0.0f,239.0f));
        for(int j=y0;j<=y1;j++)for(int i=x0;i<=x1;i++){
            float u=((b.y-c.y)*(i+.5f-c.x)+(c.x-b.x)*(j+.5f-c.y))/determinant;
            float v=((c.y-a.y)*(i+.5f-c.x)+(a.x-c.x)*(j+.5f-c.y))/determinant;
            float w=1-u-v;if(u<0||v<0||w<0)continue;
            float d=u*a.depth+v*b.depth+w*c.depth;int n=j*320+i;
            if(d>depth[n]){depth[n]=d;pixels[n]=color;}
        }
    };
    for(auto t:std::array<std::array<int,3>,3>{{{0,1,6},{2,3,4},{2,4,5}}}){
        triangle(t[0],t[1],t[2],orange?0xFC41:0x913F);triangle(t[0]+7,t[1]+7,t[2]+7,orange?0xA201:0x401B);
    }
    for(int i=0;i<7;i++){int j=(i+1)%7;uint16_t color=orange?(i%2?0xEB01:0xC241):(i%2?0x8037:0x6031);
        triangle(i,j,j+7,color);triangle(i,j+7,i+7,color);
    }
    for(int j=0;j<240;j++)for(int i=0;i<320;){
        uint16_t color=pixels[j*320+i];if(!color){i++;continue;}
        int start=i;while(i<320&&pixels[j*320+i]==color)i++;
        hud.rect(start,j,i-start,1,color);
    }
}
#ifdef QUEST64_MOD_TEST
extern "C" PROGRESS_EXPORT unsigned qs64_test_spell_power(uint8_t* ram,uint32_t spell,uint32_t status){return spell_power(ram,spell,status);}
extern "C" PROGRESS_EXPORT unsigned qs64_test_staff_power(uint8_t* ram,uint32_t status){return staff_power(ram,status);}
extern "C" PROGRESS_EXPORT int qs64_test_effectiveness(uint8_t* ram,uint32_t spell,uint32_t enemy){return elemental_percent(ram,spell,enemy);}
extern "C" PROGRESS_EXPORT uint64_t qs64_test_damage_bounds(uint8_t* ram,uint32_t status,uint32_t enemy,unsigned power,int percent){auto b=damage_bounds(ram,status,enemy,power,percent);return uint64_t(b[0])|(uint64_t(b[1])<<32);}
float test_x=0,test_y=0;
float test_hud_aspect=4.f/3.f;
#endif
}
extern "C" PROGRESS_EXPORT void qs64_progression_set(const char* id,bool enabled){
    if(!std::strcmp(id,"qs64_experience"))experience_on=enabled;
    if(!std::strcmp(id,"qs64_story_arrow"))arrow_on=enabled;
    if(!std::strcmp(id,"qs64_spirit_tracker"))spirits_on=enabled;
    if(!std::strcmp(id,"qs64_spell_preview"))spell_preview_on=enabled;
}
extern "C" PROGRESS_EXPORT void qs64_attack_experience(uint8_t* ram,uint32_t player,int magic,unsigned amount){
    if(!experience_on||player!=Player||!playing(ram)||!(rd<uint16_t>(ram,0x8008C592)&1))return;
    amount=std::min(amount,1000u);if(!amount)return;
    if(magic)add_xp(ram,0,amount);
    add_xp(ram,2,amount);
    add_xp(ram,3,amount);
}
extern "C" PROGRESS_EXPORT void qs64_progression_reset(){orbit=false;restore_camera=false;camera_map=-1;camera_scene=-1;collision_scene=0;collision.clear();locked_enemy=-1;zoom_step=1;zoom_radius=0;zoom_requested=false;lock_reframe=false;movement_restore=false;
    overlay_buffers={};overlay_frame=0;
    battle_paused=false;pause_selection=0;pause_stick=0;
#ifndef QUEST64_MOD_TEST
    recomp::set_battle_input_active(false);
#endif
}
// Called immediately after native input polling, before any simulation updates.
// 1 freezes simulation; 2 requests the game's normal retreat cleanup.
extern "C" PROGRESS_EXPORT int qs64_battle_pause_input(uint8_t* ram){
    if(!playing(ram)||!(rd<uint16_t>(ram,0x8008C592)&1)){
        battle_paused=false;pause_stick=0;return 0;
    }
    uint16_t pressed=rd<uint16_t>(ram,0x80092876);
    if(!battle_paused){
        if(!(pressed&0x1000)||(rd<uint32_t>(ram,0x8007B2E4)&0x408B))return 0;
        battle_paused=true;pause_selection=0;pause_stick=0;
    }else{
        int stick=rd<int8_t>(ram,0x80092872);
        int direction=stick>19?-1:stick<-19?1:0;
        if((pressed&0x0808)||(direction<0&&pause_stick>=0))pause_selection=(pause_selection+2)%3;
        if((pressed&0x0404)||(direction>0&&pause_stick<=0))pause_selection=(pause_selection+1)%3;
        pause_stick=direction;
        if(pressed&0x5000)battle_paused=false; // B or Start returns.
        else if(pressed&0x8000){
            if(pause_selection==0)battle_paused=false;
            else if(pause_selection==1){
                if(rd<uint16_t>(ram,0x8008C592)&0x100){wr<uint16_t>(ram,0x80092876,0);return 1;}
                battle_paused=false;locked_enemy=-1;lock_reframe=false;
                // Retreat without awarding enemy XP, drops, or boss progression.
                auto flags=rd<uint16_t>(ram,0x8008C592);
                wr<uint16_t>(ram,0x8008C592,uint16_t((flags&~0x112u)|0x404u));
                wr<uint16_t>(ram,0x8008C594,0);
                wr<uint16_t>(ram,Actor+0x60,uint16_t(rd<uint16_t>(ram,Actor+0x60)&~1u));
                wr<uint16_t>(ram,0x80092876,0);return 2;
            }else{
                battle_paused=false;locked_enemy=-1;lock_reframe=false;orbit=false;
                wr<uint16_t>(ram,0x8008C592,0);
                wr<uint32_t>(ram,0x8007B2E4,rd<uint32_t>(ram,0x8007B2E4)&~0x408Bu);
                wr<uint16_t>(ram,0x8007B2E0,3);
                wr<uint16_t>(ram,0x80092876,0);return 3;
            }
        }
    }
    // Consume menu input even on the resume frame, preventing accidental attacks.
    wr<uint16_t>(ram,0x80092876,0);
    wr<uint16_t>(ram,0x80092874,0);
    wr<int8_t>(ram,0x80092871,0);wr<int8_t>(ram,0x80092872,0);
    return battle_paused?1:0;
}
extern "C" PROGRESS_EXPORT int qs64_battle_paused(){return battle_paused?1:0;}
extern "C" PROGRESS_EXPORT void qs64_hud_mark(uint8_t* ram,int mode){
    auto p=rd<uint32_t>(ram,0x8007B2FC);
    if(p<0x80000000||p>0x807FFFE0)return;
    wr<uint32_t>(ram,p,0);wr<uint32_t>(ram,p+4,mode==1?0x51485544:mode==2?0x514C4546:mode==3?0x51524947:0x51574F52);
    wr<uint32_t>(ram,0x8007B2FC,p+8);
}
extern "C" PROGRESS_EXPORT void qs64_camera_controls(uint8_t* ram){
#ifndef QUEST64_MOD_TEST
    recomp::set_battle_input_active(playing(ram)&&(rd<uint16_t>(ram,0x8008C592)&1));
#endif
    if(!playing(ram)||(rd<uint32_t>(ram,0x8007B2E4)&0x408B)||(rd<uint16_t>(ram,0x8007BB2C)&1))return;
    uint16_t buttons=rd<uint16_t>(ram,0x80092876);
    if(camera_map>=0&&(camera_map!=rd<int32_t>(ram,0x80084EE4)||camera_scene!=rd<int32_t>(ram,0x80084EE8)))locked_enemy=-1;
    if(buttons&0x2000){
        constexpr float distances[]={40,90,160,250};
        zoom_step=(zoom_step+1)%4;zoom_radius=distances[zoom_step];zoom_requested=true;
    }
    // Z now cycles camera distance. Do not also trigger native Z attacks or
    // native L camera presets from the same press.
    wr<uint16_t>(ram,0x80092876,buttons&~uint16_t(0x2020));
    if(!(rd<uint16_t>(ram,0x8008C592)&1)){locked_enemy=-1;lock_reframe=false;return;}
    if(!living_enemy(ram,locked_enemy)){
        locked_enemy=choose_enemy(ram,false);lock_reframe=locked_enemy>=0;
    }
    if(buttons&0x20){
        locked_enemy=choose_enemy(ram,(buttons&0x20)!=0);lock_reframe=locked_enemy>=0;
    }
}
extern "C" PROGRESS_EXPORT void qs64_lock_attack(uint8_t* ram){
    if(!playing(ram)||!(rd<uint16_t>(ram,0x8008C592)&1)||
       !(rd<uint16_t>(ram,0x80092876)&0x8000)||(rd<uint32_t>(ram,0x8007B2E4)&0x408B))return;
    if(!living_enemy(ram,locked_enemy)){locked_enemy=choose_enemy(ram,false);lock_reframe=locked_enemy>=0;}
    if(locked_enemy<0)return;
    auto e=enemy_position(ram,locked_enemy);
    float angle=std::atan2(e[0]-rd<float>(ram,Actor),e[2]-rd<float>(ram,Actor+8));
    // PosRot stores X, Y, Z rotation in that order. Lock-on changes heading,
    // never the X rotation used by Brian's attack and damage animations.
    if(std::isfinite(angle))wr<float>(ram,Actor+16,angle);
}
extern "C" PROGRESS_EXPORT void qs64_movement_begin(uint8_t* ram){
    movement_restore=false;if(!orbit||!playing(ram))return;
    movement_angle=rd<float>(ram,0x80086DE8);
    // Native movement negates both stick axes before rotating the input.
    wr<float>(ram,0x80086DE8,std::remainder(view_heading+3.14159265358979323846f,6.28318530717958647692f));movement_restore=true;
}
extern "C" PROGRESS_EXPORT void qs64_movement_end(uint8_t* ram){
    if(movement_restore)wr<float>(ram,0x80086DE8,movement_angle);movement_restore=false;
}
extern "C" PROGRESS_EXPORT void qs64_camera_begin(uint8_t* ram){
    // Refresh on every rendered battle frame, including enemy turns and the
    // opening animation, rather than only inside Brian's input update.
    if(playing(ram)&&(rd<uint16_t>(ram,0x8008C592)&1)){
        if(!living_enemy(ram,locked_enemy)){locked_enemy=choose_enemy(ram,false);lock_reframe=locked_enemy>=0;}
    }else locked_enemy=-1;
    restore_camera=false;
    if(!playing(ram)||(rd<uint32_t>(ram,0x8007B2E4)&0x408B)||(rd<uint16_t>(ram,0x8007BB2C)&1))return;
    float x=0,y=0;
#ifdef QUEST64_MOD_TEST
    x=test_x;y=test_y;
#else
    if(recomp::game_input_disabled())return;
    recomp::get_camera_analog(&x,&y);
    recomp::set_right_analog_suppressed(true);
#endif
    if(!std::isfinite(x)||!std::isfinite(y))return;
    if(battle_paused)x=y=0;
    int map=rd<int32_t>(ram,0x80084EE4),scene=rd<int32_t>(ram,0x80084EE8);
    if(map!=camera_map||scene!=camera_scene){orbit=false;camera_map=map;camera_scene=scene;collision_scene=0;}
    if(!orbit&&std::abs(x)<0.01f&&std::abs(y)<0.01f&&!zoom_requested&&locked_enemy<0)return;
    for(int i=0;i<6;i++)original_camera[i]=rd<float>(ram,0x80086DCC+4*i);
    original_yaw=rd<float>(ram,0x80086DEC);
    for(float f:original_camera)if(!std::isfinite(f))return;
    if(!orbit){
        float dx=original_camera[0]-original_camera[3],dy=original_camera[1]-original_camera[4],dz=original_camera[2]-original_camera[5];
        radius=std::clamp(std::sqrt(dx*dx+dy*dy+dz*dz),30.0f,600.0f);
        yaw=std::atan2(dx,dz);pitch=std::asin(std::clamp(dy/radius,-1.0f,1.0f));actual_radius=radius;orbit=true;
    }
    if(zoom_requested){radius=zoom_radius;zoom_requested=false;}
    // Called once per native camera frame (30 Hz), independent of display FPS.
    yaw=std::remainder(yaw+std::clamp(x,-1.0f,1.0f)*0.065f,6.283185307f);
    pitch=std::clamp(pitch-std::clamp(y,-1.0f,1.0f)*0.045f,-1.35f,1.535f);
    float tx=rd<float>(ram,Actor),ty=rd<float>(ram,Actor+4)+8.0f,tz=rd<float>(ram,Actor+8);
    float desired_radius=radius;
    if(!(rd<uint16_t>(ram,0x8008C592)&1)||!living_enemy(ram,locked_enemy))locked_enemy=-1;
    if(locked_enemy>=0){
        auto e=enemy_position(ram,locked_enemy);
        if(lock_reframe){yaw=std::atan2(tx-e[0],tz-e[2]);pitch=std::max(pitch,0.25f);lock_reframe=false;}
        float distance=std::hypot(e[0]-tx,e[2]-tz);
        // Frame Brian and his target together. The user's zoom still controls
        // the requested distance; keep enough room to see both combatants.
        desired_radius=std::max(radius,std::min(300.0f,distance*.65f+25));
        tx=(tx+e[0])*.5f;ty=(ty+e[1])*.5f;tz=(tz+e[2])*.5f;
    }
    if(!std::isfinite(tx)||!std::isfinite(ty)||!std::isfinite(tz))return;
    if(collision_scene!=rd<uint32_t>(ram,0x80084F20))load_collision(ram);
    // Floor-height limit gives a low view without putting the eye underground.
    const float effective_pitch=std::max(pitch,std::asin(std::clamp(-6.0f/desired_radius,-1.0f,1.0f)));
    Vec direction={std::sin(yaw)*std::cos(effective_pitch),std::sin(effective_pitch),std::cos(yaw)*std::cos(effective_pitch)};
    Vec side={std::cos(yaw),0,-std::sin(yaw)},up=cross(direction,side);
    float safe=desired_radius;
    // Sweep the center and four near-plane corners, with a three-unit skin.
    // Snap inward immediately; recover outward smoothly after obstructions.
    for(int i=0;i<5;i++){
        Vec start={tx,ty,tz};
        if(i)for(int k=0;k<3;k++)start[k]+=side[k]*((i&1)?2.0f:-2.0f)+up[k]*((i&2)?2.0f:-2.0f);
        safe=std::min(safe,std::max(0.5f,clearance(start,direction,desired_radius+3.0f)-3.0f));
    }
    // A close wall must not trap the camera against Brian. Search neighboring
    // orbit positions, including the opposite side, only when severely blocked.
    if(safe<25&&std::abs(x)<.01f){
        float best=safe,best_yaw=yaw;
        for(float offset:std::array<float,8>{.35f,-.35f,.7f,-.7f,1.2f,-1.2f,2.0f,3.14159265f}){
            float candidate=yaw+offset;Vec d={std::sin(candidate)*std::cos(effective_pitch),std::sin(effective_pitch),std::cos(candidate)*std::cos(effective_pitch)};
            Vec s={std::cos(candidate),0,-std::sin(candidate)},u=cross(d,s);float room=desired_radius;
            for(int i=0;i<5;i++){Vec start={tx,ty,tz};if(i)for(int k=0;k<3;k++)start[k]+=s[k]*((i&1)?2.f:-2.f)+u[k]*((i&2)?2.f:-2.f);
                room=std::min(room,std::max(.5f,clearance(start,d,desired_radius+3)-3));}
            if(room>best+10){best=room;best_yaw=candidate;}
        }
        if(best>safe+10){yaw=std::remainder(best_yaw,6.283185307f);safe=best;direction={std::sin(yaw)*std::cos(effective_pitch),std::sin(effective_pitch),std::cos(yaw)*std::cos(effective_pitch)};}
    }
    actual_radius=safe<actual_radius?safe:std::min(safe,actual_radius+(desired_radius-actual_radius)*0.15f);
    const float values[]={tx+direction[0]*actual_radius,ty+direction[1]*actual_radius,tz+direction[2]*actual_radius,tx,ty,tz};
    for(int i=0;i<6;i++)wr<float>(ram,0x80086DCC+4*i,values[i]);restore_camera=true;
    view_heading=std::atan2(tx-values[0],tz-values[2]);
    // Native billboard orientation and angular culling use target-to-eye yaw.
    // Movement uses eye-to-target heading, which differs by half a turn.
    wr<float>(ram,0x80086DEC,std::atan2(values[0]-tx,values[2]-tz));
}
extern "C" PROGRESS_EXPORT void qs64_camera_end(uint8_t* ram){
    if(!restore_camera)return;
    for(int i=0;i<6;i++)wr<float>(ram,0x80086DCC+4*i,original_camera[i]);restore_camera=false;
    wr<float>(ram,0x80086DEC,original_yaw);
}
extern "C" PROGRESS_EXPORT int qs64_free_camera_rendering(){return restore_camera?1:0;}
extern "C" PROGRESS_EXPORT void qs64_progression_draw(uint8_t* ram){
    if((!experience_on&&!arrow_on&&!spirits_on&&!spell_preview_on&&locked_enemy<0&&!battle_paused)||!playing(ram))return;
    uint32_t p=rd<uint32_t>(ram,0x8007B2FC);if(p<0x80000000||p>0x807F8000)return;
    const uint32_t native_p=p;
    bool external_overlay=true;
#ifdef QUEST64_MOD_TEST
    external_overlay=test_external_overlay;
    if(external_overlay)p=0x80600000+(overlay_frame++%3)*65536;
#else
    auto& buffer=overlay_buffers[overlay_frame++%overlay_buffers.size()];
    if(!buffer){auto* memory=static_cast<uint8_t*>(recomp::alloc(ram,65536));if(!memory)return;buffer=uint32_t(memory-ram)+0x80000000u;}
    p=buffer;
#endif
    Hud hud{ram,p};hud.emit(0xE7000000,0);hud.emit(0xBA001402,0x00300000);
    float aspect=4.f/3.f;
#ifdef QUEST64_MOD_TEST
    aspect=test_hud_aspect;
#else
    const auto graphics=ultramodern::renderer::get_graphics_config();
    if(graphics.ar_option==ultramodern::renderer::AspectRatio::Manual)aspect=16.f/9.f;
    else if(graphics.ar_option==ultramodern::renderer::AspectRatio::Expand){int w=0,h=0;recompui::get_window_size(w,h);if(h>0)aspect=std::max(aspect,float(w)/h);}
#endif
    // Compact the layout while retaining every pixel of text and bar borders.
    const float panel_scale=1.f;
    const int bar_width=aspect>1.5f?70:82;
    hud.scale=panel_scale;
    hud.emit(0,0x514C4546);
    if(spell_preview_on&&!battle_paused&&(rd<uint16_t>(ram,0x8008C592)&1)){
        const auto status=rd<uint32_t>(ram,Actor+0x68);
        uint32_t spell=0;
        if(status>=0x80000000&&status<=0x807FFE00){
            const int count=rd<uint16_t>(ram,status+0x11C);
            if(count>=1&&count<=3){
                std::array<uint8_t,3> elements{};
                for(int i=0;i<count;i++)elements[i]=rd<uint8_t>(ram,status+0x119+i);
                if(count==3&&elements[2]<elements[1])std::swap(elements[1],elements[2]);
                if(elements[0]<4){
                    const auto table=rd<uint32_t>(ram,0x800C1B14+elements[0]*4);
                    if(table>=0x80000000&&table<=0x807FFB00){
                    // The native selector falls back to the first spell for an unmatched combination.
                    spell=table;
                    for(int i=0;i<15;i++){
                        const auto entry=table+i*0x44;
                        if(rd<uint16_t>(ram,entry+4)!=count)continue;
                        bool match=true;for(int k=0;k<count;k++)match&=rd<uint8_t>(ram,entry+6+k)==elements[k];
                        if(match){spell=entry;break;}
                    }
                    }
                }
            }
        }
        hud.emit(0,0x51524947);
        hud.rect(184,174,128,46,0x1085);
        hud.text(187,177,spell?"SPELL PREVIEW":"SELECT AN ELEMENT",0xFFFF);
        const uint32_t enemy=living_enemy(ram,locked_enemy)?0x8007C9BC+locked_enemy*0x128:0;
        if(spell){
            const float radius=rd<float>(ram,spell+0x1C);
            if(std::isfinite(radius)&&radius>=0&&radius<10000){
                std::string effect="NO TARGET";
                float distance=0;
                if(enemy){
                    distance=std::hypot(rd<float>(ram,enemy)-rd<float>(ram,Actor),rd<float>(ram,enemy+8)-rd<float>(ram,Actor+8));
                    const int percent=elemental_percent(ram,spell,enemy);
                    effect=percent==50?"NOT VERY EFFECTIVE 50%":percent==125?"SUPER EFFECTIVE 125%":"EFFECTIVE 100%";
                }
                if(rd<uint16_t>(ram,spell+0x18)==2)effect="SUPPORT SPELL";
                hud.text(187,184,effect,0xFFC1);
                hud.text(187,191,"RANGE "+std::to_string(int(std::round(radius)))+" DIST "+std::to_string(int(std::round(distance))),0xFFFF);
                if(enemy&&rd<uint16_t>(ram,spell+0x18)!=2)
                    hud.text(187,198,damage_preview(ram,status,enemy,spell_power(ram,spell,status),elemental_percent(ram,spell,enemy)),0xFFFF);
            }
        }
        if(enemy&&pointer(status,0x120)){
            // The native target selector admits staff targets only in reach.
            const bool staff_ready=rd<uint16_t>(ram,status+2)==locked_enemy+1;
            hud.text(187,205,staff_ready?"STAFF "+damage_preview(ram,status,enemy,staff_power(ram,status),100).substr(4):"STAFF OUT OF REACH",0xFFFF);
        }
        hud.emit(0,0x51574F52);
    }
    hud.emit(0,0x514C4546);
    if(experience_on&&!battle_paused){
        hud.rect(8,76,bar_width+8,67,0x1085);
        constexpr const char* names[]={"HP","MP","SPEED","DEFENSE","ELEMENT"};
        constexpr uint16_t colors[]={0xF843,0x043F,0xFFC1,0xFC01,0x913F};
        const bool ready=rd<uint32_t>(ram,0x8007B2E4)&8;
        for(int i=0;i<5;i++){
            int y=79+i*13,value=(i==4&&ready)?100:percent(ram,i);
            std::string label=std::string(names[i])+" "+std::to_string(value)+"%";
            if(i==4&&ready)label="ELEMENT READY";
            hud.text(11,y,label,i==4&&ready?0xFFC1:0xFFFF);
            hud.rect(11,y+7,bar_width,5,0xFFFF);hud.rect(12,y+8,bar_width-2,3,0x1085);
            hud.rect(12,y+8,((bar_width-2)*value+50)/100,3,colors[i]);
        }
    }
    if(arrow_on&&!battle_paused)if(auto goal=target(ram)){
        hud.emit(0,0x51574F52);
        hud.scale=1;
        const float px=rd<float>(ram,Actor),py=rd<float>(ram,Actor+4),pz=rd<float>(ram,Actor+8);
        arrow_mesh(hud,ram,px,py+rd<float>(ram,Player+0x1C)*rd<float>(ram,Actor+0x24)+6,pz,goal->x-px,goal->z-pz);
        hud.scale=panel_scale;
        hud.emit(0,0x514C4546);
        std::string name=goal->name;std::transform(name.begin(),name.end(),name.begin(),[](char c){return char(std::toupper(static_cast<unsigned char>(c)));});
        int label_y=experience_on?146:104;
        std::string label=(goal->locked?"LOCKED: ":"NEXT: ")+name;
        hud.rect(8,label_y,int(label.size())*4+6,9,0x1085);hud.text(11,label_y+2,label,0xD39F);
    }
    if(spirits_on&&!battle_paused){
        auto guide=spirit_guide(ram);
        int y=experience_on?(arrow_on?158:146):(arrow_on?118:76);
        std::string label="SPIRITS "+std::to_string(guide.total-guide.remaining)+"/"+std::to_string(guide.total)+" LEFT "+std::to_string(guide.remaining);
        hud.scale=panel_scale;hud.rect(8,y,int(label.size())*4+6,9,0x1085);hud.text(11,y+2,label,0xFC41);
        if(guide.direction){hud.emit(0,0x51574F52);hud.scale=1;float height=rd<float>(ram,Player+0x1C)*rd<float>(ram,Actor+0x24);
            arrow_mesh(hud,ram,rd<float>(ram,Actor),rd<float>(ram,Actor+4)+height+9,rd<float>(ram,Actor+8),(*guide.direction)[0],(*guide.direction)[1],.55f,true);}
    }
    hud.scale=1;
    hud.emit(0,0x51574F52);
    if(living_enemy(ram,locked_enemy)&&(rd<uint16_t>(ram,0x8008C592)&1)){
        const auto actor=0x8007C9BC+locked_enemy*0x128;
        const auto model=rd<uint32_t>(ram,actor+0x64);
        const float model_scale=rd<float>(ram,actor+0x24);
        float height=20,width=16;
        if(model>=0x80000000&&model<=0x807FFF00&&std::isfinite(model_scale)&&model_scale>0){
            height=rd<float>(ram,model+0x1C)*model_scale;
            width=2.f*rd<float>(ram,model+0x18)*model_scale;
        }
        height=std::isfinite(height)?std::clamp(height,4.f,2000.f):20.f;
        width=std::isfinite(width)?std::clamp(width,4.f,2000.f):16.f;
        float scale=rd<float>(ram,0x80086ED4);
        if(std::isfinite(scale)&&scale>.00001f){
            float left=10000,top=10000,right=-10000,bottom=-10000;
            for(int corner=0;corner<8;corner++){
                auto v=camera(ram,rd<float>(ram,actor)+((corner&1)?width:-width)*.5f,
                    rd<float>(ram,actor+4)+((corner&2)?height:0),rd<float>(ram,actor+8)+((corner&4)?width:-width)*.5f);
                if(!std::isfinite(v[2])||v[2]>=-.1f)continue;
                float x=160-150*v[0]/(v[2]*scale),y=120+120*v[1]/(v[2]*scale);
                left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
            }
            if(left<right&&top<bottom&&right>8&&left<312&&bottom>12&&top<228){
                int l=int(std::clamp(left-3,8.f,305.f)),r=int(std::clamp(right+3,float(l+6),311.f));
                int t=int(std::clamp(top-3,12.f,221.f)),b=int(std::clamp(bottom+3,float(t+6),227.f));
                for(int x:{l,r-5})for(int y:{t,b-1})hud.rect(x,y,6,2,0xFFC1);
                for(int x:{l,r-1})for(int y:{t,b-5})hud.rect(x,y,2,6,0xFFC1);
            }
        }
    }
    if(battle_paused){
        hud.rect(92,68,136,104,0xFFFF);hud.rect(94,70,132,100,0x1085);
        hud.text(126,80,"PAUSED",0xD39F);
        constexpr const char* options[]={"RETURN","ESCAPE","QUIT"};
        for(int i=0;i<3;i++){
            int y=102+i*20;
            if(i==pause_selection)hud.rect(100,y-3,120,13,0x5817);
            hud.text(116,y,options[i],i==1&&(rd<uint16_t>(ram,0x8008C592)&0x100)?0x739D:i==pause_selection?0xFFC1:0xFFFF);
        }
    }
    hud.emit(0xE7000000,0);hud.emit(0xBA001402,0x00100000);
    if(external_overlay){
    hud.emit(0xB8000000,0);
    wr<uint32_t>(ram,native_p,0);wr<uint32_t>(ram,native_p+4,0x51455854);
    wr<uint32_t>(ram,native_p+8,0x06000000);wr<uint32_t>(ram,native_p+12,p);
    wr<uint32_t>(ram,0x8007B2FC,native_p+16);
    }else{
    wr<uint32_t>(ram,0x8007B2FC,hud.p);
    }
}
#ifdef QUEST64_MOD_TEST
extern "C" PROGRESS_EXPORT void qs64_test_external_overlay(int enabled){test_external_overlay=enabled!=0;}
extern "C" PROGRESS_EXPORT int qs64_test_percent(uint8_t* ram,int index){return index>=0&&index<5?percent(ram,index):-1;}
extern "C" PROGRESS_EXPORT void qs64_test_camera_input(float x,float y){test_x=x;test_y=y;}
extern "C" PROGRESS_EXPORT void qs64_test_hud_aspect(float aspect){test_hud_aspect=aspect;}
extern "C" PROGRESS_EXPORT int qs64_test_spirits(uint8_t* ram,float* out){auto guide=spirit_guide(ram);if(guide.direction){out[0]=(*guide.direction)[0];out[1]=(*guide.direction)[1];}else out[0]=out[1]=0;return guide.remaining;}
extern "C" PROGRESS_EXPORT int qs64_test_target(uint8_t* ram,float* out){auto t=target(ram);if(!t)return 0;out[0]=t->x;out[1]=t->z;return 1;}
extern "C" PROGRESS_EXPORT unsigned qs64_test_collision_count(){return unsigned(collision.size());}
extern "C" PROGRESS_EXPORT unsigned qs64_test_wall_count(){unsigned n=0;for(const auto& t:collision){auto normal=cross(sub(t.b,t.a),sub(t.c,t.a));if(std::abs(normal[1])<0.001f&&(std::abs(normal[0])+std::abs(normal[2])>0.001f))n++;}return n;}
extern "C" PROGRESS_EXPORT void qs64_test_arrow(uint8_t* ram,float dx,float dz){Hud hud{ram,rd<uint32_t>(ram,0x8007B2FC)};arrow_mesh(hud,ram,0,0,0,dx,dz);wr<uint32_t>(ram,0x8007B2FC,hud.p);}
#endif


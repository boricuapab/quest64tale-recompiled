#pragma once
#include <cmath>
namespace quest64 {
struct FmvDoorRule {
    int from_map=13,from_scene=0,to_map=0,to_scene=1;
    float source_x=0,source_z=238,radius=48;
    bool once=true;
};
struct FmvDoorTrigger {
    FmvDoorRule rule;
    int map=-1,scene=-1,stable=0;
    float x=0,z=0;
    bool pending=false,played=false;
    void reset(){map=scene=-1;stable=0;pending=played=false;}
    bool tick(int next_map,int next_scene,float next_x,float next_z,bool ready){
        if(next_map!=map||next_scene!=scene){
            pending=(!rule.once||!played)&&map==rule.from_map&&scene==rule.from_scene&&
                next_map==rule.to_map&&next_scene==rule.to_scene&&
                std::hypot(x-rule.source_x,z-rule.source_z)<=rule.radius;
            stable=0;map=next_map;scene=next_scene;
        }
        x=next_x;z=next_z;
        if(!pending)return false;
        if(!ready){stable=0;return false;}
        if(++stable<15)return false;
        pending=false;played=true;return true;
    }
};
}


#include "quest64_fmv.h"
#include "quest64_fmv_trigger.h"
#include "librecomp/mods.hpp"
#include "ultramodern/ultramodern.hpp"
#include "zelda_config.h"
#include "zelda_sound.h"
#include "SDL.h"
#include "json/json.hpp"
#define PL_MPEG_IMPLEMENTATION
#include "../../lib/pl_mpeg/pl_mpeg.h"
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct Movie {std::string id,title;std::shared_ptr<std::vector<char>> bytes;quest64::FmvDoorTrigger trigger;};
std::mutex movie_mutex;
std::condition_variable movie_done;
std::vector<Movie> movies;
std::shared_ptr<std::vector<char>> requested_bytes;
std::string requested_title;
std::string requested_id;
std::atomic<bool> active=false;
std::atomic<Uint64> resume_guard=0;
bool request_pending=false,complete=true;
template<class T> T read(uint8_t* ram,uint32_t a){T v;auto o=a&0x7fffff;if constexpr(sizeof(T)==2)o^=2;std::memcpy(&v,ram+o,sizeof(v));return v;}
struct Player {
    SDL_Renderer* renderer=nullptr;SDL_Texture* texture=nullptr;
    SDL_AudioDeviceID audio=0;float volume=1;bool failed=false;
};
void video_frame(plm_t*,plm_frame_t* frame,void* opaque){
    auto& player=*static_cast<Player*>(opaque);
    if(SDL_UpdateYUVTexture(player.texture,nullptr,frame->y.data,frame->y.width,
        frame->cb.data,frame->cb.width,frame->cr.data,frame->cr.width)<0){player.failed=true;return;}
    SDL_SetRenderDrawColor(player.renderer,0,0,0,255);
    SDL_RenderClear(player.renderer);SDL_RenderCopy(player.renderer,player.texture,nullptr,nullptr);SDL_RenderPresent(player.renderer);
}
void audio_frame(plm_t*,plm_samples_t* samples,void* opaque){
    auto& player=*static_cast<Player*>(opaque);
    if(!player.audio)return;
    float output[PLM_AUDIO_SAMPLES_PER_FRAME*2];
    for(size_t i=0;i<samples->count*2;i++)output[i]=samples->interleaved[i]*player.volume;
    SDL_QueueAudio(player.audio,output,Uint32(samples->count*2*sizeof(float)));
}
bool play(std::vector<char>& bytes,const std::string& title,double maximum_seconds=0){
    plm_t* decoder=plm_create_with_memory(reinterpret_cast<uint8_t*>(bytes.data()),bytes.size(),0);
    if(!decoder)return false;
    const int width=plm_get_width(decoder),height=plm_get_height(decoder);
    if(width<=0||height<=0||width>4096||height>4096){plm_destroy(decoder);return false;}
    SDL_Window* original=SDL_GetKeyboardFocus();
    const int display=original?SDL_GetWindowDisplayIndex(original):0;
    const int center=SDL_WINDOWPOS_CENTERED_DISPLAY(display<0?0:display);
    SDL_Window* window=SDL_CreateWindow(title.c_str(),center,center,width,height,SDL_WINDOW_SHOWN|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_RESIZABLE);
    if(!window){plm_destroy(decoder);return false;}
    SDL_SetRelativeMouseMode(SDL_FALSE);
    const int cursor=SDL_ShowCursor(SDL_QUERY);SDL_ShowCursor(SDL_DISABLE);
    SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_SetWindowAlwaysOnTop(window,SDL_TRUE);SDL_RaiseWindow(window);
    Player player;
    const auto yuv_mode=SDL_GetYUVConversionMode();SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT601);
    player.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!player.renderer)player.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(player.renderer){SDL_RenderSetLogicalSize(player.renderer,width,height);player.texture=SDL_CreateTexture(player.renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,width,height);}
    if(!player.texture){SDL_SetYUVConversionMode(yuv_mode);SDL_DestroyRenderer(player.renderer);SDL_DestroyWindow(window);SDL_ShowCursor(cursor);plm_destroy(decoder);return false;}
    player.volume=zelda64::get_main_volume()/100.f;
    SDL_AudioSpec wanted{};wanted.freq=plm_get_samplerate(decoder);wanted.format=AUDIO_F32SYS;wanted.channels=2;wanted.samples=1024;
    if(wanted.freq>0)player.audio=SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);
    plm_set_video_decode_callback(decoder,video_frame,&player);
    plm_set_audio_enabled(decoder,player.audio!=0);
    plm_set_audio_decode_callback(decoder,audio_frame,&player);
    plm_set_audio_lead_time(decoder,.10);
    if(player.audio)SDL_PauseAudioDevice(player.audio,0);
    const Uint64 start=SDL_GetPerformanceCounter();Uint64 previous=start;
    bool skip=false,quit=false;std::vector<SDL_Event> deferred;
    while(!skip&&!plm_has_ended(decoder)&&!player.failed&&active.load()){
        if(maximum_seconds>0&&double(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()>=maximum_seconds)break;
        SDL_Event event;
        while(SDL_PollEvent(&event)){
            const double elapsed=double(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
            if(event.type==SDL_QUIT){quit=true;skip=true;}
            else if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE){skip=true;if(event.window.windowID!=SDL_GetWindowID(window))quit=true;}
            else if(elapsed>.5&&event.type==SDL_KEYDOWN&&!event.key.repeat&&(event.key.keysym.sym==SDLK_ESCAPE||event.key.keysym.sym==SDLK_RETURN||event.key.keysym.sym==SDLK_SPACE))skip=true;
            else if(elapsed>.5&&event.type==SDL_CONTROLLERBUTTONDOWN&&(event.cbutton.button==SDL_CONTROLLER_BUTTON_B||event.cbutton.button==SDL_CONTROLLER_BUTTON_START))skip=true;
            else if(event.type==SDL_CONTROLLERDEVICEADDED||event.type==SDL_CONTROLLERDEVICEREMOVED)deferred.push_back(event);
        }
        const Uint64 now=SDL_GetPerformanceCounter();
        plm_decode(decoder,std::min(.1,double(now-previous)/SDL_GetPerformanceFrequency()));previous=now;
        SDL_Delay(1);
    }
    if(player.audio){
        const auto deadline=SDL_GetTicks64()+300;
        if(!skip&&!player.failed)while(SDL_GetQueuedAudioSize(player.audio)>0&&SDL_GetTicks64()<deadline)SDL_Delay(5);
        SDL_ClearQueuedAudio(player.audio);SDL_CloseAudioDevice(player.audio);
    }
    SDL_DestroyTexture(player.texture);SDL_DestroyRenderer(player.renderer);SDL_DestroyWindow(window);
    SDL_SetYUVConversionMode(yuv_mode);
    SDL_ShowCursor(cursor);if(original){SDL_RaiseWindow(original);SDL_SetWindowInputFocus(original);}
    for(auto& event:deferred)SDL_PushEvent(&event);
    plm_destroy(decoder);
    if(quit){SDL_Event event{};event.type=SDL_QUIT;SDL_PushEvent(&event);}
    return !player.failed;
}
}

void quest64::fmv_enable(recomp::mods::ModContext& context,const recomp::mods::ModHandle& mod){
    try{
        recomp::mods::ModOpenError error;
        recomp::mods::ZipModFileHandle archive(context.get_mod_filename(mod.manifest.mod_id),error);
        bool exists=false;auto config_bytes=archive.read_file("quest64_fmv.json",exists);if(!exists)return;
        const auto config=nlohmann::json::parse(config_bytes);
        const auto path=config.at("video").get<std::string>();
        auto bytes=std::make_shared<std::vector<char>>(archive.read_file(path,exists));if(!exists||bytes->empty())return;
        auto* decoder=plm_create_with_memory(reinterpret_cast<uint8_t*>(bytes->data()),bytes->size(),0);
        const bool valid=decoder&&plm_get_width(decoder)>0&&plm_get_height(decoder)>0;
        if(decoder)plm_destroy(decoder);if(!valid){std::fprintf(stderr,"FMV mod %s: unsupported video\n",mod.manifest.mod_id.c_str());return;}
        Movie movie;movie.id=mod.manifest.mod_id;movie.title=config.value("title",mod.manifest.display_name);movie.bytes=std::move(bytes);
        auto& rule=movie.trigger.rule;const auto& door=config.at("door");
        rule.from_map=door.at("from_map");rule.from_scene=door.at("from_scene");rule.to_map=door.at("to_map");rule.to_scene=door.at("to_scene");
        rule.source_x=door.at("source_x");rule.source_z=door.at("source_z");rule.radius=door.value("radius",48.f);rule.once=config.value("once_per_session",true);
        std::lock_guard lock(movie_mutex);
        for(auto& existing:movies)if(existing.id==movie.id){existing.bytes=movie.bytes;existing.title=movie.title;existing.trigger.rule=movie.trigger.rule;return;}
        movies.push_back(std::move(movie));
    }catch(const std::exception& e){std::fprintf(stderr,"FMV mod %s: %s\n",mod.manifest.mod_id.c_str(),e.what());}
}
void quest64::fmv_disable(const char* id){std::lock_guard lock(movie_mutex);for(auto& movie:movies)if(movie.id==id){movie.bytes.reset();movie.trigger.pending=false;}if(requested_id==id)active=false;}
bool quest64::fmv_active(){return active.load();}
bool quest64::fmv_blocks_input(){return active.load()||SDL_GetTicks64()<resume_guard.load();}
int quest64::fmv_preview(const char* path,double maximum_seconds){
    std::ifstream input(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.empty()){std::fprintf(stderr,"Unable to read FMV\n");return 1;}
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)<0)return 1;
    active=true;bool success=play(bytes,"Quest 64 FMV Preview",maximum_seconds);active=false;
    if(success)std::printf("FMV playback completed successfully.\n");else std::fprintf(stderr,"FMV playback failed: %s\n",SDL_GetError());
    SDL_Quit();return success?0:1;
}
void quest64::fmv_poll(){
    std::shared_ptr<std::vector<char>> bytes;std::string title;
    {std::lock_guard lock(movie_mutex);if(!request_pending)return;request_pending=false;bytes=requested_bytes;title=requested_title;}
    if(bytes&&!play(*bytes,title))std::fprintf(stderr,"FMV playback failed: %s\n",SDL_GetError());
    {std::lock_guard lock(movie_mutex);resume_guard=SDL_GetTicks64()+300;active=false;complete=true;requested_bytes.reset();requested_title.clear();requested_id.clear();}
    movie_done.notify_all();
}
extern "C" void qs64_fmv_reset(){std::lock_guard lock(movie_mutex);for(auto& movie:movies)movie.trigger.reset();}
extern "C" void qs64_fmv_tick(uint8_t* ram){
    if(read<uint16_t>(ram,0x8007B2E0)!=1||read<uint16_t>(ram,0x8007BA84)==0)return;
    const bool ready=!(read<uint32_t>(ram,0x8007B2E4)&0x408B)&&!(read<uint16_t>(ram,0x8008C592)&1)&&!(read<uint16_t>(ram,0x8007BB2C)&1);
    std::unique_lock lock(movie_mutex);
    for(auto& movie:movies)if(movie.bytes&&movie.trigger.tick(read<int32_t>(ram,0x80084EE4),read<int32_t>(ram,0x80084EE8),read<float>(ram,0x8007BACC),read<float>(ram,0x8007BAD4),ready)){
        requested_bytes=movie.bytes;requested_title=movie.title;requested_id=movie.id;complete=false;request_pending=true;active=true;
        movie_done.wait(lock,[]{return complete;});
        // Discard controls held for skipping instead of attacking on resume.
        uint16_t zero=0;std::memcpy(ram+((0x80092874&0x7fffff)^2),&zero,2);std::memcpy(ram+((0x80092876&0x7fffff)^2),&zero,2);
        return;
    }
}

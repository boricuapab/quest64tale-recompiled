#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>

namespace recomp::pak_storage {
inline std::filesystem::path profile_folder, game_folder;
inline void configure(std::filesystem::path profile, std::filesystem::path game) {
    profile_folder=std::move(profile);game_folder=std::move(game);
}
inline std::filesystem::path resolve(const std::filesystem::path& name) {
    std::error_code ec;
    auto profile=profile_folder/name;
    if(std::filesystem::is_regular_file(profile,ec))return profile;
    auto local=game_folder/name;
    if(std::filesystem::is_regular_file(local,ec))return local;
    std::filesystem::create_directories(profile_folder,ec);
    return ec?local:profile;
}
inline bool exists(const std::filesystem::path& name) {
    std::error_code ec;return std::filesystem::is_regular_file(resolve(name),ec);
}
inline void remove(const std::filesystem::path& name) {
    for(auto& folder:{profile_folder,game_folder}){
        std::error_code ec;std::filesystem::remove(folder/name,ec);
        if(ec)std::cerr<<"Controller Pak delete failed: "<<ec.message()<<'\n';
    }
}
class File:public std::fstream {
    std::filesystem::path source,name;
    bool dirty=false;
public:
    void open(const std::filesystem::path& filename,std::ios::openmode mode) {
        name=filename;source=resolve(name);dirty=(mode&std::ios::trunc)!=0;
        std::fstream::open(source,mode);
        if(!is_open()&&source!=game_folder/name){
            clear();source=game_folder/name;std::fstream::open(source,mode);
        }
    }
    std::fstream& write(const char* data,std::streamsize size) {
        dirty=true;std::fstream::write(data,size);return *this;
    }
    void close() {
        if(!is_open())return;
        flush();const bool successful=good();std::fstream::close();
        if(dirty&&successful){
            for(auto& folder:{profile_folder,game_folder}){
                auto target=folder/name;if(target==source)continue;
                std::error_code ec;std::filesystem::create_directories(folder,ec);
                if(!ec)std::filesystem::copy_file(source,target,std::filesystem::copy_options::overwrite_existing,ec);
                if(ec)std::cerr<<"Controller Pak mirror failed: "<<ec.message()<<'\n';
            }
        }
        dirty=false;
    }
    ~File(){close();}
};
}

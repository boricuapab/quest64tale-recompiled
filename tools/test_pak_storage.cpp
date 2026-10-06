#include "librecomp/pak_storage.hpp"
#include <cassert>
#include <string>
using namespace recomp::pak_storage;
std::string read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==2);const auto root=std::filesystem::path(argv[1]);
    auto profile=root/"profile",game=root/"game";
    std::filesystem::create_directories(profile);std::filesystem::create_directories(game);
    configure(profile,game);
    const auto name="controllerPak_file_0.sav";
    {std::ofstream(game/name)<<"LOCAL";std::ofstream(profile/name)<<"PROFILE";}
    assert(resolve(name)==profile/name);
    {File f;f.open(name,std::ios::binary|std::ios::in|std::ios::out);char b[7];f.read(b,7);assert(std::string(b,7)=="PROFILE");f.close();}
    assert(read(game/name)=="LOCAL"); // A read does not overwrite either existing save.
    {File f;f.open(name,std::ios::binary|std::ios::in|std::ios::out);f.seekp(0);f.write("S",1);f.close();}
    assert(read(game/name)=="SROFILE"&&read(profile/name)=="SROFILE");
    std::filesystem::remove(profile/name);
    assert(resolve(name)==game/name);
    {File f;f.open(name,std::ios::binary|std::ios::in|std::ios::out);f.seekp(1);f.write("A",1);}
    assert(read(profile/name)=="SAOFILE"&&read(game/name)=="SAOFILE");
    {File f;f.open("controllerPak_header.sav",std::ios::binary|std::ios::in|std::ios::out|std::ios::trunc);f.write("HEADER",6);f.close();}
    assert(read(profile/"controllerPak_header.sav")=="HEADER"&&read(game/"controllerPak_header.sav")=="HEADER");
    recomp::pak_storage::remove(name);assert(!exists(name));
    assert(!std::filesystem::exists(game/name)&&!std::filesystem::exists(profile/name));
    std::cout<<"PASS: profile priority, local fallback, non-destructive reads, partial writes, destructor flush, mirrored creation and deletion.\n";
}

#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace quest64 {
void register_gameplay_mods();
bool request_warp(int map, int submap, int entrance);
bool request_boss(int boss);
std::vector<std::string> boss_names();
std::string debug_status();
}

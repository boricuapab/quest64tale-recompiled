#pragma once
#include <cstddef>
#include "librecomp/mods.hpp"
namespace quest64 {
recomp::mods::ModContentTypeId register_character_content();
int validate_character_file(const char* path);
}

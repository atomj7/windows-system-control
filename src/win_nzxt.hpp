#pragma once

#include "model.hpp"

#include <cstdint>
#include <string>
#include <vector>

std::vector<std::wstring> hid_paths_for(uint16_t vid, uint16_t pid);
std::string nzxt_set_light(const std::vector<std::wstring>& paths, int channel, const Zone& zone, bool protocol2023);
std::string nzxt_set_fan(const std::vector<std::wstring>& paths, int channel, int duty);

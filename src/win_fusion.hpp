#pragma once

#include "model.hpp"

#include <string>
#include <vector>

std::string fusion_set_light(const std::vector<std::wstring>& paths, int channel, const Zone& zone);
void fusion_attach_board(Device& board, uint16_t vid, uint16_t pid);

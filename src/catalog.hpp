#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct KnownHardware {
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char* vendor = "";
  const char* name = "";
  const char* kind = "unknown";
  const char* backend = "identified";
  int rgbChannels = 0;
  int fanChannels = 0;
  bool nzxt2023 = false;
};

const KnownHardware* find_known_device(uint16_t vid, uint16_t pid);
const char* vendor_from_vid(uint16_t vid);
bool is_lighting_vendor(uint16_t vid);
bool looks_like_lighting(std::string_view name);
std::string guess_kind_from_name(std::string_view name);

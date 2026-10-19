#pragma once

#include "model.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct OrgbMode {
  std::string name;
  int value = 0;
  uint32_t flags = 0;
  uint32_t speedMin = 0;
  uint32_t speedMax = 0;
  uint32_t speed = 0;
  uint32_t brightMin = 0;
  uint32_t brightMax = 0;
  uint32_t brightness = 0;
  uint32_t colorsMin = 0;
  uint32_t colorsMax = 0;
  uint32_t direction = 0;
  uint32_t colorMode = 0;
  std::vector<Color> colors;
};

struct OrgbController {
  int index = 0;
  std::string name;
  std::string vendor;
  std::string description;
  std::string location;
  int activeMode = 0;
  std::vector<OrgbMode> modes;
  std::vector<std::string> zoneNames;
  std::vector<int> zoneLeds;
  int ledCount = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
};

// Parses an OpenRGB SDK controller-data payload at the negotiated protocol version.
// `bytes` begins at the data_size field. Versions above 4 are rejected: Helios asks
// the server to speak protocol 4 so the layout stays stable.
bool parse_orgb_controller(const uint8_t* bytes, size_t size, int protocol, OrgbController& out);
bool orgb_has_effect(const std::vector<OrgbMode>& modes, const std::string& effect);

class OpenRgbBridge {
 public:
  struct Result {
    bool connected = false;
    std::string detail;
    std::vector<OrgbController> controllers;
  };

  Result discover();
  std::string apply(const OrgbController& controller, const Zone& zone, int protocol = 4);
};

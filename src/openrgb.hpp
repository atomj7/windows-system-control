#pragma once

#include "model.hpp"
#include "net.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
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
  int protocol = 4;
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
int orgb_mode_index_for(const std::vector<OrgbMode>& modes, const std::string& effect);

class OpenRgbBridge {
 public:
  struct Result {
    bool connected = false;
    std::string detail;
    std::vector<OrgbController> controllers;
  };

  OpenRgbBridge() = default;
  ~OpenRgbBridge();
  OpenRgbBridge(const OpenRgbBridge&) = delete;
  OpenRgbBridge& operator=(const OpenRgbBridge&) = delete;

  Result discover();
  std::string apply(const OrgbController& controller, const Zone& zone, int protocol = 4);

 private:
  void disconnect();
  std::string ensure();
  std::string push(const OrgbController& controller, const Zone& zone, int protocol, bool force_mode);

  std::mutex mu_;
  Socket socket_ = kInvalidSocket;
  int protocol_ = 0;
  std::unordered_map<int, int> armed_mode_;
};

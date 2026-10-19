#pragma once

#include "json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Color {
  uint8_t r = 255;
  uint8_t g = 244;
  uint8_t b = 230;
};

struct CurvePoint {
  double temp = 30;
  double duty = 30;
};

struct Zone {
  std::string id;
  std::string name;
  int leds = 1;
  Color color{};
  std::string effect = "static";
  int speed = 50;
  int brightness = 80;
  bool controllable = true;
  std::vector<std::string> effects{"static", "breathe", "spectrum", "rainbow", "wave", "off"};
  int channel = 0;
};

struct Fan {
  std::string id;
  std::string name;
  double rpm = 0;
  double duty = 35;
  double minDuty = 20;
  bool controllable = false;
  bool rpmKnown = false;
  bool dutyKnown = true;
  bool allowStop = false;
  bool isPump = false;
  std::string mode = "curve";  // curve, manual, auto
  std::string source = "cpu_temp";
  std::vector<CurvePoint> curve;
  int channel = 0;
  double appliedDuty = -1;
  std::string lastError;
};

struct Sensor {
  std::string id;
  std::string label;
  std::string unit;
  std::string kind;  // temperature, load, power, clock, rpm
  double value = 0;
};

struct Device {
  std::string id;
  std::string name;
  std::string vendor;
  std::string kind;
  std::string bus;
  std::string connection;
  std::string hardware;  // live, profile, monitor
  std::string backend;   // sim, nzxt, nvml, openrgb, identified
  std::string note;
  std::string status;
  uint16_t vid = 0;
  uint16_t pid = 0;
  int orgbIndex = -1;
  int nvmlIndex = -1;
  bool nzxt2023 = false;
  std::string hidPath;
  std::vector<Zone> zones;
  std::vector<Fan> fans;
  std::vector<Sensor> sensors;
};

struct ScanStep {
  std::string id;
  std::string title;
  std::string detail;
  std::string status;  // ok, warn, info
};

struct DriverState {
  std::string id;
  std::string name;
  std::string state;  // ready, missing, skipped, unavailable
  std::string detail;
  std::string action;  // empty or "elevate"
};

struct BusDevice {
  std::string name;
  std::string vendor;
  uint16_t vid = 0;
  uint16_t pid = 0;
  bool promoted = false;
};

struct PlatformInfo {
  std::string mode;  // simulated, windows
  std::string computer;
  std::string detail;
};

struct Profile {
  std::string id;
  std::string name;
  double createdAt = 0;
  struct ZoneSetting {
    std::string deviceId;
    std::string zoneId;
    Color color{};
    std::string effect;
    int speed = 50;
    int brightness = 80;
  };
  struct FanSetting {
    std::string deviceId;
    std::string fanId;
    std::string mode;
    double duty = 35;
    bool allowStop = false;
    std::string source;
    std::vector<CurvePoint> curve;
  };
  std::vector<ZoneSetting> zones;
  std::vector<FanSetting> fans;
};

std::string color_hex(const Color& color);
std::optional<Color> parse_color(const std::string& text);

Json zone_json(const Zone& zone);
Json fan_json(const Fan& fan);
Json sensor_json(const Sensor& sensor);
Json device_json(const Device& device);
Json step_json(const ScanStep& step);
Json driver_json(const DriverState& driver);
Json profile_json(const Profile& profile);

const std::vector<std::string>& canonical_effects();
bool known_effect(const std::string& effect);

std::vector<CurvePoint> curve_balanced();
std::vector<CurvePoint> curve_quiet();
std::vector<CurvePoint> curve_boost();
std::vector<CurvePoint> curve_pump();

#pragma once

#include <string>
#include <vector>

struct NvmlFan {
  double rpm = 0;
  double percent = 0;
  bool rpmKnown = false;
  bool percentKnown = false;
};

struct NvmlGpu {
  int index = 0;
  std::string name;
  bool tempKnown = false;
  bool loadKnown = false;
  bool powerKnown = false;
  bool clockKnown = false;
  double temp = 0;
  double load = 0;
  double power = 0;
  double clock = 0;
  bool canControl = false;
  std::vector<NvmlFan> fans;
};

class Nvml {
 public:
  Nvml() = default;
  ~Nvml();
  Nvml(const Nvml&) = delete;
  Nvml& operator=(const Nvml&) = delete;

  bool open();
  void close();
  void abandon(std::string why);
  bool ready() const { return ready_; }
  const std::string& detail() const { return detail_; }
  std::vector<NvmlGpu> sample();
  std::string set_fan(int index, int fan, int percent);
  std::string set_auto(int index, int fan);

 private:
  bool ready_ = false;
  std::string detail_;
  void* module_ = nullptr;
  void* init_ = nullptr;
  void* shutdown_ = nullptr;
  void* count_ = nullptr;
  void* handle_ = nullptr;
  void* name_ = nullptr;
  void* temperature_ = nullptr;
  void* utilization_ = nullptr;
  void* power_ = nullptr;
  void* clock_ = nullptr;
  void* fan_speed_ = nullptr;
  void* fan_speed_v2_ = nullptr;
  void* fan_rpm_ = nullptr;
  void* fan_count_ = nullptr;
  void* set_speed_ = nullptr;
  void* set_default_ = nullptr;
};

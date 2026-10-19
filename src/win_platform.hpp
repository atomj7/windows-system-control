#pragma once

#include "platform.hpp"
#include "win_nvml.hpp"

#include <string>
#include <unordered_map>
#include <vector>

bool windows_token_is_admin();

class WindowsPlatform : public Platform {
 public:
  WindowsPlatform();
  ~WindowsPlatform() override;

  PlatformInfo info() const override;
  std::vector<Device> scan(const ScanReport& report) override;
  std::vector<DriverState> drivers() const override;
  std::vector<BusDevice> bus() const override;
  void sample(std::vector<Device>& devices) override;
  std::string set_zone(const Device& device, const Zone& zone) override;
  std::string set_fan(const Device& device, const Fan& fan) override;

 private:
  void sample_cpu();
  double cpuLoad_ = 0;
  bool cpuLoadKnown_ = false;
  void* pdhQuery_ = nullptr;
  void* pdhCounter_ = nullptr;
  Nvml nvml_;
  bool nvmlReady_ = false;
  PlatformInfo info_;
  std::vector<DriverState> drivers_;
  std::vector<BusDevice> bus_;
  std::unordered_map<std::string, std::vector<std::wstring>> hid_;
};

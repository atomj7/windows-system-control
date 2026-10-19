#pragma once

#include "platform.hpp"

#include <chrono>

class MockPlatform : public Platform {
 public:
  explicit MockPlatform(std::chrono::milliseconds stepDelay = std::chrono::milliseconds(180));

  PlatformInfo info() const override;
  std::vector<Device> scan(const ScanReport& report) override;
  std::vector<DriverState> drivers() const override;
  std::vector<BusDevice> bus() const override;
  void sample(std::vector<Device>& devices) override;
  std::string set_zone(const Device& device, const Zone& zone) override;
  std::string set_fan(const Device& device, const Fan& fan) override;

 private:
  std::chrono::milliseconds stepDelay_;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point last_ = started_;
  double cpuTemp_ = 47;
  double gpuTemp_ = 42;
  double hotspot_ = 49;
  double coolant_ = 31.5;
  double vrm_ = 39;
  double board_ = 34;
  double ssd_ = 41;
};

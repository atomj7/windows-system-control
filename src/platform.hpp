#pragma once

#include "model.hpp"

#include <functional>
#include <memory>
#include <vector>

using ScanReport = std::function<void(const ScanStep&)>;

class Platform {
 public:
  virtual ~Platform() = default;
  virtual PlatformInfo info() const = 0;
  virtual std::vector<Device> scan(const ScanReport& report) = 0;
  virtual std::vector<DriverState> drivers() const = 0;
  virtual std::vector<BusDevice> bus() const = 0;
  virtual void sample(std::vector<Device>& devices) = 0;
  virtual std::string set_zone(const Device& device, const Zone& zone) = 0;
  virtual std::string set_fan(const Device& device, const Fan& fan) = 0;
};

std::unique_ptr<Platform> make_platform(bool simulate);

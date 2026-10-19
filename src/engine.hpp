#pragma once

#include "openrgb.hpp"
#include "platform.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct Series {
  std::string id;
  std::string label;
  std::string unit;
  std::string kind;
  std::deque<std::pair<double, double>> points;
};

class Engine {
 public:
  Engine(std::unique_ptr<Platform> platform, std::string dataDir);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void start();
  void scanBlocking();
  void requestScan();
  void tick();

  Json state() const;
  Json history(int seconds) const;
  void waitForTick(uint64_t seenMetrics, uint64_t seenGeneration, int timeoutMs, uint64_t& metricsSeq, uint64_t& generation,
                   Json& payload) const;

  Json patchZone(const std::string& deviceId, const std::string& zoneId, const Json& body);
  Json patchFan(const std::string& deviceId, const std::string& fanId, const Json& body);
  Json paintDevice(const std::string& deviceId, const Json& body);
  Json paintAll(const Json& body);
  Json saveProfile(const std::string& name);
  Json applyProfile(const std::string& id);
  Json removeProfile(const std::string& id);

  bool stopped() const { return stop_; }

 private:
  struct FanCommand {
    Device device;
    Fan fan;
  };
  struct ZoneCommand {
    Device device;
    Zone zone;
  };

  void scanLoop();
  void tickLoop();
  void scanOnce();
  Json fail(const std::string& error) const;
  Json okBody() const;
  void restoreApplied();
  void captureApplied();
  void saveStore();
  void loadStore();
  void seedProfiles();
  Profile buildPreset(const std::string& name, const std::string& preset) const;
  void applyProfileUnlocked(const Profile& profile);
  std::vector<ZoneCommand> paintUnlocked(Device* only, const Json& body, std::string& error);
  std::string dispatchZone(const Device& device, const Zone& zone);
  std::string dispatchFan(const Device& device, const Fan& fan);
  void recordUnlocked(double t);
  Json metricsUnlocked() const;
  Device* findDevice(const std::string& id);
  const Sensor* findSensor(const std::string& id) const;

  std::unique_ptr<Platform> platform_;
  std::string dataDir_;
  OpenRgbBridge openrgb_;
  mutable std::mutex mu_;
  mutable std::condition_variable cv_;
  std::mutex io_;
  std::thread scanThread_;
  std::thread tickThread_;
  bool stop_ = false;
  bool scanning_ = false;
  bool rescan_ = false;
  bool loaded_ = false;
  uint64_t generation_ = 0;
  uint64_t metricsSeq_ = 0;
  double lastSample_ = 0;
  PlatformInfo info_;
  std::vector<ScanStep> steps_;
  std::vector<DriverState> drivers_;
  std::vector<BusDevice> bus_;
  std::vector<Device> devices_;
  std::vector<Profile> profiles_;
  Profile applied_;
  std::unordered_map<std::string, OrgbController> orgb_;
  std::unordered_map<std::string, Series> series_;
};

#include "ipc.hpp"

#include "log.hpp"
#include "model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

void ipc_write_atomic(const std::filesystem::path& path, const std::string& text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << text;
  }
  std::filesystem::rename(tmp, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    std::filesystem::rename(tmp, path, error);
  }
}

std::optional<Json> ipc_read_json(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.empty()) return std::nullopt;
  return parse_json(text);
}

void ipc_queue_command(const std::filesystem::path& dir, Json command) {
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  static std::atomic<uint64_t> seq{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  ipc_write_atomic(dir / ("cmd-" + std::to_string(now) + "-" + std::to_string(seq.fetch_add(1) + 1) + ".json"), command.dump());
}

std::vector<Json> ipc_take_commands(const std::filesystem::path& dir) {
  std::vector<Json> out;
  std::error_code error;
  if (!std::filesystem::exists(dir, error)) return out;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
    if (entry.path().extension() == ".json") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    if (auto json = ipc_read_json(file)) out.push_back(std::move(*json));
    std::filesystem::remove(file, error);
  }
  return out;
}

void ipc_request_scan(const std::filesystem::path& dataDir) {
  ipc_queue_command(dataDir / "commands", Json::object().set("op", jstr("scan")));
}

namespace {

std::string js(const Json* object, const char* key) {
  if (!object) return {};
  const Json* value = object->find(key);
  return value && value->is_string() ? value->as_string() : std::string{};
}

double jn(const Json* object, const char* key, double fallback = 0) {
  if (!object) return fallback;
  const Json* value = object->find(key);
  return value && value->is_number() ? value->as_number() : fallback;
}

bool jb(const Json* object, const char* key, bool fallback = false) {
  if (!object) return fallback;
  const Json* value = object->find(key);
  return value && value->is_bool() ? value->as_bool() : fallback;
}

Device device_from_json(const Json& node) {
  Device device;
  device.id = js(&node, "id");
  device.name = js(&node, "name");
  device.vendor = js(&node, "vendor");
  device.kind = js(&node, "kind");
  device.bus = js(&node, "bus");
  device.connection = js(&node, "connection");
  device.hardware = js(&node, "hardware");
  device.backend = js(&node, "backend");
  device.note = js(&node, "note");
  device.status = js(&node, "status");
  device.vid = static_cast<uint16_t>(jn(&node, "vid"));
  device.pid = static_cast<uint16_t>(jn(&node, "pid"));
  if (const Json* zones = node.find("zones"); zones && zones->is_array()) {
    for (const auto& item : zones->items()) {
      Zone zone;
      zone.id = js(&item, "id");
      zone.name = js(&item, "name");
      zone.leds = static_cast<int>(jn(&item, "leds", 1));
      if (auto color = parse_color(js(&item, "color"))) zone.color = *color;
      zone.effect = js(&item, "effect");
      if (zone.effect.empty()) zone.effect = "static";
      zone.speed = static_cast<int>(jn(&item, "speed", 50));
      zone.brightness = static_cast<int>(jn(&item, "brightness", 80));
      zone.controllable = jb(&item, "controllable", true);
      device.zones.push_back(std::move(zone));
    }
  }
  if (const Json* fans = node.find("fans"); fans && fans->is_array()) {
    for (const auto& item : fans->items()) {
      Fan fan;
      fan.id = js(&item, "id");
      fan.name = js(&item, "name");
      fan.rpm = jn(&item, "rpm");
      fan.duty = jn(&item, "duty", 35);
      fan.minDuty = jn(&item, "minDuty", 20);
      fan.controllable = jb(&item, "controllable");
      fan.rpmKnown = jb(&item, "rpmKnown");
      fan.dutyKnown = jb(&item, "dutyKnown", true);
      fan.allowStop = jb(&item, "allowStop");
      fan.isPump = jb(&item, "isPump");
      fan.mode = js(&item, "mode");
      if (fan.mode.empty()) fan.mode = "auto";
      fan.source = js(&item, "source");
      device.fans.push_back(std::move(fan));
    }
  }
  if (const Json* sensors = node.find("sensors"); sensors && sensors->is_array()) {
    for (const auto& item : sensors->items()) {
      Sensor sensor;
      sensor.id = js(&item, "id");
      sensor.label = js(&item, "label");
      sensor.unit = js(&item, "unit");
      sensor.kind = js(&item, "kind");
      sensor.value = jn(&item, "value");
      device.sensors.push_back(std::move(sensor));
    }
  }
  return device;
}

class IpcPlatform : public Platform {
 public:
  explicit IpcPlatform(std::filesystem::path dataDir) : dataDir_(std::move(dataDir)) {
    info_.mode = "windows";
    info_.computer = "Этот компьютер";
    info_.detail = "Окно Helios отдельно от модуля железа. Если NVML или USB упадут, перезапустится только модуль.";
  }

  PlatformInfo info() const override { return info_; }

  std::vector<Device> scan(const ScanReport& report) override {
    load();
    report(ScanStep{"ipc", "Модуль железа", loaded_ ? "Снимок устройств получен." : "Жду процесс helios.exe --worker.",
                    loaded_ ? "ok" : "info"});
    return devices_;
  }

  std::vector<DriverState> drivers() const override { return drivers_; }
  std::vector<BusDevice> bus() const override { return bus_; }

  void sample(std::vector<Device>& devices) override {
    load();
    for (auto& device : devices) {
      for (const auto& fresh : devices_) {
        if (fresh.id != device.id) continue;
        device.sensors = fresh.sensors;
        for (auto& fan : device.fans) {
          for (const auto& next : fresh.fans) {
            if (next.id != fan.id) continue;
            fan.rpm = next.rpm;
            fan.rpmKnown = next.rpmKnown;
            if (fan.mode == "auto") {
              fan.duty = next.duty;
              fan.dutyKnown = next.dutyKnown;
            }
          }
        }
        if (!fresh.status.empty()) device.status = fresh.status;
      }
    }
  }

  std::string set_zone(const Device& device, const Zone& zone) override {
    ipc_queue_command(dataDir_ / "commands",
                      Json::object()
                          .set("op", jstr("zone"))
                          .set("deviceId", jstr(device.id))
                          .set("zoneId", jstr(zone.id))
                          .set("color", jstr(color_hex(zone.color)))
                          .set("effect", jstr(zone.effect))
                          .set("speed", jnum(zone.speed))
                          .set("brightness", jnum(zone.brightness)));
    return {};
  }

  std::string set_fan(const Device& device, const Fan& fan) override {
    ipc_queue_command(dataDir_ / "commands", Json::object()
                                                 .set("op", jstr("fan"))
                                                 .set("deviceId", jstr(device.id))
                                                 .set("fanId", jstr(fan.id))
                                                 .set("mode", jstr(fan.mode))
                                                 .set("duty", jnum(fan.duty))
                                                 .set("allowStop", jbool(fan.allowStop))
                                                 .set("source", jstr(fan.source)));
    return {};
  }

 private:
  void load() {
    auto json = ipc_read_json(dataDir_ / "live.json");
    if (!json || !json->is_object()) {
      loaded_ = false;
      return;
    }
    loaded_ = true;
    if (const Json* platform = json->find("platform"); platform && platform->is_object()) {
      info_.computer = js(platform, "computer");
      info_.detail = js(platform, "detail");
      info_.mode = js(platform, "mode");
      if (info_.mode.empty()) info_.mode = "windows";
    }
    devices_.clear();
    if (const Json* devices = json->find("devices"); devices && devices->is_array()) {
      for (const auto& item : devices->items()) devices_.push_back(device_from_json(item));
    }
    drivers_.clear();
    if (const Json* drivers = json->find("drivers"); drivers && drivers->is_array()) {
      for (const auto& item : drivers->items()) {
        DriverState driver;
        driver.id = js(&item, "id");
        driver.name = js(&item, "name");
        driver.state = js(&item, "state");
        driver.detail = js(&item, "detail");
        drivers_.push_back(std::move(driver));
      }
    }
    bus_.clear();
    if (const Json* bus = json->find("bus"); bus && bus->is_array()) {
      for (const auto& item : bus->items()) {
        BusDevice node;
        node.name = js(&item, "name");
        node.vendor = js(&item, "vendor");
        node.vid = static_cast<uint16_t>(jn(&item, "vid"));
        node.pid = static_cast<uint16_t>(jn(&item, "pid"));
        node.promoted = jb(&item, "promoted");
        bus_.push_back(std::move(node));
      }
    }
  }

  std::filesystem::path dataDir_;
  PlatformInfo info_;
  std::vector<Device> devices_;
  std::vector<DriverState> drivers_;
  std::vector<BusDevice> bus_;
  bool loaded_ = false;
};

}  // namespace

std::unique_ptr<Platform> make_ipc_platform(std::filesystem::path dataDir) {
  return std::make_unique<IpcPlatform>(std::move(dataDir));
}

int run_hardware_worker(const std::filesystem::path& dataDir, bool simulate) {
  std::error_code error;
  std::filesystem::create_directories(dataDir, error);
  std::filesystem::remove(dataDir / "worker.stop", error);
  std::filesystem::remove(dataDir / "live.json", error);
  auto platform = make_platform(simulate);
  std::vector<Device> devices;

  auto snapshot = [&] {
    Json list = Json::array();
    for (const auto& device : devices) list.push(device_json(device));
    Json drivers = Json::array();
    for (const auto& driver : platform->drivers()) drivers.push(driver_json(driver));
    Json bus = Json::array();
    for (const auto& item : platform->bus()) {
      bus.push(Json::object()
                   .set("name", jstr(item.name))
                   .set("vendor", jstr(item.vendor))
                   .set("vid", jnum(item.vid))
                   .set("pid", jnum(item.pid))
                   .set("promoted", jbool(item.promoted)));
    }
    const auto info = platform->info();
    ipc_write_atomic(dataDir / "live.json", Json::object()
                                                .set("platform", Json::object()
                                                                     .set("mode", jstr(info.mode))
                                                                     .set("computer", jstr(info.computer))
                                                                     .set("detail", jstr(info.detail)))
                                                .set("devices", std::move(list))
                                                .set("drivers", std::move(drivers))
                                                .set("bus", std::move(bus))
                                                .dump());
  };

  auto scan = [&] {
    log_info("модуль железа: обзор");
    try {
      devices = platform->scan([](const ScanStep& step) { log_info(step.title + ": " + step.detail); });
    } catch (const std::exception& ex) {
      log_error(std::string("обзор модуля: ") + ex.what());
    } catch (...) {
      log_error("обзор модуля упал");
    }
    snapshot();
  };

  scan();
  while (!std::filesystem::exists(dataDir / "worker.stop", error)) {
    for (const auto& command : ipc_take_commands(dataDir / "commands")) {
      if (!command.is_object()) continue;
      const std::string op = js(&command, "op");
      if (op == "scan") {
        scan();
        continue;
      }
      Device* device = nullptr;
      const std::string deviceId = js(&command, "deviceId");
      for (auto& item : devices) {
        if (item.id == deviceId) device = &item;
      }
      if (!device) continue;
      if (op == "zone") {
        const std::string zoneId = js(&command, "zoneId");
        for (auto& zone : device->zones) {
          if (zone.id != zoneId) continue;
          if (auto color = parse_color(js(&command, "color"))) zone.color = *color;
          if (const std::string effect = js(&command, "effect"); !effect.empty()) zone.effect = effect;
          zone.speed = static_cast<int>(jn(&command, "speed", zone.speed));
          zone.brightness = static_cast<int>(jn(&command, "brightness", zone.brightness));
          const std::string err = platform->set_zone(*device, zone);
          if (!err.empty()) {
            device->status = err;
            log_error(err);
          }
          break;
        }
      } else if (op == "fan") {
        const std::string fanId = js(&command, "fanId");
        for (auto& fan : device->fans) {
          if (fan.id != fanId) continue;
          if (const std::string mode = js(&command, "mode"); !mode.empty()) fan.mode = mode;
          fan.duty = jn(&command, "duty", fan.duty);
          fan.allowStop = jb(&command, "allowStop", fan.allowStop);
          if (const std::string source = js(&command, "source"); !source.empty()) fan.source = source;
          const std::string err = platform->set_fan(*device, fan);
          if (!err.empty()) {
            device->status = err;
            log_error(err);
          }
          break;
        }
      }
    }
    try {
      platform->sample(devices);
    } catch (const std::exception& ex) {
      log_error(std::string("опрос модуля: ") + ex.what());
    } catch (...) {
      log_error("опрос модуля упал");
    }
    snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  log_info("модуль железа остановлен");
  return 0;
}

#ifdef _WIN32
HardwareSupervisor::HardwareSupervisor(std::filesystem::path exe, std::filesystem::path dataDir)
    : exe_(std::move(exe)), dataDir_(std::move(dataDir)) {
  job_ = CreateJobObjectW(nullptr, nullptr);
  if (job_) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
      CloseHandle(job_);
      job_ = nullptr;
    }
  }
}

HardwareSupervisor::~HardwareSupervisor() { stop(); }

bool HardwareSupervisor::alive() const {
  if (!process_ || process_ == INVALID_HANDLE_VALUE) return false;
  return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

void HardwareSupervisor::request_scan() { ipc_request_scan(dataDir_); }

void HardwareSupervisor::ensure() {
  if (alive()) return;
  if (process_) {
    CloseHandle(process_);
    process_ = nullptr;
    log_error("модуль железа остановился, запускаю снова");
    if (backoff_.count() <= 0) backoff_ = std::chrono::milliseconds(400);
    else backoff_ = std::min(backoff_ * 2, std::chrono::milliseconds(5000));
  }
  const auto now = std::chrono::steady_clock::now();
  if (backoff_.count() > 0 && lastSpawn_.time_since_epoch().count() != 0 && now - lastSpawn_ < backoff_) return;
  std::error_code error;
  std::filesystem::create_directories(dataDir_, error);
  std::filesystem::remove(dataDir_ / "worker.stop", error);
  std::wstring cmd = L"\"" + exe_.wstring() + L"\" --worker --hardware --data \"" + dataDir_.wstring() + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
  buffer.push_back(0);
  lastSpawn_ = now;
  if (!CreateProcessW(exe_.wstring().c_str(), buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                      exe_.parent_path().wstring().c_str(), &si, &pi)) {
    log_error("не удалось запустить модуль железа");
    if (backoff_.count() <= 0) backoff_ = std::chrono::milliseconds(1000);
    return;
  }
  if (job_ && !AssignProcessToJobObject(job_, pi.hProcess)) {
    log_error("модуль железа не вошёл в job: при закрытии окна его придётся останавливать явно");
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  backoff_ = std::chrono::milliseconds(400);
  log_info("модуль железа запущен");
}

void HardwareSupervisor::stop() {
  if (process_) {
    ipc_write_atomic(dataDir_ / "worker.stop", "1");
    if (WaitForSingleObject(process_, 1500) != WAIT_OBJECT_0) TerminateProcess(process_, 1);
    CloseHandle(process_);
    process_ = nullptr;
  }
  if (job_) {
    CloseHandle(job_);
    job_ = nullptr;
  }
  std::error_code error;
  std::filesystem::remove(dataDir_ / "worker.stop", error);
}
#endif

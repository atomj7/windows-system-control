#include "engine.hpp"

#include "catalog.hpp"
#include "curve.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>

namespace {

constexpr int kHistorySeconds = 20 * 60;
constexpr size_t kHistoryCap = 2400;

Json curve_json(const std::vector<CurvePoint>& curve) {
  Json out = Json::array();
  for (const auto& point : curve) out.push(Json::object().set("temp", jnum(point.temp)).set("duty", jnum(point.duty)));
  return out;
}

Json profile_store_json(const Profile& profile) {
  Json zones = Json::array();
  for (const auto& zone : profile.zones) {
    zones.push(Json::object()
                   .set("deviceId", jstr(zone.deviceId))
                   .set("zoneId", jstr(zone.zoneId))
                   .set("color", jstr(color_hex(zone.color)))
                   .set("effect", jstr(zone.effect))
                   .set("speed", jnum(zone.speed))
                   .set("brightness", jnum(zone.brightness)));
  }
  Json fans = Json::array();
  for (const auto& fan : profile.fans) {
    fans.push(Json::object()
                  .set("deviceId", jstr(fan.deviceId))
                  .set("fanId", jstr(fan.fanId))
                  .set("mode", jstr(fan.mode))
                  .set("duty", jnum(fan.duty))
                  .set("allowStop", jbool(fan.allowStop))
                  .set("source", jstr(fan.source))
                  .set("curve", curve_json(fan.curve)));
  }
  return Json::object()
      .set("id", jstr(profile.id))
      .set("name", jstr(profile.name))
      .set("createdAt", jnum(profile.createdAt))
      .set("zones", std::move(zones))
      .set("fans", std::move(fans));
}

std::vector<CurvePoint> read_curve(const Json& node, std::string& error) {
  std::vector<CurvePoint> points;
  if (!node.is_array()) {
    error = "Кривая должна быть списком точек.";
    return {};
  }
  for (const auto& item : node.items()) {
    const Json* temp = item.find("temp");
    const Json* duty = item.find("duty");
    if (!temp || !duty || !temp->is_number() || !duty->is_number()) {
      error = "У точки кривой нет температуры или скважности.";
      return {};
    }
    points.push_back(CurvePoint{temp->as_number(), duty->as_number()});
  }
  if (!valid_curve(points)) error = "Кривая: от 2 до 8 точек, температура строго растёт, долг от 0 до 100.";
  return points;
}

bool read_profile(const Json& node, Profile& profile, std::string& error) {
  const Json* name = node.find("name");
  if (!name || !name->is_string()) {
    error = "У профиля нет имени.";
    return false;
  }
  profile.name = name->as_string();
  if (const Json* id = node.find("id"); id && id->is_string()) profile.id = id->as_string();
  if (const Json* created = node.find("createdAt"); created && created->is_number()) profile.createdAt = created->as_number();
  if (const Json* zones = node.find("zones"); zones && zones->is_array()) {
    for (const auto& item : zones->items()) {
      Profile::ZoneSetting zone;
      if (const Json* v = item.find("deviceId")) zone.deviceId = v->as_string();
      if (const Json* v = item.find("zoneId")) zone.zoneId = v->as_string();
      if (const Json* v = item.find("color")) {
        if (auto color = parse_color(v->as_string())) zone.color = *color;
      }
      if (const Json* v = item.find("effect")) zone.effect = v->as_string();
      if (const Json* v = item.find("speed")) zone.speed = static_cast<int>(v->as_number());
      if (const Json* v = item.find("brightness")) zone.brightness = static_cast<int>(v->as_number());
      profile.zones.push_back(std::move(zone));
    }
  }
  if (const Json* fans = node.find("fans"); fans && fans->is_array()) {
    for (const auto& item : fans->items()) {
      Profile::FanSetting fan;
      if (const Json* v = item.find("deviceId")) fan.deviceId = v->as_string();
      if (const Json* v = item.find("fanId")) fan.fanId = v->as_string();
      if (const Json* v = item.find("mode")) fan.mode = v->as_string();
      if (const Json* v = item.find("duty")) fan.duty = v->as_number();
      if (const Json* v = item.find("allowStop")) fan.allowStop = v->as_bool();
      if (const Json* v = item.find("source")) fan.source = v->as_string();
      if (const Json* curve = item.find("curve")) {
        fan.curve = read_curve(*curve, error);
        if (!error.empty()) return false;
      }
      profile.fans.push_back(std::move(fan));
    }
  }
  return true;
}

std::vector<std::string> effects_for(const OrgbController& controller) {
  std::vector<std::string> effects;
  for (const auto& effect : canonical_effects()) {
    if (orgb_has_effect(controller.modes, effect)) effects.push_back(effect);
  }
  if (effects.empty()) effects.push_back("static");
  return effects;
}

Device device_from_orgb(const OrgbController& controller) {
  Device device;
  device.id = "orgb-" + std::to_string(controller.index);
  device.name = controller.name.empty() ? "Устройство OpenRGB" : controller.name;
  device.vendor = controller.vendor.empty() ? "OpenRGB" : controller.vendor;
  device.kind = guess_kind_from_name(controller.name + " " + controller.description);
  device.bus = "openrgb";
  device.connection = "OpenRGB SDK";
  device.hardware = "live";
  device.backend = "openrgb";
  device.orgbIndex = controller.index;
  device.vid = controller.vid;
  device.pid = controller.pid;
  device.note = "Цвет уходит в уже запущенный OpenRGB. Оборотами этот мост не управляет.";
  const auto effects = effects_for(controller);
  if (controller.zoneNames.empty()) {
    Zone zone;
    zone.id = "main";
    zone.name = "Подсветка";
    zone.leds = std::max(1, controller.ledCount);
    zone.effects = effects;
    device.zones.push_back(std::move(zone));
  } else {
    for (size_t i = 0; i < controller.zoneNames.size(); ++i) {
      Zone zone;
      zone.id = "z" + std::to_string(i);
      zone.name = controller.zoneNames[i].empty() ? "Зона " + std::to_string(i + 1) : controller.zoneNames[i];
      zone.leds = i < controller.zoneLeds.size() && controller.zoneLeds[i] > 0 ? controller.zoneLeds[i] : 1;
      zone.channel = static_cast<int>(i);
      zone.effects = effects;
      device.zones.push_back(std::move(zone));
    }
  }
  return device;
}

bool same_usb(const Device& device, const OrgbController& controller) {
  return device.vid != 0 && device.vid == controller.vid && device.pid == controller.pid;
}

}  // namespace

Engine::Engine(std::unique_ptr<Platform> platform, std::string dataDir)
    : platform_(std::move(platform)), dataDir_(std::move(dataDir)) {
  info_ = platform_->info();
  loadStore();
}

Engine::~Engine() {
  stop_ = true;
  cv_.notify_all();
  if (scanThread_.joinable()) scanThread_.join();
  if (tickThread_.joinable()) tickThread_.join();
}

void Engine::start() {
  scanThread_ = std::thread([this] { scanLoop(); });
  tickThread_ = std::thread([this] { tickLoop(); });
}

void Engine::scanLoop() {
  do {
    scanOnce();
    std::unique_lock lock(mu_);
    cv_.wait(lock, [&] { return stop_ || rescan_; });
    if (stop_) return;
  } while (!stop_);
}

void Engine::tickLoop() {
  while (!stop_) {
    tick();
    for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void Engine::requestScan() {
  {
    std::lock_guard lock(mu_);
    rescan_ = true;
  }
  cv_.notify_all();
}

void Engine::scanBlocking() { scanOnce(); }

void Engine::scanOnce() {
  {
    std::lock_guard lock(mu_);
    if (scanning_) return;
    scanning_ = true;
    rescan_ = false;
    steps_.clear();
    generation_++;
  }
  cv_.notify_all();

  auto report = [&](const ScanStep& step) {
    std::lock_guard lock(mu_);
    steps_.push_back(step);
    generation_++;
    cv_.notify_all();
  };

  std::vector<Device> scanned;
  {
    std::lock_guard io(io_);
    scanned = platform_->scan(report);
  }
  auto bridge = openrgb_.discover();
  report(ScanStep{"openrgb", "Мост OpenRGB", bridge.detail, bridge.connected ? "ok" : "info"});

  std::vector<Device> merged = std::move(scanned);
  std::unordered_map<std::string, OrgbController> bindings;
  for (auto& controller : bridge.controllers) {
    Device* matched = nullptr;
    for (auto& device : merged) {
      if (device.backend == "nzxt" || device.backend == "sim") continue;
      if (same_usb(device, controller) && bindings.find(device.id) == bindings.end()) {
        matched = &device;
        break;
      }
    }
    if (matched) {
      matched->backend = "openrgb";
      matched->hardware = "live";
      matched->orgbIndex = controller.index;
      if (matched->zones.empty()) {
        Device donor = device_from_orgb(controller);
        matched->zones = std::move(donor.zones);
      } else {
        const auto effects = effects_for(controller);
        for (auto& zone : matched->zones) zone.effects = effects;
      }
      matched->note = "Опознан на шине и управляется через OpenRGB.";
      bindings[matched->id] = controller;
    } else {
      Device device = device_from_orgb(controller);
      bindings[device.id] = controller;
      merged.push_back(std::move(device));
    }
  }

  std::vector<DriverState> drivers = platform_->drivers();
  drivers.push_back(DriverState{"openrgb", "OpenRGB", bridge.connected ? "ready" : "skipped", bridge.detail});

  {
    std::lock_guard lock(mu_);
    devices_ = std::move(merged);
    orgb_ = std::move(bindings);
    drivers_ = std::move(drivers);
    bus_ = platform_->bus();
    info_ = platform_->info();
    restoreApplied();
    if (!loaded_) seedProfiles();
    scanning_ = false;
    generation_++;
  }
  cv_.notify_all();
  saveStore();
}

Json Engine::fail(const std::string& error) const {
  return Json::object().set("ok", jbool(false)).set("error", jstr(error));
}

Json Engine::okBody() const {
  return Json::object().set("ok", jbool(true)).set("generation", jnum(static_cast<double>(generation_)));
}

Device* Engine::findDevice(const std::string& id) {
  for (auto& device : devices_) {
    if (device.id == id) return &device;
  }
  return nullptr;
}

const Sensor* Engine::findSensor(const std::string& id) const {
  for (const auto& device : devices_) {
    for (const auto& sensor : device.sensors) {
      if (sensor.id == id) return &sensor;
    }
  }
  return nullptr;
}

void Engine::tick() {
  std::vector<FanCommand> commands;
  {
    std::lock_guard lock(mu_);
    if (scanning_ || devices_.empty()) return;
    for (auto& device : devices_) {
      for (auto& fan : device.fans) {
        if (!fan.controllable || fan.mode != "curve") continue;
        const Sensor* sensor = findSensor(fan.source);
        if (!sensor || sensor->kind != "temperature") continue;
        const double duty = clamp_duty(fan, duty_for_temperature(fan.curve, sensor->value));
        fan.duty = duty;
        if (fan.appliedDuty >= 0 && std::abs(duty - fan.appliedDuty) < 1.0) continue;
        fan.appliedDuty = duty;
        commands.push_back(FanCommand{device, fan});
      }
    }
  }

  struct Outcome {
    std::string deviceId;
    std::string fanId;
    std::string error;
  };
  std::vector<Outcome> outcomes;
  for (const auto& command : commands) {
    outcomes.push_back(Outcome{command.device.id, command.fan.id, dispatchFan(command.device, command.fan)});
  }

  std::lock_guard io(io_);
  std::lock_guard lock(mu_);
  if (scanning_) return;
  platform_->sample(devices_);
  const double t = now_seconds();
  recordUnlocked(t);
  lastSample_ = t;
  metricsSeq_++;
  for (const auto& outcome : outcomes) {
    Device* device = findDevice(outcome.deviceId);
    if (!device) continue;
    for (auto& fan : device->fans) {
      if (fan.id != outcome.fanId) continue;
      if (fan.lastError == outcome.error) break;
      fan.lastError = outcome.error;
      if (!outcome.error.empty()) device->status = outcome.error;
      generation_++;
      break;
    }
  }
  cv_.notify_all();
}

void Engine::recordUnlocked(double t) {
  auto push = [&](const std::string& id, const std::string& label, const std::string& unit, const std::string& kind, double value) {
    Series& series = series_[id];
    series.id = id;
    series.label = label;
    series.unit = unit;
    series.kind = kind;
    series.points.emplace_back(t, value);
    const double cutoff = t - kHistorySeconds;
    while (series.points.size() > kHistoryCap || (!series.points.empty() && series.points.front().first < cutoff)) {
      series.points.pop_front();
    }
  };
  for (const auto& device : devices_) {
    for (const auto& sensor : device.sensors) push(sensor.id, sensor.label, sensor.unit, sensor.kind, sensor.value);
    for (const auto& fan : device.fans) {
      if (!fan.rpmKnown) continue;
      push("fan:" + device.id + "/" + fan.id, device.name + " · " + fan.name, "об/мин", "rpm", fan.rpm);
    }
  }
}

Json Engine::metricsUnlocked() const {
  Json sensors = Json::array();
  Json fans = Json::array();
  for (const auto& device : devices_) {
    for (const auto& sensor : device.sensors) {
      const int digits = sensor.kind == "temperature" ? 1 : 0;
      sensors.push(Json::object().set("id", jstr(sensor.id)).set("value", jnum(round_to(sensor.value, digits))));
    }
    for (const auto& fan : device.fans) {
      fans.push(Json::object()
                    .set("deviceId", jstr(device.id))
                    .set("id", jstr(fan.id))
                    .set("rpm", jnum(round_to(fan.rpm, 0)))
                    .set("duty", jnum(round_to(fan.duty, 0)))
                    .set("rpmKnown", jbool(fan.rpmKnown)));
    }
  }
  return Json::object()
      .set("t", jnum(lastSample_))
      .set("generation", jnum(static_cast<double>(generation_)))
      .set("metrics", jnum(static_cast<double>(metricsSeq_)))
      .set("scanning", jbool(scanning_))
      .set("sensors", std::move(sensors))
      .set("fans", std::move(fans));
}

Json Engine::state() const {
  std::lock_guard lock(mu_);
  Json steps = Json::array();
  Json drivers = Json::array();
  Json devices = Json::array();
  Json profiles = Json::array();
  Json bus = Json::array();
  for (const auto& step : steps_) steps.push(step_json(step));
  for (const auto& driver : drivers_) drivers.push(driver_json(driver));
  for (const auto& device : devices_) devices.push(device_json(device));
  for (const auto& profile : profiles_) profiles.push(profile_json(profile));
  for (const auto& item : bus_) {
    bus.push(Json::object()
                 .set("name", jstr(item.name))
                 .set("vendor", jstr(item.vendor))
                 .set("vid", jnum(item.vid))
                 .set("pid", jnum(item.pid))
                 .set("promoted", jbool(item.promoted)));
  }
  return Json::object()
      .set("generation", jnum(static_cast<double>(generation_)))
      .set("scanning", jbool(scanning_))
      .set("platform", Json::object().set("mode", jstr(info_.mode)).set("computer", jstr(info_.computer)).set("detail", jstr(info_.detail)))
      .set("steps", std::move(steps))
      .set("drivers", std::move(drivers))
      .set("devices", std::move(devices))
      .set("profiles", std::move(profiles))
      .set("bus", std::move(bus));
}

Json Engine::history(int seconds) const {
  seconds = std::clamp(seconds, 15, kHistorySeconds);
  std::lock_guard lock(mu_);
  const double cutoff = lastSample_ - seconds;
  Json series = Json::array();
  for (const auto& [id, item] : series_) {
    (void)id;
    std::vector<std::pair<double, double>> window;
    for (const auto& point : item.points) {
      if (point.first >= cutoff) window.push_back(point);
    }
    if (window.size() > 240) {
      std::vector<std::pair<double, double>> reduced;
      const double step = static_cast<double>(window.size()) / 240.0;
      for (double cursor = 0; cursor < static_cast<double>(window.size()); cursor += step) {
        reduced.push_back(window[static_cast<size_t>(cursor)]);
      }
      window.swap(reduced);
    }
    Json points = Json::array();
    for (const auto& point : window) points.push(Json::array({jnum(point.first), jnum(round_to(point.second, item.kind == "temperature" ? 1 : 0))}));
    series.push(Json::object()
                    .set("id", jstr(item.id))
                    .set("label", jstr(item.label))
                    .set("unit", jstr(item.unit))
                    .set("kind", jstr(item.kind))
                    .set("points", std::move(points)));
  }
  return Json::object().set("seconds", jnum(seconds)).set("series", std::move(series));
}

void Engine::waitForTick(uint64_t seenMetrics, uint64_t seenGeneration, int timeoutMs, uint64_t& metricsSeq,
                         uint64_t& generation, Json& payload) const {
  std::unique_lock lock(mu_);
  cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
    return stop_ || metricsSeq_ != seenMetrics || generation_ != seenGeneration;
  });
  metricsSeq = metricsSeq_;
  generation = generation_;
  payload = metricsUnlocked();
}

std::string Engine::dispatchZone(const Device& device, const Zone& zone) {
  if (device.hardware == "monitor" || !zone.controllable) return {};
  if (device.backend == "openrgb") {
    OrgbController controller;
    {
      std::lock_guard state(mu_);
      const auto found = orgb_.find(device.id);
      if (found == orgb_.end()) return "Устройство OpenRGB пропало. Пересканируй.";
      controller = found->second;
    }
    std::lock_guard lock(io_);
    return openrgb_.apply(controller, zone);
  }
  if (device.hardware == "profile" && device.backend != "sim") return {};
  std::lock_guard lock(io_);
  return platform_->set_zone(device, zone);
}

std::string Engine::dispatchFan(const Device& device, const Fan& fan) {
  if (!fan.controllable) return {};
  std::lock_guard lock(io_);
  return platform_->set_fan(device, fan);
}

namespace {

bool apply_zone_body(Zone& zone, const Json& body, std::string& error) {
  if (const Json* color = body.find("color")) {
    if (!color->is_string()) {
      error = "Цвет нужен в виде #rrggbb.";
      return false;
    }
    auto parsed = parse_color(color->as_string());
    if (!parsed) {
      error = "Цвет нужен в виде #rrggbb.";
      return false;
    }
    zone.color = *parsed;
  }
  if (const Json* effect = body.find("effect")) {
    if (!effect->is_string() || !known_effect(effect->as_string())) {
      error = "Неизвестный эффект.";
      return false;
    }
    const std::string name = effect->as_string();
    const bool allowed = std::find(zone.effects.begin(), zone.effects.end(), name) != zone.effects.end();
    if (!allowed) {
      error = "Этот эффект устройство не умеет.";
      return false;
    }
    zone.effect = name;
  }
  if (const Json* speed = body.find("speed")) {
    if (!speed->is_number()) {
      error = "Скорость эффекта — число от 0 до 100.";
      return false;
    }
    zone.speed = static_cast<int>(clamp_double(speed->as_number(), 0, 100));
  }
  if (const Json* brightness = body.find("brightness")) {
    if (!brightness->is_number()) {
      error = "Яркость — число от 0 до 100.";
      return false;
    }
    zone.brightness = static_cast<int>(clamp_double(brightness->as_number(), 0, 100));
  }
  return true;
}

}  // namespace

std::vector<Engine::ZoneCommand> Engine::paintUnlocked(Device* only, const Json& body, std::string& error) {
  std::vector<ZoneCommand> commands;
  for (auto& device : devices_) {
    if (only && device.id != only->id) continue;
    if (device.hardware == "monitor") continue;
    for (auto& zone : device.zones) {
      if (!zone.controllable) continue;
      if (!apply_zone_body(zone, body, error)) return {};
      commands.push_back(ZoneCommand{device, zone});
    }
  }
  if (commands.empty() && error.empty()) error = "Нет зон, которые можно покрасить.";
  return commands;
}

Json Engine::patchZone(const std::string& deviceId, const std::string& zoneId, const Json& body) {
  std::vector<ZoneCommand> commands;
  {
    std::lock_guard lock(mu_);
    Device* device = findDevice(deviceId);
    if (!device) return fail("Устройство не найдено.");
    Zone* zone = nullptr;
    for (auto& item : device->zones) {
      if (item.id == zoneId) zone = &item;
    }
    if (!zone) return fail("Зона не найдена.");
    std::string error;
    if (!apply_zone_body(*zone, body, error)) return fail(error);
    commands.push_back(ZoneCommand{*device, *zone});
    captureApplied();
    generation_++;
  }
  cv_.notify_all();
  std::string error;
  for (const auto& command : commands) {
    error = dispatchZone(command.device, command.zone);
    if (!error.empty()) break;
  }
  {
    std::lock_guard lock(mu_);
    if (Device* device = findDevice(deviceId)) device->status = error;
    if (!error.empty()) generation_++;
  }
  saveStore();
  if (!error.empty()) return fail(error);
  std::lock_guard lock(mu_);
  return okBody();
}

Json Engine::patchFan(const std::string& deviceId, const std::string& fanId, const Json& body) {
  FanCommand command;
  bool shouldSend = false;
  {
    std::lock_guard lock(mu_);
    Device* device = findDevice(deviceId);
    if (!device) return fail("Устройство не найдено.");
    Fan* fan = nullptr;
    for (auto& item : device->fans) {
      if (item.id == fanId) fan = &item;
    }
    if (!fan) return fail("Вентилятор не найден.");
    if (const Json* mode = body.find("mode")) {
      const std::string value = mode->as_string();
      if (value != "curve" && value != "manual" && value != "auto") return fail("Режим: curve, manual или auto.");
      fan->mode = value;
    }
    if (const Json* source = body.find("source")) {
      const std::string value = source->as_string();
      const Sensor* sensor = findSensor(value);
      if (!sensor || sensor->kind != "temperature") return fail("Источник кривой должен быть датчиком температуры.");
      fan->source = value;
    }
    if (const Json* allow = body.find("allowStop")) fan->allowStop = allow->as_bool();
    if (const Json* curve = body.find("curve")) {
      std::string error;
      auto points = read_curve(*curve, error);
      if (!error.empty()) return fail(error);
      fan->curve = std::move(points);
    }
    if (const Json* duty = body.find("duty")) {
      if (!duty->is_number()) return fail("Скважность — число от 0 до 100.");
      fan->duty = clamp_duty(*fan, duty->as_number());
    }
    if (fan->mode == "curve") {
      if (const Sensor* sensor = findSensor(fan->source)) fan->duty = clamp_duty(*fan, duty_for_temperature(fan->curve, sensor->value));
    } else {
      fan->duty = clamp_duty(*fan, fan->duty);
    }
    fan->appliedDuty = fan->duty;
    shouldSend = fan->controllable && fan->mode != "curve";
    command = FanCommand{*device, *fan};
    captureApplied();
    generation_++;
  }
  cv_.notify_all();
  std::string error = shouldSend ? dispatchFan(command.device, command.fan) : "";
  {
    std::lock_guard lock(mu_);
    if (Device* device = findDevice(deviceId)) {
      device->status = error;
      for (auto& fan : device->fans) {
        if (fan.id == fanId) fan.lastError = error;
      }
    }
  }
  saveStore();
  if (!error.empty()) return fail(error);
  std::lock_guard lock(mu_);
  return okBody();
}

Json Engine::paintDevice(const std::string& deviceId, const Json& body) {
  std::vector<ZoneCommand> commands;
  std::string error;
  {
    std::lock_guard lock(mu_);
    Device* device = findDevice(deviceId);
    if (!device) return fail("Устройство не найдено.");
    commands = paintUnlocked(device, body, error);
    if (!error.empty()) return fail(error);
    captureApplied();
    generation_++;
  }
  cv_.notify_all();
  for (const auto& command : commands) {
    const std::string step = dispatchZone(command.device, command.zone);
    if (!step.empty()) error = step;
  }
  {
    std::lock_guard lock(mu_);
    if (Device* device = findDevice(deviceId)) device->status = error;
  }
  saveStore();
  if (!error.empty()) return fail(error);
  std::lock_guard lock(mu_);
  return okBody();
}

Json Engine::paintAll(const Json& body) {
  std::vector<ZoneCommand> commands;
  std::string error;
  {
    std::lock_guard lock(mu_);
    commands = paintUnlocked(nullptr, body, error);
    if (!error.empty()) return fail(error);
    captureApplied();
    generation_++;
  }
  cv_.notify_all();
  for (const auto& command : commands) {
    const std::string step = dispatchZone(command.device, command.zone);
    if (!step.empty()) error = step;
  }
  saveStore();
  if (!error.empty()) return fail(error);
  std::lock_guard lock(mu_);
  return okBody();
}

void Engine::applyProfileUnlocked(const Profile& profile) {
  for (const auto& setting : profile.zones) {
    Device* device = findDevice(setting.deviceId);
    if (!device) continue;
    for (auto& zone : device->zones) {
      if (zone.id != setting.zoneId) continue;
      zone.color = setting.color;
      if (std::find(zone.effects.begin(), zone.effects.end(), setting.effect) != zone.effects.end()) zone.effect = setting.effect;
      zone.speed = setting.speed;
      zone.brightness = setting.brightness;
    }
  }
  for (const auto& setting : profile.fans) {
    Device* device = findDevice(setting.deviceId);
    if (!device) continue;
    for (auto& fan : device->fans) {
      if (fan.id != setting.fanId) continue;
      fan.mode = setting.mode.empty() ? fan.mode : setting.mode;
      fan.allowStop = setting.allowStop;
      if (!setting.source.empty()) fan.source = setting.source;
      if (!setting.curve.empty()) fan.curve = setting.curve;
      fan.duty = clamp_duty(fan, setting.mode == "curve" && findSensor(fan.source)
                                     ? duty_for_temperature(fan.curve, findSensor(fan.source)->value)
                                     : setting.duty);
      fan.appliedDuty = -1;
    }
  }
}

Json Engine::saveProfile(const std::string& name) {
  std::string trimmed;
  for (char c : name) {
    if (c != '\n' && c != '\r') trimmed.push_back(c);
  }
  while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
  while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
  if (trimmed.empty()) return fail("У профиля нужно имя.");
  if (trimmed.size() > 40) trimmed.resize(40);
  {
    std::lock_guard lock(mu_);
    Profile profile = buildPreset(trimmed, "current");
    profile.id = "p" + std::to_string(static_cast<long long>(now_seconds() * 1000.0));
    profile.createdAt = now_seconds();
    profiles_.push_back(std::move(profile));
    generation_++;
  }
  cv_.notify_all();
  saveStore();
  std::lock_guard lock(mu_);
  return okBody();
}

Json Engine::applyProfile(const std::string& id) {
  std::vector<ZoneCommand> zones;
  std::vector<FanCommand> fans;
  {
    std::lock_guard lock(mu_);
    const Profile* profile = nullptr;
    for (const auto& item : profiles_) {
      if (item.id == id) profile = &item;
    }
    if (!profile) return fail("Профиль не найден.");
    applyProfileUnlocked(*profile);
    for (auto& device : devices_) {
      for (auto& zone : device.zones) {
        if (zone.controllable && device.hardware != "monitor") zones.push_back(ZoneCommand{device, zone});
      }
      for (auto& fan : device.fans) {
        if (!fan.controllable || fan.mode == "curve") continue;
        fan.appliedDuty = fan.duty;
        fans.push_back(FanCommand{device, fan});
      }
    }
    captureApplied();
    generation_++;
  }
  cv_.notify_all();
  std::string error;
  for (const auto& command : zones) {
    const std::string step = dispatchZone(command.device, command.zone);
    if (!step.empty()) error = step;
  }
  for (const auto& command : fans) {
    const std::string step = dispatchFan(command.device, command.fan);
    if (!step.empty()) error = step;
  }
  saveStore();
  if (!error.empty()) return fail(error);
  std::lock_guard lock(mu_);
  return okBody();
}

Json Engine::removeProfile(const std::string& id) {
  {
    std::lock_guard lock(mu_);
    const auto before = profiles_.size();
    profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const Profile& profile) { return profile.id == id; }),
                    profiles_.end());
    if (profiles_.size() == before) return fail("Профиль не найден.");
    generation_++;
  }
  cv_.notify_all();
  saveStore();
  std::lock_guard lock(mu_);
  return okBody();
}

Profile Engine::buildPreset(const std::string& name, const std::string& preset) const {
  Profile profile;
  profile.name = name;
  const bool quiet = preset == "quiet";
  const bool current = preset == "current";
  for (const auto& device : devices_) {
    for (const auto& zone : device.zones) {
      if (!zone.controllable) continue;
      Profile::ZoneSetting setting;
      setting.deviceId = device.id;
      setting.zoneId = zone.id;
      if (current) {
        setting.color = zone.color;
        setting.effect = zone.effect;
        setting.speed = zone.speed;
        setting.brightness = zone.brightness;
      } else if (quiet) {
        setting.color = Color{201, 132, 74};
        setting.effect = std::find(zone.effects.begin(), zone.effects.end(), "breathe") != zone.effects.end() ? "breathe" : "static";
        setting.speed = 28;
        setting.brightness = 34;
      } else {
        setting.color = Color{244, 241, 234};
        setting.effect = "static";
        setting.speed = 50;
        setting.brightness = 88;
      }
      profile.zones.push_back(std::move(setting));
    }
    for (const auto& fan : device.fans) {
      if (!fan.controllable) continue;
      Profile::FanSetting setting;
      setting.deviceId = device.id;
      setting.fanId = fan.id;
      setting.mode = "curve";
      setting.allowStop = false;
      setting.source = fan.source;
      if (current) {
        setting.mode = fan.mode;
        setting.allowStop = fan.allowStop;
        setting.duty = fan.duty;
        setting.curve = fan.curve;
      } else if (fan.isPump) {
        setting.curve = curve_pump();
        setting.duty = 60;
      } else if (quiet) {
        setting.curve = curve_quiet();
        setting.duty = 30;
      } else {
        setting.curve = curve_boost();
        setting.duty = 55;
      }
      profile.fans.push_back(std::move(setting));
    }
  }
  return profile;
}

void Engine::seedProfiles() {
  Profile quiet = buildPreset("Тихий", "quiet");
  quiet.id = "quiet";
  quiet.createdAt = now_seconds();
  Profile load = buildPreset("Нагрузка", "load");
  load.id = "load";
  load.createdAt = now_seconds();
  profiles_.push_back(std::move(quiet));
  profiles_.push_back(std::move(load));
  loaded_ = true;
}

void Engine::captureApplied() {
  applied_ = buildPreset("applied", "current");
  applied_.id = "applied";
}

void Engine::restoreApplied() {
  if (applied_.zones.empty() && applied_.fans.empty()) return;
  applyProfileUnlocked(applied_);
}

void Engine::loadStore() {
  const auto path = std::filesystem::path(dataDir_) / "store.json";
  std::ifstream input(path);
  if (!input) return;
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  auto json = parse_json(text);
  if (!json || !json->is_object()) return;
  std::string error;
  if (const Json* profiles = json->find("profiles"); profiles && profiles->is_array()) {
    for (const auto& item : profiles->items()) {
      Profile profile;
      error.clear();
      if (read_profile(item, profile, error)) profiles_.push_back(std::move(profile));
    }
  }
  if (const Json* applied = json->find("applied")) {
    error.clear();
    read_profile(*applied, applied_, error);
  }
  loaded_ = true;
}

void Engine::saveStore() {
  Profile applied;
  std::vector<Profile> profiles;
  {
    std::lock_guard lock(mu_);
    applied = applied_;
    profiles = profiles_;
    loaded_ = true;
  }
  std::error_code error;
  std::filesystem::create_directories(dataDir_, error);
  Json list = Json::array();
  for (const auto& profile : profiles) list.push(profile_store_json(profile));
  Json root = Json::object().set("profiles", std::move(list)).set("applied", profile_store_json(applied));
  const auto path = std::filesystem::path(dataDir_) / "store.json";
  const auto temporary = std::filesystem::path(dataDir_) / "store.json.tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return;
    output << root.dump();
  }
  std::filesystem::rename(temporary, path, error);
}

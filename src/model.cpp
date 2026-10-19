#include "model.hpp"

#include "util.hpp"

#include <optional>

std::string color_hex(const Color& color) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g, color.b);
  return buffer;
}

std::optional<Color> parse_color(const std::string& text) {
  if (text.size() != 7 || text[0] != '#') return std::nullopt;
  for (size_t i = 1; i < text.size(); ++i) {
    const char c = text[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) return std::nullopt;
  }
  auto byte = [&](size_t index) { return static_cast<uint8_t>(std::stoi(text.substr(index, 2), nullptr, 16)); };
  return Color{byte(1), byte(3), byte(5)};
}

const std::vector<std::string>& canonical_effects() {
  static const std::vector<std::string> effects{"static", "breathe", "spectrum", "rainbow", "wave", "off"};
  return effects;
}

bool known_effect(const std::string& effect) {
  for (const auto& item : canonical_effects()) {
    if (item == effect) return true;
  }
  return false;
}

std::vector<CurvePoint> curve_balanced() { return {{30, 28}, {48, 38}, {62, 55}, {74, 74}, {86, 100}}; }

std::vector<CurvePoint> curve_quiet() { return {{35, 22}, {55, 30}, {68, 42}, {80, 68}, {90, 100}}; }

std::vector<CurvePoint> curve_boost() { return {{28, 40}, {45, 58}, {60, 78}, {72, 100}}; }

std::vector<CurvePoint> curve_pump() { return {{25, 55}, {34, 68}, {42, 84}, {50, 100}}; }

Json zone_json(const Zone& zone) {
  Json effects = Json::array();
  for (const auto& effect : zone.effects) effects.push(jstr(effect));
  return Json::object()
      .set("id", jstr(zone.id))
      .set("name", jstr(zone.name))
      .set("leds", jnum(zone.leds))
      .set("color", jstr(color_hex(zone.color)))
      .set("effect", jstr(zone.effect))
      .set("speed", jnum(zone.speed))
      .set("brightness", jnum(zone.brightness))
      .set("controllable", jbool(zone.controllable))
      .set("effects", std::move(effects));
}

Json fan_json(const Fan& fan) {
  Json curve = Json::array();
  for (const auto& point : fan.curve) {
    curve.push(Json::object().set("temp", jnum(point.temp)).set("duty", jnum(point.duty)));
  }
  return Json::object()
      .set("id", jstr(fan.id))
      .set("name", jstr(fan.name))
      .set("rpm", jnum(round_to(fan.rpm, 0)))
      .set("duty", jnum(round_to(fan.duty, 0)))
      .set("minDuty", jnum(fan.minDuty))
      .set("controllable", jbool(fan.controllable))
      .set("rpmKnown", jbool(fan.rpmKnown))
      .set("dutyKnown", jbool(fan.dutyKnown))
      .set("allowStop", jbool(fan.allowStop))
      .set("isPump", jbool(fan.isPump))
      .set("mode", jstr(fan.mode))
      .set("source", jstr(fan.source))
      .set("curve", std::move(curve));
}

Json sensor_json(const Sensor& sensor) {
  const int digits = sensor.kind == "temperature" ? 1 : 0;
  return Json::object()
      .set("id", jstr(sensor.id))
      .set("label", jstr(sensor.label))
      .set("unit", jstr(sensor.unit))
      .set("kind", jstr(sensor.kind))
      .set("value", jnum(round_to(sensor.value, digits)));
}

Json device_json(const Device& device) {
  Json zones = Json::array();
  Json fans = Json::array();
  Json sensors = Json::array();
  for (const auto& zone : device.zones) zones.push(zone_json(zone));
  for (const auto& fan : device.fans) fans.push(fan_json(fan));
  for (const auto& sensor : device.sensors) sensors.push(sensor_json(sensor));
  return Json::object()
      .set("id", jstr(device.id))
      .set("name", jstr(device.name))
      .set("vendor", jstr(device.vendor))
      .set("kind", jstr(device.kind))
      .set("bus", jstr(device.bus))
      .set("connection", jstr(device.connection))
      .set("hardware", jstr(device.hardware))
      .set("backend", jstr(device.backend))
      .set("note", jstr(device.note))
      .set("status", jstr(device.status))
      .set("vid", jnum(device.vid))
      .set("pid", jnum(device.pid))
      .set("zones", std::move(zones))
      .set("fans", std::move(fans))
      .set("sensors", std::move(sensors));
}

Json step_json(const ScanStep& step) {
  return Json::object()
      .set("id", jstr(step.id))
      .set("title", jstr(step.title))
      .set("detail", jstr(step.detail))
      .set("status", jstr(step.status));
}

Json driver_json(const DriverState& driver) {
  return Json::object()
      .set("id", jstr(driver.id))
      .set("name", jstr(driver.name))
      .set("state", jstr(driver.state))
      .set("detail", jstr(driver.detail));
}

Json profile_json(const Profile& profile) {
  return Json::object()
      .set("id", jstr(profile.id))
      .set("name", jstr(profile.name))
      .set("createdAt", jnum(profile.createdAt))
      .set("zones", jnum(static_cast<double>(profile.zones.size())))
      .set("fans", jnum(static_cast<double>(profile.fans.size())));
}

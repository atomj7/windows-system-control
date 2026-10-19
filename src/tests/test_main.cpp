#include "catalog.hpp"
#include "curve.hpp"
#include "engine.hpp"
#include "ipc.hpp"
#include "json.hpp"
#include "mock_platform.hpp"
#include "model.hpp"
#include "openrgb.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL " << message << "\n";
  ++failures;
}

void test_curve() {
  const auto curve = curve_balanced();
  expect(std::abs(duty_for_temperature(curve, 0) - curve.front().duty) < 0.01, "curve below first point");
  expect(std::abs(duty_for_temperature(curve, 200) - curve.back().duty) < 0.01, "curve above last point");
  const double mid = duty_for_temperature(curve, (curve[0].temp + curve[1].temp) / 2.0);
  expect(mid > curve[0].duty && mid < curve[1].duty, "curve interpolates");
  Fan fan;
  fan.minDuty = 20;
  fan.allowStop = false;
  expect(clamp_duty(fan, 5) == 20, "duty floor");
  fan.allowStop = true;
  expect(clamp_duty(fan, 5) == 5, "allow stop");
  expect(!valid_curve({{30, 10}, {20, 20}}), "unsorted curve rejected");
}

void test_json() {
  auto parsed = parse_json(R"({"name":"Тихий","duty":35,"on":true,"pts":[1,2]})");
  expect(parsed.has_value(), "json parses");
  if (!parsed) return;
  expect(parsed->find("name") && parsed->find("name")->as_string() == "Тихий", "json string");
  expect(parsed->find("duty") && parsed->find("duty")->as_number() == 35, "json number");
  expect(parsed->find("on") && parsed->find("on")->as_bool(), "json bool");
  expect(parsed->find("pts") && parsed->find("pts")->items().size() == 2, "json array");
  auto again = parse_json(parsed->dump());
  expect(again && again->find("name") && again->find("name")->as_string() == "Тихий", "json roundtrip");
  const Json stamp = jnum(1792400123.456);
  auto stamped = parse_json(stamp.dump());
  expect(stamped && std::abs(stamped->as_number() - 1792400123.456) < 0.001, "json keeps timestamp precision");
  const Json driver = driver_json(DriverState{"pawnio", "PawnIO", "unavailable", "нет прав", "elevate"});
  expect(driver.find("action") && driver.find("action")->as_string() == "elevate", "driver action serialize");
}

void write_u16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void write_str(std::vector<uint8_t>& out, const std::string& value) {
  const std::string terminated = value + '\0';
  write_u16(out, static_cast<uint16_t>(terminated.size()));
  out.insert(out.end(), terminated.begin(), terminated.end());
}

void test_orgb() {
  std::vector<uint8_t> bytes;
  write_u32(bytes, 0);
  write_u32(bytes, 1);
  write_str(bytes, "Kraken Elite");
  write_str(bytes, "NZXT");
  write_str(bytes, "cooler");
  write_str(bytes, "1.0");
  write_str(bytes, "");
  write_str(bytes, "hid:vid_1e71&pid_3012");
  write_u16(bytes, 1);
  write_u32(bytes, 0);
  write_str(bytes, "Direct");
  write_u32(bytes, 0);
  write_u32(bytes, 0);
  write_u32(bytes, 0);
  write_u32(bytes, 100);
  write_u32(bytes, 0);
  write_u32(bytes, 100);
  write_u32(bytes, 0);
  write_u32(bytes, 1);
  write_u32(bytes, 40);
  write_u32(bytes, 80);
  write_u32(bytes, 0);
  write_u32(bytes, 1);
  write_u16(bytes, 1);
  write_u32(bytes, 0x00112233);
  write_u16(bytes, 1);
  write_str(bytes, "Ring");
  write_u32(bytes, 1);
  write_u32(bytes, 1);
  write_u32(bytes, 24);
  write_u32(bytes, 24);
  write_u16(bytes, 0);
  write_u16(bytes, 0);
  write_u16(bytes, 1);
  write_str(bytes, "LED");
  write_u32(bytes, 0);
  write_u16(bytes, 1);
  write_u32(bytes, 0x0000FF00);

  OrgbController controller;
  expect(parse_orgb_controller(bytes.data(), bytes.size(), 4, controller), "orgb payload parses");
  expect(controller.name == "Kraken Elite", "orgb name");
  expect(controller.vendor == "NZXT", "orgb vendor");
  expect(controller.vid == 0x1E71 && controller.pid == 0x3012, "orgb vid pid");
  expect(controller.modes.size() == 1 && controller.modes[0].name == "Direct", "orgb mode");
  expect(controller.zoneNames.size() == 1 && controller.zoneLeds.size() == 1 && controller.zoneLeds[0] == 24, "orgb zone");
  expect(orgb_has_effect(controller.modes, "static"), "direct maps to static");
  expect(!orgb_has_effect(controller.modes, "wave"), "missing wave");
  expect(orgb_mode_index_for(controller.modes, "static") == 0, "lone Direct is static");

  OrgbMode static_mode;
  static_mode.name = "Static";
  OrgbMode direct_mode;
  direct_mode.name = "Direct";
  OrgbMode breath_mode;
  breath_mode.name = "Breathing";
  const std::vector<OrgbMode> mixed{static_mode, breath_mode, direct_mode};
  expect(orgb_mode_index_for(mixed, "static") == 2, "prefer Direct over Static");
  expect(orgb_mode_index_for(mixed, "breathe") == 1, "breathing match");
}

void test_catalog() {
  const KnownHardware* viper = find_known_device(0x1532, 0x007B);
  expect(viper && std::string(viper->name).find("Viper") != std::string::npos, "razer viper named");
  expect(viper && !catalog_is_studio_device(*viper), "mouse stays off the studio rail");
  const KnownHardware* keys = find_known_device(0x3151, 0x4015);
  expect(keys && !catalog_is_studio_device(*keys), "yichip keyboard is not a fan hub");
  const KnownHardware* kraken = find_known_device(0x1E71, 0x3012);
  expect(kraken && catalog_is_studio_device(*kraken), "nzxt cooler stays on the rail");
  expect(find_known_device(0x048D, 0x8297) && catalog_is_studio_device(*find_known_device(0x048D, 0x8297)), "fusion stays on the rail");
}

void test_engine() {
  std::filesystem::remove_all("/tmp/helios-test");
  Engine engine(std::make_unique<MockPlatform>(std::chrono::milliseconds(0)), "/tmp/helios-test");
  engine.scanBlocking();
  const std::string dump = engine.state().dump();
  expect(dump.find("Z790 AORUS MASTER") != std::string::npos, "scan finds the board");
  expect(dump.find("Kraken Elite") != std::string::npos, "scan finds the cooler");
  expect(dump.find("\"scanning\":false") != std::string::npos, "scan finished");
  expect(dump.find("\"id\":\"pawnio\"") != std::string::npos, "drivers include pawnio");
  expect(dump.find("\"action\":\"elevate\"") == std::string::npos, "stand does not ask to elevate");
  engine.tick();
  const Json history = engine.history(60);
  const Json* series = history.find("series");
  expect(series && series->is_array() && !series->items().empty(), "history has series");

  Json body = Json::object().set("duty", jnum(5)).set("mode", jstr("manual"));
  Json patched = engine.patchFan("board", "cpu_fan", body);
  expect(patched.find("ok") && patched.find("ok")->as_bool(), "fan patch accepted");
  const std::string after = engine.state().dump();
  expect(after.find("\"duty\":20") != std::string::npos, "fan duty clamped to floor");

  Json color = Json::object().set("color", jstr("#112233")).set("effect", jstr("breathe"));
  Json painted = engine.patchZone("kraken", "ring", color);
  expect(painted.find("ok") && painted.find("ok")->as_bool(), "zone patch accepted");
  expect(engine.state().dump().find("#112233") != std::string::npos, "zone color stored");
}

void test_ipc() {
  const auto dir = std::filesystem::temp_directory_path() / "helios-ipc-test";
  std::filesystem::remove_all(dir);
  const Json sensor = Json::object()
                          .set("id", jstr("t"))
                          .set("label", jstr("GPU"))
                          .set("unit", jstr("°C"))
                          .set("kind", jstr("temperature"))
                          .set("value", jnum(61));
  const Json gpuJson = Json::object()
                           .set("id", jstr("gpu"))
                           .set("name", jstr("RTX"))
                           .set("kind", jstr("gpu"))
                           .set("zones", Json::array().push(Json::object().set("id", jstr("logo")).set("name", jstr("Лого")).set("color", jstr("#112233"))))
                           .set("fans", Json::array().push(Json::object().set("id", jstr("fan")).set("name", jstr("GPU")).set("rpm", jnum(1200)).set("controllable", jbool(true))))
                           .set("sensors", Json::array().push(sensor));
  const Json pawnio = Json::object()
                          .set("id", jstr("pawnio"))
                          .set("name", jstr("PawnIO"))
                          .set("state", jstr("unavailable"))
                          .set("detail", jstr("нет прав"))
                          .set("action", jstr("elevate"));
  const Json live = Json::object()
                        .set("platform", Json::object().set("mode", jstr("windows")).set("computer", jstr("PC")).set("detail", jstr("модуль")))
                        .set("devices", Json::array().push(gpuJson))
                        .set("drivers", Json::array().push(pawnio));
  ipc_write_atomic(dir / "live.json", live.dump());
  auto platform = make_ipc_platform(dir);
  const auto devices = platform->scan([](const ScanStep&) {});
  expect(!devices.empty() && devices[0].name == "RTX", "ipc scan reads live.json");
  expect(!devices[0].sensors.empty() && devices[0].sensors[0].value == 61, "ipc sensors");
  const auto drivers = platform->drivers();
  expect(!drivers.empty() && drivers[0].id == "pawnio" && drivers[0].action == "elevate", "ipc drivers keep elevate action");
  Device gpu = devices[0];
  Zone zone = gpu.zones.front();
  zone.color = Color{1, 2, 3};
  expect(platform->set_zone(gpu, zone).empty(), "ipc zone queues without error");
  Fan fan = gpu.fans.front();
  fan.mode = "manual";
  fan.duty = 40;
  expect(platform->set_fan(gpu, fan).empty(), "ipc fan queues without error");
  ipc_request_scan(dir);
  auto commands = ipc_take_commands(dir / "commands");
  expect(commands.size() == 3, "ipc queued zone, fan, scan");
  expect(commands[0].find("op") && commands[0].find("op")->as_string() == "zone", "ipc zone op");
  expect(commands[1].find("op") && commands[1].find("op")->as_string() == "fan", "ipc fan op");
  expect(commands[2].find("op") && commands[2].find("op")->as_string() == "scan", "ipc scan op");
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  test_curve();
  test_json();
  test_orgb();
  test_catalog();
  test_engine();
  test_ipc();
  if (failures) {
    std::cerr << failures << " failed\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}

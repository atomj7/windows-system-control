#include "mock_platform.hpp"

#include "curve.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace {

Sensor make_sensor(std::string id, std::string label, std::string unit, std::string kind, double value) {
  return Sensor{std::move(id), std::move(label), std::move(unit), std::move(kind), value};
}

Zone make_zone(std::string id, std::string name, int leds, Color color, int channel) {
  Zone zone;
  zone.id = std::move(id);
  zone.name = std::move(name);
  zone.leds = leds;
  zone.color = color;
  zone.channel = channel;
  zone.effect = "static";
  zone.speed = 45;
  zone.brightness = 70;
  zone.controllable = true;
  return zone;
}

Fan make_fan(std::string id, std::string name, double minDuty, bool pump, int channel, std::string source,
             std::vector<CurvePoint> curve) {
  Fan fan;
  fan.id = std::move(id);
  fan.name = std::move(name);
  fan.minDuty = minDuty;
  fan.isPump = pump;
  fan.channel = channel;
  fan.source = std::move(source);
  fan.curve = std::move(curve);
  fan.controllable = true;
  fan.rpmKnown = true;
  fan.dutyKnown = true;
  fan.mode = "curve";
  fan.duty = duty_for_temperature(fan.curve, 45);
  return fan;
}

double wave(double t, double speed, double phase, double amount) { return std::sin(t * speed + phase) * amount; }

}  // namespace

MockPlatform::MockPlatform(std::chrono::milliseconds stepDelay) : stepDelay_(stepDelay) {}

PlatformInfo MockPlatform::info() const {
  return PlatformInfo{
      "simulated", "Стенд · Z790 / 14700K / 4080 SUPER",
      "Это демонстрационный стенд с тем же интерфейсом. На Windows Helios читает плату, видеокарту, USB-контроллеры и датчики этой машины."};
}

std::vector<DriverState> MockPlatform::drivers() const {
  return {
      {"hid", "HID", "ready", "На стенде пакеты никуда не уходят. На Windows подсветка NZXT пишется через HID."},
      {"nvml", "NVIDIA NVML", "skipped", "На стенде нет драйвера NVIDIA. На Windows библиотека берётся из уже установленного драйвера."},
      {"adl", "AMD ADL", "skipped", "Видеокарта AMD на стенде не подключена."},
      {"pawnio", "PawnIO", "missing",
       "Нужен только на Windows, чтобы читать SuperIO: обороты и ШИМ корпусных вентиляторов. Helios сам драйвер ядра не ставит."},
  };
}

std::vector<BusDevice> MockPlatform::bus() const {
  return {
      {"NZXT Kraken Elite", "NZXT", 0x1E71, 0x3012, true},
      {"Lian Li UNI FAN SL-INF", "Lian Li", 0x0416, 0xA100, true},
      {"ITE RGB Fusion", "ITE", 0x048D, 0x8297, true},
      {"USB Audio", "Realtek", 0x0B05, 0x1A52, false},
      {"Wireless Controller", "Intel", 0x8087, 0x0032, false},
      {"Hub", "Genesys", 0x05E3, 0x0610, false},
  };
}

std::vector<Device> MockPlatform::scan(const ScanReport& report) {
  auto pause = [&] {
    if (stepDelay_.count() > 0) std::this_thread::sleep_for(stepDelay_);
  };

  report(ScanStep{"smbios", "Прошивка и SMBIOS", "GIGABYTE Z790 AORUS MASTER · Intel Core i7-14700K · 2×32 ГБ DDR5-6400", "ok"});
  pause();
  report(ScanStep{"gpu", "Видеоадаптер", "NVIDIA GeForce RTX 4080 SUPER, 16 ГБ. На стенде телеметрия считается моделью.", "ok"});
  pause();
  report(ScanStep{"usb", "USB и HID", "Kraken Elite, хаб UNI FAN и контроллер ITE 8297. Остальные USB не похожи на подсветку.", "ok"});
  pause();
  report(ScanStep{"sensors", "Датчики", "Пакет процессора, точка GPU, жидкость, VRM и накопитель. На стенде значения живые, но расчётные.", "ok"});
  pause();
  report(ScanStep{"drivers", "Драйверы", "PawnIO на стенде нет. На Windows без него крыльчатки платы видны только как заголовки, без оборотов.", "warn"});

  Device cpu;
  cpu.id = "cpu";
  cpu.name = "Core i7-14700K";
  cpu.vendor = "Intel";
  cpu.kind = "cpu";
  cpu.bus = "simulated";
  cpu.connection = "SMBIOS · сокет LGA1700";
  cpu.hardware = "monitor";
  cpu.backend = "sim";
  cpu.note = "Температура пакета — источник кривых. У процессора нет своей ленты, подсветка сидит на плате и памяти.";
  cpu.sensors = {
      make_sensor("cpu_temp", "Пакет", "°C", "temperature", cpuTemp_),
      make_sensor("cpu_load", "Загрузка", "%", "load", 18),
      make_sensor("cpu_power", "Мощность", "Вт", "power", 48),
      make_sensor("cpu_clock", "Частота", "МГц", "clock", 5100),
  };

  Device board;
  board.id = "board";
  board.name = "Z790 AORUS MASTER";
  board.vendor = "GIGABYTE";
  board.kind = "motherboard";
  board.bus = "simulated";
  board.connection = "USB · ITE 8297 · RGB Fusion 2.0";
  board.hardware = "live";
  board.backend = "sim";
  board.vid = 0x048D;
  board.pid = 0x8297;
  board.note = "На стенде зоны отвечают сразу. На живой плате Fusion отвечает только после проверенного протокола контроллера, либо через мост OpenRGB.";
  board.zones = {
      make_zone("io", "Кожух I/O", 18, Color{232, 145, 74}, 0),
      make_zone("chipset", "Чипсет", 8, Color{212, 101, 93}, 1),
      make_zone("argb1", "ARGB 1", 24, Color{126, 184, 178}, 2),
      make_zone("argb2", "ARGB 2", 24, Color{242, 239, 230}, 3),
  };
  board.fans = {
      make_fan("cpu_fan", "CPU_FAN", 20, false, 0, "cpu_temp", curve_balanced()),
      make_fan("sys1", "SYS_FAN1", 20, false, 1, "vrm_temp", curve_quiet()),
      make_fan("sys2", "SYS_FAN2", 20, false, 2, "board_temp", curve_quiet()),
      make_fan("sys3", "SYS_FAN3", 20, false, 3, "cpu_temp", curve_balanced()),
  };
  board.sensors = {
      make_sensor("vrm_temp", "VRM", "°C", "temperature", vrm_),
      make_sensor("board_temp", "Плата", "°C", "temperature", board_),
  };

  Device gpu;
  gpu.id = "gpu";
  gpu.name = "GeForce RTX 4080 SUPER";
  gpu.vendor = "NVIDIA";
  gpu.kind = "gpu";
  gpu.bus = "simulated";
  gpu.connection = "PCIe · NVML";
  gpu.hardware = "live";
  gpu.backend = "sim";
  gpu.nvmlIndex = 0;
  gpu.note = "На Windows обороты этих крыльчаток ставятся через NVML. Ручной режим снимает заводскую кривую, поэтому ниже защитного порога Helios сам не опускается.";
  gpu.zones = {make_zone("logo", "Логотип", 1, Color{214, 120, 92}, 0)};
  gpu.fans = {
      make_fan("fan0", "Крыльчатка 1", 30, false, 0, "gpu_temp", curve_balanced()),
      make_fan("fan1", "Крыльчатка 2", 30, false, 1, "gpu_temp", curve_balanced()),
  };
  gpu.sensors = {
      make_sensor("gpu_temp", "GPU", "°C", "temperature", gpuTemp_),
      make_sensor("gpu_hotspot", "Точка", "°C", "temperature", hotspot_),
      make_sensor("gpu_load", "Загрузка", "%", "load", 12),
      make_sensor("gpu_power", "Мощность", "Вт", "power", 36),
      make_sensor("gpu_clock", "Ядро", "МГц", "clock", 2550),
  };

  Device cooler;
  cooler.id = "kraken";
  cooler.name = "Kraken Elite 360";
  cooler.vendor = "NZXT";
  cooler.kind = "cooler";
  cooler.bus = "simulated";
  cooler.connection = "USB 1E71:3012";
  cooler.hardware = "live";
  cooler.backend = "sim";
  cooler.vid = 0x1E71;
  cooler.pid = 0x3012;
  cooler.nzxt2023 = true;
  cooler.note = "На Windows этот контроллер получает цвет и ШИМ напрямую, без CAM. Если CAM уже держит устройство, запись вернёт ошибку доступа.";
  cooler.zones = {make_zone("ring", "Кольцо", 24, Color{126, 184, 178}, 0)};
  cooler.fans = {
      make_fan("pump", "Помпа", 45, true, 0, "coolant_temp", curve_pump()),
      make_fan("rad", "Радиатор", 25, false, 1, "coolant_temp", curve_quiet()),
  };
  cooler.sensors = {make_sensor("coolant_temp", "Жидкость", "°C", "temperature", coolant_)};

  Device memory;
  memory.id = "ram";
  memory.name = "Trident Z5 RGB";
  memory.vendor = "G.Skill";
  memory.kind = "memory";
  memory.bus = "simulated";
  memory.connection = "DDR5 · 2 модуля · 6400";
  memory.hardware = "live";
  memory.backend = "sim";
  memory.note = "На многих платах подсветка планок идёт по SMBus, а не отдельным USB. Тогда её отдаёт мост OpenRGB, а не прямой HID.";
  memory.zones = {
      make_zone("dimm1", "DIMM A2", 8, Color{242, 239, 230}, 0),
      make_zone("dimm2", "DIMM B2", 8, Color{242, 239, 230}, 1),
  };

  Device fans;
  fans.id = "lianli";
  fans.name = "UNI FAN SL-INF";
  fans.vendor = "Lian Li";
  fans.kind = "fan_controller";
  fans.bus = "simulated";
  fans.connection = "USB 0416:A100";
  fans.hardware = "live";
  fans.backend = "sim";
  fans.vid = 0x0416;
  fans.pid = 0xA100;
  fans.note = "На стенде каналы живые. На Windows прямой протокол Lian Li ещё не подключён: цвет и обороты пойдут через OpenRGB, пока нет своего модуля.";
  fans.zones = {
      make_zone("f1", "Вентилятор 1", 16, Color{227, 145, 74}, 0),
      make_zone("f2", "Вентилятор 2", 16, Color{227, 145, 74}, 1),
      make_zone("f3", "Вентилятор 3", 16, Color{227, 145, 74}, 2),
  };
  fans.fans = {
      make_fan("f1", "Вентилятор 1", 20, false, 0, "cpu_temp", curve_quiet()),
      make_fan("f2", "Вентилятор 2", 20, false, 1, "cpu_temp", curve_quiet()),
      make_fan("f3", "Вентилятор 3", 20, false, 2, "gpu_temp", curve_balanced()),
  };

  Device disk;
  disk.id = "ssd";
  disk.name = "990 PRO 2 ТБ";
  disk.vendor = "Samsung";
  disk.kind = "storage";
  disk.bus = "simulated";
  disk.connection = "NVMe · PCIe 4.0";
  disk.hardware = "monitor";
  disk.backend = "sim";
  disk.note = "Накопитель без подсветки. Температура на стенде модельная; на Windows она появится, если её отдаёт драйвер диска.";
  disk.sensors = {make_sensor("ssd_temp", "SSD", "°C", "temperature", ssd_)};

  return {std::move(cpu), std::move(board), std::move(gpu), std::move(cooler), std::move(memory), std::move(fans), std::move(disk)};
}

void MockPlatform::sample(std::vector<Device>& devices) {
  const auto now = std::chrono::steady_clock::now();
  double dt = std::chrono::duration<double>(now - last_).count();
  last_ = now;
  if (dt < 0.05) dt = 0.05;
  if (dt > 2) dt = 0.5;
  const double t = std::chrono::duration<double>(now - started_).count();

  const double cpuLoad = clamp_double(24 + wave(t, 0.23, 0.4, 18) + wave(t, 0.71, 1.2, 10) + wave(t, 0.11, 2.0, 8), 6, 96);
  const double gpuLoad = clamp_double(16 + wave(t, 0.17, 2.4, 22) + wave(t, 0.53, 0.2, 12), 2, 99);
  const double cpuTarget = 38 + cpuLoad * 0.46 + wave(t, 0.9, 0.3, 0.8);
  const double gpuTarget = 36 + gpuLoad * 0.42 + wave(t, 0.8, 1.1, 0.7);
  const double coolantTarget = 29 + (cpuTemp_ - 40) * 0.18 + wave(t, 0.15, 0.6, 0.3);
  const double blend = std::min(1.0, dt * 0.45);
  cpuTemp_ += (cpuTarget - cpuTemp_) * blend;
  gpuTemp_ += (gpuTarget - gpuTemp_) * blend;
  hotspot_ += ((gpuTemp_ + 9 + gpuLoad * 0.05) - hotspot_) * blend;
  coolant_ += (coolantTarget - coolant_) * std::min(1.0, dt * 0.18);
  vrm_ += ((board_ + 6 + cpuLoad * 0.12) - vrm_) * blend;
  board_ += ((32 + cpuLoad * 0.08) - board_) * std::min(1.0, dt * 0.2);
  ssd_ += ((38 + cpuLoad * 0.06) - ssd_) * std::min(1.0, dt * 0.15);

  auto put = [&](const std::string& id, double value) {
    for (auto& device : devices) {
      for (auto& sensor : device.sensors) {
        if (sensor.id == id) sensor.value = value;
      }
    }
  };
  put("cpu_temp", cpuTemp_);
  put("cpu_load", cpuLoad);
  put("cpu_power", 28 + cpuLoad * 1.55);
  put("cpu_clock", cpuLoad > 70 ? 5600 : cpuLoad > 25 ? 5100 : 4200);
  put("gpu_temp", gpuTemp_);
  put("gpu_hotspot", hotspot_);
  put("gpu_load", gpuLoad);
  put("gpu_power", 22 + gpuLoad * 2.7);
  put("gpu_clock", 210 + gpuLoad * 24);
  put("coolant_temp", coolant_);
  put("vrm_temp", vrm_);
  put("board_temp", board_);
  put("ssd_temp", ssd_);

  auto sensor_value = [&](const std::string& id) {
    for (const auto& device : devices) {
      for (const auto& sensor : device.sensors) {
        if (sensor.id == id) return sensor.value;
      }
    }
    return 40.0;
  };

  for (auto& device : devices) {
    for (auto& fan : device.fans) {
      double duty = fan.duty;
      if (fan.mode == "auto") duty = clamp_duty(fan, duty_for_temperature(curve_quiet(), sensor_value(fan.source)));
      const double floor = fan.isPump ? 800 : 350;
      const double span = fan.isPump ? 2000 : 1450;
      const double wobble = 1.0 + wave(t, fan.isPump ? 1.3 : 3.1, fan.channel, 0.012);
      fan.duty = duty;
      fan.rpm = (floor + span * (duty / 100.0)) * wobble;
      fan.rpmKnown = true;
    }
  }
}

std::string MockPlatform::set_zone(const Device&, const Zone&) { return {}; }

std::string MockPlatform::set_fan(const Device&, const Fan&) { return {}; }

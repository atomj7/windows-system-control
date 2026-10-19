#include "win_nvml.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

using InitFn = int(__stdcall*)();
using ShutdownFn = int(__stdcall*)();
using CountFn = int(__stdcall*)(unsigned int*);
using HandleFn = int(__stdcall*)(unsigned int, void**);
using NameFn = int(__stdcall*)(void*, char*, unsigned int);
using TempFn = int(__stdcall*)(void*, int, unsigned int*);
using UtilFn = int(__stdcall*)(void*, unsigned int*);
using PowerFn = int(__stdcall*)(void*, unsigned int*);
using ClockFn = int(__stdcall*)(void*, int, unsigned int*);
using FanFn = int(__stdcall*)(void*, unsigned int*);
using FanRpmFn = int(__stdcall*)(void*, unsigned int, unsigned int*);
using FanCountFn = int(__stdcall*)(void*, unsigned int*);
using SetFanFn = int(__stdcall*)(void*, unsigned int, unsigned int);
using SetDefaultFn = int(__stdcall*)(void*, unsigned int);

void* load_symbol(HMODULE module, const char* name) { return reinterpret_cast<void*>(GetProcAddress(module, name)); }

}  // namespace

Nvml::~Nvml() { close(); }

bool Nvml::open() {
  if (ready_) return true;
  const wchar_t* candidates[] = {L"nvml.dll", L"C:\\Windows\\System32\\nvml.dll",
                                 L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll"};
  HMODULE module = nullptr;
  for (const wchar_t* path : candidates) {
    module = LoadLibraryW(path);
    if (module) break;
  }
  if (!module) {
    detail_ = "nvml.dll не найден. Если видеокарта NVIDIA, драйвер не установлен или это ноутбук без дискретной карты.";
    return false;
  }
  module_ = module;
  init_ = load_symbol(module, "nvmlInit_v2");
  if (!init_) init_ = load_symbol(module, "nvmlInit");
  shutdown_ = load_symbol(module, "nvmlShutdown");
  count_ = load_symbol(module, "nvmlDeviceGetCount_v2");
  if (!count_) count_ = load_symbol(module, "nvmlDeviceGetCount");
  handle_ = load_symbol(module, "nvmlDeviceGetHandleByIndex_v2");
  if (!handle_) handle_ = load_symbol(module, "nvmlDeviceGetHandleByIndex");
  name_ = load_symbol(module, "nvmlDeviceGetName");
  temperature_ = load_symbol(module, "nvmlDeviceGetTemperature");
  utilization_ = load_symbol(module, "nvmlDeviceGetUtilizationRates");
  power_ = load_symbol(module, "nvmlDeviceGetPowerUsage");
  clock_ = load_symbol(module, "nvmlDeviceGetClockInfo");
  fan_speed_ = load_symbol(module, "nvmlDeviceGetFanSpeed");
  fan_rpm_ = load_symbol(module, "nvmlDeviceGetFanSpeedRPM");
  fan_count_ = load_symbol(module, "nvmlDeviceGetNumFans");
  set_speed_ = load_symbol(module, "nvmlDeviceSetFanSpeed_v2");
  set_default_ = load_symbol(module, "nvmlDeviceSetDefaultFanSpeed_v2");
  if (!init_ || !count_ || !handle_ || !name_) {
    detail_ = "nvml.dll загрузилась, но в ней нет ожидаемых функций.";
    close();
    return false;
  }
  if (reinterpret_cast<InitFn>(init_)() != 0) {
    detail_ = "NVML не инициализировалась. Драйвер NVIDIA есть, но телеметрия недоступна.";
    close();
    return false;
  }
  ready_ = true;
  detail_ = set_speed_ ? "NVML готова, обороты видеокарты можно задавать." : "NVML читает датчики. Функции записи оборотов в этой версии нет.";
  return true;
}

void Nvml::close() {
  if (ready_ && shutdown_) reinterpret_cast<ShutdownFn>(shutdown_)();
  if (module_) FreeLibrary(static_cast<HMODULE>(module_));
  module_ = nullptr;
  ready_ = false;
  init_ = shutdown_ = count_ = handle_ = name_ = nullptr;
  temperature_ = utilization_ = power_ = clock_ = nullptr;
  fan_speed_ = fan_rpm_ = fan_count_ = set_speed_ = set_default_ = nullptr;
}

std::vector<NvmlGpu> Nvml::sample() {
  std::vector<NvmlGpu> gpus;
  if (!ready_) return gpus;
  unsigned int count = 0;
  if (reinterpret_cast<CountFn>(count_)(&count) != 0 || count > 16) return gpus;
  for (unsigned int index = 0; index < count; ++index) {
    void* handle = nullptr;
    if (reinterpret_cast<HandleFn>(handle_)(index, &handle) != 0 || !handle) continue;
    NvmlGpu gpu;
    gpu.index = static_cast<int>(index);
    char name[96] = {};
    if (reinterpret_cast<NameFn>(name_)(handle, name, sizeof(name)) == 0) gpu.name = name;
    unsigned int value = 0;
    if (temperature_ && reinterpret_cast<TempFn>(temperature_)(handle, 0, &value) == 0) {
      gpu.temp = value;
      gpu.tempKnown = true;
    }
    if (utilization_) {
      struct {
        unsigned int gpu;
        unsigned int memory;
      } rates{};
      if (reinterpret_cast<int(__stdcall*)(void*, void*)>(utilization_)(handle, &rates) == 0) {
        gpu.load = rates.gpu;
        gpu.loadKnown = true;
      }
    }
    if (power_ && reinterpret_cast<PowerFn>(power_)(handle, &value) == 0) {
      gpu.power = value / 1000.0;
      gpu.powerKnown = true;
    }
    if (clock_ && reinterpret_cast<ClockFn>(clock_)(handle, 0, &value) == 0) {
      gpu.clock = value;
      gpu.clockKnown = true;
    }
    unsigned int fans = 1;
    if (fan_count_ && reinterpret_cast<FanCountFn>(fan_count_)(handle, &fans) != 0) fans = 1;
    if (fans > 8) fans = 8;
    gpu.canControl = set_speed_ != nullptr;
    for (unsigned int fan = 0; fan < fans; ++fan) {
      NvmlFan reading;
      if (fan == 0 && fan_speed_ && reinterpret_cast<FanFn>(fan_speed_)(handle, &value) == 0) {
        reading.percent = value;
        reading.percentKnown = true;
      }
      if (fan_rpm_ && reinterpret_cast<FanRpmFn>(fan_rpm_)(handle, fan, &value) == 0 && value > 0 && value < 20000) {
        reading.rpm = value;
        reading.rpmKnown = true;
      }
      if (fan == 0 || reading.rpmKnown || reading.percentKnown || gpu.canControl) gpu.fans.push_back(reading);
    }
    if (gpu.fans.empty() && gpu.canControl) gpu.fans.push_back(NvmlFan{});
    gpus.push_back(std::move(gpu));
  }
  return gpus;
}

std::string Nvml::set_fan(int index, int fan, int percent) {
  if (!ready_ || !set_speed_ || !handle_) return "NVML не умеет задавать обороты.";
  void* handle = nullptr;
  if (reinterpret_cast<HandleFn>(handle_)(static_cast<unsigned int>(index), &handle) != 0) return "Видеокарта NVML не найдена.";
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const int code = reinterpret_cast<SetFanFn>(set_speed_)(handle, static_cast<unsigned int>(fan), static_cast<unsigned int>(percent));
  if (code != 0) return "NVIDIA не приняла обороты. На части ноутбуков и Founders это закрыто драйвером.";
  return {};
}

std::string Nvml::set_auto(int index, int fan) {
  if (!ready_ || !set_default_ || !handle_) return {};
  void* handle = nullptr;
  if (reinterpret_cast<HandleFn>(handle_)(static_cast<unsigned int>(index), &handle) != 0) return "Видеокарта NVML не найдена.";
  const int code = reinterpret_cast<SetDefaultFn>(set_default_)(handle, static_cast<unsigned int>(fan));
  if (code != 0) return "Не удалось вернуть заводскую кривую NVIDIA.";
  return {};
}

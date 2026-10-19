#include "win_platform.hpp"

#include "catalog.hpp"
#include "curve.hpp"
#include "log.hpp"
#include "util.hpp"
#include "win_nzxt.hpp"
#include "win_seh.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dxgi.h>
#include <objbase.h>
#include <pdh.h>
#include <setupapi.h>
#include <wbemidl.h>
#include <winsvc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace {

std::string wide_to_utf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

std::string trim_copy(std::string text) {
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.pop_back();
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
  return text.substr(start);
}

std::string variant_string(VARIANT& value) {
  if (value.vt == VT_BSTR && value.bstrVal) return trim_copy(wide_to_utf8(value.bstrVal));
  if (value.vt == VT_I4) return std::to_string(value.lVal);
  if (value.vt == VT_UI4) return std::to_string(value.ulVal);
  if (value.vt == VT_I2) return std::to_string(value.iVal);
  if (value.vt == VT_UI2) return std::to_string(value.uiVal);
  if (value.vt == VT_BOOL) return value.boolVal ? "1" : "0";
  if (value.vt == VT_R4) return std::to_string(value.fltVal);
  if (value.vt == VT_R8) return std::to_string(value.dblVal);
  if (value.vt == VT_I8 || value.vt == VT_UI8) return std::to_string(value.ullVal);
  return {};
}

class ComInit {
 public:
  ComInit() { hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
  ~ComInit() {
    if (SUCCEEDED(hr_)) CoUninitialize();
  }
  bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

 private:
  HRESULT hr_ = E_FAIL;
};

struct WmiRow {
  std::map<std::string, std::string> fields;
};

class Wmi {
 public:
  Wmi() {
    static std::once_flag security;
    std::call_once(security, [] {
      CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    });
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<void**>(&locator_)))) locator_ = nullptr;
  }

  ~Wmi() {
    if (locator_) locator_->Release();
  }

  std::vector<WmiRow> query(const wchar_t* nameSpace, const wchar_t* wql, const std::vector<std::wstring>& columns) {
    std::vector<WmiRow> rows;
    if (!locator_) return rows;
    BSTR name = SysAllocString(nameSpace);
    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(wql);
    IWbemServices* services = nullptr;
    const HRESULT connected = locator_->ConnectServer(name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    if (FAILED(connected) || !services) {
      SysFreeString(queryText);
      SysFreeString(language);
      SysFreeString(name);
      return rows;
    }
    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = services->ExecQuery(language, queryText, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    SysFreeString(queryText);
    SysFreeString(language);
    SysFreeString(name);
    if (FAILED(hr) || !enumerator) {
      services->Release();
      return rows;
    }
    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    while (enumerator->Next(5000, 1, &object, &returned) == S_OK && object) {
      WmiRow row;
      for (const auto& column : columns) {
        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(object->Get(column.c_str(), 0, &value, nullptr, nullptr))) {
          row.fields[wide_to_utf8(column)] = variant_string(value);
          VariantClear(&value);
        }
      }
      rows.push_back(std::move(row));
      object->Release();
      object = nullptr;
    }
    enumerator->Release();
    services->Release();
    return rows;
  }

 private:
  IWbemLocator* locator_ = nullptr;
};

std::string field(const WmiRow& row, const char* key) {
  const auto found = row.fields.find(key);
  return found == row.fields.end() ? std::string{} : found->second;
}

bool parse_vid_pid(const std::string& hardwareId, uint16_t& vid, uint16_t& pid) {
  const std::string text = lower_copy(hardwareId);
  const auto vendor = text.find("vid_");
  const auto product = text.find("pid_");
  if (vendor == std::string::npos || product == std::string::npos) return false;
  try {
    vid = static_cast<uint16_t>(std::stoul(text.substr(vendor + 4, 4), nullptr, 16));
    pid = static_cast<uint16_t>(std::stoul(text.substr(product + 4, 4), nullptr, 16));
  } catch (...) {
    return false;
  }
  return true;
}

std::string smbios_string(const uint8_t* structure, uint8_t length, const uint8_t* end, int index) {
  if (index <= 0) return {};
  const uint8_t* cursor = structure + length;
  int current = 1;
  while (cursor < end && *cursor) {
    if (current == index) return reinterpret_cast<const char*>(cursor);
    while (cursor < end && *cursor) ++cursor;
    if (cursor < end) ++cursor;
    ++current;
  }
  return {};
}

std::string read_smbios_blurb() {
  const DWORD signature = 0x52534D42;
  const UINT bytes = GetSystemFirmwareTable(signature, 0, nullptr, 0);
  if (bytes < 16 || bytes > 1024 * 1024) return {};
  std::vector<uint8_t> buffer(bytes);
  if (GetSystemFirmwareTable(signature, 0, buffer.data(), bytes) != bytes) return {};
  if (buffer.size() < 8) return {};
  const uint32_t length = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
  const uint8_t* cursor = buffer.data() + 8;
  const uint8_t* end = cursor + std::min<size_t>(length, buffer.size() - 8);
  std::string bios;
  std::string board;
  while (cursor + 4 <= end) {
    const uint8_t type = cursor[0];
    const uint8_t size = cursor[1];
    if (size < 4 || cursor + size > end) break;
    if (type == 0) bios = smbios_string(cursor, size, end, cursor[4]);
    if (type == 2 && size > 6) {
      const std::string vendor = smbios_string(cursor, size, end, cursor[4]);
      const std::string product = smbios_string(cursor, size, end, cursor[5]);
      board = trim_copy(vendor + " " + product);
    }
    const uint8_t* next = cursor + size;
    while (next + 1 < end && !(next[0] == 0 && next[1] == 0)) ++next;
    cursor = next + 2;
  }
  if (!board.empty() && !bios.empty()) return board + " · BIOS " + bios;
  return board.empty() ? bios : board;
}

bool pawnio_open_path(const wchar_t* path, DWORD* error) {
  HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
    return true;
  }
  if (error) *error = GetLastError();
  return false;
}

bool pawnio_file_exists(const wchar_t* pattern) {
  wchar_t expanded[MAX_PATH];
  if (!ExpandEnvironmentStringsW(pattern, expanded, MAX_PATH)) return false;
  return GetFileAttributesW(expanded) != INVALID_FILE_ATTRIBUTES;
}

struct PawnioProbe {
  bool opened = false;
  bool accessDenied = false;
  bool installed = false;
  DWORD error = 0;
  std::string where;
};

PawnioProbe probe_pawnio() {
  PawnioProbe probe;
  const wchar_t* devices[] = {L"\\\\?\\GLOBALROOT\\Device\\PawnIO", L"\\\\.\\PawnIO", L"\\\\.\\Global\\PawnIO"};
  for (const wchar_t* device : devices) {
    DWORD error = 0;
    if (pawnio_open_path(device, &error)) {
      probe.opened = true;
      probe.installed = true;
      probe.where = wide_to_utf8(device);
      return probe;
    }
    probe.error = error;
    if (error == ERROR_ACCESS_DENIED) probe.accessDenied = true;
  }

  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager) {
    SC_HANDLE service = OpenServiceW(manager, L"PawnIO", SERVICE_QUERY_STATUS);
    if (service) {
      probe.installed = true;
      SERVICE_STATUS status{};
      if (QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_RUNNING) {
        probe.where = "служба PawnIO";
      } else {
        probe.where = "служба PawnIO (не запущена)";
      }
      CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
  }

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\PawnIO", 0, KEY_READ | KEY_WOW64_64KEY, &key) ==
      ERROR_SUCCESS) {
    probe.installed = true;
    if (probe.where.empty()) probe.where = "реестр службы";
    RegCloseKey(key);
  }

  const wchar_t* files[] = {L"%ProgramFiles%\\PawnIO\\PawnIO.sys", L"%ProgramFiles%\\PawnIO\\PawnIOLib.dll",
                            L"%ProgramFiles(x86)%\\PawnIO\\PawnIO.sys", L"%ProgramW6432%\\PawnIO\\PawnIO.sys",
                            L"%ProgramFiles%\\PawnIO\\PawnIO.exe"};
  for (const wchar_t* file : files) {
    if (pawnio_file_exists(file)) {
      probe.installed = true;
      if (probe.where.empty()) probe.where = "файлы установки";
      break;
    }
  }
  return probe;
}

DriverState pawnio_driver(const PawnioProbe& probe) {
  DriverState driver;
  driver.id = "pawnio";
  driver.name = "PawnIO";
  if (probe.opened) {
    driver.state = "ready";
    driver.detail = "Драйвер открывается (" + probe.where +
                    "). Регистры SuperIO Helios пока не пишет: сначала карта чипа платы, иначе крыльчатки можно остановить.";
    return driver;
  }
  if (probe.accessDenied) {
    driver.state = "unavailable";
    driver.detail =
        "PawnIO установлен, Windows не дала открыть устройство (часто нужны права администратора или ACL «для всех "
        "пользователей» в установщике PawnIO). Helios его видит, SuperIO не читает.";
    return driver;
  }
  if (probe.installed) {
    driver.state = "unavailable";
    char code[16];
    std::snprintf(code, sizeof(code), "%lu", static_cast<unsigned long>(probe.error));
    driver.detail = "PawnIO стоит (" + (probe.where.empty() ? std::string("служба или файлы") : probe.where) +
                    "), а устройство не открылось, код " + code +
                    ". После установки иногда нужен перезапуск Windows. WinRing0 Helios не использует.";
    return driver;
  }
  driver.state = "missing";
  driver.detail =
      "Не найден ни драйвер, ни служба, ни файлы PawnIO. Helios не ставит драйверы ядра сам и не использует WinRing0: он в "
      "блок-листе Microsoft. PawnIO — подписанная замена, её ставишь ты.";
  return driver;
}

bool library_present(const wchar_t* name) {
  HMODULE module = LoadLibraryW(name);
  if (!module) return false;
  FreeLibrary(module);
  return true;
}

Sensor make_sensor(std::string id, std::string label, std::string unit, std::string kind, double value) {
  return Sensor{std::move(id), std::move(label), std::move(unit), std::move(kind), value};
}

const GUID kUsbDevice = {0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

struct UsbHit {
  std::string name;
  uint16_t vid = 0;
  uint16_t pid = 0;
};

std::vector<UsbHit> enumerate_usb() {
  std::vector<UsbHit> hits;
  HDEVINFO info = SetupDiGetClassDevsW(&kUsbDevice, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (info == INVALID_HANDLE_VALUE) return hits;
  SP_DEVICE_INTERFACE_DATA interfaceData{};
  interfaceData.cbSize = sizeof(interfaceData);
  for (DWORD index = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &kUsbDevice, index, &interfaceData); ++index) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, nullptr, 0, &needed, nullptr);
    if (needed < offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + sizeof(wchar_t)) continue;
    std::vector<uint8_t> buffer(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
    detail->cbSize = offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + sizeof(wchar_t);
    SP_DEVINFO_DATA deviceInfo{};
    deviceInfo.cbSize = sizeof(deviceInfo);
    if (!SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, detail, needed, nullptr, &deviceInfo)) continue;
    wchar_t hardware[512] = {};
    wchar_t description[256] = {};
    SetupDiGetDeviceRegistryPropertyW(info, &deviceInfo, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(hardware), sizeof(hardware), nullptr);
    SetupDiGetDeviceRegistryPropertyW(info, &deviceInfo, SPDRP_DEVICEDESC, nullptr, reinterpret_cast<PBYTE>(description), sizeof(description), nullptr);
    UsbHit hit;
    hit.name = wide_to_utf8(description);
    if (!parse_vid_pid(wide_to_utf8(hardware), hit.vid, hit.pid)) continue;
    hits.push_back(std::move(hit));
  }
  SetupDiDestroyDeviceInfoList(info);
  return hits;
}

Device make_nzxt_device(const KnownHardware& known, int ordinal, const std::string& usbName) {
  Device device;
  device.id = "usb-" + hex4(known.vid) + "-" + hex4(known.pid) + "-" + std::to_string(ordinal);
  device.name = known.name;
  device.vendor = known.vendor;
  device.kind = known.kind;
  device.bus = "usb";
  device.connection = "USB " + hex4(known.vid) + ":" + hex4(known.pid);
  if (!usbName.empty()) device.connection += " · " + usbName;
  device.hardware = "live";
  device.backend = "nzxt";
  device.vid = known.vid;
  device.pid = known.pid;
  device.nzxt2023 = known.nzxt2023;
  device.note = "Прямой протокол NZXT. Если CAM или OpenRGB уже открыли устройство, запись вернёт ошибку доступа. Обороты не трогаются, пока не выберешь кривую или ручной режим.";
  for (int channel = 0; channel < known.rgbChannels; ++channel) {
    Zone zone;
    zone.id = "ch" + std::to_string(channel);
    zone.name = "Канал " + std::to_string(channel + 1);
    zone.leds = 8;
    zone.channel = channel;
    zone.color = Color{126, 184, 178};
    device.zones.push_back(std::move(zone));
  }
  for (int channel = 0; channel < known.fanChannels; ++channel) {
    Fan fan;
    fan.id = "fan" + std::to_string(channel);
    fan.name = "Канал " + std::to_string(channel + 1);
    fan.channel = channel;
    fan.controllable = true;
    fan.mode = "auto";
    fan.minDuty = 30;
    fan.duty = 40;
    fan.dutyKnown = false;
    fan.rpmKnown = false;
    fan.source = "cpu_temp";
    fan.curve = curve_balanced();
    device.fans.push_back(std::move(fan));
  }
  return device;
}

Device make_identified_device(const KnownHardware* known, uint16_t vid, uint16_t pid, const std::string& usbName, int ordinal) {
  Device device;
  device.id = "usb-" + hex4(vid) + "-" + hex4(pid) + "-" + std::to_string(ordinal);
  device.vid = vid;
  device.pid = pid;
  device.vendor = known ? known->vendor : (vendor_from_vid(vid) ? vendor_from_vid(vid) : "USB");
  device.name = known ? known->name : (usbName.empty() ? device.vendor + " " + hex4(pid) : usbName);
  device.kind = known ? known->kind : guess_kind_from_name(device.name);
  device.bus = "usb";
  device.connection = "USB " + hex4(vid) + ":" + hex4(pid);
  device.hardware = "profile";
  device.backend = "identified";
  device.note = "Устройство найдено. Прямой протокол ещё не подключён, чтобы не слать в него непроверенные пакеты. Цвет можно собрать в профиль: на железо он уйдёт через OpenRGB, если мост запущен и знает эту модель.";
  const int channels = known && known->rgbChannels > 0 ? known->rgbChannels : 1;
  if (looks_like_lighting(device.name) || known) {
    for (int channel = 0; channel < channels; ++channel) {
      Zone zone;
      zone.id = "ch" + std::to_string(channel);
      zone.name = channels == 1 ? "Подсветка" : "Канал " + std::to_string(channel + 1);
      zone.channel = channel;
      zone.leds = 8;
      device.zones.push_back(std::move(zone));
    }
  }
  return device;
}

int nvml_open_cb(void* self) { return static_cast<Nvml*>(self)->open() ? 1 : 0; }

int nvml_sample_cb(void* raw) {
  auto* bag = static_cast<std::pair<Nvml*, std::vector<NvmlGpu>*>*>(raw);
  *bag->second = bag->first->sample();
  return 1;
}

std::vector<NvmlGpu> nvml_sample_safe(Nvml& nvml) {
  std::vector<NvmlGpu> gpus;
  unsigned long code = 0;
  std::pair<Nvml*, std::vector<NvmlGpu>*> bag{&nvml, &gpus};
  if (helios_protect(nvml_sample_cb, &bag, &code) < 0) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08lX", code);
    log_error(std::string("NVML sample упал, код 0x") + buffer);
    return {};
  }
  return gpus;
}

struct DxgiBag {
  std::vector<Device>* devices;
  std::vector<NvmlGpu>* gpus;
  int* count;
  bool* amdSeen;
};

int dxgi_enum_cb(void* raw) {
  auto* bag = static_cast<DxgiBag*>(raw);
  IDXGIFactory1* factory = nullptr;
  const HRESULT made = CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory));
  if (FAILED(made) || !factory) return 0;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1* adapter = nullptr;
    const HRESULT hr = factory->EnumAdapters1(index, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr) || !adapter) break;
    DXGI_ADAPTER_DESC1 desc{};
    const HRESULT got = adapter->GetDesc1(&desc);
    adapter->Release();
    adapter = nullptr;
    if (FAILED(got)) continue;
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    ++(*bag->count);
    const std::string name = wide_to_utf8(desc.Description);
    if (desc.VendorId == 0x1002) *bag->amdSeen = true;
    Device gpu;
    gpu.id = "gpu-" + std::to_string(index);
    gpu.name = name.empty() ? "Видеоадаптер" : name;
    gpu.vendor = desc.VendorId == 0x10DE ? "NVIDIA" : desc.VendorId == 0x1002 ? "AMD" : "GPU";
    gpu.kind = "gpu";
    gpu.bus = "pci";
    const double gib = static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
    gpu.connection = "PCIe · " + std::to_string(std::max(1, static_cast<int>(std::llround(gib)))) + " ГБ";
    gpu.hardware = "monitor";
    gpu.backend = "dxgi";
    gpu.note = "Имя и память взяты из DXGI.";
    if (desc.VendorId == 0x10DE) {
      const NvmlGpu* match = nullptr;
      for (const auto& item : *bag->gpus) {
        if (!item.name.empty() && (contains_ci(name, item.name) || contains_ci(item.name, name) || !match)) {
          match = &item;
          if (contains_ci(name, item.name) || contains_ci(item.name, name)) break;
        }
      }
      if (!match && !bag->gpus->empty()) match = &bag->gpus->front();
      if (match) {
        gpu.backend = "nvml";
        gpu.nvmlIndex = match->index;
        gpu.hardware = match->canControl ? "live" : "monitor";
        gpu.note = match->canControl
                       ? "Телеметрия NVML. Заводская кривая остаётся, пока не включишь кривую Helios или ручной режим. Ниже защитного порога крыльчатка сама не опустится."
                       : "NVML читает датчики, но запись оборотов эта библиотека не отдала.";
        if (match->tempKnown) gpu.sensors.push_back(make_sensor("gpu" + std::to_string(match->index) + "_temp", "GPU", "°C", "temperature", match->temp));
        if (match->loadKnown) gpu.sensors.push_back(make_sensor("gpu" + std::to_string(match->index) + "_load", "Загрузка", "%", "load", match->load));
        if (match->powerKnown) gpu.sensors.push_back(make_sensor("gpu" + std::to_string(match->index) + "_power", "Мощность", "Вт", "power", match->power));
        if (match->clockKnown) gpu.sensors.push_back(make_sensor("gpu" + std::to_string(match->index) + "_clock", "Ядро", "МГц", "clock", match->clock));
        int fanIndex = 0;
        for (const auto& fanReading : match->fans) {
          Fan fan;
          fan.id = "fan" + std::to_string(fanIndex);
          fan.name = match->fans.size() == 1 ? "Крыльчатка" : "Крыльчатка " + std::to_string(fanIndex + 1);
          fan.channel = fanIndex;
          fan.controllable = match->canControl;
          fan.mode = "auto";
          fan.minDuty = 30;
          fan.rpm = fanReading.rpm;
          fan.rpmKnown = fanReading.rpmKnown;
          fan.duty = fanReading.percentKnown ? fanReading.percent : 40;
          fan.dutyKnown = fanReading.percentKnown;
          fan.source = gpu.sensors.empty() ? "cpu_temp" : gpu.sensors[0].id;
          fan.curve = curve_balanced();
          gpu.fans.push_back(std::move(fan));
          ++fanIndex;
        }
      }
    } else if (desc.VendorId == 0x1002) {
      gpu.note = "AMD виден через DXGI. Закрытые структуры ADL не разбираются: так меньше шансов задеть драйвер. Температуру и обороты добавлю, когда будет стабильный вызов.";
    }
    bag->devices->push_back(std::move(gpu));
  }
  factory->Release();
  return 1;
}

}  // namespace

WindowsPlatform::WindowsPlatform() {
  info_.mode = "windows";
  info_.computer = "Этот компьютер";
  info_.detail = "Helios читает устройства этой машины. Обороты крыльчаток платы без PawnIO недоступны: WinRing0 сюда сознательно не входит.";
  unsigned long code = 0;
  if (helios_protect(nvml_open_cb, &nvml_, &code) < 0) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08lX", code);
    log_error(std::string("NVML упала при старте, код 0x") + buffer);
    nvmlReady_ = false;
    nvml_.close();
  } else {
    nvmlReady_ = nvml_.ready();
  }
}

WindowsPlatform::~WindowsPlatform() {
  if (pdhQuery_) PdhCloseQuery(static_cast<PDH_HQUERY>(pdhQuery_));
  nvml_.close();
}

PlatformInfo WindowsPlatform::info() const { return info_; }

std::vector<DriverState> WindowsPlatform::drivers() const { return drivers_; }

std::vector<BusDevice> WindowsPlatform::bus() const { return bus_; }

std::vector<Device> WindowsPlatform::scan(const ScanReport& report) {
  ComInit com;
  Wmi wmi;
  std::vector<Device> devices;
  hid_.clear();
  bus_.clear();
  drivers_.clear();

  std::string boardVendor;
  std::string boardName;
  std::string cpuName;
  std::string computer;
  if (com.ok()) {
    auto boards = wmi.query(L"ROOT\\CIMV2", L"SELECT Manufacturer, Product FROM Win32_BaseBoard", {L"Manufacturer", L"Product"});
    if (!boards.empty()) {
      boardVendor = field(boards[0], "Manufacturer");
      boardName = field(boards[0], "Product");
    }
    auto processors = wmi.query(L"ROOT\\CIMV2", L"SELECT Name, MaxClockSpeed FROM Win32_Processor", {L"Name", L"MaxClockSpeed"});
    if (!processors.empty()) cpuName = field(processors[0], "Name");
    auto systems = wmi.query(L"ROOT\\CIMV2", L"SELECT Manufacturer, Model FROM Win32_ComputerSystem", {L"Manufacturer", L"Model"});
    if (!systems.empty()) computer = trim_copy(field(systems[0], "Manufacturer") + " " + field(systems[0], "Model"));
  }
  const std::string firmware = read_smbios_blurb();
  if (!computer.empty()) info_.computer = computer;
  else if (!boardName.empty()) info_.computer = trim_copy(boardVendor + " " + boardName);

  std::string identity = firmware.empty() ? trim_copy(boardVendor + " " + boardName) : firmware;
  if (!cpuName.empty()) identity += identity.empty() ? cpuName : " · " + cpuName;
  report(ScanStep{"smbios", "Прошивка и плата", identity.empty() ? "SMBIOS и WMI не отдали имя платы." : identity,
                  identity.empty() ? "warn" : "ok"});

  if (!cpuName.empty()) {
    Device cpu;
    cpu.id = "cpu";
    cpu.name = cpuName;
    cpu.vendor = cpuName.rfind("AMD", 0) == 0 || contains_ci(cpuName, "Ryzen") ? "AMD" : "Intel";
    cpu.kind = "cpu";
    cpu.bus = "smbios";
    cpu.connection = "Процессор";
    cpu.hardware = "monitor";
    cpu.backend = "wmi";
    cpu.note = "Загрузка берётся из счётчика Windows. Температура пакета без PawnIO обычно недоступна: ACPI-зона — это не ядро.";
    cpu.sensors.push_back(make_sensor("cpu_load", "Загрузка", "%", "load", 0));
    devices.push_back(std::move(cpu));
  }

  if (!boardName.empty() || !boardVendor.empty()) {
    Device board;
    board.id = "board";
    board.name = boardName.empty() ? "Материнская плата" : boardName;
    board.vendor = boardVendor.empty() ? "Плата" : boardVendor;
    board.kind = "motherboard";
    board.bus = "smbios";
    board.connection = "SMBIOS";
    board.hardware = "monitor";
    board.backend = "wmi";
    board.note = "Сама плата без USB-контроллера подсветки только называется. Крыльчатки SuperIO появятся после PawnIO и карты регистров чипа — их нельзя угадывать.";
    devices.push_back(std::move(board));
  }

  if (com.ok()) {
    auto modules = wmi.query(L"ROOT\\CIMV2", L"SELECT Manufacturer, PartNumber, Speed, Capacity FROM Win32_PhysicalMemory",
                             {L"Manufacturer", L"PartNumber", L"Speed", L"Capacity"});
    if (!modules.empty()) {
      Device memory;
      memory.id = "ram";
      memory.kind = "memory";
      memory.bus = "smbios";
      memory.hardware = "monitor";
      memory.backend = "wmi";
      const std::string part = field(modules[0], "PartNumber");
      const std::string maker = field(modules[0], "Manufacturer");
      memory.vendor = maker.empty() ? "Память" : maker;
      memory.name = part.empty() ? "Модули памяти" : part;
      memory.connection = std::to_string(modules.size()) + " модул.";
      if (!field(modules[0], "Speed").empty()) memory.connection += " · " + field(modules[0], "Speed") + " МТ/с";
      memory.note = "Подсветка планок почти никогда не видна как отдельное USB-устройство. Её отдаёт контроллер платы или OpenRGB.";
      devices.push_back(std::move(memory));
    }
    auto disks = wmi.query(L"ROOT\\CIMV2", L"SELECT Model, Size FROM Win32_DiskDrive", {L"Model", L"Size"});
    int diskIndex = 0;
    for (const auto& diskRow : disks) {
      const std::string model = field(diskRow, "Model");
      if (model.empty()) continue;
      Device disk;
      disk.id = "disk-" + std::to_string(diskIndex++);
      disk.name = model;
      disk.vendor = "Накопитель";
      disk.kind = "storage";
      disk.bus = "wmi";
      disk.hardware = "monitor";
      disk.backend = "wmi";
      const std::string size = field(diskRow, "Size");
      if (!size.empty()) {
        try {
          const double gb = std::stod(size) / 1e9;
          disk.connection = std::to_string(static_cast<int>(std::llround(gb))) + " ГБ";
        } catch (...) {
          disk.connection = "Диск";
        }
      }
      disk.note = "Температура NVMe зависит от драйвера диска и здесь не выдумывается.";
      devices.push_back(std::move(disk));
    }
  }

  log_info("обзор: NVML sample");
  std::vector<NvmlGpu> gpus = nvmlReady_ ? nvml_sample_safe(nvml_) : std::vector<NvmlGpu>{};
  log_info("обзор: DXGI");
  int dxgiCount = 0;
  bool amdSeen = false;
  unsigned long dxgiCode = 0;
  DxgiBag dxgiBag{&devices, &gpus, &dxgiCount, &amdSeen};
  if (helios_protect(dxgi_enum_cb, &dxgiBag, &dxgiCode) < 0) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08lX", dxgiCode);
    log_error(std::string("DXGI упала, код 0x") + buffer);
  }
  if (dxgiCount == 0 && com.ok()) {
    auto adapters = wmi.query(L"ROOT\\CIMV2", L"SELECT Name, AdapterRAM, PNPDeviceID FROM Win32_VideoController",
                              {L"Name", L"AdapterRAM", L"PNPDeviceID"});
    int index = 0;
    for (const auto& row : adapters) {
      const std::string name = field(row, "Name");
      if (name.empty() || contains_ci(name, "basic") || contains_ci(name, "microsoft")) continue;
      Device gpu;
      gpu.id = "gpu-wmi-" + std::to_string(index++);
      gpu.name = name;
      gpu.vendor = contains_ci(name, "nvidia") ? "NVIDIA" : contains_ci(name, "amd") || contains_ci(name, "radeon") ? "AMD" : "GPU";
      gpu.kind = "gpu";
      gpu.bus = "wmi";
      gpu.connection = "WMI";
      gpu.hardware = "monitor";
      gpu.backend = "wmi";
      gpu.note = "Имя видеокарты из WMI. DXGI/NVML на этой машине не трогаем, если они роняют процесс.";
      devices.push_back(std::move(gpu));
      ++dxgiCount;
    }
  }
  report(ScanStep{"gpu", "Видеоадаптеры", "DXGI: " + std::to_string(dxgiCount) + ", NVML: " + (nvmlReady_ ? nvml_.detail() : "нет"),
                  dxgiCount > 0 ? "ok" : "warn"});

  log_info("обзор: USB");
  const auto usb = enumerate_usb();
  int hubs = 0;
  int promoted = 0;
  std::map<std::string, int> ordinals;
  for (const auto& hit : usb) {
    const bool hub = contains_ci(hit.name, "hub") || contains_ci(hit.name, "концентратор");
    const KnownHardware* known = find_known_device(hit.vid, hit.pid);
    const bool interesting = known || (is_lighting_vendor(hit.vid) && looks_like_lighting(hit.name));
    if (hub && !interesting) {
      ++hubs;
      continue;
    }
    BusDevice bus;
    bus.name = hit.name.empty() ? hex4(hit.vid) + ":" + hex4(hit.pid) : hit.name;
    bus.vendor = vendor_from_vid(hit.vid) ? vendor_from_vid(hit.vid) : "";
    bus.vid = hit.vid;
    bus.pid = hit.pid;
    bus.promoted = interesting;
    bus_.push_back(bus);
    if (!interesting) continue;
    const std::string key = hex4(hit.vid) + hex4(hit.pid);
    const int ordinal = ordinals[key]++;
    Device device = known && std::string(known->backend) == "nzxt" ? make_nzxt_device(*known, ordinal, hit.name)
                                                                   : make_identified_device(known, hit.vid, hit.pid, hit.name, ordinal);
    if (device.backend == "nzxt") hid_[device.id] = hid_paths_for(hit.vid, hit.pid);
    devices.push_back(std::move(device));
    ++promoted;
  }
  report(ScanStep{"usb", "USB", "Устройств: " + std::to_string(usb.size()) + ", хабы скрыты: " + std::to_string(hubs) +
                                    ", подсветка и охлаждение: " + std::to_string(promoted),
                  "ok"});

  sample_cpu();
  for (auto& device : devices) {
    for (auto& sensor : device.sensors) {
      if (sensor.id == "cpu_load" && cpuLoadKnown_) sensor.value = cpuLoad_;
    }
  }
  if (com.ok()) {
    auto zones = wmi.query(L"ROOT\\WMI", L"SELECT InstanceName, CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                           {L"InstanceName", L"CurrentTemperature"});
    int added = 0;
    for (const auto& zone : zones) {
      const std::string raw = field(zone, "CurrentTemperature");
      if (raw.empty()) continue;
      double celsius = 0;
      try {
        celsius = std::stod(raw) / 10.0 - 273.15;
      } catch (...) {
        continue;
      }
      if (celsius < 1 || celsius > 125) continue;
      Sensor sensor = make_sensor("acpi_" + std::to_string(added), "ACPI " + std::to_string(added + 1), "°C", "temperature", celsius);
      bool attached = false;
      for (auto& device : devices) {
        if (device.id == "cpu") {
          device.sensors.push_back(sensor);
          attached = true;
          break;
        }
      }
      if (!attached && !devices.empty()) devices.front().sensors.push_back(sensor);
      ++added;
    }
  }
  report(ScanStep{"sensors", "Датчики",
                  cpuLoadKnown_ ? "Загрузка процессора читается. Температура пакета без PawnIO не обещается."
                                : pdhQuery_ ? "Счётчик загрузки открыт, первое значение появится через секунду."
                                            : "Счётчик загрузки процессора не открылся.",
                  pdhQuery_ ? "ok" : "warn"});

  drivers_.push_back(DriverState{"hid", "HID", "ready", "Встроенный стек Windows. Им уходит подсветка и ШИМ NZXT."});
  drivers_.push_back(DriverState{"nvml", "NVIDIA NVML", nvmlReady_ ? "ready" : "missing", nvmlReady_ ? nvml_.detail() : nvml_.detail()});
  const bool adl = library_present(L"atiadlxx.dll") || library_present(L"atiadlxy.dll");
  drivers_.push_back(DriverState{"adl", "AMD ADL", !amdSeen ? "skipped" : adl ? "ready" : "missing",
                                 !amdSeen ? "Видеокарта AMD не видна в DXGI."
                                          : adl ? "Библиотека драйвера AMD на месте. Её закрытые структуры пока не вызываются."
                                                : "В DXGI есть AMD, а atiadlxx.dll не загрузилась."});
  const PawnioProbe pawn = probe_pawnio();
  const DriverState pawnDriver = pawnio_driver(pawn);
  drivers_.push_back(pawnDriver);
  log_info(std::string("PawnIO: ") + pawnDriver.state + " — " + pawnDriver.detail);
  const bool pawnOk = pawn.opened;
  const bool pawnSeen = pawn.opened || pawn.installed || pawn.accessDenied;
  report(ScanStep{"drivers", "Драйверы",
                  pawnOk ? "PawnIO на месте, устройство открывается."
                         : pawnSeen ? "PawnIO установлен, устройство пока не открылось."
                                    : "PawnIO нет, низкоуровневые обороты платы закрыты.",
                  pawnOk ? "ok" : "warn"});
  return devices;
}

void WindowsPlatform::sample(std::vector<Device>& devices) {
  sample_cpu();
  for (auto& device : devices) {
    for (auto& sensor : device.sensors) {
      if (sensor.id == "cpu_load" && cpuLoadKnown_) sensor.value = cpuLoad_;
    }
  }
  if (!nvmlReady_) return;
  for (const auto& gpu : nvml_sample_safe(nvml_)) {
    for (auto& device : devices) {
      if (device.nvmlIndex != gpu.index || device.backend != "nvml") continue;
      auto put = [&](const std::string& suffix, double value) {
        const std::string id = "gpu" + std::to_string(gpu.index) + suffix;
        for (auto& sensor : device.sensors) {
          if (sensor.id == id) sensor.value = value;
        }
      };
      if (gpu.tempKnown) put("_temp", gpu.temp);
      if (gpu.loadKnown) put("_load", gpu.load);
      if (gpu.powerKnown) put("_power", gpu.power);
      if (gpu.clockKnown) put("_clock", gpu.clock);
      for (size_t index = 0; index < gpu.fans.size() && index < device.fans.size(); ++index) {
        auto& fan = device.fans[index];
        if (gpu.fans[index].rpmKnown) {
          fan.rpm = gpu.fans[index].rpm;
          fan.rpmKnown = true;
        }
        if (fan.mode == "auto" && gpu.fans[index].percentKnown) {
          fan.duty = gpu.fans[index].percent;
          fan.dutyKnown = true;
        }
      }
    }
  }
}

void WindowsPlatform::sample_cpu() {
  if (!pdhQuery_) {
    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
    HMODULE pdh = GetModuleHandleW(L"pdh.dll");
    using AddFn = PDH_STATUS(__stdcall*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    auto add = pdh ? reinterpret_cast<AddFn>(reinterpret_cast<void*>(GetProcAddress(pdh, "PdhAddEnglishCounterW"))) : nullptr;
    PDH_HCOUNTER counter = nullptr;
    const PDH_STATUS status = add ? add(query, L"\\Processor(_Total)\\% Processor Time", 0, &counter)
                                  : PdhAddCounterW(query, L"\\Processor(_Total)\\% Processor Time", 0, &counter);
    if (status != ERROR_SUCCESS) {
      PdhCloseQuery(query);
      return;
    }
    PdhCollectQueryData(query);
    pdhQuery_ = query;
    pdhCounter_ = counter;
    return;
  }
  if (PdhCollectQueryData(static_cast<PDH_HQUERY>(pdhQuery_)) != ERROR_SUCCESS) return;
  PDH_FMT_COUNTERVALUE formatted{};
  if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(pdhCounter_), PDH_FMT_DOUBLE, nullptr, &formatted) != ERROR_SUCCESS) return;
  if (formatted.CStatus != ERROR_SUCCESS || formatted.doubleValue < 0 || formatted.doubleValue > 100) return;
  cpuLoad_ = formatted.doubleValue;
  cpuLoadKnown_ = true;
}

std::string WindowsPlatform::set_zone(const Device& device, const Zone& zone) {
  if (device.backend != "nzxt") return {};
  const auto found = hid_.find(device.id);
  if (found == hid_.end()) return "Канал NZXT потерян. Пересканируй.";
  return nzxt_set_light(found->second, zone.channel, zone, device.nzxt2023);
}

std::string WindowsPlatform::set_fan(const Device& device, const Fan& fan) {
  if (device.backend == "nvml") {
    if (fan.mode == "auto") return nvml_.set_auto(device.nvmlIndex, fan.channel);
    return nvml_.set_fan(device.nvmlIndex, fan.channel, static_cast<int>(std::lround(fan.duty)));
  }
  if (device.backend == "nzxt") {
    if (fan.mode == "auto") return {};
    const auto found = hid_.find(device.id);
    if (found == hid_.end()) return "Канал NZXT потерян. Пересканируй.";
    return nzxt_set_fan(found->second, fan.channel, static_cast<int>(std::lround(fan.duty)));
  }
  return {};
}

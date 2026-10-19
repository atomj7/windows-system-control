#include "win_nzxt.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>

#include <algorithm>

namespace {

bool write_report(const std::wstring& path, const uint8_t* data, size_t size) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  DWORD written = 0;
  BOOL ok = WriteFile(handle, data, static_cast<DWORD>(size), &written, &overlapped);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(overlapped.hEvent, 400) == WAIT_OBJECT_0) ok = GetOverlappedResult(handle, &overlapped, &written, FALSE);
  }
  if (overlapped.hEvent) CloseHandle(overlapped.hEvent);
  CloseHandle(handle);
  return ok == TRUE && written == size;
}

bool write_any(const std::vector<std::wstring>& paths, const uint8_t* data, size_t size) {
  for (const auto& path : paths) {
    if (write_report(path, data, size)) return true;
  }
  return false;
}

uint8_t scale(uint8_t channel, int brightness) {
  return static_cast<uint8_t>(channel * std::clamp(brightness, 0, 100) / 100);
}

int hue_mode(const std::string& effect) {
  if (effect == "breathe") return 0x07;
  if (effect == "spectrum") return 0x02;
  if (effect == "wave") return 0x03;
  if (effect == "rainbow") return 0x0B;
  return 0x00;
}

int hue_speed(int percent) { return std::clamp(percent, 0, 100) * 4 / 100; }

}  // namespace

std::vector<std::wstring> hid_paths_for(uint16_t vid, uint16_t pid) {
  std::vector<std::wstring> paths;
  GUID guid{};
  HidD_GetHidGuid(&guid);
  HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (info == INVALID_HANDLE_VALUE) return paths;
  SP_DEVICE_INTERFACE_DATA interfaceData{};
  interfaceData.cbSize = sizeof(interfaceData);
  for (DWORD index = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &guid, index, &interfaceData); ++index) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, nullptr, 0, &needed, nullptr);
    if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
    std::vector<uint8_t> buffer(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, detail, needed, nullptr, nullptr)) continue;
    HANDLE handle = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) continue;
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    const bool match = HidD_GetAttributes(handle, &attributes) && attributes.VendorID == vid && attributes.ProductID == pid;
    CloseHandle(handle);
    if (match) paths.emplace_back(detail->DevicePath);
  }
  SetupDiDestroyDeviceInfoList(info);
  return paths;
}

std::string nzxt_set_light(const std::vector<std::wstring>& paths, int channel, const Zone& zone, bool protocol2023) {
  if (paths.empty()) return "HID-интерфейс NZXT не открылся.";
  if (channel < 0 || channel > 5) return "Неверный канал подсветки.";
  const bool off = zone.effect == "off";
  const uint8_t red = off ? 0 : scale(zone.color.r, zone.brightness);
  const uint8_t green = off ? 0 : scale(zone.color.g, zone.brightness);
  const uint8_t blue = off ? 0 : scale(zone.color.b, zone.brightness);
  uint8_t packet[64] = {};
  const int mode = off ? 0x00 : hue_mode(zone.effect);
  const int speed = hue_speed(zone.speed);
  if (!protocol2023) {
    packet[0] = 0x28;
    packet[1] = 0x03;
    packet[2] = static_cast<uint8_t>(1u << channel);
    packet[3] = 0x28;
    packet[4] = static_cast<uint8_t>(mode);
    packet[5] = static_cast<uint8_t>(speed);
    packet[6] = 1;
    packet[7] = 0;
    packet[8] = 1;
    packet[0x0A] = green;
    packet[0x0B] = red;
    packet[0x0C] = blue;
  } else {
    const uint8_t slow[5][2] = {{0x28, 0x00}, {0x1E, 0x00}, {0x14, 0x00}, {0x0A, 0x00}, {0x04, 0x00}};
    const int bucket = std::clamp(speed, 0, 4);
    packet[0] = 0x2A;
    packet[1] = 0x04;
    packet[2] = static_cast<uint8_t>(1u << channel);
    packet[3] = packet[2];
    packet[4] = static_cast<uint8_t>(mode);
    packet[5] = slow[4 - bucket][0];
    packet[6] = slow[4 - bucket][1];
    packet[7] = green;
    packet[8] = red;
    packet[9] = blue;
    packet[0x38] = 1;
    packet[0x39] = mode == 0x07 ? 0x08 : 0x00;
    packet[0x3A] = 0x08;
  }
  if (!write_any(paths, packet, sizeof(packet))) return "NZXT не принял пакет. Часто устройство уже открыто CAM или OpenRGB.";
  return {};
}

std::string nzxt_set_fan(const std::vector<std::wstring>& paths, int channel, int duty) {
  if (paths.empty()) return "HID-интерфейс NZXT не открылся.";
  if (channel < 0 || channel > 5) return "Неверный канал вентилятора.";
  duty = std::clamp(duty, 0, 100);
  uint8_t packet[64] = {};
  packet[0] = 0x62;
  packet[1] = 0x01;
  packet[2] = static_cast<uint8_t>(1u << channel);
  packet[3 + channel] = static_cast<uint8_t>(duty);
  if (!write_any(paths, packet, sizeof(packet))) return "NZXT не принял обороты. Канал может быть занят CAM.";
  return {};
}

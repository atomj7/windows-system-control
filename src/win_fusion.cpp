#include "win_fusion.hpp"

#include "util.hpp"
#include "win_nzxt.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern "C" {
#include <hidsdi.h>
}

#include <algorithm>
#include <cstring>

namespace {

bool send_feature(const std::wstring& path, uint8_t* data, DWORD size) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  const BOOL ok = HidD_SetFeature(handle, data, size);
  CloseHandle(handle);
  return ok == TRUE;
}

bool send_feature_any(const std::vector<std::wstring>& paths, uint8_t* data, DWORD size) {
  for (const auto& path : paths) {
    if (send_feature(path, data, size)) return true;
  }
  return false;
}

uint8_t effect_code(const std::string& effect) {
  if (effect == "off") return 0;
  if (effect == "breathe") return 2;
  if (effect == "spectrum" || effect == "rainbow") return 4;
  if (effect == "wave") return 6;
  return 1;
}

}  // namespace

void fusion_attach_board(Device& board, uint16_t vid, uint16_t pid) {
  board.hardware = "live";
  board.backend = "fusion";
  board.vid = vid;
  board.pid = pid;
  board.connection = "RGB Fusion 2.0 · USB " + hex4(vid) + ":" + hex4(pid);
  board.note =
      "Контроллер ITE RGB Fusion найден. Статичный цвет и эффекты уходят на разъёмы платы. Если фирменный RGB Fusion или "
      "OpenRGB уже открыли устройство, запись не пройдёт — закрой их. Крыльчатки SuperIO это не трогает.";
  board.zones.clear();
  const char* names[] = {"Плата", "12 В RGB", "5 В ARGB"};
  const int channels[] = {0, 5, 6};
  for (int i = 0; i < 3; ++i) {
    Zone zone;
    zone.id = "fusion" + std::to_string(channels[i]);
    zone.name = names[i];
    zone.leds = channels[i] >= 5 ? 1 : 1;
    zone.channel = channels[i];
    zone.color = Color{227, 145, 74};
    zone.effect = "static";
    zone.brightness = 80;
    board.zones.push_back(std::move(zone));
  }
}

std::string fusion_set_light(const std::vector<std::wstring>& paths, int channel, const Zone& zone) {
  if (paths.empty()) return "HID RGB Fusion не открылся.";
  if (channel < 0 || channel > 7) return "Неверная зона RGB Fusion.";
  uint8_t packet[64] = {};
  packet[0] = 0xCC;
  packet[1] = static_cast<uint8_t>(0x20 + channel);
  const uint32_t bit = 1u << channel;
  std::memcpy(packet + 2, &bit, sizeof(bit));
  packet[11] = effect_code(zone.effect);
  packet[12] = static_cast<uint8_t>(std::clamp(zone.brightness, 0, 100) * 255 / 100);
  if (zone.effect != "off") {
    packet[14] = zone.color.b;
    packet[15] = zone.color.g;
    packet[16] = zone.color.r;
  }
  if (!send_feature_any(paths, packet, sizeof(packet))) {
    return "RGB Fusion не принял цвет. Закрой RGB Fusion / OpenRGB, если они держат контроллер.";
  }
  uint8_t apply[64] = {};
  apply[0] = 0xCC;
  apply[1] = 0x28;
  apply[2] = 0xFF;
  send_feature_any(paths, apply, sizeof(apply));
  return {};
}

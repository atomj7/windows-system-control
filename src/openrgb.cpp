#include "openrgb.hpp"

#include "net.hpp"
#include "util.hpp"

#include <cstring>

namespace {

constexpr int kClientProtocol = 4;
constexpr uint32_t kPacketVersion = 40;
constexpr uint32_t kPacketSetName = 50;
constexpr uint32_t kPacketCount = 0;
constexpr uint32_t kPacketData = 1;
constexpr uint32_t kPacketUpdateLeds = 1050;
constexpr uint32_t kPacketUpdateMode = 1101;

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : cur_(data), end_(data + size) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return static_cast<size_t>(end_ - cur_); }

  uint8_t u8() {
    if (cur_ >= end_) {
      ok_ = false;
      return 0;
    }
    return *cur_++;
  }

  uint16_t u16() {
    const uint16_t lo = u8();
    const uint16_t hi = u8();
    return static_cast<uint16_t>(lo | (hi << 8));
  }

  uint32_t u32() {
    const uint32_t b0 = u8();
    const uint32_t b1 = u8();
    const uint32_t b2 = u8();
    const uint32_t b3 = u8();
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }

  int32_t i32() { return static_cast<int32_t>(u32()); }

  void skip(size_t count) {
    if (remaining() < count) {
      ok_ = false;
      cur_ = end_;
      return;
    }
    cur_ += count;
  }

  std::string str16() {
    const uint16_t length = u16();
    if (!ok_ || remaining() < length) {
      ok_ = false;
      return {};
    }
    std::string value(reinterpret_cast<const char*>(cur_), reinterpret_cast<const char*>(cur_) + length);
    cur_ += length;
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
  bool ok_ = true;
};

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

uint32_t pack_color(const Color& color) {
  return static_cast<uint32_t>(color.r) | (static_cast<uint32_t>(color.g) << 8) |
         (static_cast<uint32_t>(color.b) << 16);
}

Color unpack_color(uint32_t value) {
  return Color{static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
               static_cast<uint8_t>((value >> 16) & 0xFF)};
}

bool parse_mode(Reader& reader, int protocol, OrgbMode& mode) {
  mode.name = reader.str16();
  if (protocol < 6) mode.value = reader.i32();
  mode.flags = reader.u32();
  mode.speedMin = reader.u32();
  mode.speedMax = reader.u32();
  if (protocol >= 3) {
    mode.brightMin = reader.u32();
    mode.brightMax = reader.u32();
  }
  mode.colorsMin = reader.u32();
  mode.colorsMax = reader.u32();
  mode.speed = reader.u32();
  if (protocol >= 3) mode.brightness = reader.u32();
  mode.direction = reader.u32();
  mode.colorMode = reader.u32();
  const uint16_t colors = reader.u16();
  if (!reader.ok() || colors > 64) return false;
  mode.colors.clear();
  for (uint16_t i = 0; i < colors; ++i) mode.colors.push_back(unpack_color(reader.u32()));
  return reader.ok();
}

std::vector<uint8_t> serialize_mode(const OrgbMode& mode, int protocol) {
  std::vector<uint8_t> out;
  write_str(out, mode.name);
  if (protocol < 6) write_u32(out, static_cast<uint32_t>(mode.value));
  write_u32(out, mode.flags);
  write_u32(out, mode.speedMin);
  write_u32(out, mode.speedMax);
  if (protocol >= 3) {
    write_u32(out, mode.brightMin);
    write_u32(out, mode.brightMax);
  }
  write_u32(out, mode.colorsMin);
  write_u32(out, mode.colorsMax);
  write_u32(out, mode.speed);
  if (protocol >= 3) write_u32(out, mode.brightness);
  write_u32(out, mode.direction);
  write_u32(out, mode.colorMode);
  write_u16(out, static_cast<uint16_t>(mode.colors.size()));
  for (const auto& color : mode.colors) write_u32(out, pack_color(color));
  return out;
}

std::vector<uint8_t> packet(uint32_t device, uint32_t id, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out;
  out.push_back('O');
  out.push_back('R');
  out.push_back('G');
  out.push_back('B');
  write_u32(out, device);
  write_u32(out, id);
  write_u32(out, static_cast<uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

bool read_packet(Socket socket, uint32_t& device, uint32_t& id, std::vector<uint8_t>& payload) {
  uint8_t header[16];
  if (!recv_full(socket, header, sizeof(header), 800)) return false;
  if (std::memcmp(header, "ORGB", 4) != 0) return false;
  auto read_u32_at = [&](int offset) {
    return static_cast<uint32_t>(header[offset]) | (static_cast<uint32_t>(header[offset + 1]) << 8) |
           (static_cast<uint32_t>(header[offset + 2]) << 16) | (static_cast<uint32_t>(header[offset + 3]) << 24);
  };
  device = read_u32_at(4);
  id = read_u32_at(8);
  const uint32_t size = read_u32_at(12);
  if (size > 8 * 1024 * 1024) return false;
  payload.assign(size, 0);
  if (size == 0) return true;
  return recv_full(socket, payload.data(), payload.size(), 1500);
}

void parse_vid_pid(const std::string& location, uint16_t& vid, uint16_t& pid) {
  const std::string text = lower_copy(location);
  auto read_id = [&](const char* key, uint16_t& out) {
    const auto pos = text.find(key);
    if (pos == std::string::npos || pos + 8 > text.size()) return;
    try {
      out = static_cast<uint16_t>(std::stoul(text.substr(pos + 4, 4), nullptr, 16));
    } catch (...) {
    }
  };
  read_id("vid_", vid);
  read_id("pid_", pid);
}

int mode_index_for(const std::vector<OrgbMode>& modes, const std::string& effect) {
  const std::string wanted = lower_copy(effect);
  auto matches = [&](const std::string& name) {
    if (wanted == "off") return name.find("off") != std::string::npos || name.find("none") != std::string::npos;
    if (wanted == "static") {
      return name.find("direct") != std::string::npos || name.find("static") != std::string::npos ||
             name.find("fixed") != std::string::npos || name.find("custom") != std::string::npos ||
             name.find("solid") != std::string::npos;
    }
    if (wanted == "breathe") return name.find("breath") != std::string::npos || name.find("fad") != std::string::npos;
    if (wanted == "spectrum") return name.find("spectrum") != std::string::npos || name.find("cycle") != std::string::npos;
    if (wanted == "rainbow") return name.find("rainbow") != std::string::npos;
    if (wanted == "wave") {
      return name.find("wave") != std::string::npos || name.find("marquee") != std::string::npos ||
             name.find("flow") != std::string::npos;
    }
    return false;
  };
  for (size_t i = 0; i < modes.size(); ++i) {
    if (matches(lower_copy(modes[i].name))) return static_cast<int>(i);
  }
  return -1;
}

uint32_t scale_range(int percent, uint32_t min_value, uint32_t max_value) {
  if (max_value <= min_value) return min_value;
  const double t = clamp_double(percent, 0, 100) / 100.0;
  return min_value + static_cast<uint32_t>(t * (max_value - min_value));
}

}  // namespace

bool orgb_has_effect(const std::vector<OrgbMode>& modes, const std::string& effect) {
  if (effect == "static") return true;
  return mode_index_for(modes, effect) >= 0;
}

bool parse_orgb_controller(const uint8_t* bytes, size_t size, int protocol, OrgbController& out) {
  if (protocol < 0 || protocol > 4) return false;
  Reader reader(bytes, size);
  const uint32_t data_size = reader.u32();
  if (!reader.ok() || data_size > size) return false;
  reader.i32();  // device type
  out.name = reader.str16();
  if (protocol >= 1) out.vendor = reader.str16();
  out.description = reader.str16();
  reader.str16();  // version
  reader.str16();  // serial
  out.location = reader.str16();
  const uint16_t mode_count = reader.u16();
  out.activeMode = reader.i32();
  if (!reader.ok() || mode_count > 64) return false;
  out.modes.clear();
  for (uint16_t i = 0; i < mode_count; ++i) {
    OrgbMode mode;
    if (!parse_mode(reader, protocol, mode)) return false;
    out.modes.push_back(std::move(mode));
  }
  const uint16_t zone_count = reader.u16();
  if (!reader.ok() || zone_count > 64) return false;
  out.zoneNames.clear();
  out.zoneLeds.clear();
  for (uint16_t i = 0; i < zone_count; ++i) {
    const std::string name = reader.str16();
    reader.i32();  // type
    reader.u32();  // leds min
    reader.u32();  // leds max
    const uint32_t leds = reader.u32();
    const uint16_t matrix_len = reader.u16();
    reader.skip(matrix_len);
    if (protocol >= 4) {
      const uint16_t segments = reader.u16();
      if (segments > 64) return false;
      for (uint16_t segment = 0; segment < segments; ++segment) {
        reader.str16();
        reader.i32();
        reader.u32();
        reader.u32();
      }
    }
    if (!reader.ok()) return false;
    out.zoneNames.push_back(name);
    out.zoneLeds.push_back(static_cast<int>(leds));
  }
  const uint16_t led_count = reader.u16();
  if (led_count > 2048) return false;
  for (uint16_t i = 0; i < led_count; ++i) {
    reader.str16();
    if (protocol < 6) reader.u32();
  }
  const uint16_t color_count = reader.u16();
  if (!reader.ok() || color_count > 4096) return false;
  reader.skip(static_cast<size_t>(color_count) * 4);
  out.ledCount = led_count;
  parse_vid_pid(out.location, out.vid, out.pid);
  return reader.ok();
}

OpenRgbBridge::Result OpenRgbBridge::discover() {
  Result result;
  Socket socket = connect_local(6742, 1500);
  if (socket == kInvalidSocket) {
    result.detail =
        "SDK-сервер OpenRGB на 127.0.0.1:6742 не отвечает. Скачай OpenRGB, запусти от администратора (SMBus планок без этого "
        "часто не виден), Settings → SDK Server → Start, порт 6742. Потом в Helios нажми «Обзор».";
    return result;
  }
  struct Guard {
    Socket socket;
    ~Guard() { close_socket(socket); }
  } guard{socket};

  std::vector<uint8_t> version_payload;
  write_u32(version_payload, kClientProtocol);
  auto hello = packet(0, kPacketVersion, version_payload);
  if (!send_all(socket, hello.data(), hello.size())) {
    result.detail = "OpenRGB принял соединение и сразу его закрыл.";
    return result;
  }
  uint32_t device = 0;
  uint32_t id = 0;
  std::vector<uint8_t> payload;
  if (!read_packet(socket, device, id, payload) || id != kPacketVersion || payload.size() < 4) {
    result.detail = "OpenRGB не сообщил версию протокола.";
    return result;
  }
  const int server_protocol = static_cast<int>(payload[0] | (payload[1] << 8) | (payload[2] << 16) | (payload[3] << 24));
  const int protocol = server_protocol < kClientProtocol ? server_protocol : kClientProtocol;
  if (protocol < 1) {
    result.detail = "OpenRGB отвечает слишком старым протоколом.";
    return result;
  }

  const std::string name = "Helios";
  std::vector<uint8_t> name_payload(name.begin(), name.end());
  name_payload.push_back(0);
  auto name_packet = packet(0, kPacketSetName, name_payload);
  send_all(socket, name_packet.data(), name_packet.size());

  auto count_packet = packet(0, kPacketCount, {});
  if (!send_all(socket, count_packet.data(), count_packet.size()) || !read_packet(socket, device, id, payload) ||
      payload.size() < 4) {
    result.detail = "OpenRGB не отдал список устройств.";
    return result;
  }
  const uint32_t count = payload[0] | (payload[1] << 8) | (payload[2] << 16) | (payload[3] << 24);
  if (count > 256) {
    result.detail = "OpenRGB вернул неправдоподобное число устройств.";
    return result;
  }

  for (uint32_t index = 0; index < count; ++index) {
    std::vector<uint8_t> request;
    write_u32(request, static_cast<uint32_t>(protocol));
    auto data_packet = packet(index, kPacketData, request);
    if (!send_all(socket, data_packet.data(), data_packet.size()) || !read_packet(socket, device, id, payload)) break;
    OrgbController controller;
    controller.index = static_cast<int>(index);
    if (!parse_orgb_controller(payload.data(), payload.size(), protocol, controller)) continue;
    result.controllers.push_back(std::move(controller));
  }
  result.connected = true;
  result.detail = "Подключён SDK OpenRGB, устройств: " + std::to_string(result.controllers.size()) + ".";
  return result;
}

std::string OpenRgbBridge::apply(const OrgbController& controller, const Zone& zone, int protocol) {
  const int mode_index = mode_index_for(controller.modes, zone.effect);
  if (mode_index < 0 && zone.effect != "static") {
    return "У этого устройства в OpenRGB нет эффекта «" + zone.effect + "».";
  }
  Socket socket = connect_local(6742, 1500);
  if (socket == kInvalidSocket) return "OpenRGB сейчас не слушает порт 6742. Запусти SDK-сервер и повтори.";
  struct Guard {
    Socket socket;
    ~Guard() { close_socket(socket); }
  } guard{socket};

  if (mode_index >= 0) {
    OrgbMode mode = controller.modes[static_cast<size_t>(mode_index)];
    mode.speed = scale_range(zone.speed, mode.speedMin, mode.speedMax);
    if (mode.brightMax > mode.brightMin) mode.brightness = scale_range(zone.brightness, mode.brightMin, mode.brightMax);
    if (mode.colors.empty()) mode.colors.push_back(zone.color);
    else mode.colors[0] = zone.color;
    auto mode_bytes = serialize_mode(mode, protocol);
    std::vector<uint8_t> body;
    const uint32_t data_size = 4 + static_cast<uint32_t>(mode_bytes.size());
    write_u32(body, data_size);
    write_u32(body, static_cast<uint32_t>(mode_index));
    body.insert(body.end(), mode_bytes.begin(), mode_bytes.end());
    auto message = packet(static_cast<uint32_t>(controller.index), kPacketUpdateMode, body);
    if (!send_all(socket, message.data(), message.size())) return "Не удалось отправить режим в OpenRGB.";
  }

  const int count = controller.ledCount > 0 ? controller.ledCount : 1;
  std::vector<uint8_t> leds;
  const uint32_t data_size = 2 + static_cast<uint32_t>(count) * 4;
  write_u32(leds, data_size);
  write_u16(leds, static_cast<uint16_t>(count));
  for (int i = 0; i < count; ++i) write_u32(leds, pack_color(zone.effect == "off" ? Color{0, 0, 0} : zone.color));
  auto led_packet = packet(static_cast<uint32_t>(controller.index), kPacketUpdateLeds, leds);
  if (!send_all(socket, led_packet.data(), led_packet.size())) return "Не удалось отправить цвет в OpenRGB.";
  return {};
}

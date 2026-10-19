#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

inline double now_seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string lower_copy(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

inline bool contains_ci(std::string_view haystack, std::string_view needle) {
  return lower_copy(haystack).find(lower_copy(needle)) != std::string::npos;
}

inline std::string hex4(uint16_t value) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%04X", value);
  return buffer;
}

inline std::string hex_byte(uint8_t value) {
  char buffer[4];
  std::snprintf(buffer, sizeof(buffer), "%02X", value);
  return buffer;
}

inline double round_to(double value, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::round(value * scale) / scale;
}

inline double clamp_double(double value, double lo, double hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

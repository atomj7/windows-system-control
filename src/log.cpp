#include "log.hpp"

#include <fstream>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::mutex mu;
std::ofstream file;
std::filesystem::path path;

void write(const char* level, const std::string& message) {
  std::lock_guard lock(mu);
  const std::string line = std::string("[") + level + "] " + message;
  std::cerr << line << "\n";
  if (file) {
    file << line << "\n";
    file.flush();
  }
}
}  // namespace

void log_open(const std::filesystem::path& next) {
  std::lock_guard lock(mu);
  path = next;
  file.open(path, std::ios::app);
}

void log_info(const std::string& message) { write("info", message); }

void log_error(const std::string& message) { write("error", message); }

void show_fatal(const std::string& message) {
  log_error(message);
#ifdef _WIN32
  std::wstring wide;
  const int size = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
  if (size > 0) {
    wide.resize(static_cast<size_t>(size - 1));
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), size);
  }
  const std::wstring extra = path.empty() ? L"" : (L"\n\nЛог: " + path.wstring());
  MessageBoxW(nullptr, (wide + extra).c_str(), L"Helios", MB_OK | MB_ICONERROR);
#endif
}

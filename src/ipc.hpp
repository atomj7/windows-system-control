#pragma once

#include "json.hpp"
#include "platform.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void ipc_write_atomic(const std::filesystem::path& path, const std::string& text);
std::optional<Json> ipc_read_json(const std::filesystem::path& path);
void ipc_queue_command(const std::filesystem::path& dir, Json command);
std::vector<Json> ipc_take_commands(const std::filesystem::path& dir);
void ipc_request_scan(const std::filesystem::path& dataDir);

std::unique_ptr<Platform> make_ipc_platform(std::filesystem::path dataDir);
int run_hardware_worker(const std::filesystem::path& dataDir, bool simulate);

#ifdef _WIN32
class HardwareSupervisor {
 public:
  HardwareSupervisor(std::filesystem::path exe, std::filesystem::path dataDir);
  ~HardwareSupervisor();
  HardwareSupervisor(const HardwareSupervisor&) = delete;
  HardwareSupervisor& operator=(const HardwareSupervisor&) = delete;
  void ensure();
  void stop();
  void request_scan();
  bool alive() const;

 private:
  std::filesystem::path exe_;
  std::filesystem::path dataDir_;
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  std::chrono::steady_clock::time_point lastSpawn_{};
  std::chrono::milliseconds backoff_{0};
};
#endif
